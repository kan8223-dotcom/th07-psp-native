from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE = "TH07_PSP_ME_RENDER_WORKER"
MAKE_FEATURE = "PSP_ME_RENDER_WORKER"


def function_body(source: str, signature: str) -> str:
    """Return a C/C++ function body without depending on its return type."""
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


def make_conditional_block(makefile: str, opening: str) -> str:
    """Extract one nested Make conditional, including its final endif."""
    lines = makefile.splitlines()
    start = next(index for index, line in enumerate(lines) if line.strip() == opening)
    depth = 0
    conditional = re.compile(r"^(?:ifeq|ifneq|ifdef|ifndef)\b")
    for index in range(start, len(lines)):
        token = lines[index].strip()
        if conditional.match(token):
            depth += 1
        elif token == "endif":
            depth -= 1
            if depth == 0:
                return "\n".join(lines[start : index + 1])
    raise AssertionError(f"unterminated Make conditional: {opening}")


def make_target_recipe(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    match = re.search(r"\n(?=[A-Za-z0-9_.-]+(?:\s+[^\n:]*)?:)", makefile[start + 1 :])
    if match is None:
        return makefile[start:]
    return makefile[start : start + 1 + match.start()]


def feature_regions(source: str) -> list[str]:
    """Collect direct preprocessor regions guarded by the research feature.

    This deliberately does not require a particular implementation function name;
    M0 is still moving, while the compile-time isolation contract is stable.
    """
    lines = source.splitlines()
    regions: list[str] = []
    conditional = re.compile(r"^#\s*(?:if|ifdef|ifndef)\b")
    for start, line in enumerate(lines):
        stripped = line.strip()
        if not conditional.match(stripped) or FEATURE not in stripped:
            continue
        depth = 0
        for end in range(start, len(lines)):
            token = lines[end].strip()
            if conditional.match(token):
                depth += 1
            elif re.match(r"^#\s*endif\b", token):
                depth -= 1
                if depth == 0:
                    regions.append("\n".join(lines[start : end + 1]))
                    break
        else:
            raise AssertionError(f"unterminated {FEATURE} region at line {start + 1}")
    return regions


def without_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", source)


def with_feature_undefined(source: str, feature: str) -> str:
    """Keep the textual branch selected when one nested feature is off."""
    output: list[str] = []
    # (is_target_condition, parent_enabled, branch_enabled)
    stack: list[tuple[bool, bool, bool]] = []
    enabled = True
    for line in source.splitlines():
        token = line.strip()
        if re.match(r"^#\s*(?:if|ifdef|ifndef)\b", token):
            is_target = feature in token
            if is_target:
                negative = (
                    f"!defined({feature})" in token
                    or re.match(rf"^#\s*ifndef\s+{re.escape(feature)}\b", token)
                    is not None
                )
                branch_enabled = negative
                stack.append((True, enabled, branch_enabled))
                enabled = enabled and branch_enabled
            else:
                stack.append((False, enabled, True))
                if enabled:
                    output.append(line)
            continue
        if re.match(r"^#\s*else\b", token) and stack:
            is_target, parent_enabled, branch_enabled = stack[-1]
            if is_target:
                branch_enabled = not branch_enabled
                stack[-1] = (is_target, parent_enabled, branch_enabled)
                enabled = parent_enabled and branch_enabled
            elif enabled:
                output.append(line)
            continue
        if re.match(r"^#\s*endif\b", token) and stack:
            is_target, parent_enabled, _ = stack.pop()
            if not is_target and enabled:
                output.append(line)
            enabled = parent_enabled
            continue
        if enabled:
            output.append(line)
    return "\n".join(output)


class PspMeRenderWorkerM0SourceContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.audio = (ROOT / "psp" / "audio_me.c").read_text(encoding="utf-8")
        cls.audio_h = (ROOT / "psp" / "audio_me.h").read_text(encoding="utf-8")
        cls.sound = (ROOT / "psp" / "SoundPlayerPsp.cpp").read_text(encoding="utf-8")
        cls.core = (
            ROOT / "psp" / "third_party" / "me-custom-core" / "me-core.h"
        ).read_text(encoding="utf-8")
        cls.me_lib = (
            ROOT / "psp" / "third_party" / "me-custom-core" / "me-lib.c"
        ).read_text(encoding="utf-8")
        cls.game = (ROOT / "src" / "GameWindow.cpp").read_text(encoding="utf-8")
        cls.chain = (ROOT / "src" / "Chain.cpp").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src" / "BulletManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.bullets_h = (ROOT / "src" / "BulletManager.hpp").read_text(
            encoding="utf-8"
        )
        cls.all_game_sources = "\n".join(
            path.read_text(encoding="utf-8")
            for path in sorted((ROOT / "src").glob("*.cpp"))
        )
        cls.feature_code = "\n".join(
            feature_regions(cls.audio)
            + feature_regions(cls.audio_h)
            + feature_regions(cls.game)
            + feature_regions(cls.chain)
            + feature_regions(cls.bullets)
        )

    def assert_source_regex(self, source: str, pattern: str, label: str = "source") -> None:
        """Regex assertion that does not dump multi-megabyte sources on failure."""
        if re.search(pattern, source, flags=re.MULTILINE) is None:
            self.fail(f"{label} does not match /{pattern}/")

    def test_make_profile_is_exact_psp3000_audio4m_dense_perf_accept(self) -> None:
        self.assertIn(f"{MAKE_FEATURE} ?= 0", self.makefile)
        block = make_conditional_block(
            self.makefile, f"ifeq ($({MAKE_FEATURE}),1)"
        )
        required_values = {
            "PSP_1000": "0",
            "PSP_SHIKIGAMI": "1",
            "PSP_MECC_AUDIO_4M": "1",
            "PSP_PERF_DIAG": "1",
            "PSP_PERF_PROFILE": "PERF_ACCEPT",
            "PSP_PERF_DENSE_SLICE": "1",
            "PSP_BULLET_ROTATED_DIRECT": "1",
            "PSP_BULLET_UNIFIED_QUADS": "1",
            "PSP_BULLET_ONEPASS_ROTATED": "1",
            "PSP_BULLET_AXIS_FAST": "0",
            "PSP_BULLET_SNAPSHOT_EMITTER": "0",
            "PSP_BULLET_HOT_PREFETCH": "0",
            "PSP_BULLET_WARM_QUEUE": "0",
            "PSP_BULLET_STATIC_PROXY": "0",
            "PSP_ENEMY_P5_WARM_QUEUE": "0",
            "PSP_BULLET_QUIESCENT_ANM": "0",
        }
        for variable, value in required_values.items():
            with self.subTest(variable=variable):
                self.assertIn(f"ifneq ($({variable}),{value})", block)
        self.assertIn(f"CFLAGS += -D{FEATURE}", block)
        self.assertIn(f"CXXFLAGS += -D{FEATURE}", block)
        stamp = next(
            line
            for line in self.makefile.splitlines()
            if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn(f"$({MAKE_FEATURE})", stamp)

        recipe = make_target_recipe(self.makefile, "psp3000-me-render-m0-build")
        for variable, value in required_values.items():
            with self.subTest(recipe_variable=variable):
                self.assertIn(f"{variable}={value}", recipe)

    def test_standard_psp_and_release_recipes_keep_worker_off(self) -> None:
        for target in (
            "psp1000-build",
            "psp2000plus-build",
            "psp2000plus-shikigami-build",
            "psp3000-mecc-bgm384k-build",
            "psp3000-mecc-audio4m-build",
        ):
            with self.subTest(target=target):
                recipe = make_target_recipe(self.makefile, target)
                self.assertIn(f"{MAKE_FEATURE}=0", recipe)
        self.assertIn("release-build: psp2000plus-build", self.makefile)
        self.assertIn("$(MAKE) psp1000-build", self.makefile)
        self.assertIn("$(MAKE) psp2000plus-build", self.makefile)

    def test_runtime_gate_rejects_psp2000_and_go_model_ids(self) -> None:
        # AUDIO4M's model-3 takeover check is also M0's hardware gate.  A generic
        # PSP-2000+ compile is not permission to start custom core on model 2/4.
        self.assert_source_regex(
            self.audio, r"ME_(?:BGM|RENDER)_REQUIRED_MODEL\s*=\s*3", "audio_me.c"
        )
        init = function_body(self.audio, "th07_psp_me_audio_init(void)")
        self.assertIn("kuKernelGetModel()", init)
        self.assert_source_regex(
            init, r"model\s*!=\s*ME_(?:BGM|RENDER)_REQUIRED_MODEL", "ME init"
        )
        self.assertIn("return 0", init)

    def test_me_full_dcache_flush_stays_on_rid22_hardware_proven_sequence(self) -> None:
        flush = function_body(
            self.me_lib, "void meLibDcacheWritebackInvalidateAll()"
        )
        self.assertIn("index < 8192", flush)
        self.assertIn("index += 64", flush)
        self.assertNotIn("way1Index", flush)
        self.assertEqual(flush.count('cache 0x14, 0(%0)'), 2)
        self.assertEqual(flush.count('"r"(index)'), 2)

        audit = (ROOT / "tools" / "audit_mecc_proven.py").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "RENDER_WORKER_RID22_PROVEN_ME_LIB_SHA256", audit
        )
        self.assertIn(
            "9c50aa7af6d22dc00abe42830fb2d85b9c1da29364d127016bd3fc02b40462a3",
            audit,
        )
        expected_assignment = audit[
            audit.index('expected_members["me-lib.c.obj"]') :
        ]
        expected_assignment = expected_assignment.split("\n\n", 1)[0]
        self.assertIn(
            "RENDER_WORKER_RID22_PROVEN_ME_LIB_SHA256",
            expected_assignment,
        )
        self.assertNotIn(
            "RENDER_WORKER_RID27_REJECTED_ME_LIB_SHA256",
            expected_assignment,
        )

    def test_render_abi_is_pointer_free_on_me_and_main_ram_bounded(self) -> None:
        guarded_header = "\n".join(feature_regions(self.audio_h))
        self.assertIn("Th07PspMeRenderRecord32", guarded_header)
        record = guarded_header[
            guarded_header.index("typedef struct Th07PspMeRenderRecord32") :
            guarded_header.index("} Th07PspMeRenderRecord32")
        ]
        self.assertNotRegex(record, r"(?:void|Bullet|AnmVm|char|float)\s*\*")
        self.assertIn("sizeof(Th07PspMeRenderRecord32) == 32u", self.audio)

    def test_render_jobs_are_confined_to_low_level_owned_exact_pool_pairs(self) -> None:
        # The ME has no MMU.  A generic "somewhere in Main RAM" check would let
        # a corrupt descriptor overwrite engine state, the mailbox or its stack.
        # Keep both benchmark and runtime payloads owned by audio_me.c and make
        # the SC submit path and ME worker share the same exact-pair allowlist.
        for symbol in (
            "gMeRenderBenchInputArea",
            "gMeRenderBenchOutputArea",
            "gMeRenderRuntimeInput",
            "gMeRenderRuntimeOutput",
        ):
            with self.subTest(owner=symbol):
                self.assertIn(f"static unsigned char {symbol}", self.audio)
                self.assertNotIn(symbol, self.bullets)

        self.assertIn("th07_psp_me_render_runtime_input", self.audio_h)
        self.assertIn("th07_psp_me_render_runtime_output", self.audio_h)
        self.assertIn("th07_psp_me_render_runtime_input()", self.bullets)
        self.assertIn("th07_psp_me_render_runtime_output()", self.bullets)

        allowlist = function_body(self.audio, "me_render_owned_pool_pair_valid(")
        self.assertIn("inputPhys == benchInput && outputPhys == benchOutput", allowlist)
        self.assertIn("inputPhys == runtimeInput && outputPhys == runtimeOutput", allowlist)
        self.assertIn("inputCapacity <= ME_RENDER_BENCH_INPUT_BYTES", allowlist)
        self.assertIn("outputCapacity <= ME_RENDER_BENCH_OUTPUT_BYTES", allowlist)

        bounds = function_body(self.audio, "me_render_bounds_valid(")
        self.assertIn("me_render_owned_pool_pair_valid", bounds)
        sc_begin = function_body(self.audio, "th07_psp_me_render_begin(")
        me_worker = function_body(self.audio, "process_render_expand_on_me(")
        self.assertIn("me_render_bounds_valid", sc_begin)
        self.assertIn("me_render_bounds_valid", me_worker)
        self.assertRegex(self.audio, r"ME_RENDER_BENCH_INPUT_BYTES\s*==\s*65536u")
        self.assertRegex(self.audio, r"ME_RENDER_BENCH_OUTPUT_BYTES\s*==\s*98304u")
        self.assertGreaterEqual(self.audio.count("__attribute__((aligned(64)))"), 3)

        render_code = without_comments("\n".join(feature_regions(self.audio)))
        self.assert_source_regex(render_code, r"0x0?8000000u?", "render feature")
        self.assert_source_regex(render_code, r"0x0?c000000u?", "render feature")
        self.assert_source_regex(
            render_code, r"(?:&\s*63u|%\s*64u|aligned.*64)", "render feature"
        )
        for field in (
            "renderInputPhys",
            "renderInputBytes",
            "renderInputStride",
            "renderRecordCount",
            "renderOutputPhys",
            "renderOutputCapacity",
            "renderOutputBytes",
        ):
            with self.subTest(field=field):
                self.assertIn(field, render_code)
        self.assert_source_regex(
            render_code,
            r"(?:32u?.*48u?.*64u?|case\s+32.*case\s+48.*case\s+64)",
            "render stride validation",
        )
        self.assertIn("TH07_PSP_ME_RENDER_MAX_RECORDS", render_code)
        self.assert_source_regex(
            render_code,
            r"(?:inputEnd.*output|outputEnd.*input|ranges?_overlap|nonoverlap)",
            "render overlap validation",
        )

    def test_me_kernel_never_touches_edram_vfpu_or_ge(self) -> None:
        render_code = without_comments("\n".join(feature_regions(self.audio)))
        forbidden = (
            "ME_BGM_RING_BASE",
            "ME_SFX_ATLAS_BASE",
            "ME_LOCAL_BYTES",
            "sceGu",
            "sceGe",
            "th07_psp_ge4",
            "meLibEdram",
        )
        for token in forbidden:
            with self.subTest(token=token):
                self.assertNotIn(token, render_code)
        forbidden_vfpu = (
            r"\b(?:lv\.q|sv\.q|vadd|vsub|vmul|vdiv|vrot|vsin|vcst|mfv|mtv)\b"
        )
        if re.search(forbidden_vfpu, render_code):
            self.fail("render feature contains a VFPU instruction/token")
        # The custom handler enables COP1 scalar FPU, not COP2/VFPU.
        self.assertIn("li             $k0, 0x30000000", self.core)

    def test_fpu_matrix_and_all_vertex_words_are_compared(self) -> None:
        combined = self.audio_h + "\n" + self.audio
        for value in ("0", "128", "512", "768", "1024"):
            with self.subTest(record_count=value):
                self.assert_source_regex(
                    combined, rf"\b{value}u?\b", "M0 count matrix"
                )
        for stride in ("32", "48", "64"):
            with self.subTest(stride=stride):
                self.assert_source_regex(
                    combined, rf"\b{stride}u?\b", "M0 stride matrix"
                )
        self.assertIn("TH07_PSP_ME_RENDER_CACHE_COLD", combined)
        self.assertIn("TH07_PSP_ME_RENDER_CACHE_WARM", combined)
        self.assertIn("TH07_PSP_ME_RENDER_RECORD_ROTATED", combined)
        self.assert_source_regex(combined, r"\b(?:cfc1|CFC1)\b", "FCR31 read")
        self.assert_source_regex(combined, r"\b(?:ctc1|CTC1)\b", "FCR31 write")
        self.assertIn("mismatchWords", combined)
        self.assertIn("TH07_PSP_ME_RENDER_VERTEX_BYTES = 24", self.audio_h)
        self.assertIn("TH07_PSP_ME_RENDER_VERTICES_PER_RECORD = 4", self.audio_h)
        # A byte/word comparison must cover the complete reported output, not
        # positions alone.  The exact helper name is intentionally unconstrained.
        self.assert_source_regex(
            without_comments(self.audio),
            r"(?:memcmp\s*\([^;]+outputBytes|"
            r"for\s*\([^)]*outputBytes\s*/\s*4|"
            r"for\s*\([^)]*word[^)]*<\s*4u?\s*\*\s*6u?)",
            "complete vertex-word comparison",
        )
        self.assert_source_regex(
            without_comments(self.audio),
            r"meFcr31Effective\s*(?:==|!=)\s*0u?",
            "canonical ME FCR31 gate",
        )

    def test_begin_probe_retire_are_nonblocking_and_split_cache_ownership(self) -> None:
        for symbol in (
            "th07_psp_me_render_begin",
            "th07_psp_me_render_probe",
            "th07_psp_me_render_retire",
            "th07_psp_me_render_hard_fault",
        ):
            self.assertIn(symbol, self.audio_h)
            self.assertIn(symbol, self.audio)

        begin = function_body(self.audio, "th07_psp_me_render_begin(")
        probe = function_body(self.audio, "th07_psp_me_render_probe(")
        retire = function_body(self.audio, "th07_psp_me_render_retire(")
        for name, body in (("begin", begin), ("probe", probe), ("retire", retire)):
            with self.subTest(name=name):
                self.assertNotIn("sceKernelDelayThread", body)
                self.assertNotRegex(body, r"\bwhile\s*\(")
                self.assertNotIn("wait_for_worker", body)
                self.assertNotIn("sceIo", body)
        self.assertRegex(begin, r"DcacheWriteback(?:Range)?")
        self.assertRegex(begin, r"command\s*=\s*ME_CMD_RENDER")
        self.assertNotRegex(probe, r"Dcache(?:Writeback|Invalidate)")
        self.assertRegex(retire, r"DcacheInvalidateRange")
        self.assertIn("release_me", retire)

    def test_shadow_call_sites_cannot_submit_me_output_to_renderer(self) -> None:
        call_sources: list[str] = []
        for path in sorted((ROOT / "src").glob("*.cpp")):
            text = path.read_text(encoding="utf-8")
            if "th07_psp_me_render_begin" in text:
                call_sources.extend(feature_regions(text))
        self.assertTrue(call_sources, "M-ME0B has no feature-guarded shadow call site")
        shadow = with_feature_undefined(
            without_comments("\n".join(call_sources)),
            "TH07_PSP_ME_RENDER_CORRECTNESS",
        )
        self.assertIn("th07_psp_me_render_probe", shadow)
        self.assertIn("th07_psp_me_render_retire", shadow)
        for forbidden in (
            "sceGuDraw",
            "DrawPrimitive",
            "DrawPrimitiveUP",
            "vertexBufferCurPtr",
            "sceGuGetMemory",
            "SubmitAndRestart",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, shadow)

    def test_calc_completion_uses_priority18_sentinel_not_chain_return(self) -> None:
        combined = self.all_game_sources
        self.assert_source_regex(
            combined,
            r"(?i)(?:ME_RENDER_CALC_COMPLETE|MeRenderCalcCompleteSentinel)",
            "calc-complete sentinel",
        )
        self.assert_source_regex(
            combined, r"AddToCalcChain\s*\([^,]+,\s*18\s*\)", "calc sentinel"
        )
        shadow_regions = "\n".join(
            region
            for source in (self.game, self.chain, self.bullets, combined)
            for region in feature_regions(source)
            if "ME_RENDER" in region or "me_render" in region
        )
        self.assert_source_regex(
            shadow_regions, r"(?:generation|Generation|frameSeq)", "shadow generation"
        )
        # BREAK returns 1, so a naked nonzero RunCalcChain result is not a
        # publication proof.  The shadow path must explicitly consult sentinel state.
        self.assert_source_regex(
            shadow_regions,
            r"(?:CALC_COMPLETE|calcComplete|sentinel|completionGeneration)",
            "explicit calc completion proof",
        )

    def test_fixed30_skipped_draws_do_not_publish_obsolete_jobs(self) -> None:
        regions = "\n".join(feature_regions(self.game))
        self.assertIn("g_PspFixed30Fps", regions)
        self.assertIn("g_PspDrawNextFrame", regions)
        self.assert_source_regex(
            regions,
            r"(?:th07_psp_me_render_begin|Th07PspMeRenderAfterCalc)",
            "shadow publication entry point",
        )
        # Keep this semantic rather than pinning one helper name: publication
        # must be under a condition involving the next draw decision.
        self.assert_source_regex(
            without_comments(regions),
            r"(?:if\s*\([^)]*(?:g_PspDrawNextFrame|drawNext|nextDraw|drawThisFrame)"
            r"[^)]*\)[\s\S]{0,1800}(?:th07_psp_me_render_begin|"
            r"Th07PspMeRenderAfterCalc)|Th07PspMeRenderAfterCalc\s*\("
            r"[^;]*(?:g_PspDrawNextFrame|drawNext|nextDraw|drawThisFrame)[^;]*\))",
            "fixed-30 shadow publication",
        )

    def test_telemetry_is_render_specific_ram_only_and_fail_closed(self) -> None:
        combined = (
            self.audio_h
            + "\n"
            + self.audio
            + "\n"
            + self.game
            + "\n"
            + self.bullets_h
            + "\n"
            + self.bullets
        )
        for token in (
            "mismatchWords",
            "timeouts",
            "boundsFaults",
            "guardFaults",
            "protocolFaults",
            "inputBytes",
            "outputBytes",
            "scWritebackUs",
            "scSubmitUs",
            "scInvalidateUs",
            "scCopyUs",
            "meInvalidateCycles",
            "meKernelCycles",
            "meWritebackCycles",
        ):
            with self.subTest(token=token):
                self.assert_source_regex(
                    combined, rf"\b{re.escape(token)}\b", "MERW phase telemetry"
                )
        semantic_counters = {
            "eligible": r"eligible",
            "submitted": r"(?:submitted|published)",
            "ready": r"ready",
            # M0B is shadow-only, so implementations may name the successful
            # gate `wouldConsume` rather than imply that ME bytes reached GE.
            "consumed/would-consume": r"(?:consumed|wouldConsume)",
            "not-ready": r"notReady",
            "global-signature drop": r"(?:signatureDrop|globalDrop)",
            "late ignored": r"(?:lateIgnored|lateRetired)",
            "quarantined": r"quarantined",
            "epoch drop": r"epochDrop",
            "generation drop": r"generationDrop",
            "bounds drop": r"boundsDrop",
            "fallback frame": r"fallbackFrames",
        }
        for label, pattern in semantic_counters.items():
            with self.subTest(counter=label):
                self.assert_source_regex(
                    combined, rf"(?i)\b{pattern}\b", "MERW telemetry"
                )

        begin = function_body(self.audio, "th07_psp_me_render_begin(")
        probe = function_body(self.audio, "th07_psp_me_render_probe(")
        retire = function_body(self.audio, "th07_psp_me_render_retire(")
        hot = begin + probe + retire
        self.assertNotRegex(hot, r"sceIo(?:Open|Write)|th07_psp_(?:boot|perf)_note")
        hard_fault = function_body(self.audio, "th07_psp_me_render_hard_fault(")
        self.assert_source_regex(
            hard_fault, r"(?:poison_me|gMeUnsafe|gMePoisoned)", "hard fault"
        )
        # A runtime shadow hang must eventually call the poison API; defining
        # it only for the synchronous boot bench is not fail-closed M0B.
        self.assertIn("th07_psp_me_render_hard_fault", self.bullets)
        self.assert_source_regex(
            self.bullets,
            r"pendingSubmitUs[\s\S]{0,500}(?:100000|TIMEOUT)[\s\S]{0,500}"
            r"th07_psp_me_render_hard_fault|"
            r"th07_psp_me_render_hard_fault[\s\S]{0,500}(?:100000|TIMEOUT)",
            "M0B hang watchdog",
        )
        self.assertIn("ME_RENDER_BENCH_TIMEOUT_US = 100000", self.audio)
        self.assertIn("stack_guards_match_on_me", self.audio)
        self.assert_source_regex(
            self.audio, r"render.*guard|guard.*render", "render guard telemetry"
        )

    def test_completed_job_is_retired_before_age_watchdog(self) -> None:
        # Loading/title transitions can keep the SC out of this hook for much
        # longer than 100 ms after ME has already published DONE.  Completion
        # must therefore be observed before wall-clock age can poison ME.
        after_calc = function_body(
            self.bullets, "Th07PspMeRenderAfterCalc("
        )
        first_retire = after_calc.index("PspMeRenderRetirePending(false)")
        hard_fault = after_calc.index("th07_psp_me_render_hard_fault()")
        self.assertLess(first_retire, hard_fault)
        self.assertGreaterEqual(
            after_calc[:hard_fault].count("PspMeRenderRetirePending(false)"),
            2,
            "timeout boundary needs a final DONE probe before poisoning ME",
        )
        self.assert_source_regex(
            after_calc,
            r"retired\s*<\s*0[\s\S]{0,240}hardFaulted\s*=\s*1u",
            "protocol-fault latch",
        )

    def test_kcall_startup_cost_and_irreversible_lifecycle_are_explicit(self) -> None:
        combined = (
            self.audio_h
            + "\n"
            + self.audio
            + "\n"
            + self.core
            + "\n"
            + self.me_lib
        )
        for token in (
            "prxBytes",
            "prxWriteUs",
            "prxLoadUs",
            "prxWriteResult",
            "prxLoadResult",
            "takeoverUs",
        ):
            with self.subTest(token=token):
                self.assert_source_regex(
                    combined, rf"\b{re.escape(token)}\b", "kcall timing telemetry"
                )
        loader = function_body(self.me_lib, "meLibLoadPrx(")
        self.assertGreaterEqual(loader.count("sceKernelGetSystemTimeLow"), 4)
        self.assertIn("meLibPrxWriteUs", loader)
        self.assertIn("meLibPrxLoadUs", loader)
        self.assertIn("meLibPrxWriteResult", loader)
        self.assertIn("meLibPrxLoadResult", loader)

        init = function_body(self.audio, "th07_psp_me_audio_init(void)")
        for field in (
            "prxWriteUs",
            "prxLoadUs",
            "prxWriteResult",
            "prxLoadResult",
        ):
            with self.subTest(published_field=field):
                self.assert_source_regex(
                    init,
                    rf"gMeRenderBenchSummary\.{field}\s*=\s*meLibPrx",
                    "published kcall timing",
                )
        bench = function_body(self.audio, "selftest_render_bench(void)")
        # The bench clears its summary before filling the matrix. Preserve the
        # loader transaction across that reset rather than logging four zeros.
        if "memset(&gMeRenderBenchSummary" in bench:
            for field in (
                "prxWriteUs",
                "prxLoadUs",
                "prxWriteResult",
                "prxLoadResult",
            ):
                with self.subTest(preserved_field=field):
                    self.assertGreaterEqual(
                        bench.count(field), 2,
                        f"selftest reset loses {field}",
                    )
        self.assertIn("embedded_kcall_len", self.audio)
        self.assertIn("writePrx", self.me_lib)
        self.assertIn("pspSdkLoadStartModule", self.me_lib)
        lifecycle_text = "\n".join(feature_regions(self.audio)) + "\n" + self.sound
        self.assert_source_regex(
            lifecycle_text, r"(?i)(?:kcall\.prx|\bPRX\b)", "lifecycle log"
        )
        self.assert_source_regex(
            lifecycle_text,
            r"(?i)(?:cold reboot|suspend.*unavailable|no suspend)",
            "lifecycle warning",
        )
        self.assertIn("scePowerLock(0)", self.audio)
        shutdown = function_body(self.audio, "th07_psp_me_audio_shutdown(void)")
        self.assert_source_regex(
            shutdown, r"(?i)render.*drain|drain.*render", "in-flight exit drain"
        )
        self.assertIn("ME_CMD_STOP", shutdown)
        self.assertIn("ME_WORKER_STOPPED", shutdown)

    def test_audio_and_render_job_telemetry_remain_separate(self) -> None:
        combined = self.audio_h + "\n" + self.audio + "\n" + self.sound
        contracts = {
            "audio job telemetry": r"ME_AUDIO_(?:JOBS|jobs)|meAudioJobs",
            "render job telemetry": (
                r"MERW_(?:SUBMITTED|COMPLETED)|meRenderSubmitted"
            ),
            "eDRAM telemetry": (
                r"(?i)(?:ME[ _]EDRAM.*(?:UNUSED|0/0)|MERW.*EDRAM0)"
            ),
        }
        for label, pattern in contracts.items():
            with self.subTest(contract=label):
                self.assert_source_regex(combined, pattern, label)

        telemetry_sources = "\n".join(
            path.read_text(encoding="utf-8")
            for tree in (ROOT / "src", ROOT / "psp")
            for path in sorted(tree.rglob("*.cpp"))
        )
        # One occurrence is the BulletManager definition. A second occurrence
        # proves that RAM counters are actually harvested by diagnostics.
        self.assertGreaterEqual(
            telemetry_sources.count("Th07PspTakeMeRenderShadowWindow"),
            2,
            "M0B RAM telemetry has no consumer",
        )


if __name__ == "__main__":
    unittest.main()
