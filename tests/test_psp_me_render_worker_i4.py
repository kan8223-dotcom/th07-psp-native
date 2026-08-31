from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def make_target(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    match = re.search(
        r"\n(?=[A-Za-z0-9_.-]+(?:\s+[^\n:]*)?:)", makefile[start + 1 :]
    )
    return makefile[start:] if match is None else makefile[start : start + match.start() + 1]


class PspMeRenderWorkerI4Contracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.audio = (ROOT / "psp" / "audio_me.c").read_text(encoding="utf-8")
        cls.audio_h = (ROOT / "psp" / "audio_me.h").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src" / "BulletManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.graphics = (
            ROOT / "psp" / "graphics" / "PspGuGraphics.cpp"
        ).read_text(encoding="utf-8")
        cls.main = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")

    def test_profile_is_explicit_psp3000_performance_only(self) -> None:
        self.assertIn("PSP_ME_RENDER_RAW_LIVE ?= 0", self.makefile)
        self.assertIn("CXXFLAGS += -DTH07_PSP_ME_RENDER_RAW_LIVE", self.makefile)
        self.assertIn("CFLAGS += -DTH07_PSP_ME_RENDER_RAW_LIVE", self.makefile)
        self.assertIn(
            "$(error PSP_ME_RENDER_RAW_LIVE requires PSP_ME_RENDER_PERFORMANCE=1)",
            self.makefile,
        )
        self.assertIn(
            "$(error PSP_ME_RENDER_RAW_LIVE is PSP-2000+ only)", self.makefile
        )
        recipe = make_target(self.makefile, "psp3000-me-render-i4-raw-build")
        for setting in (
            "PSP_1000=0",
            "PSP_ME_RENDER_WORKER=1",
            "PSP_ME_RENDER_CORRECTNESS=1",
            "PSP_ME_RENDER_GE_CONSUME=1",
            "PSP_ME_RENDER_PERFORMANCE=1",
            "PSP_ME_RENDER_RAW_LIVE=1",
            "PSP_AUDIO4M_BUILD_ID=0x26083014u",
        ):
            self.assertIn(setting, recipe)
        self.assertIn("$(PSP_ME_RENDER_RAW_LIVE)-", self.makefile)
        self.assertIn("MERW I-ME4 RAW-LIVE BULLET", self.main)

    def test_raw_abi_has_independent_version_and_exact_sizes(self) -> None:
        for evidence in (
            "TH07_PSP_ME_RENDER_STREAM_RAW_VERSION = 0x4d453134u",
            "TH07_PSP_ME_RENDER_RAW_LAYOUT_VERSION = 0x524c3031u",
            "TH07_PSP_ME_RENDER_STREAM_RAW_RECORD_BYTES = 32",
            "sizeof(Th07PspMeRenderRawRecord) == 32u",
            "sizeof(Th07PspMeRenderRawLayout) == 116u",
        ):
            self.assertIn(evidence, self.audio_h + self.audio + self.bullets)
        for field in (
            "bulletBasePhys",
            "bulletStride",
            "bulletCount",
            "spriteBasePhys",
            "spriteStride",
            "spriteCount",
            "representativePhys",
            "representativeStride",
            "representativeCount",
            "vmRotationZOffset",
            "vmSpriteOffset",
        ):
            self.assertIn(field, self.audio_h)

    def test_sc_capture_is_32_bytes_and_defers_live_render_fields(self) -> None:
        capture = function_body(self.bullets, "PspMeRenderCaptureFusedRecord(")
        build = function_body(self.bullets, "PspMeRenderBuildFusedSnapshot(")
        self.assertIn("Th07PspMeRenderRawRecord &record", capture)
        for field in (
            "record.posXBits",
            "record.posYBits",
            "record.sinBits",
            "record.cosBits",
            "record.vmPhys",
            "record.logicalState",
            "record.slot",
            "record.generation",
        ):
            self.assertIn(field, capture)
        self.assertIn(
            "reinterpret_cast<Th07PspMeRenderRawRecord *>(", build
        )
        self.assertIn("sizeof(Th07PspMeRenderRawRecord)", build)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_JOB_RAW_LIVE", build)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_RAW_VERSION", build)

    def test_me_proves_exact_bullet_vm_and_sprite_ownership(self) -> None:
        layout = function_body(self.audio, "me_render_stream_raw_layout_valid(")
        record = function_body(self.audio, "me_render_stream_raw_record_valid(")
        sprite = function_body(self.audio, "me_render_stream_raw_sprite_physical(")
        for exact in (
            "layout->bulletStride != ME_RENDER_RAW_BULLET_STRIDE",
            "layout->vmBytes != ME_RENDER_RAW_VM_BYTES",
            "layout->spriteStride != ME_RENDER_RAW_SPRITE_BYTES",
        ):
            self.assertIn(exact, layout)
        self.assertIn("ME_RENDER_RAW_BULLET_COUNT", layout)
        self.assertIn("ME_RENDER_RAW_SPRITE_COUNT", layout)
        self.assertIn("record->logicalState < 1u", record)
        self.assertIn("record->logicalState > 5u", record)
        self.assertIn("record->slot * layout->bulletStride", record)
        self.assertIn("(record->logicalState - 1u) * layout->vmBytes", record)
        self.assertIn("record->vmPhys != expectedVm", record)
        self.assertIn("delta % layout->spriteStride", sprite)
        self.assertIn("delta / layout->spriteStride >= layout->spriteCount", sprite)

    def test_hidden_vm_is_rejected_before_sprite_or_rotation_dereference(self) -> None:
        reconstruct = function_body(
            self.audio, "me_render_stream_reconstruct_raw_record("
        )
        drawable = reconstruct.index("const int drawable")
        hidden_return = reconstruct.index("if (!drawable)")
        rotation = reconstruct.index("const uint32_t rotationBits")
        sprite_validation = reconstruct.index("me_render_stream_raw_sprite_physical(")
        self.assertLess(drawable, hidden_return)
        self.assertLess(hidden_return, rotation)
        self.assertLess(rotation, sprite_validation)

    def test_raw_cache_publication_and_me_invalidation_are_whole_cache(self) -> None:
        submit = function_body(self.audio, "th07_psp_me_render_stream_submit(")
        worker = function_body(self.audio, "process_render_stream_on_me(")
        self.assertIn("if (rawLive)\n        sceKernelDcacheWritebackAll();", submit)
        self.assertIn("if (rawLive)", worker)
        self.assertIn("meLibDcacheWritebackInvalidateAll();", worker)
        self.assertNotIn("sceAudiocodecGetEDRAM", worker)

    def test_record_failure_is_one_frame_fallback_not_process_poison(self) -> None:
        low_retire = function_body(
            self.audio, "th07_psp_me_render_stream_retire("
        )
        high_retire = function_body(
            self.bullets,
            "PspMeRenderCorrectnessRetire(bool atDeadline, u32 expectedDrawSeq)",
        )
        self.assertIn("int softRecordReject", low_retire)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD", low_retire)
        self.assertIn("const int recyclable = valid || softRecordReject", low_retire)
        self.assertIn("PspMeRenderRawRecordSoftRejectMatches", high_retire)
        self.assertIn("th07_psp_me_render_stream_release_ready", high_retire)
        self.assertIn("++gPspMeRenderShadowWindow.coverageDrop", high_retire)

    def test_live_owners_are_drained_before_stage_anm_release(self) -> None:
        deleted = function_body(self.bullets, "PspMeRenderManagerDeleted()")
        lifecycle = function_body(
            self.bullets, "BulletManager::DeletedCallback(BulletManager *arg)"
        )
        self.assertLess(
            deleted.index("Th07PspFenceMeRenderBeforeMeShutdown()"),
            deleted.index("th07_psp_me_render_stream_drain_live()"),
        )
        self.assertLess(
            deleted.index("th07_psp_me_render_stream_drain_live()"),
            deleted.index("PspMeRenderResetRepresentativeSourceCache()"),
        )
        self.assertIn("return rawOwnersDrained", deleted)
        self.assertIn("PspMeRenderRawFailStop(", deleted)
        self.assertIn("pspMeRenderOwnersSafe &&", lifecycle)
        self.assertLess(
            lifecycle.index("PspMeRenderManagerDeleted()"),
            lifecycle.index("g_AnmManager->ReleaseAnm(11)"),
        )

    def test_deadline_cannot_carry_a_live_reader_into_next_calc(self) -> None:
        deadline = function_body(
            self.bullets, "PspMeRenderCorrectnessDrawDeadline()"
        )
        fail_stop = function_body(self.bullets, "PspMeRenderRawFailStop(")
        self.assertIn("kPspMeRenderRawDeadlineTimeoutUs", deadline)
        self.assertIn("PspMeRenderCorrectnessRetire(", deadline)
        self.assertIn("gPspMeRenderShadow.drawSeq", deadline)
        self.assertIn("sceKernelDelayThread(20u)", deadline)
        self.assertIn("th07_psp_me_render_stream_hard_fault(&state.token)", deadline)
        self.assertIn("PspMeRenderRawFailStop(", deadline)
        self.assertIn("sceKernelExitGame()", fail_stop)
        self.assertIn("for (;;)", fail_stop)

    def test_i4_telemetry_uses_raw_stride(self) -> None:
        self.assertIn("PERF MERW I4", self.graphics)
        self.assertIn("PERF MERWT I4", self.graphics)
        self.assertIn("sizeof(Th07PspMeRenderRawRecord)", self.graphics)


if __name__ == "__main__":
    unittest.main()
