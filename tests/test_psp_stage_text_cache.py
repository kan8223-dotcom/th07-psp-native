from __future__ import annotations

import unittest
import random
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANM_SOURCE = ROOT / "src" / "AnmManager.cpp"
GUI_SOURCE = ROOT / "src" / "Gui.cpp"
ECL_SOURCE = ROOT / "src" / "EclManager.cpp"
BOMB_SOURCE = ROOT / "src" / "BombData.cpp"
TEXT_SOURCE = ROOT / "src" / "TextHelper.cpp"
TEXT_HEADER = ROOT / "src" / "TextHelper.hpp"
GAME_SOURCE = ROOT / "src" / "GameManager.cpp"
SOUND_HEADER = ROOT / "src" / "SoundPlayer.hpp"
SOUND_PSP_SOURCE = ROOT / "psp" / "SoundPlayerPsp.cpp"
OPTIONAL_SOURCE = ROOT / "psp" / "optional_ram_budget.cpp"
OPTIONAL_HEADER = ROOT / "psp" / "optional_ram_budget.hpp"


def function_body(source: str, signature: str, next_signature: str) -> str:
    return source[source.index(signature) : source.index(next_signature)]


def encode_transparent_span_rgba(raw: bytes) -> bytes:
    if len(raw) % 4:
        raise ValueError("RGBA input must contain complete pixels")
    output = bytearray()
    pixel_count = len(raw) // 4
    pixel_idx = 0
    while pixel_idx < pixel_count:
        pixel = raw[pixel_idx * 4 : pixel_idx * 4 + 4]
        transparent = pixel == b"\0\0\0\0"
        run = 1
        while run < 128 and pixel_idx + run < pixel_count:
            candidate = raw[(pixel_idx + run) * 4 : (pixel_idx + run + 1) * 4]
            if (candidate == b"\0\0\0\0") != transparent:
                break
            run += 1
        output.append((0x80 if transparent else 0) | (run - 1))
        if not transparent:
            output.extend(raw[pixel_idx * 4 : (pixel_idx + run) * 4])
        pixel_idx += run
    return bytes(output)


def decode_transparent_span_rgba(payload: bytes, raw_bytes: int) -> bytes | None:
    source_idx = 0
    output = bytearray()
    while source_idx < len(payload) and len(output) < raw_bytes:
        control = payload[source_idx]
        source_idx += 1
        run_bytes = ((control & 0x7F) + 1) * 4
        if len(output) + run_bytes > raw_bytes:
            return None
        if control & 0x80:
            output.extend(bytes(run_bytes))
        else:
            if source_idx + run_bytes > len(payload):
                return None
            output.extend(payload[source_idx : source_idx + run_bytes])
            source_idx += run_bytes
    if source_idx != len(payload) or len(output) != raw_bytes:
        return None
    return bytes(output)


def scan_synthetic_ecl_sub(blob: bytes, start: int = 0) -> tuple[bool, int]:
    """Behavioral model for the bounded PSP ECL source-enumeration contract."""
    end = len(blob)
    if start < 0 or start % 4 or end - start < 12:
        return False, 0
    cursor = start
    spell_count = 0
    while cursor < end and end - cursor >= 12:
        time, opcode, size, unused, difficulty, param_mask = struct.unpack_from(
            "<IhhBBH", blob, cursor
        )
        if size < 12 or size > end - cursor or size % 4:
            return False, spell_count
        physical_end = (
            time == 0xFFFFFFFF
            and opcode == -1
            and size == 12
            and unused == 0
            and difficulty == 0xFF
            and param_mask == 0x00FF
            and cursor + size == end
        )
        if physical_end:
            return True, spell_count
        if opcode == 90 and difficulty & 8:
            if size < 64:
                return False, spell_count
            decoded_name = bytes(byte ^ 0xAA for byte in blob[cursor + 16 : cursor + 64])
            if b"\0" not in decoded_name:
                return False, spell_count
            spell_count += 1
        cursor += size
    return False, spell_count


class EclPhysicalTerminatorBehaviorTest(unittest.TestCase):
    @staticmethod
    def marker() -> bytes:
        return struct.pack("<IhhBBH", 0xFFFFFFFF, -1, 12, 0, 0xFF, 0x00FF)

    def test_sentinel_only_subroutine_is_complete(self) -> None:
        self.assertEqual(scan_synthetic_ecl_sub(self.marker()), (True, 0))

    def test_opcode_one_does_not_hide_corrupt_trailing_bytes(self) -> None:
        logical_return = struct.pack("<IhhBBH", 0, 1, 12, 0, 8, 0)
        self.assertEqual(scan_synthetic_ecl_sub(logical_return + bytes(12)), (False, 0))

    def test_unaligned_start_and_instruction_size_fail_closed(self) -> None:
        self.assertEqual(scan_synthetic_ecl_sub(b"x" + self.marker(), 1), (False, 0))
        unaligned = struct.pack("<IhhBBH", 0, 2, 14, 0, 8, 0) + bytes(2)
        self.assertEqual(scan_synthetic_ecl_sub(unaligned + self.marker()), (False, 0))

    def test_bad_spell_payloads_fail_and_valid_payload_reaches_sentinel(self) -> None:
        short_spell = struct.pack("<IhhBBH", 0, 90, 12, 0, 8, 0)
        self.assertEqual(scan_synthetic_ecl_sub(short_spell + self.marker()), (False, 0))

        no_nul = struct.pack("<IhhBBH", 0, 90, 64, 0, 8, 0) + bytes([0] * 52)
        self.assertEqual(scan_synthetic_ecl_sub(no_nul + self.marker()), (False, 0))

        encoded = bytearray([0xAA] * 48)
        encoded[0:2] = bytes((ord("A") ^ 0xAA, 0xAA))
        valid_spell = (
            struct.pack("<IhhBBH", 0, 90, 64, 0, 8, 0) + bytes(4) + bytes(encoded)
        )
        self.assertEqual(scan_synthetic_ecl_sub(valid_spell + self.marker()), (True, 1))


class SpellNameSingleUploadTest(unittest.TestCase):
    def test_draw_string_format_uses_one_real_text_upload(self) -> None:
        source = ANM_SOURCE.read_text(encoding="utf-8")
        draw = function_body(
            source,
            "void AnmManager::DrawStringFormat(",
            "void AnmManager::DrawStringFormat2(",
        )

        # The real render clears and replaces the complete upload row.  The
        # old preliminary render of one space produced the same transparent
        # row and doubled a spell-name update from 32 KiB to 64 KiB.
        desktop_guard_start = draw.index("#if !defined(TH07_PSP)")
        desktop_guard_end = draw.index("#endif", desktop_guard_start)
        desktop_only = draw[desktop_guard_start:desktop_guard_end]
        psp_path = draw[desktop_guard_end:]
        self.assertEqual(desktop_only.count("this->DrawTextToSprite("), 1)
        self.assertIn('(char *)" "', desktop_only)
        self.assertEqual(psp_path.count("this->DrawTextToSprite("), 1)
        self.assertNotIn('(char *)" "', psp_path)
        self.assertLess(psp_path.index("x ="), psp_path.index("this->DrawTextToSprite("))
        self.assertLess(psp_path.index("this->DrawTextToSprite("),
                        psp_path.index("vm->visible = 1"))

    def test_single_render_still_replaces_the_complete_row(self) -> None:
        source = TEXT_SOURCE.read_text(encoding="utf-8")
        allocate = function_body(
            source,
            "bool TextHelper::AllocateBuffer(",
            "bool TextHelper::InvertAlpha(",
        )
        render = function_body(
            source,
            "void TextHelper::RenderTextToTextureBold(",
            "i32 TextHelper::GetLogicalStringWidth(",
        )
        make_upload = function_body(
            source,
            "static SDL_Surface *CreateTextUploadSurface(",
            "static void UploadTextPixels(",
        )
        upload = function_body(
            source,
            "static void UploadTextPixels(",
            "bool TextHelper::CopyTextToTexture(",
        )

        # AllocateBuffer clears all pixels which feed the filtered row before
        # any glyph is blended. CopyTextToTexture then uploads the whole sprite
        # width, including transparent pixels on both sides of the name.
        self.assertIn("SDL_Rect usedRect = {0, 0, width, height}", allocate)
        self.assertIn("SDL_FillRect(this->buffer, &usedRect, 0)", allocate)
        self.assertIn("g_TextWorkBuffer.AllocateBuffer(dWidth, dHeight)", render)
        self.assertIn("SDL_CreateRGBSurfaceWithFormat(0, spriteWidth, uploadHeight, 32",
                      make_upload)
        self.assertIn("SetTextureSubImage(0, yPos, spriteWidth, uploadHeight", upload)

    def test_dialogue_line_clear_is_not_removed_with_the_spell_clear(self) -> None:
        source = GUI_SOURCE.read_text(encoding="utf-8")
        dialogue = source[source.index("case MSG_DIALOGUE:") : source.index("case MSG_PAUSE:")]

        # This is semantically different from DrawStringFormat's redundant
        # internal clear: when a new line zero starts, it deliberately erases
        # the previously visible second dialogue line.
        self.assertIn("args->dialogue.textLine == 0", dialogue)
        self.assertIn('this->msg.dialogueLines + 1', dialogue)
        self.assertIn('this->msg.textColorsB[args->dialogue.textColor], " ")', dialogue)


class StageTextCacheContractTest(unittest.TestCase):
    def test_public_cache_api_is_borrowed_and_stage_scoped(self) -> None:
        header = TEXT_HEADER.read_text(encoding="utf-8")
        for declaration in (
            "static bool AttachStageTextCache(void *arena, u32 capacityBytes);",
            "static bool PreRenderTextToCacheBold(",
            "static bool EndStageTextCache(bool sourceEnumerationComplete);",
            "static bool IsStageTextCacheReady();",
            "static bool GetStageTextCacheStats(StageTextCacheStats *outStats);",
            "static void DetachStageTextCache();",
        ):
            self.assertIn(declaration, header)
        self.assertIn("borrowed", header)
        self.assertIn("sole allocator/owner", header)

    def test_cache_hit_requires_full_key_and_string_equality(self) -> None:
        source = TEXT_SOURCE.read_text(encoding="utf-8")
        find = function_body(
            source,
            "const StageTextCacheEntry *FindStageTextCache(",
            "bool StoreStageTextCache(",
        )

        # A hash-only hit could substitute a different Japanese string and is
        # therefore not an acceptable visual-preservation guarantee.
        self.assertIn("entry.hash == hash", find)
        self.assertIn("entry.stringBytes == stringBytes", find)
        self.assertIn("StageTextCacheKeysEqual(entry.key, key)", find)
        self.assertIn("std::memcmp(", find)

    def test_cache_is_one_contiguous_bounded_arena(self) -> None:
        source = TEXT_SOURCE.read_text(encoding="utf-8")
        self.assertIn("constexpr u32 kStageTextCacheMaxBytes", source)
        self.assertIn("u8 *arena", source)
        self.assertNotIn("std::vector<StageTextCacheEntry", source)
        self.assertNotIn("std::map<StageTextCache", source)
        self.assertNotIn("std::unordered_map<StageTextCache", source)

    def test_psp1000_attach_is_a_strict_no_pointer_retention_path(self) -> None:
        source = TEXT_SOURCE.read_text(encoding="utf-8")
        attach = function_body(
            source,
            "bool TextHelper::AttachStageTextCache(",
            "bool TextHelper::EndStageTextCache(bool sourceEnumerationComplete)",
        )
        self.assertIn("#if defined(TH07_PSP) && !defined(TH07_PSP_1000)", attach)
        no_op = attach[attach.index("#else") : attach.index("#endif")]
        self.assertIn("retains no pointer", no_op)
        self.assertIn("return false", no_op)
        self.assertNotIn("g_StageTextCache.arena =", no_op)

    def test_psp1000_compiles_out_cache_state_helpers_and_runtime_probe(self) -> None:
        source = TEXT_SOURCE.read_text(encoding="utf-8")
        cache_namespace = source.index(
            "#if defined(TH07_PSP) && !defined(TH07_PSP_1000)\nnamespace"
        )
        cache_namespace_end = source.index("} // namespace\n#endif", cache_namespace)
        for token in (
            "StageTextCacheState g_StageTextCache",
            "FindStageTextCache(",
            "StoreStageTextCache(",
            "DecodeStageTextCachePayload(",
        ):
            position = source.index(token)
            self.assertGreater(position, cache_namespace)
            self.assertLess(position, cache_namespace_end)

        render = function_body(
            source,
            "void TextHelper::RenderTextToTextureBold(",
            "i32 TextHelper::GetLogicalStringWidth(",
        )
        runtime_guard = render.index(
            "#if defined(TH07_PSP) && !defined(TH07_PSP_1000)"
        )
        runtime_end = render.index("#endif", runtime_guard)
        self.assertLess(runtime_guard, render.index("const StageTextCacheKey"))
        self.assertLess(render.index("FindStageTextCache("), runtime_end)

    def test_psp2000_gate_uses_actual_owner_allocations_not_free_queries(self) -> None:
        owner = OPTIONAL_SOURCE.read_text(encoding="utf-8")
        text = TEXT_SOURCE.read_text(encoding="utf-8")
        self.assertIn("kGuardBytes = 2u * 1024u * 1024u", owner)
        for size in ("1536u * 1024u", "768u * 1024u", "384u * 1024u", "256u * 1024u"):
            self.assertIn(size, owner)
        self.assertIn("std::malloc(kGuardBytes)", owner)
        self.assertNotIn("sceKernelTotalFreeMemSize", owner + text)
        self.assertNotIn("sceKernelMaxFreeMemSize", owner + text)

    def test_owner_is_the_only_allocator_of_stage_cache_storage(self) -> None:
        owner = OPTIONAL_SOURCE.read_text(encoding="utf-8")
        text = TEXT_SOURCE.read_text(encoding="utf-8")
        self.assertIn("std::malloc(bytes)", owner)
        self.assertIn("std::free(g_OptionalRamBudget.textPool)", owner)
        self.assertNotIn("std::malloc(requestedBytes)", text)
        self.assertNotIn("std::free(g_StageTextCache.arena)", text)
        detach = function_body(
            text,
            "void TextHelper::DetachStageTextCache()",
            "bool TextHelper::PreRenderTextToCacheBold(",
        )
        self.assertNotIn("std::free", detach)
        self.assertNotIn("free(", detach)

    def test_owner_reports_one_fixed_stage_budget_summary(self) -> None:
        owner = OPTIONAL_SOURCE.read_text(encoding="utf-8")
        self.assertEqual(owner.count('"optram guard=%s text=%uK bgmpre=0 anm=0"'), 1)
        self.assertEqual(owner.count('"i1text cap=%uK entries=%u cov=%u/%u hit=%u miss=%u full=%u ready=%u valid=%u"'), 1)
        prepare = function_body(
            owner,
            "bool Th07PspOptionalRamPrepareStage()",
            "bool Th07PspOptionalRamEnterGameplay(",
        )
        self.assertNotIn("th07_psp_boot_note", prepare)
        self.assertIn("reportPending", owner)

        release = function_body(
            owner,
            "void ReleaseTextPool()",
            "void ReleaseGuard()",
        )
        self.assertLess(release.index("ReportTextStats()"),
                        release.index("TextHelper::DetachStageTextCache()"))
        self.assertLess(release.index("TextHelper::DetachStageTextCache()"),
                        release.index("std::free(g_OptionalRamBudget.textPool)"))

    def test_release_text_buffer_routes_owner_lifetime_before_font_teardown(self) -> None:
        source = TEXT_SOURCE.read_text(encoding="utf-8")
        release = function_body(
            source,
            "void TextHelper::ReleaseTextBuffer()",
            "void TextHelper::RenderTextToTextureBold(",
        )
        guard = release.index("#if defined(TH07_PSP) && !defined(TH07_PSP_1000)")
        owner = release.index("Th07PspOptionalRamEndStage()", guard)
        self.assertLess(owner, release.index("g_TextWorkBuffer.ReleaseBuffer()"))

    def test_prerender_never_binds_or_uploads_a_texture(self) -> None:
        source = TEXT_SOURCE.read_text(encoding="utf-8")
        prerender = function_body(
            source,
            "bool TextHelper::PreRenderTextToCacheBold(",
            "ZunResult TextHelper::CreateTextBuffer()",
        )
        render = function_body(
            source,
            "void TextHelper::RenderTextToTextureBold(",
            "i32 TextHelper::GetLogicalStringWidth(",
        )
        self.assertIn("g_StageTextPreRenderOnly = true", prerender)
        self.assertIn("GfxTextureHandle()", prerender)
        self.assertIn("g_StageTextPreRenderOnly = false", prerender)
        self.assertNotIn("UploadTextPixels", prerender)
        self.assertIn(
            "g_StageTextPreRenderOnly &&\n"
            "                        StoreStageTextCache(cacheKey, string, uploadSurface)",
            render,
        )
        store = render.index("StoreStageTextCache(")
        prerender_branch = render.index("if (g_StageTextPreRenderOnly)", store)
        upload_branch = render.index("else", prerender_branch)
        upload = render.index("UploadTextPixels(", upload_branch)
        self.assertLess(store, prerender_branch)
        self.assertLess(prerender_branch, upload_branch)
        self.assertLess(upload_branch, upload)

    def test_publish_requires_full_zero_miss_100_percent_coverage(self) -> None:
        source = TEXT_SOURCE.read_text(encoding="utf-8")
        finish = function_body(
            source,
            "bool TextHelper::EndStageTextCache(bool sourceEnumerationComplete)",
            "bool TextHelper::IsStageTextCacheReady()",
        )
        for token in (
            "sourceEnumerationComplete",
            "expectedKeyCount == g_StageTextCache.coveredKeyCount",
            "g_StageTextCache.fullCount == 0",
            "g_StageTextCache.missCount == 0",
        ):
            self.assertIn(token, finish)

        find = function_body(
            source,
            "const StageTextCacheEntry *FindStageTextCache(",
            "bool StoreStageTextCache(",
        )
        self.assertIn("g_StageTextCache.ready = false", find)

        owner = OPTIONAL_SOURCE.read_text(encoding="utf-8")
        enter = function_body(
            owner,
            "bool Th07PspOptionalRamEnterGameplay(",
            "void Th07PspOptionalRamEndStage()",
        )
        self.assertLess(enter.index("ReleaseTextPool()"), enter.index("ReleaseGuard()"))
        self.assertLess(enter.index("th07_psp_boot_notef("), enter.index("ReleaseGuard()"))
        self.assertLess(enter.index("ReleaseGuard()"), enter.index("return textReady"))
        self.assertIn("partial caches are", enter)


class StageTextPreRenderIntegrationTest(unittest.TestCase):
    def test_msg_file_size_is_captured_before_other_loads(self) -> None:
        source = GUI_SOURCE.read_text(encoding="utf-8")
        load = function_body(
            source,
            "ZunResult Gui::LoadMsg(",
            "void Gui::FreeMsgFile()",
        )
        positions = (
            load.index("FileSystem::OpenFile(param_1, 0)"),
            load.index("this->impl->msgFileSize = g_LastFileSize"),
            load.index("this->impl->msg.currentMsgIdx = -1"),
        )
        self.assertEqual(positions, tuple(sorted(positions)))

        release = function_body(
            source,
            "void Gui::FreeMsgFile()",
            "bool Gui::PreRenderStageText()",
        )
        self.assertIn("this->impl->msgFileSize = 0", release)

    def test_msg_scan_is_current_character_only_and_fully_bounded(self) -> None:
        source = GUI_SOURCE.read_text(encoding="utf-8")
        prewarm = function_body(
            source,
            "bool Gui::PreRenderStageText()",
            "void Gui::MsgRead(",
        )
        for token in (
            "msgFileSize >= sizeof(MsgRawHeader)",
            "entryCount <= (msgFileSize - sizeof(MsgRawHeader)) / sizeof(u32)",
            "offset >= headerBytes && offset <= msgFileSize",
            "character >= CHAR_REIMU && character <= CHAR_SAKUYA",
            "const u32 firstEntry = static_cast<u32>(character) * 10u",
            "const u32 lastEntry = std::min(entryCount, firstEntry + 10u)",
            "candidate > startOffset && candidate < endOffset",
            "endOffset - cursor >= 4u",
            "const u32 instrBytes = 4u + static_cast<u32>(instr->argsize)",
            "instrBytes > endOffset - cursor",
            "instr->argsize < 5u",
            "std::memchr(textArgs.text, '\\0', textBytes)",
            "color < 0 || color >= 2 || line < 0 || line >= 2",
        ):
            self.assertIn(token, prewarm)
        self.assertIn("instr->opcode == MSG_DIALOGUE", prewarm)
        self.assertIn("instr->opcode == MSG_TEXT_INTRODUCE", prewarm)
        self.assertIn("instr->opcode == MSG_DELETE", prewarm)

        # Preserve and pre-render the deliberate stale-line clear as a cache
        # hit; it must not be confused with the removed spell-name clear.
        blank = prewarm.index('&dialogueLines[1]')
        self.assertLess(prewarm.rfind("if (line == 0)", 0, blank), blank)
        self.assertIn('kMsgTextColorsB[color], " ")', prewarm[blank : blank + 240])

    def test_ecl_spell_scan_is_current_difficulty_only_and_fully_bounded(self) -> None:
        source = ECL_SOURCE.read_text(encoding="utf-8")
        load = function_body(source, "ZunResult EclManager::Load(", "void EclManager::Unload()")
        self.assertLess(load.index("FileSystem::OpenFile(path, 0)"),
                        load.index("this->eclFileSize = g_LastFileSize"))

        unload = function_body(
            source,
            "void EclManager::Unload()",
            "u32 EclManager::PreRenderSpellcardNames(",
        )
        self.assertIn("this->eclFileSize = 0", unload)

        scan = function_body(
            source,
            "u32 EclManager::PreRenderSpellcardNames(",
            "ZunResult EclManager::CallEclSub(",
        )
        for token in (
            "this->eclFileSize < sizeof(EclRawHeader)",
            "const u32 headerBytes = sizeof(EclRawHeader) + subCount * sizeof(u32)",
            "constexpr u32 kMaxEclSubCount = 1024",
            "subCount > kMaxEclSubCount",
            "subCount > (this->eclFileSize - sizeof(EclRawHeader)) / sizeof(u32)",
            "offset < headerBytes",
            "offset > this->eclFileSize - sizeof(EclRawInstr)",
            "candidate > startOffset && candidate < endOffset",
            "timelineCount < 0 || timelineCount > 16",
            "offset < headerBytes || offset > this->eclFileSize",
            "constexpr u32 kInstrAlignment = alignof(EclRawInstr)",
            "reinterpret_cast<uintptr_t>(file) & (kInstrAlignment - 1u)",
            "offset & (kInstrAlignment - 1u)",
            "endOffset - cursor >= sizeof(EclRawInstr)",
            "instr->size < static_cast<i16>(sizeof(EclRawInstr))",
            "static_cast<u32>(instr->size) > endOffset - cursor",
            "static_cast<u32>(instr->size) & (kInstrAlignment - 1u)",
            "instr->time == 0xffffffffu",
            "instr->id == -1",
            "instr->unused_8 == 0",
            "instr->skipInstrOnDifficulty == 0xff",
            "instr->paramMask == 0x00ff",
            "cursor + static_cast<u32>(instr->size) == endOffset",
            "instr->id == 90",
            "instr->size < 64",
            "instr->skipInstrOnDifficulty & g_GameManager.difficultyMask",
            "std::memcpy(spellcardName, &instr->args[1], 48)",
            "spellcardName[i] = static_cast<u8>(spellcardName[i]) ^ 0xaa",
            "std::memchr(spellcardName, '\\0', 48)",
            "g_AnmManager->PreRenderString(nameVm, 0xfff0f0, 0, spellcardName)",
            "*sourceScanComplete = true",
        ):
            self.assertIn(token, scan)
        self.assertLess(scan.index("instr->size <"), scan.index("reachedPhysicalTerminator"))
        self.assertNotIn("if (instr->id == 1 &&", scan)

    def test_prerender_scan_failure_cannot_publish_and_vm_probe_restores_state(self) -> None:
        gui = GUI_SOURCE.read_text(encoding="utf-8")
        prewarm = function_body(gui, "bool Gui::PreRenderStageText()", "void Gui::MsgRead(")
        for token in (
            "const Rng rngBeforeVmInit = g_Rng",
            "const i32 scriptsBeforeVmInit",
            "const i32 scriptTicksBeforeVmInit",
            "g_Rng = rngBeforeVmInit",
            "SetScriptsExecuted(scriptsBeforeVmInit)",
            "SetScriptTicks(scriptTicksBeforeVmInit)",
            "bool messageScanComplete = false",
            "bool reachedTerminator = false",
            "bool spellScanComplete = false",
            "const bool spellEnumerationComplete = spellScanComplete && spellCount != 0",
            "bool bombScanComplete = true",
            "const bool sourceEnumerationComplete = !vmInitUsedRng",
            "TextHelper::EndStageTextCache(sourceEnumerationComplete)",
            'th07_psp_boot_notef("i1text OFF reason=%s", reason)',
        ):
            self.assertIn(token, prewarm)
        for reason in (
            "vm-rng",
            "msg-enum",
            "spell-enum",
            "spell-enum-0",
            "bomb-enum",
            "coverage",
        ):
            self.assertIn(f'"{reason}"', prewarm)

        text = TEXT_SOURCE.read_text(encoding="utf-8")
        finish = function_body(
            text,
            "bool TextHelper::EndStageTextCache(bool sourceEnumerationComplete)",
            "bool TextHelper::IsStageTextCacheReady()",
        )
        self.assertIn("g_StageTextCache.prewarming = false", finish)
        self.assertIn("g_StageTextCache.arena && sourceEnumerationComplete", finish)

    def test_bomb_names_have_one_runtime_and_prewarm_source_of_truth(self) -> None:
        source = BOMB_SOURCE.read_text(encoding="utf-8")
        expected = (
            "霊符「夢想封印　散」",
            "霊符「夢想封印　集」",
            "夢符「封魔陣」",
            "夢符「二重結界」",
            "魔符「スターダストレヴァリエ」",
            "魔符「ミルキーウェイ」",
            "恋符「ノンディレクショナルレーザー」",
            "恋符「マスタースパーク」",
            "幻符「インディスクリミネイト」",
            "幻符「殺人ドール」",
            "時符「パーフェクトスクウェア」",
            "時符「プライベートスクウェア」",
        )
        table = source[source.index("const char *const g_BombNames[6][2]") :
                       source.index("} // namespace")]
        for name in expected:
            self.assertEqual(source.count(f'"{name}"'), 1)
            self.assertIn(f'"{name}"', table)

        import re

        runtime_pairs = {
            (int(shot), int(focused))
            for shot, focused in re.findall(r"ShowBombNamePortrait\([^,]+, GetBombName\((\d), (\d)\)\)",
                                             source)
        }
        self.assertEqual(runtime_pairs, {(shot, focused) for shot in range(6)
                                         for focused in range(2)})

        getter = function_body(
            source,
            "const char *BombData::GetBombName(",
            "void BombData::DarkenViewport(",
        )
        self.assertIn("shotTypeAndCharacter < 0 || shotTypeAndCharacter >= 6", getter)
        self.assertIn("focused < 0 || focused >= 2", getter)
        self.assertIn("return g_BombNames[shotTypeAndCharacter][focused]", getter)

        gui = GUI_SOURCE.read_text(encoding="utf-8")
        prewarm = function_body(gui, "bool Gui::PreRenderStageText()", "void Gui::MsgRead(")
        self.assertIn(
            "BombData::GetBombName(g_GameManager.shotTypeAndCharacter, focused)", prewarm
        )
        self.assertIn("focused < 2", prewarm)

    def test_prerender_geometry_matches_the_two_runtime_text_paths(self) -> None:
        source = ANM_SOURCE.read_text(encoding="utf-8")
        vm_text = function_body(
            source,
            "bool AnmManager::PreRenderVmText(",
            "bool AnmManager::PreRenderString(",
        )
        string = function_body(
            source,
            "bool AnmManager::PreRenderString(",
            "void AnmManager::DrawStringFormat(",
        )
        runtime_vm = function_body(
            source,
            "void AnmManager::DrawVmTextFmt(",
            "bool AnmManager::PreRenderVmText(",
        )
        runtime_string = function_body(
            source,
            "void AnmManager::DrawStringFormat(",
            "void AnmManager::DrawStringFormat2(",
        )

        for token in (
            "vm->sprite->startPixelInclusive.x",
            "vm->sprite->startPixelInclusive.y",
            "vm->sprite->textureWidth",
            "vm->sprite->textureHeight",
            "fontWidth",
            "fontHeight",
            "vm->sprite->cols",
            "vm->sprite->rows",
        ):
            self.assertIn(token, vm_text)
            self.assertIn(token, runtime_vm)

        for token in (
            "vm->sprite->startPixelInclusive.x + vm->sprite->widthPx * vm->sprite->cols",
            "TextHelper::GetLogicalStringWidth(text)",
            "fontWidth *",
            "vm->sprite->cols / 2.0f",
            "vm->sprite->startPixelInclusive.y",
            "vm->sprite->textureWidth",
            "vm->sprite->textureHeight",
            "vm->sprite->rows",
        ):
            self.assertIn(token, string)
        self.assertIn("TextHelper::GetLogicalStringWidth(buf)", runtime_string)
        self.assertIn("vm->sprite->cols / 2.0f", runtime_string)

    def test_stage_cache_hooks_move_after_audio_queues_and_before_gameplay(self) -> None:
        source = GUI_SOURCE.read_text(encoding="utf-8")
        prewarm = function_body(
            source,
            "bool Gui::PreRenderStageText()",
            "void Gui::MsgRead(",
        )
        positions = (
            prewarm.index("PreRenderVmText("),
            prewarm.index("PreRenderSpellcardNames("),
            prewarm.index("BombData::GetBombName("),
            prewarm.index("TextHelper::EndStageTextCache(sourceEnumerationComplete)"),
        )
        self.assertEqual(positions, tuple(sorted(positions)))

        added = function_body(
            source,
            "ZunResult Gui::ActualAddedCallback()",
            "ZunResult Gui::LoadMsg(",
        )
        self.assertNotIn("PreRenderStageText()", added)

        game = GAME_SOURCE.read_text(encoding="utf-8")
        game_added = function_body(
            game,
            "ZunResult GameManager::AddedCallback(",
            "ZunResult GameManager::DeletedCallback(",
        )
        gate_on = game_added.index("SetBgmStageLoadBlocked(true)")
        gate_off = game_added.index("SetBgmStageLoadBlocked(false)")
        positions = (
            gate_on,
            game_added.index("while (g_SoundPlayer.ProcessQueues())"),
            game_added.index("Th07PspOptionalRamPrepareStage()"),
            game_added.index("g_Gui.PreRenderStageText()"),
            game_added.index("Th07PspOptionalRamEnterGameplay(textCoverageComplete)"),
            game_added.index("arg->notInMenu = 1"),
            game_added.index('th07_psp_boot_note("game added ready")'),
            gate_off,
            game_added.rindex("return ZUN_SUCCESS"),
        )
        self.assertEqual(positions, tuple(sorted(positions)))
        guard = game_added.rfind(
            "#if defined(TH07_PSP) && !defined(TH07_PSP_1000)", 0, gate_on
        )
        self.assertGreaterEqual(guard, 0)
        self.assertNotIn("return", game_added[gate_on:gate_off])

        deleted = function_body(
            source,
            "ZunResult Gui::DeletedCallback(",
            "ZunResult Gui::RegisterChain(",
        )
        self.assertLess(deleted.index("Th07PspOptionalRamEndStage()"),
                        deleted.index("g_AnmManager->ReleaseAnm(24)"))

    def test_stage_load_gate_blocks_only_bgm_consumption(self) -> None:
        header = SOUND_HEADER.read_text(encoding="utf-8")
        self.assertIn("void SetBgmStageLoadBlocked(bool blocked);", header)

        sound = SOUND_PSP_SOURCE.read_text(encoding="utf-8")
        output = function_body(sound, "int BgmOutputThread(SceSize, void *)", "void StopThreads()")
        wants = output[output.index("const bool wantsBgm") : output.index("if (wantsBgm)")]
        for condition in ("gBgmPlaying", "gBgmPaused", "gBgmStageLoadBlocked"):
            self.assertIn(condition, wants)

        setter = function_body(
            sound,
            "void SoundPlayer::SetBgmStageLoadBlocked(bool blocked)",
            "ZunResult SoundPlayer::ReopenBGM(",
        )
        self.assertIn(
            "__atomic_store_n(&gBgmStageLoadBlocked, blocked, __ATOMIC_RELEASE)", setter
        )
        self.assertLess(
            setter.index('th07_psp_boot_note(blocked ? "bgm stage-load gate on"'),
            setter.index("__atomic_store_n(&gBgmStageLoadBlocked"),
        )
        for forbidden in (
            "__atomic_store_n(&gReadFrame",
            "__atomic_store_n(&gWriteFrame",
            "__atomic_add_fetch(&gGeneration",
            "__atomic_store_n(&gBgmPlaying",
            "__atomic_store_n(&gBgmPaused",
        ):
            self.assertNotIn(forbidden, setter)

        initialize = function_body(
            sound,
            "ZunResult SoundPlayer::InitializeSound()",
            "ZunResult SoundPlayer::InitSoundBuffers()",
        )
        self.assertIn("gBgmStageLoadBlocked = false", initialize)
        release = function_body(
            sound, "ZunResult SoundPlayer::Release()", "ZunResult SoundPlayer::LoadFmt("
        )
        self.assertIn(
            "__atomic_store_n(&gBgmStageLoadBlocked, false, __ATOMIC_RELEASE)", release
        )


class StageTextCacheCompressionTest(unittest.TestCase):
    def test_transparent_span_codec_is_byte_exact_on_adversarial_rows(self) -> None:
        rng = random.Random(0x1707)
        transparent = b"\0\0\0\0"
        opaque = bytes((17, 93, 201, 255))
        cases = {
            "empty": b"",
            "all-transparent": transparent * 8192,
            "all-opaque": opaque * 8192,
            "alternating": (transparent + opaque) * 4096,
            "run-127": transparent * 127 + opaque,
            "run-128": transparent * 128 + opaque,
            "run-129": transparent * 129 + opaque,
            "random": bytes(rng.randrange(256) for _ in range(8192 * 4)),
        }
        for name, raw in cases.items():
            with self.subTest(name=name):
                payload = encode_transparent_span_rgba(raw)
                self.assertEqual(decode_transparent_span_rgba(payload, len(raw)), raw)

    def test_sparse_text_like_row_compresses_below_raw_size(self) -> None:
        row = bytearray(512 * 16 * 4)
        # Synthetic coloured glyph strokes separated by transparent columns.
        for y in range(3, 14):
            for glyph in range(18):
                base_x = 8 + glyph * 24
                for x in (base_x, base_x + 1, base_x + 10, base_x + 11):
                    offset = (y * 512 + x) * 4
                    row[offset : offset + 4] = bytes((240, 224, 192, 255))
        payload = encode_transparent_span_rgba(bytes(row))
        self.assertLess(len(payload), len(row) // 4)
        self.assertEqual(decode_transparent_span_rgba(payload, len(row)), bytes(row))

    def test_decoder_rejects_truncation_and_output_overrun(self) -> None:
        literal_two_pixels = bytes((1,)) + bytes(range(8))
        self.assertIsNone(decode_transparent_span_rgba(literal_two_pixels[:-1], 8))
        transparent_128 = bytes((0xFF,))
        self.assertIsNone(decode_transparent_span_rgba(transparent_128, 4))

    def test_cpp_decoder_checks_both_input_and_output_boundaries(self) -> None:
        source = TEXT_SOURCE.read_text(encoding="utf-8")
        decode = function_body(
            source,
            "const u8 *DecodeStageTextCachePayload(",
            "bool StoreStageTextCache(",
        )
        for token in (
            "entry.rawBytes > kStageTextCacheDecodeBytes",
            "entry.encodedBytes > g_StageTextCache.capacityBytes - entry.payloadOffset",
            "entry.encodedBytes != entry.rawBytes",
            "entry.encoding != STAGE_TEXT_CACHE_ZERO_RLE",
            "runBytes > static_cast<u32>(outEnd - out)",
            "runBytes > static_cast<u32>(inEnd - in)",
            "in == inEnd && out == outEnd",
        ):
            self.assertIn(token, decode)


if __name__ == "__main__":
    unittest.main()
