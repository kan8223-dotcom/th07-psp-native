#!/usr/bin/env python3
"""Build and audit the redistributable TH07 PSP-1000 Noto subset.

``msgothic-subset.ttf`` is a runtime compatibility filename.  The file built
here is derived only from OFL-licensed Noto Sans CJK JP; it is not MS Gothic
and this tool never reads a Microsoft font or original TH07 data.

The character input is the tracked numeric-only authority table.  The source,
fontTools version, option contract, license, and final bytes are all pinned so
that a release cannot silently acquire a locally generated/private font.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = REPO_ROOT / "psp" / "assets" / "NotoSansJP-Regular.ttf"
DEFAULT_OUTPUT = REPO_ROOT / "psp" / "assets" / "msgothic-subset.ttf"
AUTHORITY_HEADER = REPO_ROOT / "src" / "Th07FontCoverage.hpp"
OFL_LICENSE = REPO_ROOT / "licenses" / "NotoSansJP" / "OFL.txt"

FONTTOOLS_VERSION = "4.62.1"
AUTHORITY_COUNT = 1190
AUTHORITY_SHA256 = (
    "da81e0e1a2b8b5d44c135d2ac43f3f91a90ce684c62b985206992e3855a90aa4"
)
# Current upstream OFL text with one trailing space normalized for the source
# tree's no-trailing-whitespace policy.
OFL_SHA256 = "babcfe66c8a098b2fa279bc724a3a342f8124f77ce18941fbcc1bbb39823cded"
OUTPUT_SIZE = 264_288
OUTPUT_SHA256 = "c456df98197c895c2919a690c737ab3c4a2924799bb4d92fa3a53849c6b56dec"
OUTPUT_FILENAME = "msgothic-subset.ttf"


@dataclass(frozen=True)
class ApprovedSource:
    label: str
    size: int
    sha256: str


# The first file is the existing redistributable 2.004 full-font payload.  The
# second is the language-specific JP OTF from the upstream Sans2.004 release.
# Both independently produce the exact OUTPUT_SHA256 above.
APPROVED_SOURCES = {
    "6ab1664d8adc20b19237ddc451c94e31f493cb851a1917242debf66f9af6da05":
        ApprovedSource(
            "tracked Noto Sans CJK JP 2.004 full payload",
            4_491_696,
            "6ab1664d8adc20b19237ddc451c94e31f493cb851a1917242debf66f9af6da05",
        ),
    "68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5":
        ApprovedSource(
            "upstream Noto Sans CJK JP 2.004 OTF",
            16_467_736,
            "68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5",
        ),
}

EXPECTED_TABLES = {
    "GlyphOrder", "head", "hhea", "maxp", "OS/2", "name", "cmap",
    "post", "CFF ", "BASE", "GPOS", "GSUB", "VORG", "hmtx", "vhea",
    "vmtx",
}
EXPECTED_NAMES = {
    0: "© 2014-2021 Adobe (http://www.adobe.com/).",
    1: "Noto Sans CJK JP",
    2: "Regular",
    3: "2.004;GOOG;NotoSansCJKjp-Regular;ADOBE",
    4: "Noto Sans CJK JP",
    5: "Version 2.004;hotconv 1.0.118;makeotfexe 2.5.65603",
    6: "NotoSansCJKjp-Regular",
    13: (
        "This Font Software is licensed under the SIL Open Font License, "
        "Version 1.1. This Font Software is distributed on an \"AS IS\" "
        "BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express "
        "or implied. See the SIL Open Font License for the specific language, "
        "permissions and limitations governing your use of this Font Software."
    ),
    14: "http://scripts.sil.org/OFL",
}


class ReleaseFontError(RuntimeError):
    """A release-font input or output is outside the pinned contract."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def codepoint_hash(codepoints: Iterable[int]) -> str:
    payload = "".join(f"U+{value:04X}\n" for value in sorted(set(codepoints)))
    return sha256_bytes(payload.encode("ascii"))


def load_authority(path: Path = AUTHORITY_HEADER) -> tuple[int, ...]:
    source = path.read_text(encoding="utf-8")
    match = re.search(
        r"kTh07PspStockFontCodepoints\[\]\s*=\s*\{(?P<body>.*?)\};",
        source,
        re.DOTALL,
    )
    if match is None:
        raise ReleaseFontError(f"cannot find numeric font authority in {path}")
    values = tuple(
        int(token, 16)
        for token in re.findall(r"0x([0-9A-Fa-f]+)u", match.group("body"))
    )
    if len(values) != len(set(values)):
        raise ReleaseFontError("numeric font authority contains duplicate codepoints")
    if tuple(sorted(values)) != values:
        raise ReleaseFontError("numeric font authority is not strictly sorted")
    if len(values) != AUTHORITY_COUNT or codepoint_hash(values) != AUTHORITY_SHA256:
        raise ReleaseFontError(
            "numeric font authority differs from the accepted 1,190-codepoint profile"
        )
    declared_count = re.search(
        r"kTh07PspStockFontCodepointCount\s*=\s*(\d+)u", source
    )
    declared_hash = re.search(
        r"kTh07PspStockFontProfileSha256\[\]\s*=\s*\n?\s*\"([0-9a-f]{64})\"",
        source,
    )
    if (
        declared_count is None
        or int(declared_count.group(1)) != AUTHORITY_COUNT
        or declared_hash is None
        or declared_hash.group(1) != AUTHORITY_SHA256
    ):
        raise ReleaseFontError("numeric font authority metadata is stale")
    return values


def require_fonttools() -> tuple[Any, Any]:
    try:
        import fontTools
        from fontTools import subset
        from fontTools.ttLib import TTFont
    except ModuleNotFoundError as exc:
        raise ReleaseFontError(
            "fontTools 4.62.1 is required; install the pinned version with: "
            f"{sys.executable} -m pip install fonttools==4.62.1"
        ) from exc
    actual = str(getattr(fontTools, "__version__", "unknown"))
    if actual != FONTTOOLS_VERSION:
        raise ReleaseFontError(
            f"fontTools version {actual} is not the pinned {FONTTOOLS_VERSION}"
        )
    return subset, TTFont


def audit_license(path: Path = OFL_LICENSE) -> None:
    if not path.is_file():
        raise ReleaseFontError(f"Noto OFL license is missing: {path}")
    if sha256_file(path) != OFL_SHA256:
        raise ReleaseFontError("Noto OFL license does not match the pinned upstream text")
    first_line = path.read_text(encoding="utf-8").splitlines()[0]
    if first_line != (
        "Copyright 2014-2021 Adobe (http://www.adobe.com/), "
        "with Reserved Font Name 'Source'"
    ):
        raise ReleaseFontError("Noto OFL copyright/RFN notice is not the expected one")


def decoded_names(font: Any) -> dict[int, str]:
    result: dict[int, str] = {}
    if "name" not in font:
        return result
    for record in font["name"].names:
        if record.nameID not in EXPECTED_NAMES:
            continue
        try:
            value = record.toUnicode()
        except Exception as exc:
            raise ReleaseFontError(f"cannot decode font name ID {record.nameID}") from exc
        previous = result.get(record.nameID)
        if previous is not None and previous != value:
            raise ReleaseFontError(f"font name ID {record.nameID} is ambiguous")
        result[record.nameID] = value
    return result


def audit_legal_metadata(font: Any, label: str) -> None:
    if "OS/2" not in font or int(font["OS/2"].fsType) != 0:
        raise ReleaseFontError(f"{label}: embedding permission fsType is not zero")
    names = decoded_names(font)
    for name_id, expected in EXPECTED_NAMES.items():
        if names.get(name_id) != expected:
            raise ReleaseFontError(f"{label}: unexpected/missing name ID {name_id}")
    joined = "\n".join(names.values()).casefold()
    if "microsoft" in joined or "ms gothic" in joined:
        raise ReleaseFontError(f"{label}: Microsoft font identity is forbidden")
    if "open font license" not in names[13].casefold():
        raise ReleaseFontError(f"{label}: OFL metadata is missing")


def audit_source(source: Path, codepoints: Sequence[int], TTFont: Any) -> ApprovedSource:
    if not source.is_file():
        raise ReleaseFontError(f"source font is missing: {source}")
    digest = sha256_file(source)
    approved = APPROVED_SOURCES.get(digest)
    if approved is None or source.stat().st_size != approved.size:
        raise ReleaseFontError(
            f"unapproved source font (size={source.stat().st_size}, sha256={digest})"
        )
    font = TTFont(str(source), lazy=False, recalcTimestamp=False)
    try:
        audit_legal_metadata(font, "source")
        cmap = font.getBestCmap() or {}
        missing = set(codepoints).difference(cmap)
        if missing:
            first = " ".join(f"U+{value:04X}" for value in sorted(missing)[:8])
            raise ReleaseFontError(f"source lacks required codepoints: {first}")
        if "CFF " not in font:
            raise ReleaseFontError("source is not the accepted OpenType/CFF font")
    finally:
        font.close()
    return approved


def subset_options(subset_module: Any) -> Any:
    """Return the fully pinned fontTools 4.62.1 option contract."""

    options = subset_module.Options()
    options.bidi_closure = True
    options.canonical_order = True
    options.desubroutinize = False
    options.drop_tables = [
        "JSTF", "DSIG", "EBDT", "EBLC", "EBSC", "PCLT", "LTSH", "Feat",
        "Glat", "Gloc", "Silf", "Sill",
    ]
    options.flavor = None
    options.glyph_names = False
    options.hinting = True
    options.hinting_tables = ["cvt", "cvar", "fpgm", "prep", "hdmx", "VDMX"]
    options.ignore_missing_glyphs = False
    options.ignore_missing_unicodes = False
    options.layout_closure = True
    options.layout_features = ["kern"]
    options.layout_scripts = ["*"]
    options.legacy_cmap = False
    options.legacy_kern = False
    options.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14]
    options.name_languages = [1033]
    options.name_legacy = False
    options.notdef_glyph = True
    options.notdef_outline = False
    options.obfuscate_names = False
    options.passthrough_tables = False
    options.prune_codepage_ranges = True
    options.prune_unicode_ranges = True
    options.recalc_average_width = False
    options.recalc_bounds = True
    options.recalc_max_context = False
    options.recalc_timestamp = False
    options.recommended_glyphs = False
    options.retain_gids = False
    options.symbol_cmap = False
    return options


def generate_once(
    source: Path,
    destination: Path,
    codepoints: Sequence[int],
    subset_module: Any,
    TTFont: Any,
) -> None:
    font = TTFont(
        str(source), lazy=False, recalcBBoxes=True, recalcTimestamp=False
    )
    try:
        subsetter = subset_module.Subsetter(options=subset_options(subset_module))
        subsetter.populate(unicodes=codepoints)
        subsetter.subset(font)
        font.recalcTimestamp = False
        font.save(str(destination), reorderTables=True)
    finally:
        font.close()


def layout_features(font: Any, table_tag: str) -> list[str]:
    if table_tag not in font:
        return []
    feature_list = font[table_tag].table.FeatureList
    if feature_list is None:
        return []
    return sorted(record.FeatureTag for record in feature_list.FeatureRecord)


def audit_output(path: Path, codepoints: Sequence[int], TTFont: Any) -> bytes:
    if not path.is_file():
        raise ReleaseFontError(f"release subset is missing: {path}")
    payload = path.read_bytes()
    digest = sha256_bytes(payload)
    if len(payload) != OUTPUT_SIZE or digest != OUTPUT_SHA256:
        raise ReleaseFontError(
            f"release subset differs from contract (size={len(payload)}, sha256={digest})"
        )
    font = TTFont(str(path), lazy=False, recalcTimestamp=False)
    try:
        audit_legal_metadata(font, "release subset")
        if set(font.keys()) != EXPECTED_TABLES:
            raise ReleaseFontError(
                "release subset table inventory differs from the accepted candidate"
            )
        cmap = font.getBestCmap() or {}
        if set(cmap) != set(codepoints):
            raise ReleaseFontError("release subset cmap is not exactly the authority set")
        if len(font.getGlyphOrder()) != AUTHORITY_COUNT + 1:
            raise ReleaseFontError("release subset glyph count is not 1,191")
        if layout_features(font, "GPOS") != ["kern"]:
            raise ReleaseFontError("release subset must retain only GPOS kern")
        if layout_features(font, "GSUB"):
            raise ReleaseFontError("release subset must not retain GSUB features")
    finally:
        font.close()
    return payload


def build_and_verify(source: Path, output: Path, *, check: bool) -> ApprovedSource:
    if output.name != OUTPUT_FILENAME:
        raise ReleaseFontError(f"release output filename must be {OUTPUT_FILENAME}")
    audit_license()
    codepoints = load_authority()
    subset_module, TTFont = require_fonttools()
    approved = audit_source(source, codepoints, TTFont)

    with tempfile.TemporaryDirectory(prefix="th07-noto-release-") as temporary:
        temporary_dir = Path(temporary)
        first = temporary_dir / "first.ttf"
        second = temporary_dir / "second.ttf"
        generate_once(source, first, codepoints, subset_module, TTFont)
        generate_once(source, second, codepoints, subset_module, TTFont)
        first_bytes = audit_output(first, codepoints, TTFont)
        second_bytes = audit_output(second, codepoints, TTFont)
        if first_bytes != second_bytes:
            raise ReleaseFontError("two clean subset builds are not byte-identical")

    if check:
        checked = audit_output(output, codepoints, TTFont)
        if checked != first_bytes:
            raise ReleaseFontError("checked-in release subset is not the regenerated output")
    else:
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(
            prefix=f".{OUTPUT_FILENAME}.", dir=output.parent, delete=False
        ) as stream:
            temporary_output = Path(stream.name)
            stream.write(first_bytes)
        try:
            os.replace(temporary_output, output)
        finally:
            temporary_output.unlink(missing_ok=True)
        audit_output(output, codepoints, TTFont)
    return approved


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--check",
        action="store_true",
        help="regenerate twice and verify the existing output without replacing it",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        source = args.source.resolve(strict=False)
        output = args.output.resolve(strict=False)
        if source == output:
            raise ReleaseFontError("source and output paths must differ")
        approved = build_and_verify(source, output, check=args.check)
    except (OSError, ReleaseFontError) as exc:
        print(f"release Noto subset failed: {exc}", file=sys.stderr)
        return 1
    verb = "audited" if args.check else "built"
    print(
        f"[OK] {verb} {output}: {OUTPUT_SIZE} bytes, sha256={OUTPUT_SHA256}, "
        f"source={approved.label}, fontTools={FONTTOOLS_VERSION}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
