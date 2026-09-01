from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src" / "PspBulletPositionSoa.hpp"
HARNESS = ROOT / "tests" / "psp_bullet_position_soa_d2a_harness.cpp"
MAKEFILE = ROOT / "Makefile"
BULLETS = ROOT / "src" / "BulletManager.cpp"
BULLETS_H = ROOT / "src" / "BulletManager.hpp"
GRAPHICS = ROOT / "psp" / "graphics" / "PspGuGraphics.cpp"


def function_body(source: str, signature: str) -> str:
    """Return a brace-balanced function/struct/branch body."""
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
    raise AssertionError(f"unterminated function or branch: {signature}")


def make_target_body(makefile: str, target: str) -> str:
    start_match = re.search(
        rf"(?m)^{re.escape(target)}:[^\n]*(?:\n|$)", makefile
    )
    if start_match is None:
        raise AssertionError(f"missing Make target: {target}")
    next_target = re.search(
        r"(?m)^[A-Za-z0-9_.-]+:[^\n]*(?:\n|$)",
        makefile[start_match.end() :],
    )
    if next_target is None:
        return makefile[start_match.start() :]
    return makefile[
        start_match.start() : start_match.end() + next_target.start()
    ]


def make_conditional_block(makefile: str, opening: str) -> str:
    """Return a nested Make ifeq/ifneq/ifdef block through its endif."""
    start = makefile.index(opening)
    line_start = makefile.rfind("\n", 0, start) + 1
    depth = 0
    cursor = line_start
    while cursor < len(makefile):
        line_end = makefile.find("\n", cursor)
        if line_end < 0:
            line_end = len(makefile)
        stripped = makefile[cursor:line_end].strip()
        if re.match(r"^(?:ifeq|ifneq|ifdef|ifndef)\b", stripped):
            depth += 1
        elif stripped == "endif":
            depth -= 1
            if depth == 0:
                return makefile[line_start:line_end]
        cursor = line_end + 1
    raise AssertionError(f"unterminated Make conditional: {opening}")


def flattened(source: str) -> str:
    return re.sub(r"\s+", " ", source).strip()


def assert_ordered(
    testcase: unittest.TestCase, source: str, *tokens: str
) -> None:
    cursor = -1
    for token in tokens:
        found = source.find(token, cursor + 1)
        testcase.assertNotEqual(found, -1, f"missing ordered token: {token}")
        testcase.assertGreater(found, cursor, f"out-of-order token: {token}")
        cursor = found


class PspBulletPositionSoaD2bTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = MAKEFILE.read_text(encoding="utf-8")
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.bullets = BULLETS.read_text(encoding="utf-8")
        cls.bullets_h = BULLETS_H.read_text(encoding="utf-8")
        cls.graphics = GRAPHICS.read_text(encoding="utf-8")

    def compile_and_run(self, optimization: str) -> subprocess.CompletedProcess[str]:
        compiler = shutil.which("g++")
        if compiler is None:
            raise unittest.SkipTest("host C++ compiler is unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "d2b-position-soa"
            build = subprocess.run(
                [
                    compiler,
                    "-std=gnu++17",
                    optimization,
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT),
                    str(HARNESS),
                    "-o",
                    str(output),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(build.returncode, 0, build.stderr)
            return subprocess.run(
                [str(output)], check=False, capture_output=True, text=True
            )

    def test_feature_is_default_off_fail_closed_and_profile_stamped(self) -> None:
        feature = "PSP_BULLET_POSITION_SOA_READ"
        self.assertRegex(
            self.makefile, rf"(?m)^{feature}\s*\?=\s*0\s*$"
        )
        self.assertRegex(
            self.makefile,
            rf"(?m)^ifneq \(\$\(filter-out 0 1,\$\({feature}\)\),\)$",
        )
        self.assertIn(f"$({feature})", next(
            line
            for line in self.makefile.splitlines()
            if line.startswith("PROFILE_STAMP :=")
        ))

        gate = make_conditional_block(
            self.makefile, f"ifeq ($({feature}),1)"
        )
        required_gates = (
            "ifneq ($(PSP_BULLET_POSITION_SOA_SHADOW),1)",
            "ifneq ($(PSP_ME_RENDER_DIRECT_LIST),1)",
            "ifneq ($(PSP_PERF_AB_COMPARE),1)",
            "ifneq ($(PSP_1000),0)",
            "CXXFLAGS += -DTH07_PSP_BULLET_POSITION_SOA_READ",
            "CFLAGS += -DTH07_PSP_BULLET_POSITION_SOA_READ",
        )
        for contract in required_gates:
            with self.subTest(contract=contract):
                self.assertIn(contract, gate)

        self.assertRegex(
            self.makefile,
            r"(?m)^PSP_RID30_AB_ME_POSITION_SOA_READ\s*\?=\s*0\s*$",
        )
        base_profile = make_target_body(
            self.makefile, "psp3000-rid30-ab-me-build"
        )
        for contract in (
            "PSP_PERF_PROFILE=PERF_ACCEPT",
            "PSP_PERF_AB_COMPARE=1",
            "PSP_ME_RENDER_CORRECTNESS=1",
            "PSP_ME_RENDER_DIRECT_LIST=1",
            "PSP_BULLET_POSITION_SOA_READ=$(PSP_RID30_AB_ME_POSITION_SOA_READ)",
        ):
            with self.subTest(base_profile_contract=contract):
                self.assertIn(contract, base_profile)

    def test_d2b_profile_target_enables_only_the_position_read_increment(self) -> None:
        target = make_target_body(
            self.makefile, "psp3000-a6v4w-d2b-position-soa-read-build"
        )
        for assignment in (
            "PSP_1000=0",
            "PSP_RID30_AB_ME_UV16=0",
            "PSP_RID30_AB_ME_XYZ16=0",
            "PSP_RID30_AB_ME_C1_GE_EXPERIMENT=0",
            "PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY=0",
            "PSP_RID30_AB_ME_SEED_SOA=0",
            "PSP_RID30_AB_ME_POSITION_SOA_SHADOW=1",
            "PSP_RID30_AB_ME_POSITION_SOA_READ=1",
        ):
            with self.subTest(assignment=assignment):
                self.assertIn(assignment, target)
        self.assertIn("psp3000-rid30-ab-me-build", target)

    def test_shared_u16_slot_identity_preserves_the_frozen_bullet_abi(self) -> None:
        bullet = function_body(self.bullets_h, "struct Bullet\n")
        slot_field = re.compile(
            r"u8\s+grazed\s*;\s*"
            r"#if\s+defined\(TH07_PSP\).*?"
            r"u16\s+pspSlotIndex\s*;\s*"
            r"#else.*?#endif\s*Bullet\s*\*\s*next\s*;",
            re.DOTALL,
        )
        self.assertRegex(bullet, slot_field)
        self.assertRegex(
            self.bullets,
            r"__builtin_offsetof\(\s*Bullet\s*,\s*pspSlotIndex\s*\)\s*"
            r"==\s*3074u",
        )
        self.assertRegex(
            self.bullets,
            r"__builtin_offsetof\(\s*Bullet\s*,\s*next\s*\)\s*"
            r"==\s*3076u",
        )
        self.assertIn("static_assert(sizeof(Bullet) == 3452u", self.bullets)

        track = function_body(
            self.bullets_h, "void PspTrackBulletSlot(i32 index)"
        )
        assert_ordered(
            self,
            track,
            "BulletAt(index)->pspSlotIndex = static_cast<u16>(index);",
            "pspActiveBulletBits[index >> 5] |= 1u << (index & 31);",
            "++pspMeRenderSlotGenerations[index]",
        )
        forget = function_body(
            self.bullets_h, "void PspForgetBulletSlot(i32 index)"
        )
        assert_ordered(
            self,
            forget,
            "pspActiveBulletBits[index >> 5] &= ~(1u << (index & 31));",
            "++pspMeRenderSlotGenerations[index]",
            "BulletAt(index)->pspSlotIndex = 0xffffu;",
        )

        resolver = function_body(self.bullets, "PspResolveBulletSlot(")
        for identity_fence in (
            "const u32 candidate = static_cast<u32>(bullet->pspSlotIndex);",
            "candidate >= static_cast<u32>(BulletManager::kBulletCapacity)",
            "manager->BulletAt(static_cast<i32>(candidate)) != bullet",
            "!manager->PspIsBulletSlotTracked(static_cast<i32>(candidate))",
            "manager->pspMeRenderSlotGenerations[candidate] == 0u",
            "*slot = candidate;",
        ):
            with self.subTest(identity_fence=identity_fence):
                self.assertIn(identity_fence, resolver)
        self.assertNotIn("/ sizeof(Bullet)", resolver)

    def test_calc_phase_forces_aos_then_end_publishes_readability(self) -> None:
        runtime = function_body(
            self.bullets, "struct PspBulletPositionSoaRuntime"
        )
        for field in (
            "u32 readableCalcSerial;",
            "bool traversalActive;",
            "bool readEnabled;",
            "bool readFaulted;",
        ):
            with self.subTest(runtime_field=field):
                self.assertIn(field, runtime)

        begin = function_body(
            self.bullets, "void PspBulletPositionSoaBeginCalc()"
        )
        assert_ordered(
            self,
            begin,
            "runtime.expectedPublishCalcSerial = runtime.shadow.activeCalcSerial;",
            "runtime.shadow.BeginCalc(runtime.managerSerial, nextCalc);",
            "runtime.readableCalcSerial = 0u;",
            "runtime.traversalActive = true;",
            "++gPspBulletPositionSoaWindow.calcPasses;",
        )

        end = function_body(
            self.bullets, "void PspBulletPositionSoaEndCalc()"
        )
        end_flat = flattened(end)
        assert_ordered(
            self,
            end_flat,
            "runtime.traversalActive = false;",
            "runtime.readableCalcSerial = runtime.readEnabled && runtime.shadow.IsInitialized() ? runtime.shadow.activeCalcSerial : 0u;",
        )

        update = function_body(self.bullets, "u32 BulletManager::OnUpdate(")
        stopped = function_body(update, "if (g_GameManager.isTimeStopped)")
        self.assertIn("PspBulletPositionSoaPauseBoundary();", stopped)
        self.assertNotIn("PspBulletPositionSoaBeginCalc();", stopped)
        self.assertNotIn("PspBulletPositionSoaEndCalc();", stopped)
        self.assertEqual(update.count("PspBulletPositionSoaBeginCalc();"), 1)
        self.assertEqual(update.count("PspBulletPositionSoaEndCalc();"), 1)
        assert_ordered(
            self,
            update,
            "PspBulletPositionSoaBeginCalc();",
            "for (i = 0; i < kBulletCapacity; i++)",
            "PspBulletPositionSoaEndCalc();",
            "PspMeRenderPublishFusedCapture(arg);",
        )

    def test_pause_demo_and_manager_reset_revoke_read_authority(self) -> None:
        invalidate_all = function_body(
            self.bullets, "inline void PspBulletPositionSoaInvalidateRuntimeAll()"
        )
        assert_ordered(
            self,
            invalidate_all,
            "shadow.InvalidateAll();",
            "memset(gPspBulletPositionSoa.deferredEligibleBits, 0",
            "gPspBulletPositionSoa.readableCalcSerial = 0u;",
            "gPspBulletPositionSoa.traversalActive = false;",
        )

        for signature in (
            "void PspBulletPositionSoaPauseBoundary()",
            "void Th07PspBulletPositionSoaDemoRestartBoundary()",
        ):
            with self.subTest(boundary=signature):
                boundary = function_body(self.bullets, signature)
                assert_ordered(
                    self,
                    boundary,
                    "PspBulletPositionSoaCountDeferred();",
                    "PspBulletPositionSoaInvalidateRuntimeAll();",
                    "expectedPublishCalcSerial = 0u;",
                )

        reset = function_body(
            self.bullets, "void PspBulletPositionSoaResetManager()"
        )
        assert_ordered(
            self,
            reset,
            "runtime.readableCalcSerial = 0u;",
            "runtime.traversalActive = false;",
            "runtime.readEnabled = !runtime.readFaulted;",
        )
        self.assertNotIn("runtime.readFaulted = false", reset)

    def test_load_raw_preserves_words_and_classifies_every_fence(self) -> None:
        load_raw = function_body(self.header, "LoadRaw(uint32_t slot,")
        load_flat = flattened(load_raw)
        for classification in (
            "TH07_PSP_BULLET_POSITION_SOA_INVALID_SLOT",
            "TH07_PSP_BULLET_POSITION_SOA_NOT_INITIALIZED",
            "TH07_PSP_BULLET_POSITION_SOA_MANAGER_MISMATCH",
            "TH07_PSP_BULLET_POSITION_SOA_NOT_VALID",
            "TH07_PSP_BULLET_POSITION_SOA_GENERATION_MISMATCH",
            "TH07_PSP_BULLET_POSITION_SOA_CALC_MISMATCH",
            "TH07_PSP_BULLET_POSITION_SOA_MATCH",
        ):
            with self.subTest(classification=classification):
                self.assertIn(classification, load_raw)
        assert_ordered(
            self,
            load_flat,
            "const uint32_t loadedX = posXBits[slot];",
            "const uint32_t loadedY = posYBits[slot];",
            "const uint32_t loadedZ = posZBits[slot];",
            "*xBits = loadedX;",
            "*yBits = loadedY;",
            "*zBits = loadedZ;",
            "return TH07_PSP_BULLET_POSITION_SOA_MATCH;",
        )
        self.assertEqual(load_raw.count("IsSlotValid(slot)"), 2)
        self.assertEqual(load_raw.count("publishManagerSerial[slot]"), 2)
        self.assertEqual(load_raw.count("generation[slot]"), 2)
        self.assertEqual(load_raw.count("publishCalcSerial[slot]"), 2)

        for optimization in ("-O0", "-O3"):
            with self.subTest(optimization=optimization):
                completed = self.compile_and_run(optimization)
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertIn("D2A position SoA", completed.stdout)
                self.assertIn("raw bits, generation, serial fences", completed.stdout)

    def test_accepted_sc_reads_raw_compare_aos_and_fault_process_wide(self) -> None:
        read = function_body(
            self.bullets, "bool BulletManager::PspTryReadDeferredPosition("
        )
        guard = read[: read.index("u32 xBits")]
        for eligibility_fence in (
            "!runtime.readEnabled",
            "runtime.readFaulted",
            "runtime.traversalActive",
            "runtime.readableCalcSerial == 0u",
            "bullet->pspSlotIndex != static_cast<u16>(slot)",
            "BulletAt(slot) != bullet",
            "!PspIsBulletSlotTracked(slot)",
            "bullet->state == BULLET_INACTIVE",
            "!PspBulletPositionSoaWasDeferred(static_cast<u32>(slot))",
            "pspMeRenderSlotGenerations[slot] == 0u",
        ):
            with self.subTest(eligibility_fence=eligibility_fence):
                self.assertIn(eligibility_fence, guard)
        assert_ordered(
            self,
            guard,
            "++window.readAttempts;",
            "++window.readFallbacks;",
            "return false;",
        )

        self.assertIn("runtime.shadow.LoadRaw(", read)
        read_flat = flattened(read)
        self.assertIn(
            "runtime.managerSerial, runtime.readableCalcSerial", read_flat
        )
        mismatch = function_body(
            read, "if (result != TH07_PSP_BULLET_POSITION_SOA_MATCH"
        )
        for raw_compare in (
            "xBits != Th07PspBulletPositionSoaShadow::FloatBits(bullet->pos.x)",
            "yBits != Th07PspBulletPositionSoaShadow::FloatBits(bullet->pos.y)",
            "zBits != Th07PspBulletPositionSoaShadow::FloatBits(bullet->pos.z)",
        ):
            with self.subTest(raw_compare=raw_compare):
                self.assertIn(raw_compare, read_flat)
        assert_ordered(
            self,
            mismatch,
            "++window.readFaults;",
            "runtime.readFaulted = true;",
            "runtime.readEnabled = false;",
            "PspBulletPositionSoaInvalidateRuntimeAll();",
            "++window.readFallbacks;",
            "return false;",
        )

        accepted = read[read.index("out->x =", read.index(mismatch)) :]
        assert_ordered(
            self,
            accepted,
            "out->x = Th07PspBulletPositionSoaShadow::BitsFloat(xBits);",
            "out->y = Th07PspBulletPositionSoaShadow::BitsFloat(yBits);",
            "out->z = Th07PspBulletPositionSoaShadow::BitsFloat(zBits);",
            "++window.readHits;",
            "return true;",
        )

        fallback = function_body(
            self.bullets, "void BulletManager::PspReadPositionOrAoS("
        )
        self.assertIn(
            "if (!PspTryReadDeferredPosition(bullet, slot, out))",
            flattened(fallback),
        )
        self.assertIn("*out = bullet->pos;", fallback)
        self.assertIn("*out = ZunVec3{};", fallback)

    def test_sc_fills_complete_ps01_descriptor_and_falls_back_to_aos(self) -> None:
        position_type = function_body(
            (ROOT / "psp" / "audio_me.h").read_text(encoding="utf-8"),
            "typedef struct Th07PspMeRenderPositionSource",
        )
        descriptor_fields = (
            "version",
            "bytes",
            "kind",
            "flags",
            "ownerBasePhys",
            "ownerBytes",
            "slotCount",
            "slotStrideBytes",
            "posXBasePhys",
            "posYBasePhys",
            "posZBasePhys",
            "validBitsPhys",
            "validWordCount",
            "fullGenerationBasePhys",
            "publishManagerSerialBasePhys",
            "publishCalcSerialBasePhys",
            "expectedManagerSerial",
            "expectedCalcSerial",
        )
        for field in descriptor_fields:
            with self.subTest(descriptor_field=field):
                self.assertRegex(
                    position_type,
                    rf"\bunsigned\s+int\s+{field}\s*;",
                )
        self.assertEqual(
            len(re.findall(r"\bunsigned\s+int\s+[A-Za-z0-9_]+\s*;", position_type)),
            len(descriptor_fields),
        )
        self.assertIn(
            "static_assert(sizeof(Th07PspMeRenderPositionSource) == 72u",
            self.bullets,
        )

        build = function_body(self.bullets, "bool PspMeRenderBuildFusedSnapshot(")
        descriptor_start = build.index(
            "Th07PspMeRenderPositionSource &positionSource"
        )
        assert_ordered(
            self,
            flattened(build[:descriptor_start]),
            "job->listLayout.bulletPosXOffset = __builtin_offsetof(Bullet, pos.x);",
            "job->listLayout.bulletPosYOffset = __builtin_offsetof(Bullet, pos.y);",
            "job->listLayout.bulletPosZOffset = __builtin_offsetof(Bullet, pos.z);",
        )
        descriptor_end = build.index(
            "job->listLayout.bulletRenderAngleOffset", descriptor_start
        )
        descriptor = build[descriptor_start:descriptor_end]
        common = descriptor[: descriptor.index("const bool soaReadable")]
        common_flat = flattened(common)
        assert_ordered(
            self,
            common_flat,
            "positionSource = Th07PspMeRenderPositionSource{};",
            "positionSource.version = TH07_PSP_ME_RENDER_POSITION_SOURCE_VERSION;",
            "positionSource.bytes = sizeof(positionSource);",
            "positionSource.slotCount = BulletManager::kBulletCapacity;",
            "positionSource.validWordCount = sizeof(manager->pspActiveBulletBits) / sizeof(manager->pspActiveBulletBits[0]);",
        )

        predicate_start = descriptor.index("const bool soaReadable")
        soa_branch = function_body(descriptor, "if (soaReadable)")
        predicate = flattened(descriptor[predicate_start : descriptor.index("if (soaReadable)")])
        for readiness_fence in (
            "positionRuntime.readEnabled",
            "!positionRuntime.readFaulted",
            "!positionRuntime.traversalActive",
            "positionRuntime.readableCalcSerial != 0u",
            "positionRuntime.managerSerial != 0u",
            "positionRuntime.shadow.IsInitialized()",
            "positionRuntime.shadow.managerSerial == positionRuntime.managerSerial",
            "positionRuntime.shadow.activeCalcSerial == positionRuntime.readableCalcSerial",
        ):
            with self.subTest(readiness_fence=readiness_fence):
                self.assertIn(readiness_fence, predicate)

        soa_assignments = (
            "positionSource.kind = TH07_PSP_ME_RENDER_POSITION_SOURCE_SOA;",
            "positionSource.ownerBasePhys = physicalAddress(&shadow);",
            "positionSource.ownerBytes = sizeof(shadow);",
            "positionSource.slotStrideBytes = sizeof(shadow.posXBits[0]);",
            "positionSource.posXBasePhys = physicalAddress(&shadow.posXBits[0]);",
            "positionSource.posYBasePhys = physicalAddress(&shadow.posYBits[0]);",
            "positionSource.posZBasePhys = physicalAddress(&shadow.posZBits[0]);",
            "positionSource.validBitsPhys = physicalAddress(&shadow.validBits[0]);",
            "physicalAddress(&shadow.generation[0]);",
            "physicalAddress(&shadow.publishManagerSerial[0]);",
            "physicalAddress(&shadow.publishCalcSerial[0]);",
            "positionSource.expectedManagerSerial = positionRuntime.managerSerial;",
            "positionSource.expectedCalcSerial = positionRuntime.readableCalcSerial;",
            "++gPspBulletPositionSoaWindow.meSoaJobs;",
        )
        soa_flat = flattened(soa_branch)
        for assignment in soa_assignments:
            with self.subTest(soa_assignment=assignment):
                self.assertIn(assignment, soa_flat)

        after_soa = descriptor[descriptor.index(soa_branch) + len(soa_branch) :]
        aos_branch = function_body(after_soa, "else")
        aos_flat = flattened(aos_branch)
        aos_assignments = (
            "positionSource.kind = TH07_PSP_ME_RENDER_POSITION_SOURCE_AOS;",
            "positionSource.ownerBasePhys = job->listLayout.bulletBasePhys;",
            "positionSource.ownerBytes = BulletManager::kBulletCapacity * sizeof(Bullet);",
            "positionSource.slotStrideBytes = sizeof(Bullet);",
            "job->listLayout.bulletBasePhys + job->listLayout.bulletPosXOffset;",
            "job->listLayout.bulletBasePhys + job->listLayout.bulletPosYOffset;",
            "job->listLayout.bulletBasePhys + job->listLayout.bulletPosZOffset;",
            "positionSource.validBitsPhys = job->listLayout.activeBitsPhys;",
            "positionSource.fullGenerationBasePhys = job->listLayout.generationBasePhys;",
            "++gPspBulletPositionSoaWindow.meAosJobs;",
        )
        for assignment in aos_assignments:
            with self.subTest(aos_assignment=assignment):
                self.assertIn(assignment, aos_flat)
        self.assertNotIn("return false", aos_branch)

    def test_read_fault_and_phase_telemetry_survive_window_rotation(self) -> None:
        take = function_body(
            self.bullets, "void Th07PspTakeBulletPositionSoaWindow("
        )
        before_reset, after_reset = take.split(
            "gPspBulletPositionSoaWindow = Th07PspBulletPositionSoaWindow{};",
            1,
        )
        before_reset_flat = flattened(before_reset)
        self.assertIn(
            "window->readDisabled = gPspBulletPositionSoa.readEnabled ? 0u : 1u;",
            before_reset_flat,
        )
        self.assertIn(
            "window->readableCalcSerial = gPspBulletPositionSoa.readableCalcSerial;",
            before_reset_flat,
        )
        self.assertIn(
            "const unsigned int readFaults = gPspBulletPositionSoaWindow.readFaults;",
            before_reset_flat,
        )
        self.assertIn(
            "gPspBulletPositionSoaWindow.readFaults = readFaults;",
            after_reset,
        )
        for transient in (
            "readAttempts",
            "readHits",
            "readFallbacks",
            "meSoaJobs",
            "meAosJobs",
        ):
            with self.subTest(transient=transient):
                self.assertNotIn(
                    f"gPspBulletPositionSoaWindow.{transient} =", after_reset
                )

    def test_perf_accept_closes_read_accounting_and_emits_mapped_tokens(self) -> None:
        window = function_body(
            self.bullets_h, "struct Th07PspBulletPositionSoaWindow"
        )
        for field in (
            "unsigned long long readAttempts;",
            "unsigned long long readHits;",
            "unsigned long long readFallbacks;",
            "unsigned long long meSoaJobs;",
            "unsigned long long meAosJobs;",
            "unsigned int readFaults;",
            "unsigned int readDisabled;",
            "unsigned int readableCalcSerial;",
        ):
            with self.subTest(window_field=field):
                self.assertIn(field, window)

        report = function_body(self.graphics, "void ReportPerfWindow(")
        report_flat = flattened(report)
        self.assertIn(
            "abPositionSoa.readHits + abPositionSoa.readFallbacks == abPositionSoa.readAttempts",
            report_flat,
        )
        self.assertIn("abPositionSoa.readFaults == 0u", report_flat)
        self.assertIn("abPositionSoa.readDisabled == 0u", report_flat)
        assert_ordered(
            self,
            report_flat,
            "const bool abPositionSoaValid =",
            "abPositionSoa.readHits + abPositionSoa.readFallbacks == abPositionSoa.readAttempts",
            "abPositionSoa.readFaults == 0u",
            "abPositionSoa.readDisabled == 0u",
            "acceptProfileValid = acceptProfileValid && abPositionSoaValid;",
        )

        self.assertRegex(
            report,
            r'"PSRA%llu PSRH%llu PSRF%llu PSRX%u/%u/%u '
            r'PSME%llu/%llu "',
        )
        d2b_format = report.index('"PSRA%llu PSRH%llu PSRF%llu')
        mapped_arguments = report[d2b_format:]
        assert_ordered(
            self,
            mapped_arguments,
            "abPositionSoa.readAttempts",
            "abPositionSoa.readHits",
            "abPositionSoa.readFallbacks",
            "abPositionSoa.readFaults",
            "abPositionSoa.readDisabled",
            "abPositionSoa.readableCalcSerial",
            "abPositionSoa.meSoaJobs",
            "abPositionSoa.meAosJobs",
        )
        d2b_buffer = re.search(
            r"#if defined\(TH07_PSP_BULLET_POSITION_SOA_READ\)\s*"
            r"char acceptMessage\[(\d+)\];",
            report,
        )
        self.assertIsNotNone(d2b_buffer)
        assert d2b_buffer is not None
        self.assertGreaterEqual(int(d2b_buffer.group(1)), 640)

    def test_core_render_paths_route_positions_through_the_single_reader(self) -> None:
        adapter = function_body(self.bullets, "inline void PspReadBulletPosition(")
        self.assertIn("manager->PspReadPositionOrAoS(", adapter)
        self.assertIn("*out = bullet->pos;", adapter)

        routed_paths = (
            ("void Bullet::Draw()", "drawPosition"),
            ("Bullet::PreparePspBulletRenderRecord(", "drawPosition"),
            ("PspDrawNormalAutoRotatedOnePass(", "drawPosition"),
            ("bool PspMeRenderBuildShadowSnapshot(", "renderPosition"),
            ("PspMeRenderCaptureFusedRecord(", "renderPosition"),
            ("bool PspMeRenderBuildCorrectnessSnapshot(", "renderPosition"),
            ("bool PspMeRenderBuildLiveRecord(", "renderPosition"),
            ("void PspMeRenderCommitVmSideEffects(", "renderPosition"),
        )
        for signature, local_position in routed_paths:
            with self.subTest(render_path=signature):
                body = function_body(self.bullets, signature)
                read = body.index("PspReadBulletPosition(")
                x_use = body.index(f"{local_position}.x", read)
                y_use = body.index(f"{local_position}.y", x_use)
                self.assertLess(read, x_use)
                self.assertLess(x_use, y_use)

        update = function_body(self.bullets, "u32 BulletManager::OnUpdate(")
        self.assertNotIn("PspReadPositionOrAoS(", update)
        self.assertNotIn("PspTryReadDeferredPosition(", update)


if __name__ == "__main__":
    unittest.main()
