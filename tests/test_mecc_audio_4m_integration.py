from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SFX_SOURCE = ROOT / "psp" / "audio4m_sfx.cpp"
SOUND_SOURCE = ROOT / "psp" / "SoundPlayerPsp.cpp"
MAKEFILE = ROOT / "Makefile"


def allocate_prefixes(buffer_bytes: list[int], atlas_bytes: int) -> list[int]:
    """Reference the C++ exact-fill allocator without touching PSP memory."""
    canonical = sum(buffer_bytes)
    replica = atlas_bytes - canonical
    count = len(buffer_bytes)
    if replica < count * 2 or replica > canonical:
        raise ValueError("unsupported canonical/replica split")
    remaining = replica - count * 2
    capacity = canonical - count * 2
    prefixes: list[int] = []
    for size in buffer_bytes:
        extra_capacity = size - 2
        extra = (remaining * extra_capacity // capacity) if capacity else 0
        extra &= ~1
        prefixes.append(2 + min(extra, extra_capacity))
    left = replica - sum(prefixes)
    for index, size in enumerate(buffer_bytes):
        if left == 0:
            break
        add = min(left, size - prefixes[index]) & ~1
        prefixes[index] += add
        left -= add
    if left:
        raise ValueError("allocator left unassigned bytes")
    return prefixes


def defer_initial_continuous_block(
    expected_next: int, continues: int, available: int, initial_prefill: int
) -> bool:
    """Executable reference for the output-side startup arm decision."""
    return expected_next == 0 and continues != 0 and available < initial_prefill


def mix_wide_sfx_into_bgm(background: int, effect: int) -> tuple[int, int]:
    """Reference the sole final s32-to-s16 saturation in the R4 path."""
    total = background + effect
    if total > 32767:
        return 32767, 1
    if total < -32768:
        return -32768, 1
    return total, 0


class ExactAtlasPlannerTest(unittest.TestCase):
    def test_adversarial_splits_fill_every_byte_without_padding(self) -> None:
        atlas = 2 * 1024 * 1024
        cases = (
            [34952] * 29 + [34954],
            [2] * 29 + [atlas // 2 - 58],
            [2000 + index * 3000 for index in range(30)],
        )
        for raw in cases:
            sizes = list(raw)
            if sum(sizes) > atlas - 60:
                excess = sum(sizes) - (atlas - 60)
                sizes[-1] -= excess
            if sum(sizes) < atlas // 2:
                sizes[-1] += atlas // 2 - sum(sizes)
            prefixes = allocate_prefixes(sizes, atlas)
            self.assertEqual(sum(sizes) + sum(prefixes), atlas)
            self.assertTrue(all(prefix % 2 == 0 for prefix in prefixes))
            self.assertTrue(all(2 <= prefix <= size for prefix, size in zip(prefixes, sizes)))

    def test_invalid_too_small_canonical_set_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            allocate_prefixes([2] * 30, 2 * 1024 * 1024)


class FullAudioSourcePolicyTest(unittest.TestCase):
    def test_audio4m_profile_selects_main_ram_bgm_backend(self) -> None:
        makefile = MAKEFILE.read_text(encoding="utf-8")
        self.assertIn("PSP_AUDIO4M_BUILD_ID ?= 0x2608280bu", makefile)
        self.assertIn("TH07_SHIKIGAMI_BUILD_ID=$(PSP_AUDIO4M_BUILD_ID)", makefile)
        self.assertIn("-DTH07_PSP_BGM_MAIN_RAM", makefile)
        self.assertIn("TH07 SHIKIGAMI MAIN RAM AUDIO GE", makefile)

        source = SOUND_SOURCE.read_text(encoding="utf-8")
        selector = (
            "(defined(TH07_PSP_MECC_AUDIO_4M) && "
            "!defined(TH07_PSP_BGM_MAIN_RAM))"
        )
        self.assertIn(selector, source)
        self.assertIn("BGM MAIN RAM 384K SC-DIRECT; ME EDRAM UNUSED", source)
        self.assertIn("alignas(64) i16 gBgmRing[kRingFrames * kChannels]", source)

    def test_dac_pcm_storage_is_double_buffered(self) -> None:
        source = SOUND_SOURCE.read_text(encoding="utf-8")
        output = source[
            source.index("int BgmOutputThread") : source.index("void StopThreads")
        ]
        self.assertIn("constexpr u32 kDacBufferCount = 2", source)
        self.assertIn(
            "i16 blocks[kDacBufferCount][kFramesPerOutput * kChannels]", output
        )
        self.assertIn("i16 *const block = blocks[outputIndex]", output)
        submit = output.index("sceAudioOutputBlocking")
        toggle = output.index("outputIndex ^= 1u", submit)
        self.assertLess(submit, toggle)
        self.assertLess(output.index("if (outputResult < 0)"), toggle)
        self.assertIn("std::memcpy(block, slot.samples, kDacBufferBytes)", output)
        self.assertIn("std::memset(block, 0, kDacBufferBytes)", output)

    def test_atlas_is_exact_and_has_no_padding_path(self) -> None:
        source = SFX_SOURCE.read_text(encoding="utf-8")
        self.assertIn("constexpr unsigned int kAtlasBytes = 2u * 1024u * 1024u", source)
        self.assertIn("gAtlasLogicalBytes != kAtlasBytes", source)
        self.assertIn("gUploadedBytes != kAtlasBytes", source)
        self.assertIn("AppendCanonicalPrefix", source)
        finalize = source[source.index("int th07_audio4m_sfx_finalize") :]
        self.assertNotIn("memset(gStaging + gStageBytes", finalize)

    def test_coverage_is_two_real_dac_passes(self) -> None:
        source = SFX_SOURCE.read_text(encoding="utf-8")
        self.assertIn("gCoverageNextPass == 2u", source)
        self.assertIn("voice.replica = gCoverageNextPass != 0u", source)
        self.assertIn("kCoverageVoice = kVoiceCount - 1u", source)
        commit = source[
            source.index("void th07_audio4m_sfx_output_committed") :
            source.index("void th07_audio4m_sfx_shutdown")
        ]
        self.assertLess(commit.index("if (!submitted)"),
                        commit.index("gCanonicalOutputMask"))
        self.assertIn("(canonical & kRequiredMask) == kRequiredMask", commit)
        self.assertIn("(replica & kRequiredMask) == kRequiredMask", commit)

    def test_continuous_sfx_prefill_is_independent_of_fifo_capacity(self) -> None:
        source = SFX_SOURCE.read_text(encoding="utf-8")
        consume = source[
            source.index("unsigned int th07_audio4m_sfx_consume") :
            source.index("void th07_audio4m_sfx_output_committed")
        ]
        self.assertIn("const unsigned int available = write - read", consume)
        self.assertIn("DeferInitialContinuousBlock(", consume)
        helper = source[
            source.index("constexpr bool DeferInitialContinuousBlock") :
            source.index("struct AtlasBuffer")
        ]
        self.assertIn("constexpr unsigned int kFifoBlocks = 2u", source)
        self.assertIn("constexpr unsigned int kInitialPrefillBlocks = 2u", source)
        self.assertIn("available < kInitialPrefillBlocks", helper)
        self.assertNotIn("available < kFifoBlocks", helper)
        self.assertIn("DeferInitialContinuousBlock(0u, 1u, 1u)", helper)
        self.assertIn(
            "DeferInitialContinuousBlock(0u, 1u, kInitialPrefillBlocks)",
            helper,
        )
        self.assertIn("DeferInitialContinuousBlock(1u, 1u, 1u)", helper)
        self.assertIn("DeferInitialContinuousBlock(0u, 0u, 1u)", helper)
        self.assertIn("sizeof(FifoBlock) == 4160u", source)
        self.assertIn("sizeof(gFifo) == 8320u", source)
        self.assertLess(
            consume.index("DeferInitialContinuousBlock("),
            consume.index("gConsumedToken, slot.token"),
        )
        # A promised continuation still fails closed after playback starts.
        self.assertIn("MECC AUDIO4M SFX FIFO MISS -> COLD REBOOT", consume)

        cases = (
            ((0, 1, 1, 2), True),   # initial continuous block: hold
            ((0, 1, 2, 2), False),  # startup prefill complete: arm
            ((1, 1, 1, 2), False),  # armed stream: never conceal underrun
            ((0, 0, 1, 2), False),  # terminal one-block sound: play now
        )
        for arguments, expected in cases:
            with self.subTest(arguments=arguments):
                self.assertEqual(
                    defer_initial_continuous_block(*arguments), expected
                )

    def test_sfx_remains_wide_until_the_single_final_mix(self) -> None:
        sfx = SFX_SOURCE.read_text(encoding="utf-8")
        me = (ROOT / "psp" / "audio_me.c").read_text(encoding="utf-8")
        header = (ROOT / "psp" / "audio_me.h").read_text(encoding="utf-8")
        me_mix = me[
            me.index("static void process_sfx_mix_on_me") :
            me.index("static void process_vertices_on_me")
        ]
        consume = sfx[
            sfx.index("unsigned int th07_audio4m_sfx_consume") :
            sfx.index("void th07_audio4m_sfx_output_committed")
        ]

        self.assertIn("int samples[kBlockSamples]", sfx)
        self.assertIn("int *output", header)
        self.assertIn("sizeof(int)", me_mix)
        self.assertIn("chunkOutput[sample] = wide[sample]", me_mix)
        self.assertNotIn("clamp_s16", me_mix)
        self.assertIn("static_cast<int>(io[sample]) + slot.samples[sample]", consume)
        self.assertIn("mixed[0] != 60000", me)
        self.assertEqual(mix_wide_sfx_into_bgm(-30000, 60000), (30000, 0))
        self.assertEqual(mix_wide_sfx_into_bgm(30000, 60000), (32767, 1))
        self.assertEqual(mix_wide_sfx_into_bgm(-30000, -60000), (-32768, 1))

    def test_inflight_mix_is_discarded_after_fatal_or_shutdown(self) -> None:
        source = SFX_SOURCE.read_text(encoding="utf-8")
        feeder = source[source.index("int FeederThread") :
                        source.index("} // namespace")]
        after_mix = feeder[feeder.index("th07_psp_me_sfx_mix") :]
        stale_guard = after_mix.index("gReady, __ATOMIC_ACQUIRE")
        self.assertLess(stale_guard, after_mix.index("voice.position_frame +="))
        self.assertLess(stale_guard, after_mix.index("gFifoWrite, write + 1u"))
        self.assertLess(stale_guard, after_mix.index("gMixJobs"))

    def test_full_bgm_wraps_are_counted_at_upload_fetch_and_dac(self) -> None:
        source = SOUND_SOURCE.read_text(encoding="utf-8")
        self.assertIn("gBgmUploadWraps", source)
        self.assertIn("gBgmFetchWraps", source)
        self.assertIn("gBgmOutputWraps", source)
        producer = source[
            source.index("int BgmProducerThread") :
            source.index("int BgmMeccFeederThread")
        ]
        feeder = source[
            source.index("int BgmMeccFeederThread") :
            source.index("int BgmOutputThread")
        ]
        output = source[
            source.index("int BgmOutputThread") : source.index("void StopThreads")
        ]
        self.assertLess(producer.index("th07_psp_me_bgm_upload"),
                        producer.index("gBgmUploadWraps"))
        self.assertLess(feeder.index("th07_psp_me_bgm_fetch"),
                        feeder.index("gBgmFetchWraps"))
        self.assertLess(output.index("sceAudioOutputBlocking"),
                        output.index("gBgmOutputWraps"))

    def test_crc_diagnostics_never_write_memory_stick_on_feeder(self) -> None:
        source = SOUND_SOURCE.read_text(encoding="utf-8")
        feeder = source[
            source.index("int BgmMeccFeederThread") :
            source.index("int BgmOutputThread")
        ]
        self.assertNotIn("th07_psp_boot_note", feeder)
        self.assertIn("gBgmCrcMismatchRecords", feeder)
        self.assertIn("std::memset(slot.samples, 0, kMeccFifoBlockBytes)", feeder)
        release = source[
            source.index("ZunResult SoundPlayer::Release") :
            source.index("i32 SoundPlayer::GetFmtIndexByName")
        ]
        self.assertLess(release.index("StopThreads()"),
                        release.index("FlushBgmCrcMismatchRecords()"))
        telemetry = source[
            source.index('extern "C" void th07_psp_audio_shikigami_snapshot') :
        ]
        zero_faults = telemetry[
            telemetry.index("#if defined(TH07_PSP_SFX_MAIN_RAM)") :
            telemetry.index("snapshot->audio4m_proof_flags = proof")
        ]
        self.assertEqual(zero_faults.count("gBgmCrcMismatches"), 2)

    def test_normal_start_never_schedules_audible_exhaustive_coverage(self) -> None:
        source = SOUND_SOURCE.read_text(encoding="utf-8")
        init = source[
            source.index("ZunResult SoundPlayer::InitSoundBuffers") :
            source.index("void SoundPlayer::PlaySoundByIdx")
        ]
        self.assertIn("th07_audio4m_sfx_finalize", init)
        self.assertNotIn("th07_audio4m_sfx_start_coverage", init)
        self.assertNotIn("AUDIBLE SE COVERAGE", init)
        self.assertIn("AUDIO4M READY: GAME-REQUESTED SE ONLY", init)

        header = (ROOT / "psp" / "audio4m_sfx.h").read_text(encoding="utf-8")
        self.assertIn("diagnostic profile", header)
        self.assertEqual(source.count("th07_audio4m_sfx_start_coverage"), 0)


if __name__ == "__main__":
    unittest.main()
