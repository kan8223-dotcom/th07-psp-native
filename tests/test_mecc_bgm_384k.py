from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class MeccBgmSourcePolicyTest(unittest.TestCase):
    def test_fixed_extent_and_stack_partition(self) -> None:
        source = (ROOT / "psp" / "audio_me.c").read_text(encoding="utf-8")
        self.assertIn("ME_BGM_RING_BASE = 0x00200000", source)
        self.assertIn("ME_BGM_RING_BYTES = 0x00060000", source)
        self.assertIn("ME_BGM_STACK_BOTTOM", source)
        self.assertIn("ME_BGM_RING_END <= ME_BGM_STACK_BOTTOM", source)
        self.assertIn("ME_BGM_REQUIRED_MODEL = 3", source)
        self.assertIn("ME_BGM_REQUIRED_TABLE = 2", source)

    def test_only_me_worker_dereferences_local_extent(self) -> None:
        sound = (ROOT / "psp" / "SoundPlayerPsp.cpp").read_text(encoding="utf-8")
        observer = (ROOT / "psp" / "shikigami_th07.c").read_text(encoding="utf-8")
        self.assertNotIn("0x00200000", sound)
        self.assertNotIn("0x00200000", observer)
        self.assertIn("ME_BGM_RING_BASE + offset", (ROOT / "psp" / "audio_me.c").read_text())

    def test_output_thread_never_waits_for_me(self) -> None:
        source = (ROOT / "psp" / "SoundPlayerPsp.cpp").read_text(encoding="utf-8")
        output = source[source.index("int BgmOutputThread") : source.index("void StopThreads")]
        feeder = source[
            source.index("int BgmMeccFeederThread") : source.index("int BgmOutputThread")
        ]
        self.assertNotIn("th07_psp_me_bgm_fetch", output)
        self.assertIn("th07_psp_me_bgm_fetch", feeder)
        self.assertIn("gMeccBgmFifo", output)

    def test_diagnostic_profile_isolated_and_mist_exclusive(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        profile = makefile[
            makefile.index("ifeq ($(PSP_MECC_BGM_384K),1)") :
            makefile.index("ifeq ($(PSP_DIRECT_GAME),1)")
        ]
        self.assertIn("PSP_SHIKIGAMI", profile)
        self.assertIn("PSP_EASY_MIST_AUDIO", profile)
        self.assertIn("TH07_PSP_MECC_BGM_384K", profile)
        self.assertIn("audit_mecc_proven.py", makefile)

    def test_generation_is_checked_after_blocking_commands(self) -> None:
        source = (ROOT / "psp" / "SoundPlayerPsp.cpp").read_text(encoding="utf-8")
        producer = source[
            source.index("int BgmProducerThread") : source.index("int BgmMeccFeederThread")
        ]
        feeder = source[
            source.index("int BgmMeccFeederThread") : source.index("int BgmOutputThread")
        ]
        comparison = "generation != __atomic_load_n(&gGeneration"
        self.assertIn(comparison, producer)
        self.assertIn(comparison, feeder)

    def test_track_reset_waits_for_all_sc_worker_epoch_acks(self) -> None:
        source = (ROOT / "psp" / "SoundPlayerPsp.cpp").read_text(encoding="utf-8")
        reset = source[source.index("void ResetRing") : source.index("u16 ReadLe16")]
        barrier = source[
            source.index("bool WaitForMeccScWorkers") : source.index("u8 MeccSelftestByte")
        ]
        self.assertIn("gMeccProducerAckGeneration", barrier)
        self.assertIn("gMeccFeederAckGeneration", barrier)
        self.assertIn("gMeccOutputAckGeneration", barrier)
        self.assertLess(reset.index("WaitForMeccScWorkers"), reset.index("th07_psp_me_bgm_reset"))
        self.assertLess(reset.index("th07_psp_me_bgm_reset"), reset.index("gMeccFifoWrite"))
        for thread in ("BgmProducerThread", "BgmMeccFeederThread", "BgmOutputThread"):
            body = source[source.index(f"int {thread}") :]
            self.assertLess(body.index("AckGeneration"), body.index("gSystemSuspended"))

    def test_transition_and_suspend_boundaries_are_fail_closed(self) -> None:
        source = (ROOT / "psp" / "SoundPlayerPsp.cpp").read_text(encoding="utf-8")
        reopen = source[source.index("ZunResult SoundPlayer::ReopenBGM") :
                        source.index("ZunResult SoundPlayer::PreloadBGM")]
        self.assertLess(reopen.index("LockFile();"), reopen.index("gGeneration"))
        callback = source[
            source.index('extern "C" void th07_psp_audio_set_system_suspended') :
            source.index("SoundPlayer g_SoundPlayer")
        ]
        self.assertIn("th07_psp_me_audio_reset_committed", callback)
        self.assertNotIn("th07_psp_boot_note", callback)
        release = source[source.index("ZunResult SoundPlayer::Release") :
                         source.index("i32 SoundPlayer::GetFmtIndexByName")]
        self.assertIn("th07_psp_me_audio_faulted", release)

    def test_mailbox_rejects_unaligned_command_buffers(self) -> None:
        source = (ROOT / "psp" / "audio_me.c").read_text(encoding="utf-8")
        self.assertIn("(box->bgmBufferPhys & 63u) != 0u", source)
        self.assertIn("((uintptr_t)source & 63u) != 0u", source)
        self.assertIn("((uintptr_t)destination & 63u) != 0u", source)

    def test_exact_command_granularity(self) -> None:
        source = (ROOT / "psp" / "SoundPlayerPsp.cpp").read_text(encoding="utf-8")
        self.assertRegex(source, r"kIoFrames \* kBytesPerFrame == 65536")
        self.assertRegex(source, r"kMeccFifoBlockBytes == 2048")
        self.assertRegex(source, r"kMeccFifoBlocks == 8")


class RingArithmeticTest(unittest.TestCase):
    def test_upload_and_fetch_offsets_never_cross_extent(self) -> None:
        ring_frames = 96 * 1024
        upload_frames = 16 * 1024
        fetch_frames = 512
        bytes_per_frame = 4

        self.assertEqual(ring_frames * bytes_per_frame, 393216)
        for write in range(0, ring_frames, upload_frames):
            offset = write * bytes_per_frame
            self.assertLessEqual(offset + upload_frames * bytes_per_frame, 393216)
            self.assertEqual(offset % 64, 0)
        for fetch in range(0, ring_frames, fetch_frames):
            offset = fetch * bytes_per_frame
            self.assertLessEqual(offset + fetch_frames * bytes_per_frame, 393216)
            self.assertEqual(offset % 64, 0)

    def test_profile_stamp_contains_backend_bit(self) -> None:
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        stamp = next(
            line for line in makefile.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_MECC_BGM_384K)", stamp)


if __name__ == "__main__":
    unittest.main()
