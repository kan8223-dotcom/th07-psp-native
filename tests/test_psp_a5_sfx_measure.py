from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = (ROOT / "Makefile").read_text(encoding="utf-8")
FILEIO = (ROOT / "psp/fileio.cpp").read_text(encoding="utf-8")
SOUND = (ROOT / "psp/SoundPlayerPsp.cpp").read_text(encoding="utf-8")
GRAPHICS = (ROOT / "psp/graphics/PspGuGraphics.cpp").read_text(
    encoding="utf-8"
)
TELEMETRY = (ROOT / "psp/sfx_mixer_telemetry.h").read_text(
    encoding="utf-8"
)


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


def make_conditional_body(makefile: str, opener: str) -> str:
    lines = makefile.splitlines(keepends=True)
    start = next(i for i, line in enumerate(lines) if line.strip() == opener)
    depth = 0
    conditional = re.compile(r"^(?:ifeq|ifneq|ifdef|ifndef)\b")
    for index in range(start, len(lines)):
        stripped = lines[index].strip()
        if conditional.match(stripped):
            depth += 1
        elif stripped == "endif":
            depth -= 1
            if depth == 0:
                return "".join(lines[start : index + 1])
    raise AssertionError(f"unterminated Make conditional: {opener}")


def make_target_body(makefile: str, target: str) -> str:
    match = re.search(rf"^{re.escape(target)}:\s*.*$", makefile, re.MULTILINE)
    if match is None:
        raise AssertionError(f"missing Make target: {target}")
    following = re.search(
        r"^[A-Za-z0-9_./%+-]+:\s*.*$",
        makefile[match.end() :],
        re.MULTILINE,
    )
    end = len(makefile) if following is None else match.end() + following.start()
    return makefile[match.start() : end]


def assert_ordered(
    test: unittest.TestCase, source: str, needles: tuple[str, ...]
) -> None:
    position = 0
    for needle in needles:
        found = source.find(needle, position)
        test.assertNotEqual(found, -1, f"missing or out of order: {needle}")
        position = found + len(needle)


def choose_disabled_cpp_branch(source: str, macro: str) -> str:
    """Select #else for exact ``#if defined(MACRO)`` blocks.

    Other preprocessor conditionals are left intact.  This is intentionally a
    small source-contract helper, not a general C preprocessor.
    """

    opener = re.compile(rf"^\s*#if\s+defined\({re.escape(macro)}\)\s*$")
    any_if = re.compile(r"^\s*#\s*(?:if|ifdef|ifndef)\b")
    endif = re.compile(r"^\s*#\s*endif\b")
    otherwise = re.compile(r"^\s*#\s*else\b")

    while True:
        lines = source.splitlines(keepends=True)
        try:
            start = next(i for i, line in enumerate(lines) if opener.match(line))
        except StopIteration:
            return source

        depth = 0
        alternative = None
        end = None
        for index in range(start, len(lines)):
            line = lines[index]
            if any_if.match(line):
                depth += 1
            elif otherwise.match(line) and depth == 1:
                alternative = index
            elif endif.match(line):
                depth -= 1
                if depth == 0:
                    end = index
                    break
        if end is None:
            raise AssertionError(f"unterminated #if defined({macro})")
        replacement = [] if alternative is None else lines[alternative + 1 : end]
        source = "".join(lines[:start] + replacement + lines[end + 1 :])


class PspA5SfxMeasureTests(unittest.TestCase):
    def test_make_profile_is_default_off_gated_and_stamped(self) -> None:
        self.assertIn("PSP_PERF_SFX_MIX ?= 0", MAKEFILE)
        gate = make_conditional_body(
            MAKEFILE, "ifeq ($(PSP_PERF_SFX_MIX),1)"
        )
        for condition in (
            "ifneq ($(PSP_1000),0)",
            "ifneq ($(PSP_PERF_DIAG),1)",
            "ifneq ($(PSP_PERF_PROFILE),PERF_ACCEPT)",
            "ifneq ($(PSP_PERF_DENSE_SLICE),1)",
        ):
            with self.subTest(condition=condition):
                self.assertIn(condition, gate)
        for contract in (
            "PSP_PERF_SFX_MIX is PSP-2000+-only",
            "PSP_PERF_SFX_MIX requires PSP_PERF_DIAG=1",
            "PSP_PERF_SFX_MIX requires PSP_PERF_PROFILE=PERF_ACCEPT",
            "PSP_PERF_SFX_MIX requires PSP_PERF_DENSE_SLICE=1",
            "CXXFLAGS += -DTH07_PSP_PERF_SFX_MIX",
            "PSP_PERF_SFX_MIX must be 0 or 1",
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, gate)
        stamp = next(
            line
            for line in MAKEFILE.splitlines()
            if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_PERF_SFX_MIX)", stamp)

    def test_a5_target_enables_only_the_recursive_observer_switch(self) -> None:
        self.assertIn("PSP_RID30_AB_ME_PERF_SFX_MIX ?= 0", MAKEFILE)
        recursive = make_target_body(MAKEFILE, "psp3000-rid30-ab-me-build")
        self.assertIn(
            "PSP_PERF_SFX_MIX=$(PSP_RID30_AB_ME_PERF_SFX_MIX)",
            recursive,
        )
        candidate = make_target_body(
            MAKEFILE, "psp3000-a7-a5-sfx-measure-build"
        )
        self.assertIn("PSP_RID30_AB_ME_PERF_SFX_MIX=1", candidate)
        self.assertIn("PSP_RID30_AB_ME_PERF_A1_SAME=0", candidate)
        self.assertIn("PSP_RID30_AB_ME_POSITION_SOA_SHADOW=0", candidate)
        self.assertIn("PSP_RID30_AB_ME_POSITION_SOA_READ=0", candidate)
        self.assertIn("psp3000-rid30-ab-me-build", candidate)
        baseline = make_target_body(
            MAKEFILE, "psp3000-a6v4w-stage6-font-tail-fix-build"
        )
        self.assertIn("PSP_RID30_AB_ME_PERF_SFX_MIX=0", baseline)
        self.assertRegex(
            MAKEFILE,
            r"(?s)\.PHONY:.*\bpsp3000-a7-a5-sfx-measure-build\b",
        )

    def test_ram_log_has_a5_identity_and_bounded_observer_capacity(self) -> None:
        token = function_body(FILEIO, "const char *PerfProfileToken()")
        self.assertRegex(
            token,
            r"^\{\s*#if defined\(TH07_PSP_PERF_SFX_MIX\)\s*"
            r'return "A5M";',
        )
        self.assertRegex(
            FILEIO,
            r"(?s)#if defined\(TH07_PSP_1000\).*?"
            r"kPerfLogBufferBytes = 128u \* 1024u;\s*"
            r"#elif defined\(TH07_PSP_PERF_SFX_MIX\).*?"
            r"kPerfLogBufferBytes = 256u \* 1024u;\s*"
            r"#elif defined\(TH07_PSP_PERF_ACCEPT\)",
        )

    def test_wire_schema_and_log_arguments_have_one_fixed_field_order(self) -> None:
        structure = re.search(
            r"typedef struct Th07PspSfxMixerWindow\s*\{(?P<body>.*?)\}\s*"
            r"Th07PspSfxMixerWindow;",
            TELEMETRY,
            re.DOTALL,
        )
        self.assertIsNotNone(structure)
        fields = re.findall(r"uint32_t\s+([a-z0-9_]+)\s*;", structure["body"])
        self.assertEqual(
            fields,
            [
                "mix_total_us",
                "mix_calls",
                "mix_average_us",
                "mix_p99_us",
                "mix_max_us",
                "active_voice_visits",
                "active_voice_max",
                "divisor_one_calls",
                "trigger_count",
                "limited_samples",
                "sample_overflow",
            ],
        )

        report = function_body(GRAPHICS, "void ReportPerfWindow(")
        producer_start = report.index("const int sfxMixerLength")
        producer_end = report.index("sfxMixerValid ? 1u : 0u", producer_start)
        producer = report[producer_start:producer_end]
        self.assertIn(
            '"PERF A5M S%d ST%d N%u MU%u MC%u MA%u MP99%u MX%u "\n'
            '            "AV%u AVM%u D1%u TR%u FX%d/%u LIM%u OF%u G%u"',
            producer,
        )
        assert_ordered(
            self,
            producer,
            (
                "mPerfWindowState",
                "mPerfWindowStage",
                "mPerfFrames",
                "sfxMixerWindow.mix_total_us",
                "sfxMixerWindow.mix_calls",
                "sfxMixerWindow.mix_average_us",
                "sfxMixerWindow.mix_p99_us",
                "sfxMixerWindow.mix_max_us",
                "sfxMixerWindow.active_voice_visits",
                "sfxMixerWindow.active_voice_max",
                "sfxMixerWindow.divisor_one_calls",
                "sfxMixerWindow.trigger_count",
                "g_EffectManager.activeEffectsCount",
                "mPerfMaxEffects",
                "sfxMixerWindow.limited_samples",
                "sfxMixerWindow.sample_overflow",
            ),
        )

    def test_a5_record_follows_accept_and_uses_the_same_window_id(self) -> None:
        report = function_body(GRAPHICS, "void ReportPerfWindow(")
        self.assertRegex(
            report,
            r"th07_psp_perf_set_window_id\(\+\+mPerfWindowSerial\);\s*"
            r"#if defined\(TH07_PSP_PERF_SFX_MIX\)\s*"
            r"Th07PspSfxMixerWindow sfxMixerWindow\{\};\s*"
            r"th07_psp_sfx_mixer_window_take\(&sfxMixerWindow\);",
        )
        self.assertRegex(
            report,
            r"th07_psp_perf_note\(acceptMessage\);\s*"
            r"#if defined\(TH07_PSP_PERF_SFX_MIX\)\s*"
            r"const bool sfxMixerValid",
        )

    def test_gameplay_activation_discards_pre_window_audio(self) -> None:
        pending = GRAPHICS.index("if (mPerfGameplayPending)")
        active = GRAPHICS.index("mPerfGameplayActive = true;", pending)
        activation = GRAPHICS[pending:active]
        assert_ordered(
            self,
            activation,
            (
                "ResetPerfWindowCounters();",
                "#if defined(TH07_PSP_PERF_SFX_MIX)",
                "th07_psp_sfx_mixer_window_discard();",
                "#endif",
            ),
        )

    def test_mixer_reuses_existing_elapsed_time_without_an_observer_timer(self) -> None:
        mix = function_body(SOUND, "bool MixSfxBlock(")
        self.assertEqual(mix.count("sceKernelGetSystemTimeLow()"), 2)
        assert_ordered(
            self,
            mix,
            (
                "const u32 mixStartUs = sceKernelGetSystemTimeLow();",
                "th07_psp_sc_audio_mix_into",
                "const u32 mixElapsedUs = sceKernelGetSystemTimeLow() - mixStartUs;",
                "#if defined(TH07_PSP_PERF_SFX_MIX)",
                "RecordSfxMixerMeasure(mixElapsedUs, mixJob.inputCount,",
                "#endif",
            ),
        )
        after_elapsed = mix[mix.index("const u32 mixElapsedUs") :]
        record = after_elapsed.index("RecordSfxMixerMeasure")
        elapsed_statement_end = after_elapsed.index(";") + 1
        self.assertNotIn(
            "sceKernelGetSystemTime",
            after_elapsed[elapsed_statement_end:record],
        )

    def test_sample_cap_and_lock_free_ring_retirement_are_bounded(self) -> None:
        self.assertIn("constexpr u32 kSfxMixerMeasureSamples = 512u;", SOUND)
        self.assertIn(
            "SfxMixerMeasureSample gSfxMixerMeasureSamples[kSfxMixerMeasureSamples];",
            SOUND,
        )
        record = function_body(SOUND, "void RecordSfxMixerMeasure(")
        assert_ordered(
            self,
            record,
            (
                "__atomic_load_n(&gSfxMixerMeasureCommitted",
                "sequence % kSfxMixerMeasureSamples",
                "__atomic_store_n(&sample->elapsedUs",
                "__atomic_store_n(&sample->activeVoices",
                "__atomic_store_n(&sample->divisor",
                "__atomic_store_n(&sample->limitedSamples",
                "__atomic_store_n(&sample->sequence, sequence + 1u, __ATOMIC_RELEASE)",
                "__atomic_store_n(&gSfxMixerMeasureCommitted, sequence + 1u",
            ),
        )
        self.assertNotRegex(record, r"\bwhile\s*\(")

        take = function_body(SOUND, "void TakeSfxMixerMeasure(")
        assert_ordered(
            self,
            take,
            (
                "const u32 endSequence",
                "__atomic_load_n(&gSfxMixerMeasureCommitted",
                "const u32 span = endSequence - startSequence;",
                "if (span > kSfxMixerMeasureSamples)",
                "window->sample_overflow = span - kSfxMixerMeasureSamples;",
                "for (u32 sequence = startSequence; sequence != endSequence;",
                "__atomic_load_n(&sample->sequence, __ATOMIC_ACQUIRE)",
                "__atomic_load_n(&sample->elapsedUs, __ATOMIC_RELAXED)",
                "__atomic_load_n(&sample->sequence, __ATOMIC_ACQUIRE)",
                "gSfxMixerMeasureSortScratch[ordinal] = elapsedUs;",
                "std::sort(gSfxMixerMeasureSortScratch",
            ),
        )
        self.assertNotRegex(take, r"\bwhile\s*\(")

    def test_feature_off_source_keeps_original_audio_and_accept_paths(self) -> None:
        disabled_sound = choose_disabled_cpp_branch(
            SOUND, "TH07_PSP_PERF_SFX_MIX"
        )
        for observer_symbol in (
            '"sfx_mixer_telemetry.h"',
            "SfxMixerMeasureSlot",
            "RecordSfxMixerMeasure",
            "RecordSfxMixerTriggers",
            "ResetSfxMixerMeasureAll",
            "th07_psp_sfx_mixer_window_take",
        ):
            with self.subTest(observer_symbol=observer_symbol):
                self.assertNotIn(observer_symbol, disabled_sound)
        disabled_mix = function_body(disabled_sound, "bool MixSfxBlock(")
        self.assertIn("th07_psp_sc_audio_mix_into", disabled_mix)
        self.assertIn("gSfxScTotalMixUs += mixElapsedUs", disabled_mix)
        self.assertRegex(
            disabled_sound,
            r"gSfxTriggerCount \+= static_cast<u32>\(__builtin_popcount\(pendingLow\) \+\s*"
            r"__builtin_popcount\(pendingHigh\)\);",
        )

        disabled_graphics = choose_disabled_cpp_branch(
            GRAPHICS, "TH07_PSP_PERF_SFX_MIX"
        )
        self.assertNotIn("th07_psp_sfx_mixer_window_", disabled_graphics)
        self.assertNotIn("PERF A5M", disabled_graphics)
        self.assertIn("PERF ACCEPT", disabled_graphics)
        self.assertIn("mPerfGameplayActive = true;", disabled_graphics)


if __name__ == "__main__":
    unittest.main()
