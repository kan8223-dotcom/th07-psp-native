from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE = "PSP_BULLET_QUIESCENT_ANM"
MACRO = "TH07_PSP_BULLET_QUIESCENT_ANM"


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


def recipe_body(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    tail = makefile[start + len(target) + 1 :]
    match = re.search(r"^[A-Za-z0-9_.-]+:", tail, re.MULTILINE)
    if match is None:
        return makefile[start:]
    return makefile[start : start + len(target) + 1 + match.start()]


class PspBulletQuiescentAnmSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.header = (ROOT / "src" / "BulletManager.hpp").read_text(encoding="utf-8")
        cls.source = (ROOT / "src" / "BulletManager.cpp").read_text(encoding="utf-8")
        cls.classifier = function_body(
            cls.source, "PspClassifyQuiescentBulletAnm(const AnmVm *vm)"
        )
        cls.fast = function_body(
            cls.source,
            "PspExecuteQuiescentBulletAnm(AnmVm *vm, u8 *classification)",
        )
        cls.assign = function_body(cls.source, "void Bullet::AssignTypeSprites")
        cls.spawn = function_body(cls.source, "i32 BulletManager::SpawnSingleBullet")
        cls.added = function_body(cls.source, "ZunResult BulletManager::AddedCallback")
        cls.update = function_body(cls.source, "u32 BulletManager::OnUpdate")

    def test_feature_is_default_off_psp2000plus_only_and_profile_stamped(self) -> None:
        self.assertIn(f"{FEATURE} ?= 0", self.makefile)
        start = self.makefile.index(f"ifeq ($({FEATURE}),1)")
        end = self.makefile.index("ifeq ($(PSP_ASCII_POPUP_BATCH),1)", start)
        block = self.makefile[start:end]
        self.assertIn(f"-D{MACRO}", block)
        self.assertIn("PSP-2000+", block)
        self.assertIn("ifneq ($(PSP_1000),0)", block)
        stamp = next(
            line for line in self.makefile.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn(f"$({FEATURE})", stamp)

    def test_attrib_m3_and_empty_timer_profiles_reject_candidate(self) -> None:
        self.assertRegex(
            self.makefile,
            rf"(?s)PSP_PERF_PROFILE\),ATTRIB\).*?"
            rf"ifneq \(\$\({FEATURE}\),0\).*?PERF_ACCEPT",
        )
        self.assertIn(
            "M3 attribution requires PSP_BULLET_QUIESCENT_ANM=0", self.makefile
        )
        self.assertIn(
            "Empty-timer A/A calibration requires PSP_BULLET_QUIESCENT_ANM=0",
            self.makefile,
        )

    def test_named_and_release_builds_keep_candidate_off(self) -> None:
        for target in (
            "psp1000-build",
            "psp2000plus-build",
            "psp2000plus-shikigami-build",
            "psp3000-mecc-bgm384k-build",
            "psp3000-mecc-audio4m-build",
        ):
            with self.subTest(target=target):
                self.assertIn(f"{FEATURE}=0", recipe_body(self.makefile, target))
        self.assertIn("release-build: psp2000plus-build", self.makefile)

    def test_classification_reuses_only_bullet_type_tail_padding(self) -> None:
        struct = function_body(self.header, "struct BulletTypeSprites")
        collision = struct.index("u8 collisionType;")
        flag = struct.index("u8 pspQuiescentAnm;")
        self.assertLess(collision, flag)
        self.assertIn(f"#if defined({MACRO})", struct[collision:flag])
        self.assertIn("// pad 1", struct[flag:])
        self.assertRegex(
            self.header,
            r"static_assert\(sizeof\(BulletTypeSprites\)\s*==\s*0xb8c",
        )
        self.assertNotIn("pspQuiescentAnm", function_body(self.header, "struct Bullet"))

    def test_helpers_and_all_calls_are_compile_time_guarded(self) -> None:
        for needle in (
            "PspClassifyQuiescentBulletAnm(const AnmVm *vm)",
            "PspExecuteQuiescentBulletAnm(AnmVm *vm, u8 *classification)",
        ):
            start = self.source.index(needle)
            guard = self.source.rfind("#if", 0, start)
            end = self.source.index("#endif", start)
            self.assertIn(MACRO, self.source[guard:start])
            self.assertLess(start, end)
        for body in (self.assign, self.spawn, self.added, self.update):
            call = body.index("PspClassifyQuiescentBulletAnm") if "PspClassify" in body else body.index("PspExecuteQuiescentBulletAnm")
            guard = body.rfind("#if", 0, call)
            self.assertIn(MACRO, body[guard:call])

    def test_template_spawn_and_retheme_each_reclassify_live_vm(self) -> None:
        for label, body, target in (
            ("retheme", self.assign, "this->sprites.pspQuiescentAnm"),
            ("spawn", self.spawn, "bullet->sprites.pspQuiescentAnm"),
            ("template", self.added, "arg->bulletTypeTemplates[i].pspQuiescentAnm"),
        ):
            with self.subTest(label=label):
                assignment = body.index(target)
                classify = body.index("PspClassifyQuiescentBulletAnm", assignment)
                self.assertLess(assignment, classify)
                self.assertIn("spriteBullet", body[classify : classify + 180])
        self.assertLess(
            self.assign.index("this->sprites = source;"),
            self.assign.index("PspClassifyQuiescentBulletAnm"),
        )
        self.assertLess(
            self.spawn.index("AnmVm::AssignVm(&bullet->sprites.spriteBullet"),
            self.spawn.index("PspClassifyQuiescentBulletAnm"),
        )
        self.assertLess(
            self.added.index("SetAnmIdxAndExecuteScript"),
            self.added.index("PspClassifyQuiescentBulletAnm"),
        )

    def test_classifier_is_narrow_stop_or_stop_hide_with_inert_dynamics(self) -> None:
        for required in (
            "!vm",
            "!vm->currentInstruction",
            "vm->pendingInterrupt != 0",
            "opcode != ANM_STOP && opcode != ANM_STOP_HIDE",
            "vm->angleVel.x != 0.0f",
            "vm->angleVel.y != 0.0f",
            "vm->angleVel.z != 0.0f",
            "vm->scaleGrowth.x != 0.0f",
            "vm->scaleGrowth.y != 0.0f",
            "vm->interpEndTimes[i].current > 0",
            "PspFloatRawBits(vm->uvScrollVel.x) != 0u",
            "PspFloatRawBits(vm->uvScrollVel.y) != 0u",
            "PspStableUnitUv(vm->uvScrollPos.x)",
            "PspStableUnitUv(vm->uvScrollPos.y)",
        ):
            with self.subTest(required=required):
                self.assertIn(required, self.classifier)
        self.assertNotIn("ExecuteScript", self.classifier)

    def test_runtime_rechecks_speed_timer_mode_instruction_and_classification(self) -> None:
        speed = self.fast.index("effectiveFramerateMultiplier")
        flags = self.fast.index("g_Supervisor.flags & 0x20u")
        classify = self.fast.index("PspClassifyQuiescentBulletAnm(vm)")
        instruction = self.fast.index("AnmRawInstr *instr = vm->currentInstruction;")
        first_mutation = self.fast.index("vm->visible = 0;")
        self.assertLess(speed, flags)
        self.assertLess(flags, classify)
        self.assertLess(classify, instruction)
        self.assertLess(instruction, first_mutation)
        self.assertIn("std::isfinite(g_Supervisor.effectiveFramerateMultiplier)", self.fast)
        self.assertIn("effectiveFramerateMultiplier <= 0.99f", self.fast)
        self.assertIn("*classification = 0;", self.fast)
        self.assertIn("instr->time <= current", self.fast)

    def test_fast_transition_preserves_stop_hide_timer_and_tick_side_effects(self) -> None:
        due = self.fast.index("if (instr->time <= current)")
        future = self.fast.index("else", due)
        tick = self.fast.index("++g_AnmManager->scriptTicksThisFrame;")
        for required in (
            "instr->opcode == ANM_STOP_HIDE",
            "vm->visible = 0;",
            "vm->isStopped = 1;",
            "vm->currentTimeInScript.previous = current - 1;",
            "vm->currentTimeInScript.previous = current;",
            "vm->currentTimeInScript.current = current + 1;",
        ):
            self.assertIn(required, self.fast)
        self.assertLess(due, future)
        self.assertLess(future, tick)
        self.assertNotIn("subFrame =", self.fast)
        self.assertNotIn("ExecuteScript", self.fast)

    def test_normal_update_accepted_arm_does_not_call_execute_script(self) -> None:
        helper = self.update.index("PspExecuteQuiescentBulletAnm(")
        accepted = self.update.index("++pspQuiescentHits;", helper)
        fallback = self.update.index("g_AnmManager->ExecuteScript", accepted)
        accepted_arm = self.update[helper:fallback]
        self.assertNotIn("ExecuteScript", accepted_arm)
        self.assertLess(helper, accepted)
        self.assertLess(accepted, fallback)
        self.assertIn("&bullet->sprites.pspQuiescentAnm", self.update[helper:fallback])

    def test_fast_helper_has_no_allocation_io_logging_or_hidden_manager_call(self) -> None:
        for forbidden in (
            "ExecuteScript",
            "malloc(",
            "calloc(",
            "realloc(",
            "free(",
            "new ",
            "delete ",
            "fopen(",
            "fread(",
            "fwrite(",
            "sceIo",
            "th07_psp_boot_note",
            "sceKernel",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, self.fast)


class PspBulletQuiescentAnmDifferentialHarnessTests(unittest.TestCase):
    def test_future_and_due_stop_transitions_match_legacy_byte_for_byte(self) -> None:
        compiler = shutil.which("g++") or shutil.which("c++")
        if compiler is None:
            self.skipTest("host C++ compiler is required for the differential harness")

        source = r"""
            #include <array>
            #include <cmath>
            #include <cstdint>
            #include <cstdio>
            #include <cstdlib>
            #include <cstring>
            #include <limits>
            #include <string>
            #include <vector>

            using u8 = std::uint8_t;
            using u16 = std::uint16_t;
            using u32 = std::uint32_t;
            using i16 = std::int16_t;
            using i32 = std::int32_t;

            enum : i16 { ANM_STOP = 20, ANM_STOP_HIDE = 23, ANM_OTHER = 28 };
            constexpr u32 VISIBLE = 1u << 0;
            constexpr u32 STOPPED = 1u << 13;

            struct Timer {
                i32 previous;
                float subFrame;
                i32 current;
            };

            struct Instr {
                i16 opcode;
                u16 size;
                i16 time;
                u16 flags;
            };

            struct Vec2 { float x, y; };
            struct Vec3 { float x, y, z; };

            struct Vm {
                Vec3 angleVel;
                Vec2 scaleGrowth;
                Vec2 uvScrollPos;
                Vec2 uvScrollVel;
                Timer currentTimeInScript;
                Timer interpEndTimes[5];
                u32 flags;
                i16 pendingInterrupt;
                i16 pad;
                Instr *currentInstruction;
                u32 fallbackMutation;
            };

            struct Supervisor { float effectiveFramerateMultiplier; u32 flags; };
            struct Manager { i32 scriptTicksThisFrame; i32 executeCalls; };

            static u32 FloatBits(float value) {
                u32 bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                return bits;
            }

            static float FloatFromBits(u32 bits) {
                float value = 0.0f;
                std::memcpy(&value, &bits, sizeof(value));
                return value;
            }

            static bool StableUnitUv(float value) {
                const u32 bits = FloatBits(value);
                return bits == 0u || (bits >= 0x00800000u && bits < 0x3f800000u);
            }

            static bool Classify(const Vm *vm) {
                if (!vm || !vm->currentInstruction || vm->pendingInterrupt != 0) return false;
                const i16 opcode = vm->currentInstruction->opcode;
                if (opcode != ANM_STOP && opcode != ANM_STOP_HIDE) return false;
                if (vm->angleVel.x != 0.0f || vm->angleVel.y != 0.0f ||
                    vm->angleVel.z != 0.0f || vm->scaleGrowth.x != 0.0f ||
                    vm->scaleGrowth.y != 0.0f) return false;
                for (int i = 0; i < 5; ++i) {
                    if (vm->interpEndTimes[i].current > 0) return false;
                }
                if (FloatBits(vm->uvScrollVel.x) != 0u ||
                    FloatBits(vm->uvScrollVel.y) != 0u ||
                    !StableUnitUv(vm->uvScrollPos.x) ||
                    !StableUnitUv(vm->uvScrollPos.y)) return false;
                return true;
            }

            static void Tick(Timer &timer, const Supervisor &supervisor) {
                timer.previous = timer.current;
                if (supervisor.effectiveFramerateMultiplier <= 0.99f) {
                    timer.subFrame += supervisor.effectiveFramerateMultiplier;
                    if (timer.subFrame >= 1.0f) {
                        ++timer.current;
                        timer.subFrame -= 1.0f;
                    }
                } else {
                    ++timer.current;
                }
            }

            static void Decrement(Timer &timer, const Supervisor &supervisor) {
                if ((supervisor.flags & 0x20u) != 0u) {
                    --timer.current;
                    timer.subFrame = 0.0f;
                    timer.previous = -999;
                }
                if (supervisor.effectiveFramerateMultiplier > 0.99f) {
                    --timer.current;
                } else {
                    timer.previous = timer.current;
                    timer.subFrame -= supervisor.effectiveFramerateMultiplier;
                    while (timer.subFrame < 0.0f) {
                        --timer.current;
                        timer.subFrame += 1.0f;
                    }
                }
            }

            static int LegacyExecute(Vm &vm, Manager &manager, const Supervisor &supervisor) {
                ++manager.executeCalls;
                if (!vm.currentInstruction) return 1;
                if (vm.pendingInterrupt != 0) {
                    vm.pendingInterrupt = 0;
                    vm.fallbackMutation ^= 0x91a7u;
                    vm.flags &= ~STOPPED;
                }
                Instr *instr = vm.currentInstruction;
                if (instr->time <= vm.currentTimeInScript.current) {
                    if (instr->opcode == ANM_STOP_HIDE) vm.flags &= ~VISIBLE;
                    if (instr->opcode == ANM_STOP || instr->opcode == ANM_STOP_HIDE) {
                        vm.flags |= STOPPED;
                        Decrement(vm.currentTimeInScript, supervisor);
                    } else {
                        vm.fallbackMutation ^= 0x5a5au;
                    }
                }
                vm.uvScrollPos.x += vm.uvScrollVel.x;
                if (vm.uvScrollPos.x >= 1.0f) vm.uvScrollPos.x -= 1.0f;
                else if (vm.uvScrollPos.x < 0.0f) vm.uvScrollPos.x += 1.0f;
                vm.uvScrollPos.y += vm.uvScrollVel.y;
                if (vm.uvScrollPos.y >= 1.0f) vm.uvScrollPos.y -= 1.0f;
                else if (vm.uvScrollPos.y < 0.0f) vm.uvScrollPos.y += 1.0f;
                Tick(vm.currentTimeInScript, supervisor);
                ++manager.scriptTicksThisFrame;
                return 0;
            }

            static bool CandidateFast(Vm &vm, u8 &classification, Manager &manager,
                                      const Supervisor &supervisor) {
                if (!classification) return false;
                if (!std::isfinite(supervisor.effectiveFramerateMultiplier) ||
                    supervisor.effectiveFramerateMultiplier <= 0.99f ||
                    (supervisor.flags & 0x20u) != 0u) return false;
                if (!Classify(&vm)) {
                    classification = 0;
                    return false;
                }
                Instr *instr = vm.currentInstruction;
                const i32 current = vm.currentTimeInScript.current;
                if (instr->time <= current) {
                    if (instr->opcode == ANM_STOP_HIDE) vm.flags &= ~VISIBLE;
                    vm.flags |= STOPPED;
                    vm.currentTimeInScript.previous = current - 1;
                } else {
                    vm.currentTimeInScript.previous = current;
                    vm.currentTimeInScript.current = current + 1;
                }
                ++manager.scriptTicksThisFrame;
                return true;
            }

            static Vm MakeVm(Instr *instr) {
                Vm vm;
                std::memset(&vm, 0, sizeof(vm));
                vm.currentInstruction = instr;
                vm.currentTimeInScript.previous = -77;
                vm.currentTimeInScript.current = 10;
                vm.currentTimeInScript.subFrame = 0.375f;
                vm.uvScrollPos.x = 0.0f;
                vm.uvScrollPos.y = 0.25f;
                vm.flags = VISIBLE;
                for (int i = 0; i < 5; ++i) {
                    vm.interpEndTimes[i].previous = -100 - i;
                    vm.interpEndTimes[i].current = -i;
                    vm.interpEndTimes[i].subFrame = 0.125f * static_cast<float>(i);
                }
                return vm;
            }

            [[noreturn]] static void Fail(const char *label, const char *what) {
                std::fprintf(stderr, "%s: %s\n", label, what);
                std::exit(1);
            }

            static void CompareVm(const char *label, const Vm &a, const Vm &b) {
                if (std::memcmp(&a, &b, sizeof(Vm)) != 0) Fail(label, "VM bytes differ");
            }

            static void RunAccepted(const char *label, i16 opcode, i16 time,
                                    float uvX, bool initiallyVisible,
                                    bool initiallyStopped) {
                Instr instr{opcode, sizeof(Instr), time, 0};
                Vm reference = MakeVm(&instr);
                reference.uvScrollPos.x = uvX;
                if (!initiallyVisible) reference.flags &= ~VISIBLE;
                if (initiallyStopped) reference.flags |= STOPPED;
                Vm candidate = reference;
                Manager refManager{17, 0};
                Manager candidateManager = refManager;
                Supervisor supervisor{1.0f, 0u};
                u8 classification = Classify(&candidate) ? 1u : 0u;
                if (!classification) Fail(label, "accepted fixture did not classify");

                LegacyExecute(reference, refManager, supervisor);
                if (!CandidateFast(candidate, classification, candidateManager, supervisor))
                    Fail(label, "fast path rejected accepted fixture");
                CompareVm(label, reference, candidate);
                if (refManager.scriptTicksThisFrame != candidateManager.scriptTicksThisFrame)
                    Fail(label, "script tick mismatch");
                if (refManager.executeCalls != 1 || candidateManager.executeCalls != 0)
                    Fail(label, "ExecuteScript call accounting mismatch");
                if (classification != 1u) Fail(label, "accepted classification was cleared");

                const bool due = time <= 10;
                if (candidate.currentTimeInScript.current != (due ? 10 : 11))
                    Fail(label, "timer current mismatch");
                if (candidate.currentTimeInScript.previous != (due ? 9 : 10))
                    Fail(label, "timer previous mismatch");
                if (FloatBits(candidate.currentTimeInScript.subFrame) != FloatBits(0.375f))
                    Fail(label, "timer subFrame changed");
                if (opcode == ANM_STOP_HIDE && due && (candidate.flags & VISIBLE))
                    Fail(label, "STOP_HIDE did not hide");
                if (due && !(candidate.flags & STOPPED)) Fail(label, "STOP did not stop");
            }

            template <typename Mutator>
            static void RunFallback(const char *label, Mutator mutate,
                                    float speed = 1.0f, u32 supervisorFlags = 0u) {
                Instr instr{ANM_STOP, sizeof(Instr), 10, 0};
                Vm candidate = MakeVm(&instr);
                u8 classification = 1u;
                Supervisor supervisor{speed, supervisorFlags};
                mutate(candidate, instr, classification);
                Vm reference = candidate;
                Manager refManager{31, 0};
                Manager candidateManager = refManager;

                LegacyExecute(reference, refManager, supervisor);
                const bool handled = CandidateFast(candidate, classification,
                                                   candidateManager, supervisor);
                if (handled) Fail(label, "unsafe fixture was fast-handled");
                LegacyExecute(candidate, candidateManager, supervisor);
                CompareVm(label, reference, candidate);
                if (std::memcmp(&refManager, &candidateManager, sizeof(Manager)) != 0)
                    Fail(label, "fallback manager state differs");
                if (candidateManager.executeCalls != 1)
                    Fail(label, "fallback did not execute legacy exactly once");
            }

            int main() {
                for (i16 opcode : {i16(ANM_STOP), i16(ANM_STOP_HIDE)}) {
                    for (i16 time : {i16(9), i16(10), i16(11), i16(120)}) {
                        for (float uv : {0.0f, 0.25f, 0.999f}) {
                            const std::string label = "accepted-" + std::to_string(opcode) +
                                                      "-" + std::to_string(time);
                            RunAccepted(label.c_str(), opcode, time, uv, true, false);
                            RunAccepted((label + "-state").c_str(), opcode, time, uv,
                                        false, true);
                        }
                    }
                }

                auto noop = [](Vm &, Instr &, u8 &) {};
                RunFallback("classification-zero", [](Vm &, Instr &, u8 &c) { c = 0; });
                RunFallback("slow", noop, 0.5f);
                RunFallback("threshold", noop, 0.99f);
                RunFallback("negative-zero-speed", noop, FloatFromBits(0x80000000u));
                RunFallback("nan-speed", noop, std::numeric_limits<float>::quiet_NaN());
                RunFallback("positive-inf-speed", noop, std::numeric_limits<float>::infinity());
                RunFallback("negative-inf-speed", noop, -std::numeric_limits<float>::infinity());
                RunFallback("timer-mode-bit5", noop, 1.0f, 0x20u);
                RunFallback("pending-interrupt", [](Vm &vm, Instr &, u8 &) {
                    vm.pendingInterrupt = 3;
                });
                RunFallback("null-instruction", [](Vm &vm, Instr &, u8 &) {
                    vm.currentInstruction = nullptr;
                });
                RunFallback("opcode-mismatch", [](Vm &, Instr &instr, u8 &) {
                    instr.opcode = ANM_OTHER;
                });
                RunFallback("active-interpolation", [](Vm &vm, Instr &, u8 &) {
                    vm.interpEndTimes[3].current = 1;
                });
                RunFallback("angle-nan", [](Vm &vm, Instr &, u8 &) {
                    vm.angleVel.x = std::numeric_limits<float>::quiet_NaN();
                });
                RunFallback("angle-inf", [](Vm &vm, Instr &, u8 &) {
                    vm.angleVel.y = std::numeric_limits<float>::infinity();
                });
                RunFallback("angle-active", [](Vm &vm, Instr &, u8 &) {
                    vm.angleVel.z = 0.125f;
                });
                RunFallback("scale-active", [](Vm &vm, Instr &, u8 &) {
                    vm.scaleGrowth.x = -0.25f;
                });
                RunFallback("uv-velocity-negative-zero", [](Vm &vm, Instr &, u8 &) {
                    vm.uvScrollVel.x = FloatFromBits(0x80000000u);
                });
                RunFallback("uv-velocity-subnormal", [](Vm &vm, Instr &, u8 &) {
                    vm.uvScrollVel.y = FloatFromBits(0x00000001u);
                });
                RunFallback("uv-velocity-nan", [](Vm &vm, Instr &, u8 &) {
                    vm.uvScrollVel.x = std::numeric_limits<float>::quiet_NaN();
                });
                RunFallback("uv-position-negative-zero", [](Vm &vm, Instr &, u8 &) {
                    vm.uvScrollPos.x = FloatFromBits(0x80000000u);
                });
                RunFallback("uv-position-subnormal", [](Vm &vm, Instr &, u8 &) {
                    vm.uvScrollPos.y = FloatFromBits(0x00000001u);
                });
                RunFallback("uv-position-negative", [](Vm &vm, Instr &, u8 &) {
                    vm.uvScrollPos.x = -0.25f;
                });
                RunFallback("uv-position-wrap", [](Vm &vm, Instr &, u8 &) {
                    vm.uvScrollPos.y = 1.0f;
                });
                RunFallback("uv-position-nan", [](Vm &vm, Instr &, u8 &) {
                    vm.uvScrollPos.x = std::numeric_limits<float>::quiet_NaN();
                });
                RunFallback("uv-position-inf", [](Vm &vm, Instr &, u8 &) {
                    vm.uvScrollPos.y = std::numeric_limits<float>::infinity();
                });

                std::puts("quiescent ANM differential harness: PASS");
                return 0;
            }
        """

        with tempfile.TemporaryDirectory(prefix="th07-qanm-") as temp_dir:
            temp = Path(temp_dir)
            source_path = temp / "quiescent_anm.cpp"
            binary_path = temp / "quiescent_anm"
            source_path.write_text(textwrap.dedent(source), encoding="utf-8")
            compile_result = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(source_path),
                    "-o",
                    str(binary_path),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                msg=f"host harness compile failed:\n{compile_result.stderr}",
            )
            run_result = subprocess.run(
                [str(binary_path)], capture_output=True, text=True, check=False
            )
            self.assertEqual(
                run_result.returncode,
                0,
                msg=(
                    "host harness failed:\n"
                    f"stdout:\n{run_result.stdout}\n"
                    f"stderr:\n{run_result.stderr}"
                ),
            )
            self.assertIn("quiescent ANM differential harness: PASS", run_result.stdout)


if __name__ == "__main__":
    unittest.main()
