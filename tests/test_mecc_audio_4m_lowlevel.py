from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AUDIO_C = ROOT / "psp" / "audio_me.c"
AUDIO_H = ROOT / "psp" / "audio_me.h"
CORE_H = ROOT / "psp" / "third_party" / "me-custom-core" / "me-core.h"
CUSTOM_H = ROOT / "psp" / "third_party" / "me-custom-core" / "me-core-custom.h"


class Full4MLowLevelSourceTest(unittest.TestCase):
    def test_exact_full_local_audio_partition(self) -> None:
        source = AUDIO_C.read_text(encoding="utf-8")
        self.assertIn("ME_LOCAL_BYTES = 0x00400000", source)
        # The active Main-RAM-SE profile owns only a non-null, lower-eDRAM
        # 384 KiB ring.  The old atlas/upper layout remains source-isolated for
        # an optional non-Main-RAM SFX profile, not this build.
        self.assertIn("ME_BGM_RING_BASE = 0x00010000", source)
        self.assertIn("ME_BGM_RING_END == 0x00070000u", source)
        self.assertIn("ME_BGM_RING_END <= 0x00200000u", source)
        self.assertIn(
            "defined(TH07_PSP_MECC_AUDIO_4M) && defined(TH07_PSP_SFX_MAIN_RAM)",
            source,
        )
        self.assertIn("ME_SFX_ATLAS_BASE = 0x00000000", source)
        self.assertIn("ME_SFX_ATLAS_BYTES = 0x00200000", source)
        self.assertIn("ME_BGM_RING_BASE = 0x00200000", source)
        self.assertIn("ME_BGM_RING_BYTES = 0x00060000", source)
        self.assertIn("ME_SFX_ATLAS_END == ME_BGM_RING_BASE", source)
        self.assertIn("ME_BGM_RING_END == 0x00260000u", source)
        self.assertIn("ME_BGM_RING_END <= ME_LOCAL_BYTES", source)

    def test_mainram_sfx_profile_cannot_write_lower_ring(self) -> None:
        source = AUDIO_C.read_text(encoding="utf-8")
        command_guard = source[
            source.index("ME_CMD_BGM_FETCH = 5") : source.index("ME_CMD_STOP = 0xff")
        ]
        self.assertIn("!defined(TH07_PSP_SFX_MAIN_RAM)", command_guard)
        stubs = source[
            source.index("// This profile keeps every SE sample in Main RAM") :
            source.index("int th07_psp_me_audio_stack_guard_ok")
        ]
        self.assertEqual(stubs.count("return 0;"), 3)
        self.assertIn("*base = 0", stubs)
        self.assertIn("*bytes = 0", stubs)

    def test_no_local_stack_or_accumulator_in_full_profile(self) -> None:
        source = AUDIO_C.read_text(encoding="utf-8")
        custom = CUSTOM_H.read_text(encoding="utf-8")
        core = CORE_H.read_text(encoding="utf-8")
        self.assertIn("TH07_ME_MAIN_STACK_BYTES 8192u", custom)
        self.assertIn("TH07_ME_MAIN_STACK_GUARD_BYTES 64u", custom)
        self.assertIn("gTh07MeMainStackArea", source)
        self.assertIn("gMeAudioWide", source)
        self.assertIn("sizeof(gMeAudioWide) == 2048u", source)
        sfx_mix = source[
            source.index("static void process_sfx_mix_on_me") :
            source.index("static void process_vertices_on_me")
        ]
        self.assertIn("chunkStart += ME_AUDIO_ACCUM_FRAMES", sfx_mix)
        self.assertIn("if (chunkFrames > ME_AUDIO_ACCUM_FRAMES)", sfx_mix)
        self.assertLess(
            core.index("gTh07MeMainStackArea + TH07_ME_MAIN_STACK_GUARD_BYTES"),
            core.index("HW_SYS_BUS_CLOCK_ENABLE"),
        )
        full_branch = core[core.index("#if defined(TH07_PSP_MECC_AUDIO_4M)") :]
        self.assertIn("#if !defined(TH07_PSP_MECC_AUDIO_4M)", full_branch)
        self.assertIn("stack_guards_match_on_me", source)
        self.assertIn("stack_guards_match_on_sc", source)

    def test_vme_mode_one_is_full_profile_only(self) -> None:
        custom = CUSTOM_H.read_text(encoding="utf-8")
        profile = custom[
            custom.index("#if defined(TH07_PSP_MECC_AUDIO_4M)",
                         custom.index("#define meLibUnlockMemory")) :
            custom.index("#define meLibSetMinimalVmeConfig")
        ]
        self.assertIn("ME_LIB_VME_MEMORY_MODE 1", profile)
        self.assertIn("ME_LIB_VME_MEMORY_MODE 2", profile)
        self.assertIn("hw(0xBCC00040) = ME_LIB_VME_MEMORY_MODE", custom)

    def test_base_zero_uses_non_null_cached_alias(self) -> None:
        source = AUDIO_C.read_text(encoding="utf-8")
        self.assertIn("(volatile unsigned char *)(0x80000000u | offset0)", source)
        self.assertIn("(const volatile short *)(0x80000000u | localOffset)", source)
        self.assertNotIn("(volatile unsigned char *)offset0", source)

    def test_sfx_upload_gather_and_mix_contract_is_public(self) -> None:
        header = AUDIO_H.read_text(encoding="utf-8")
        source = AUDIO_C.read_text(encoding="utf-8")
        for symbol in (
            "th07_psp_me_sfx_upload",
            "th07_psp_me_sfx_gather",
            "th07_psp_me_sfx_mix",
        ):
            self.assertIn(symbol, header)
            self.assertIn(symbol, source)
        for field in (
            "segment0Offset",
            "segment0Frames",
            "segment1Offset",
            "segment1Frames",
        ):
            self.assertIn(field, header)
        self.assertIn("TH07_PSP_ME_SFX_MAX_VOICES = 16", header)
        self.assertIn("voiceCount > TH07_PSP_ME_SFX_MAX_VOICES", source)
        self.assertIn("bytes1 == 0u && atlasOffset1 != 0u", source)
        self.assertIn("sfx_voice_valid", source)

    def test_power_lock_spans_the_irreversible_ownership_window(self) -> None:
        source = AUDIO_C.read_text(encoding="utf-8")
        init = source[
            source.index("int th07_psp_me_audio_init") :
            source.index("void th07_psp_me_audio_shutdown")
        ]
        shutdown = source[
            source.index("void th07_psp_me_audio_shutdown") :
            source.index("void th07_psp_me_audio_diag_window")
        ]
        acquire = source[
            source.index("static int acquire_power_lock") :
            source.index("static int release_power_lock_after_stop")
        ]
        release = source[
            source.index("static int release_power_lock_after_stop") :
            source.index("static int stack_guards_match(",
                         source.index("static int release_power_lock_after_stop"))
        ]
        self.assertIn("scePowerLock(0)", acquire)
        self.assertIn("result != 0", acquire)
        self.assertLess(init.index("acquire_power_lock()"),
                        init.index("meLibDefaultInit()"))
        self.assertIn("scePowerUnlock(0)", release)
        self.assertIn("result != 0", release)
        self.assertEqual(source.count("scePowerUnlock(0)"), 1)
        self.assertLess(shutdown.index("ME_WORKER_STOPPED"),
                        shutdown.index("stack_guards_match_on_sc"))
        self.assertLess(shutdown.index("stack_guards_match_on_sc"),
                        shutdown.index("release_power_lock_after_stop"))
        self.assertIn("gMeStarted, __ATOMIC_ACQUIRE", source)
        self.assertIn("ME_OWNER_SHUTDOWN", release)
        self.assertIn("workerState != ME_WORKER_STOPPED", release)
        self.assertIn("command != ME_CMD_NONE", release)
        self.assertIn("status != ME_STAT_DONE", release)
        self.assertIn("suspendRequested != 0u", release)
        self.assertIn("stack_guards_match_on_sc()", release)
        sleep = source[
            source.index("void meLibOnSleep") :
            source.index("void meLibOnWake")
        ]
        self.assertIn("box->suspendRequested = 1", sleep)
        self.assertIn("gMeUnsafe, 1", sleep)
        self.assertIn("gMeStarted, __ATOMIC_ACQUIRE", sleep)
        worker = source[
            source.index("void meLibOnProcess") :
            source.index("static void release_me")
        ]
        stop = worker[worker.index("if (command == ME_CMD_STOP)") :]
        self.assertLess(stop.index("box->command = ME_CMD_NONE"),
                        stop.index("box->status = ME_STAT_DONE"))
        between = stop[
            stop.index("box->command = ME_CMD_NONE") :
            stop.index("box->status = ME_STAT_DONE")
        ]
        self.assertIn('volatile("sync")', between)

    def test_every_full4m_me_owner_uses_priority_ceiling(self) -> None:
        source = AUDIO_C.read_text(encoding="utf-8")
        ceiling = source[
            source.index("typedef struct MeOwnerPriorityGuard") :
            source.index("static int claim_me_for_audio")
        ]
        self.assertIn("ME_OWNER_PRIORITY_CEILING = 0x11", source)
        self.assertIn("0x10 DAC worker", source)
        self.assertIn("sceKernelGetThreadCurrentPriority()", ceiling)
        self.assertIn("sceKernelChangeThreadPriority", ceiling)
        self.assertIn("MECC AUDIO4M PRIORITY RESTORE FAILED -> COLD REBOOT", ceiling)
        restore = ceiling[
            ceiling.index("static int restore_me_candidate_priority") :
            ceiling.index("static void publish_me_owner_priority")
        ]
        self.assertIn("sceKernelChangeThreadPriority", restore)
        self.assertIn("< 0", restore)
        self.assertIn("me_priority_restore_failed()", restore)

        # AUDIO, VERTEX, BGM/SFX/RESET and SHUTDOWN are the four owner CAS
        # sites.  Each candidate must enter the ceiling before publishing the
        # owner; a successful CAS then publishes only the saved release context.
        self.assertEqual(source.count("__atomic_compare_exchange_n(&gMeOwner"), 4)
        claim_ranges = (
            ("static int claim_me_for_audio", "static int claim_me_for_vertex"),
            ("static int claim_me_for_vertex", "static void poison_me"),
            ("static int claim_me_for_bgm", "static int claim_me_for_shutdown"),
            ("static int claim_me_for_shutdown", "static int dispatch_bgm"),
        )
        for start, end in claim_ranges:
            with self.subTest(claim=start):
                claim = source[source.index(start) : source.index(end)]
                self.assertLess(
                    claim.index("enter_me_candidate_priority(&priority)"),
                    claim.index("__atomic_compare_exchange_n(&gMeOwner"),
                )
                self.assertLess(
                    claim.index("__atomic_compare_exchange_n(&gMeOwner"),
                    claim.index("publish_me_owner_priority(&priority)"),
                )
                if "sceKernelDelayThread" in claim:
                    self.assertLess(
                        claim.index("__atomic_compare_exchange_n(&gMeOwner"),
                        claim.index("restore_me_candidate_priority(&priority)"),
                    )
                    self.assertLess(
                        claim.index("restore_me_candidate_priority(&priority)"),
                        claim.index("sceKernelDelayThread"),
                    )

        release = source[
            source.index("static void release_me") :
            source.index("static int claim_me_for_audio")
        ]
        self.assertLess(
            release.index("gMeOwner, ME_OWNER_NONE"),
            release.rindex("sceKernelChangeThreadPriority"),
        )
        self.assertIn("originalPriority > ME_OWNER_PRIORITY_CEILING", release)
        self.assertIn("sceKernelChangeThreadPriority", release)
        self.assertIn("< 0", release)
        self.assertIn("me_priority_restore_failed()", release)

        vertex = source[
            source.index("int th07_psp_me_vertex_pack") :
            source.index("int th07_psp_me_audio_mix")
        ]
        self.assertIn("sceKernelDelayThread(20)", vertex)

        shutdown = source[
            source.index("void th07_psp_me_audio_shutdown") :
            source.index("void th07_psp_me_audio_diag_window")
        ]
        precondition_failure = shutdown[
            shutdown.index("MECC AUDIO4M STOP PRECONDITION NG") - 300 :
            shutdown.index("MECC AUDIO4M STOP PRECONDITION NG") + 100
        ]
        self.assertNotIn("release_me()", precondition_failure)

    def test_psp_object_compiles_when_toolchain_is_available(self) -> None:
        compiler = shutil.which("psp-gcc")
        objdump = shutil.which("psp-objdump")
        if not compiler or not objdump:
            self.skipTest("PSP toolchain is not installed")
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "audio_me_4m.o"
            command = [
                compiler,
                "-DTH07_PSP_MECC_AUDIO_4M",
                "-DTH07_PSP",
                "-D_PSP_FW_VERSION=660",
                f"-I{ROOT}",
                f"-I{ROOT / 'psp'}",
                f"-I{ROOT / 'psp' / 'third_party' / 'me-custom-core'}",
                "-I/usr/local/pspdev/psp/include",
                "-I/usr/local/pspdev/psp/sdk/include",
                "-G0",
                "-march=allegrex",
                "-mtune=allegrex",
                "-fno-pic",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-std=gnu11",
                "-c",
                str(AUDIO_C),
                "-o",
                str(output),
            ]
            subprocess.run(command, check=True, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE)
            disassembly = subprocess.run(
                [objdump, "-dr", str(output)], check=True,
                stdout=subprocess.PIPE, text=True
            ).stdout
        start = disassembly.index("<meLibHandler>")
        return_instruction = re.search(r"\n.*\bjr\s+ra\b", disassembly[start:])
        self.assertIsNotNone(return_instruction)
        assert return_instruction is not None
        handler = disassembly[start : start + return_instruction.end()]
        self.assertIn("R_MIPS_HI16\tgTh07MeMainStackArea", handler)
        self.assertLess(handler.index("gTh07MeMainStackArea"), handler.index("0xbc10"))
        self.assertNotIn("80200000", handler)
        self.assertNotIn("80400000", handler)
        instruction_offsets = [
            int(match.group(1), 16)
            for match in re.finditer(r"^\s*([0-9a-f]+):", handler, re.MULTILINE)
        ]
        self.assertTrue(instruction_offsets)
        self.assertLess(max(instruction_offsets), 0x110)


class TwoSegmentSfxReferenceTest(unittest.TestCase):
    @staticmethod
    def logical_samples(
        atlas: list[int], offset0: int, frames0: int,
        offset1: int, frames1: int
    ) -> list[int]:
        return (
            atlas[offset0 : offset0 + frames0]
            + atlas[offset1 : offset1 + frames1]
        )

    def test_replica_prefix_flows_into_canonical_suffix(self) -> None:
        atlas = list(range(128))
        logical = self.logical_samples(atlas, 64, 5, 20, 7)
        self.assertEqual(logical, [64, 65, 66, 67, 68, 20, 21, 22, 23, 24, 25, 26])
        # A source position immediately before the boundary must consume both
        # local segments without inserting a padding or silence frame.
        self.assertEqual(logical[4:8], [68, 20, 21, 22])

    def test_canonical_descriptor_has_no_second_segment(self) -> None:
        atlas = list(range(64))
        self.assertEqual(self.logical_samples(atlas, 9, 6, 0, 0),
                         [9, 10, 11, 12, 13, 14])


if __name__ == "__main__":
    unittest.main()
