from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class SfxMainRamRoutePolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.sound = (ROOT / "psp/SoundPlayerPsp.cpp").read_text(encoding="utf-8")
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")

    def test_audio4m_profile_selects_main_ram_sfx_explicitly(self) -> None:
        profile = self.makefile[
            self.makefile.index("ifeq ($(PSP_MECC_AUDIO_4M),1)") :
            self.makefile.index("ifeq ($(PSP_DIRECT_GAME),1)")
        ]
        self.assertIn("-DTH07_PSP_MECC_AUDIO_4M", profile)
        self.assertIn("-DTH07_PSP_SFX_MAIN_RAM", profile)
        self.assertIn("-DTH07_PSP_BGM_MAIN_RAM", profile)
        self.assertIn("-DTH07_PSP_GE_PORTRAIT_CACHE", profile)

    def test_samples_remain_owned_by_main_ram(self) -> None:
        load = self.sound[
            self.sound.index("ZunResult SoundPlayer::LoadSound") :
            self.sound.index("ZunResult SoundPlayer::LoadFmt")
        ]
        self.assertIn("new (std::nothrow) PspSfxSample[storedFrames]", load)
        self.assertIn(
            "defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)",
            load,
        )
        self.assertIn("sceKernelDcacheWritebackRange(samples", load)
        self.assertLess(
            load.index("th07_audio4m_sfx_upload_buffer"),
            load.index("sceKernelDcacheWritebackRange(samples"),
        )

    def test_output_uses_sc_mixer_but_keeps_audio4m_dac_guards(self) -> None:
        output = self.sound[
            self.sound.index("int BgmOutputThread") :
            self.sound.index("void StopThreads")
        ]
        self.assertIn("const bool haveSfx = MixSfxBlock(block, kFramesPerOutput)", output)
        self.assertIn("sceAudioOutputBlocking(gAudioChannel, PSP_AUDIO_VOLUME_MAX, block)", output)
        self.assertIn("LatchMeccFatal(\"MECC AUDIO4M DAC OUTPUT FAILED", output)
        self.assertIn("gBgmOutputWraps", output)
        mixer = self.sound[
            self.sound.index("bool MixSfxBlock") : self.sound.index("void CloseTrackFile")
        ]
        self.assertIn("th07_psp_sc_audio_mix_into", mixer)
        self.assertIn("mixJob.mixDivisor = 1", mixer)

    def test_requests_and_stops_use_main_cpu_voice_masks(self) -> None:
        stop = self.sound[
            self.sound.index("void SoundPlayer::StopSoundByIdx") :
            self.sound.index("i32 SoundPlayer::ProcessQueues")
        ]
        process = self.sound[
            self.sound.index("i32 SoundPlayer::ProcessQueues") :
            self.sound.index("void SoundPlayer::PushCommand")
        ]
        selector = (
            "defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_SFX_MAIN_RAM)"
        )
        self.assertIn(selector, stop)
        self.assertIn("gStopSfxMaskLow", stop)
        self.assertIn(selector, process)
        self.assertIn("gPendingSfxMaskLow", process)

    def test_me_sfx_atlas_lifecycle_is_not_started_in_main_ram_profile(self) -> None:
        init = self.sound[
            self.sound.index("ZunResult SoundPlayer::InitSoundBuffers") :
            self.sound.index("void SoundPlayer::PlaySoundByIdx")
        ]
        self.assertGreaterEqual(init.count("!defined(TH07_PSP_SFX_MAIN_RAM)"), 3)
        self.assertIn("SE Main RAM PCM / SC wide mixer", init)
        self.assertIn("BGM Main RAM 384K; ME eDRAM disabled", init)

    def test_bgm_uses_sc_direct_main_ram_and_retains_shikigami(self) -> None:
        self.assertIn(
            "defined(TH07_PSP_MECC_AUDIO_4M) && !defined(TH07_PSP_BGM_MAIN_RAM)",
            self.sound,
        )
        self.assertIn("alignas(64) i16 gBgmRing[kRingFrames * kChannels]", self.sound)
        self.assertIn("BGM MAIN RAM 384K SC-DIRECT; ME EDRAM UNUSED", self.sound)
        self.assertIn("th07_psp_audio_shikigami_snapshot", self.sound)
        telemetry = self.sound[
            self.sound.index("th07_psp_audio_shikigami_snapshot") :
        ]
        self.assertIn("Th07Audio4mSfxSnapshot sfx{}", telemetry)
        self.assertIn("#if !defined(TH07_PSP_SFX_MAIN_RAM)", telemetry)


if __name__ == "__main__":
    unittest.main()
