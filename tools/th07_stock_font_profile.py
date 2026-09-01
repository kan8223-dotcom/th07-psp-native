#!/usr/bin/env python3
"""Extract the authoritative TH07 text/glyph profile without redistributing text.

The original-data archive is supplied locally by the user.  This module emits
only Unicode codepoint numbers, counts and hashes; it never writes the decoded
game text or archive payload into the repository.
"""

from __future__ import annotations

import ast
import hashlib
import importlib.util
import re
import struct
import sys
import unicodedata
from dataclasses import dataclass
from pathlib import Path
from types import ModuleType
from typing import Callable, Iterable, Sequence


REPO_ROOT = Path(__file__).resolve().parents[1]
ARCHIVE_HELPER = REPO_ROOT / "tools" / "inspect_ecl_sounds.py"
AUTHORITY_HEADER = REPO_ROOT / "src" / "Th07FontCoverage.hpp"
DEFAULT_STOCK_ARCHIVE = REPO_ROOT / "artifacts" / "th07.analysis.tmp.dat"

PROFILE_VERSION = "th07-stock-font-profile-v1"
EXPECTED_CODEPOINT_COUNT = 1190
EXPECTED_CODEPOINT_SHA256 = (
    "da81e0e1a2b8b5d44c135d2ac43f3f91a90ce684c62b985206992e3855a90aa4"
)
EXPECTED_NAME_ENTRY_COUNT = 94
EXPECTED_NAME_ENTRY_SHA256 = (
    "d15aa5848f63221de8a59f330b385188aa999669cb3d7d4023ff1e659697af0f"
)

MSG_FILES = tuple(f"msg{index}.dat" for index in range(1, 9))
ECL_FILES = tuple(f"ecldata{index}.ecl" for index in range(1, 9))
ENDING_FILES = (
    "end00.end",
    "end01.end",
    "end10.end",
    "end11.end",
    "end20.end",
    "end21.end",
    "end00b.end",
    "end10b.end",
    "end20b.end",
)


class StockProfileError(RuntimeError):
    """The local archive/source tree is not the supported stock profile."""


@dataclass(frozen=True)
class ProfileGroup:
    name: str
    sources: tuple[str, ...]
    row_count: int
    unique_row_count: int
    codepoints: frozenset[int]
    content_sha256: str


@dataclass(frozen=True)
class StockFontProfile:
    archive_filename: str
    archive_sha256: str
    groups: tuple[ProfileGroup, ...]
    codepoints: frozenset[int]
    codepoint_sha256: str
    name_entry_codepoints: frozenset[int]
    name_entry_sha256: str


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def codepoints_from_text(text: str) -> frozenset[int]:
    result: set[int] = set()
    for char in unicodedata.normalize("NFC", text):
        if not unicodedata.category(char).startswith("C"):
            result.add(ord(char))
    return frozenset(result)


def authority_codepoint_hash(codepoints: Iterable[int]) -> str:
    payload = "".join(f"U+{value:04X}\n" for value in sorted(set(codepoints)))
    return sha256_bytes(payload.encode("ascii"))


def _rows_hash(rows: Sequence[str]) -> str:
    payload = bytearray()
    for row in rows:
        encoded = unicodedata.normalize("NFC", row).encode("utf-8")
        payload.extend(struct.pack("<I", len(encoded)))
        payload.extend(encoded)
    return sha256_bytes(bytes(payload))


def _make_group(name: str, sources: Sequence[str], rows: Sequence[str]) -> ProfileGroup:
    normalized = tuple(unicodedata.normalize("NFC", row) for row in rows)
    codepoints = codepoints_from_text("".join(normalized))
    return ProfileGroup(
        name=name,
        sources=tuple(sources),
        row_count=len(normalized),
        unique_row_count=len(set(normalized)),
        codepoints=codepoints,
        content_sha256=_rows_hash(normalized),
    )


def _decode_cp932(payload: bytes, source: str) -> str:
    try:
        return payload.decode("cp932")
    except UnicodeDecodeError as exc:
        raise StockProfileError(f"{source}: invalid CP932 text: {exc}") from exc


def _load_archive_helper() -> ModuleType:
    spec = importlib.util.spec_from_file_location("th07_archive_helper", ARCHIVE_HELPER)
    if spec is None or spec.loader is None:
        raise StockProfileError(f"cannot load archive helper: {ARCHIVE_HELPER}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    if not callable(getattr(module, "extract_entry", None)):
        raise StockProfileError("archive helper has no extract_entry()")
    return module


def _extract_archive_entries(
    archive: Path, names: Sequence[str], extract_entry: Callable[[str, str], bytes]
) -> dict[str, bytes]:
    entries: dict[str, bytes] = {}
    for name in names:
        try:
            entries[name] = extract_entry(str(archive), name)
        except (KeyError, OSError, ValueError, IndexError, struct.error) as exc:
            raise StockProfileError(f"cannot extract {name}: {exc}") from exc
    return entries


def parse_msg_rows(data: bytes, source: str) -> tuple[str, ...]:
    if len(data) < 4:
        raise StockProfileError(f"{source}: truncated MSG header")
    entry_count = struct.unpack_from("<i", data)[0]
    if entry_count <= 0 or entry_count > (len(data) - 4) // 4:
        raise StockProfileError(f"{source}: invalid MSG entry count {entry_count}")
    offsets = struct.unpack_from(f"<{entry_count}I", data, 4)
    header_size = 4 + entry_count * 4
    if any(offset < header_size or offset >= len(data) for offset in offsets):
        raise StockProfileError(f"{source}: invalid MSG entry offset")

    # Many table cells alias the same physical message. Scan each physical
    # stream once; scanning every table cell would count the same row 16 times.
    starts = sorted(set(offsets))
    rows: list[str] = []
    for start_index, start in enumerate(starts):
        end = starts[start_index + 1] if start_index + 1 < len(starts) else len(data)
        cursor = start
        terminated = False
        while cursor + 4 <= end:
            _time, opcode, arg_size = struct.unpack_from("<HBB", data, cursor)
            instruction_size = 4 + arg_size
            if instruction_size > end - cursor:
                raise StockProfileError(f"{source}: MSG instruction crosses entry boundary")
            if opcode == 0:
                terminated = True
                break
            if opcode in (3, 8):
                if arg_size < 5:
                    raise StockProfileError(f"{source}: short MSG text instruction")
                field = data[cursor + 8 : cursor + instruction_size]
                nul = field.find(b"\0")
                if nul < 0:
                    raise StockProfileError(f"{source}: unterminated MSG text")
                if nul:
                    rows.append(_decode_cp932(field[:nul], source))
            cursor += instruction_size
        if not terminated:
            raise StockProfileError(f"{source}: MSG stream has no terminator")
    return tuple(rows)


def parse_ecl_spell_rows(data: bytes, source: str) -> tuple[str, ...]:
    if len(data) < 0x44:
        raise StockProfileError(f"{source}: truncated ECL header")
    sub_count, timeline_count = struct.unpack_from("<hh", data)
    if sub_count <= 0 or timeline_count < 0 or timeline_count > 16:
        raise StockProfileError(f"{source}: invalid ECL counts")
    header_size = 0x44 + sub_count * 4
    if header_size > len(data):
        raise StockProfileError(f"{source}: truncated ECL sub table")
    timeline_offsets = struct.unpack_from("<16I", data, 4)[:timeline_count]
    sub_offsets = struct.unpack_from(f"<{sub_count}I", data, 0x44)
    section_offsets = tuple(offset for offset in timeline_offsets + sub_offsets if offset)
    if any(
        offset < header_size or offset >= len(data) or offset % 4
        for offset in section_offsets
    ):
        raise StockProfileError(f"{source}: invalid ECL section offset")
    boundaries = sorted(set(section_offsets)) + [len(data)]

    rows: list[str] = []
    for start in sub_offsets:
        end = min(boundary for boundary in boundaries if boundary > start)
        cursor = start
        terminated = False
        while cursor + 12 <= end:
            time, opcode, size, unused, difficulty, param_mask = struct.unpack_from(
                "<IhhBBH", data, cursor
            )
            if size < 12 or size % 4 or size > end - cursor:
                raise StockProfileError(f"{source}: invalid ECL instruction size")
            if (
                time == 0xFFFFFFFF
                and opcode == -1
                and size == 12
                and unused == 0
                and difficulty == 0xFF
                and param_mask == 0x00FF
                and cursor + size == end
            ):
                terminated = True
                break
            if opcode == 90:
                if size < 64:
                    raise StockProfileError(f"{source}: short ECL spell-name instruction")
                field = bytes(value ^ 0xAA for value in data[cursor + 16 : cursor + 64])
                nul = field.find(b"\0")
                if nul < 0:
                    raise StockProfileError(f"{source}: unterminated ECL spell name")
                if nul:
                    rows.append(_decode_cp932(field[:nul], source))
            cursor += size
        if not terminated:
            raise StockProfileError(f"{source}: ECL subroutine has no physical terminator")
    return tuple(rows)


def parse_music_rows(data: bytes, source: str) -> tuple[tuple[str, ...], int, int]:
    # Mirror MusicRoom::AddedCallback byte-for-byte, including its title-line
    # CR/LF skip typo. The first of eight description slots therefore consumes
    # an empty line in the stock file.
    cursor = 0
    titles: list[str] = []
    descriptions: list[str] = []
    size = len(data)
    while cursor < size:
        if data[cursor] != ord("@"):
            cursor += 1
            continue
        cursor += 1
        while cursor < size and data[cursor] not in (10, 13):
            cursor += 1  # archive path: not rendered
        while cursor < size and data[cursor] in (10, 13):
            cursor += 1
        title_start = cursor
        while cursor < size and data[cursor] not in (10, 13):
            cursor += 1
        titles.append(_decode_cp932(data[title_start:cursor], source))

        # The original condition is `while (c == '\n' && c == '\r')`, which
        # can never advance. Preserve that observable parser behavior.
        for _line_index in range(8):
            if cursor >= size or data[cursor] == ord("@"):
                break
            line_start = cursor
            while cursor < size and data[cursor] not in (10, 13):
                cursor += 1
            line = _decode_cp932(data[line_start:cursor], source)
            if line:
                descriptions.append(line)
            while cursor < size and data[cursor] in (10, 13):
                cursor += 1
    return tuple(titles + descriptions), len(titles), len(descriptions)


_ENDING_PARAM_COUNTS = {
    ord("a"): 3,
    ord("V"): 2,
    ord("v"): 1,
    ord("M"): 1,
    ord("s"): 2,
    ord("c"): 1,
    ord("r"): 2,
    ord("w"): 2,
    ord("0"): 1,
    ord("1"): 1,
    ord("2"): 1,
    ord("3"): 1,
}


def parse_ending_rows(data: bytes, source: str) -> tuple[str, ...]:
    cursor = 0
    rows: list[str] = []
    current = bytearray()
    while cursor < len(data):
        value = data[cursor]
        if value == ord("@"):
            if current:
                raise StockProfileError(f"{source}: ending command interrupts text row")
            cursor += 1
            if cursor >= len(data):
                raise StockProfileError(f"{source}: truncated ending command")
            command = data[cursor]
            cursor += 1
            for _ in range(_ENDING_PARAM_COUNTS.get(command, 0)):
                while cursor < len(data) and data[cursor] != 0:
                    cursor += 1
                if cursor >= len(data):
                    raise StockProfileError(f"{source}: unterminated ending parameter")
                while cursor < len(data) and data[cursor] == 0:
                    cursor += 1
            while cursor < len(data) and data[cursor] not in (10, 13):
                cursor += 1
            while cursor < len(data) and data[cursor] in (10, 13):
                cursor += 1
            continue
        if value in (0, 10, 13):
            if current:
                rows.append(_decode_cp932(bytes(current), source))
                current.clear()
            while cursor < len(data) and data[cursor] in (0, 10, 13):
                cursor += 1
            continue
        if cursor + 1 >= len(data):
            raise StockProfileError(f"{source}: truncated two-byte ending text")
        current.extend(data[cursor : cursor + 2])
        cursor += 2
    if current:
        rows.append(_decode_cp932(bytes(current), source))
    return tuple(rows)


_C_STRING_RE = re.compile(r'(?:(?:u8|u|U|L))?"(?:\\.|[^"\\])*"')


def _c_string_literals(source: str) -> tuple[str, ...]:
    rows: list[str] = []
    for token in _C_STRING_RE.findall(source):
        quote = token.index('"')
        try:
            value = ast.literal_eval(token[quote:])
        except (SyntaxError, ValueError) as exc:
            raise StockProfileError(f"cannot decode source string {token!r}: {exc}") from exc
        rows.append(value)
    return tuple(rows)


def _source_region(path: Path, start_marker: str, end_marker: str) -> str:
    source = path.read_text(encoding="utf-8")
    start = source.find(start_marker)
    if start < 0:
        raise StockProfileError(f"{path.name}: missing marker {start_marker!r}")
    end = source.find(end_marker, start + len(start_marker))
    if end < 0:
        raise StockProfileError(f"{path.name}: missing marker {end_marker!r}")
    return source[start:end]


def parse_static_rows(repo_root: Path = REPO_ROOT) -> tuple[str, ...]:
    regions = (
        ("src/MainMenu.cpp", "const char *g_KeyConfigStrings", "void InitializeTimingVars"),
        (
            "src/BombData.cpp",
            "const char *const g_BombNames",
            "const char *BombData::GetBombName",
        ),
        (
            "src/ResultScreen.cpp",
            "const char *g_CharacterList",
            "const char *g_CharactersAndShotTypesStrings",
        ),
        (
            "src/ResultScreen.cpp",
            "for (vmIdx = arg->lastSpellcardSelected * 10;",
            "if (arg->frameTimer < 30)",
        ),
        ("src/ResultScreen.cpp", "i32 ResultScreen::DrawStats()", "    case 22:"),
    )
    rows: list[str] = []
    for relative, start, end in regions:
        rows.extend(_c_string_literals(_source_region(repo_root / relative, start, end)))
    rows.extend(("さむ～", " "))
    return tuple(rows)


def extract_name_entry_charset(repo_root: Path = REPO_ROOT) -> str:
    source = (repo_root / "src" / "ResultScreen.cpp").read_text(encoding="utf-8")
    pattern = re.compile(
        r"const\s+char\s*\*\s*g_AlphabetList\s*=\s*"
        r"((?:\"(?:\\.|[^\"\\])*\"\s*)+);"
    )
    match = pattern.search(source)
    if not match:
        raise StockProfileError("cannot extract g_AlphabetList")
    return "".join(_c_string_literals(match.group(1)))


def build_stock_profile(
    archive: Path,
    *,
    repo_root: Path = REPO_ROOT,
    extract_entry: Callable[[str, str], bytes] | None = None,
) -> StockFontProfile:
    if not archive.is_file():
        raise StockProfileError(f"stock archive does not exist: {archive}")
    if extract_entry is None:
        extract_entry = _load_archive_helper().extract_entry

    wanted = MSG_FILES + ECL_FILES + ("musiccmt.txt",) + ENDING_FILES
    entries = _extract_archive_entries(archive, wanted, extract_entry)

    message_rows: list[str] = []
    for name in MSG_FILES:
        message_rows.extend(parse_msg_rows(entries[name], name))
    ecl_rows: list[str] = []
    for name in ECL_FILES:
        ecl_rows.extend(parse_ecl_spell_rows(entries[name], name))
    music_rows, music_titles, music_descriptions = parse_music_rows(
        entries["musiccmt.txt"], "musiccmt.txt"
    )
    ending_rows: list[str] = []
    for name in ENDING_FILES:
        ending_rows.extend(parse_ending_rows(entries[name], name))
    static_rows = parse_static_rows(repo_root)

    groups = (
        _make_group("messages", MSG_FILES, message_rows),
        _make_group("spell_names", ECL_FILES, ecl_rows),
        _make_group("music_room", ("musiccmt.txt",), music_rows),
        _make_group("endings", ENDING_FILES, ending_rows),
        _make_group(
            "static_ui",
            ("src/MainMenu.cpp", "src/BombData.cpp", "src/ResultScreen.cpp", "src/TextHelper.cpp"),
            static_rows,
        ),
    )
    expected_metrics = {
        "messages": (1109, 1016, 749),
        "spell_names": (141, 141, 343),
        "music_room": (141, 139, 482),
        "endings": (280, 263, 447),
        "static_ui": (69, 66, 243),
    }
    for group in groups:
        actual = (group.row_count, group.unique_row_count, len(group.codepoints))
        if actual != expected_metrics[group.name]:
            raise StockProfileError(
                f"{group.name}: stock profile mismatch {actual}, "
                f"expected {expected_metrics[group.name]}"
            )
    if (music_titles, music_descriptions) != (20, 121):
        raise StockProfileError(
            "music_room: parser layout mismatch "
            f"titles={music_titles} descriptions={music_descriptions}"
        )

    name_entry = extract_name_entry_charset(repo_root)
    name_codepoints = codepoints_from_text(name_entry)
    name_hash = authority_codepoint_hash(name_codepoints)
    if (
        len(name_codepoints) != EXPECTED_NAME_ENTRY_COUNT
        or name_hash != EXPECTED_NAME_ENTRY_SHA256
    ):
        raise StockProfileError(
            "g_AlphabetList authority mismatch: "
            f"count={len(name_codepoints)} hash={name_hash}"
        )

    codepoints: set[int] = set(range(0x20, 0x7F))
    codepoints.update(name_codepoints)
    for group in groups:
        codepoints.update(group.codepoints)
    profile_hash = authority_codepoint_hash(codepoints)
    if (
        len(codepoints) != EXPECTED_CODEPOINT_COUNT
        or profile_hash != EXPECTED_CODEPOINT_SHA256
    ):
        raise StockProfileError(
            "archive/source text is not the supported stock profile: "
            f"count={len(codepoints)} hash={profile_hash}; use the full font for modified data"
        )
    return StockFontProfile(
        archive_filename=archive.name,
        archive_sha256=sha256_file(archive),
        groups=groups,
        codepoints=frozenset(codepoints),
        codepoint_sha256=profile_hash,
        name_entry_codepoints=name_codepoints,
        name_entry_sha256=name_hash,
    )


def render_authority_header(profile: StockFontProfile) -> str:
    if (
        len(profile.codepoints) != EXPECTED_CODEPOINT_COUNT
        or profile.codepoint_sha256 != EXPECTED_CODEPOINT_SHA256
    ):
        raise StockProfileError("refusing to render a non-authoritative header")
    values = sorted(profile.codepoints)
    rows = []
    for offset in range(0, len(values), 8):
        formatted = ", ".join(
            f"0x{value:04X}u" for value in values[offset : offset + 8]
        )
        rows.append("    " + formatted + ",")
    return "\n".join(
        (
            "#pragma once",
            "",
            "// Generated as numeric coverage metadata only. No font or original "
            "text is embedded.",
            "// Source profile SHA-256: " + profile.codepoint_sha256,
            "#include <SDL2/SDL_stdinc.h>",
            "",
            "static const Uint32 kTh07PspStockFontCodepoints[] = {",
            *rows,
            "};",
            "static const Uint32 kTh07PspStockFontCodepointCount = 1190u;",
            "static const char kTh07PspStockFontProfileSha256[] =",
            f'    "{profile.codepoint_sha256}";',
            "static const Uint32 kTh07PspNameEntryCodepointCount = 94u;",
            "static const char kTh07PspNameEntryCodepointSha256[] =",
            f'    "{profile.name_entry_sha256}";',
            "static_assert(sizeof(kTh07PspStockFontCodepoints) /",
            "                  sizeof(kTh07PspStockFontCodepoints[0]) ==",
            "              kTh07PspStockFontCodepointCount,",
            '              "TH07 font authority count mismatch");',
            "",
        )
    )


def verify_authority_header(profile: StockFontProfile, path: Path = AUTHORITY_HEADER) -> None:
    expected = render_authority_header(profile)
    try:
        actual = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise StockProfileError(f"cannot read authority header {path}: {exc}") from exc
    if actual != expected:
        raise StockProfileError(
            f"authority header is out of sync: {path}; regenerate/review the numeric table"
        )
