from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE = "PSP_ENEMY_P5_WARM_QUEUE"
MACRO = "TH07_PSP_ENEMY_P5_WARM_QUEUE"


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
    return makefile[start:] if match is None else makefile[start : start + len(target) + 1 + match.start()]


class PspEnemyP5WarmQueueSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.source = (ROOT / "src" / "EnemyManager.cpp").read_text(encoding="utf-8")
        cls.header = (ROOT / "src" / "EnemyManager.hpp").read_text(encoding="utf-8")
        cls.renderer = (ROOT / "psp" / "graphics" / "PspGuGraphics.cpp").read_text(
            encoding="utf-8"
        )
        cls.update = function_body(cls.source, "u32 EnemyManager::OnUpdate")
        cls.capture = function_body(
            cls.source, "bool EnemyManager::PspCaptureEnemyP5WarmRecord"
        )
        cls.fast = function_body(cls.source, "inline void PspDrawEnemyP5WarmFast")
        cls.validate = function_body(cls.source, "bool PspValidateEnemyP5WarmQueue")
        cls.draw1 = function_body(cls.source, "u32 EnemyManager::OnDraw1")
        cls.draw2 = function_body(cls.source, "u32 EnemyManager::OnDraw2")

    def test_feature_is_default_off_stamped_psp2000_and_perf_accept_only(self) -> None:
        self.assertIn(f"{FEATURE} ?= 0", self.makefile)
        start = self.makefile.index(f"ifeq ($({FEATURE}),1)")
        end = self.makefile.index("ifeq ($(PSP_BULLET_QUIESCENT_ANM),1)", start)
        block = self.makefile[start:end]
        for required in (
            f"-D{MACRO}",
            "PSP-2000+",
            "ifneq ($(PSP_1000),0)",
            "ifneq ($(PSP_PERF_DIAG),1)",
            "ifneq ($(PSP_PERF_PROFILE),PERF_ACCEPT)",
            "ifneq ($(PSP_PERF_DENSE_SLICE),1)",
            "PSP_BULLET_ROTATED_DIRECT),1",
            "PSP_BULLET_UNIFIED_QUADS),1",
            "PSP_BULLET_ONEPASS_ROTATED),1",
            f"{FEATURE} and PSP_BULLET_WARM_QUEUE are mutually exclusive",
        ):
            self.assertIn(required, block)
        stamp = next(
            line for line in self.makefile.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn(f"$({FEATURE})", stamp)
        fileio = (ROOT / "psp" / "fileio.cpp").read_text(encoding="utf-8")
        enemy_dense = fileio.index('return "ENP5D";')
        ordinary_dense = fileio.index('return "DENSE";')
        self.assertLess(enemy_dense, ordinary_dense)

    def test_named_release_roots_explicitly_disable_queue(self) -> None:
        for target in (
            "psp1000-build",
            "psp2000plus-build",
            "psp2000plus-shikigami-build",
            "psp3000-mecc-bgm384k-build",
            "psp3000-mecc-audio4m-build",
        ):
            with self.subTest(target=target):
                self.assertIn(f"{FEATURE}=0", recipe_body(self.makefile, target))

    def test_single_aligned_heap_allocation_is_one_cache_line_per_slot(self) -> None:
        self.assertIn("struct alignas(64) PspEnemyP5WarmRecord", self.source)
        self.assertIn("sizeof(PspEnemyP5WarmRecord) == 64", self.source)
        self.assertIn("sizeof(PspEnemyP5WarmQueue) <= 96u * 1024u", self.source)
        ensure = function_body(self.source, "bool EnemyManager::PspEnsureEnemyP5WarmQueue")
        release = function_body(self.source, "void EnemyManager::PspReleaseEnemyP5WarmQueue")
        initialize = function_body(self.source, "void EnemyManager::Initialize")
        deleted = function_body(self.source, "ZunResult EnemyManager::DeletedCallback")
        self.assertEqual(ensure.count("memalign("), 1)
        self.assertIn("memalign(64, sizeof(PspEnemyP5WarmQueue))", ensure)
        self.assertIn("heapBefore.fordblks", ensure)
        self.assertIn("heapAfter.fordblks", ensure)
        self.assertIn("std::free(this->pspEnemyP5WarmQueue)", release)
        self.assertIn("PspReleaseEnemyP5WarmQueue();", deleted)
        self.assertIn("void *enemyP5WarmQueue = this->pspEnemyP5WarmQueue", initialize)
        self.assertIn("this->pspEnemyP5WarmQueue = enemyP5WarmQueue", initialize)

    def test_hot_tail_capture_preserves_all_four_heads_and_exact_source_order(self) -> None:
        link = self.update.index("enemy->next = arg->enemyHead[enemy->zLayer]")
        capture = self.update.index("PspCaptureEnemyP5WarmRecord(", link)
        self.assertLess(link, capture)
        self.assertIn("record.nextIndex = queue->heads[headIndex]", self.capture)
        self.assertIn("queue->heads[headIndex] = static_cast<u16>(slotIndex)", self.capture)
        self.assertIn("headIndex < 2u", self.capture)
        begin = self.update.index("PspBeginEnemyP5WarmQueue()")
        publish = self.update.index("PspPublishEnemyP5WarmQueue(")
        self.assertLess(begin, link)
        self.assertLess(capture, publish)

    def test_vm_side_effects_are_deferred_and_draw_inputs_stay_live(self) -> None:
        for forbidden in (
            "SetRotationZ",
            "updateRotation",
            "arcadeRegionTopLeftPos",
            "colorMulEnabled",
            "viewport",
            "DrawPspFastSprite",
        ):
            self.assertNotIn(forbidden, self.capture)
        for required in (
            "SetRotationZ",
            "updateRotation = 1",
            "arcadeRegionTopLeftPos.x",
            "arcadeRegionTopLeftPos.y",
            "DrawPspFastSprite",
        ):
            self.assertIn(required, self.fast)
        self.assertLess(self.fast.index("record.child0"), self.fast.index("record.primary"))
        self.assertLess(self.fast.index("record.primary"), self.fast.index("record.child1"))

    def test_only_p5_uses_queue_and_p7_remains_canonical_after_player_p6(self) -> None:
        register = function_body(self.source, "ZunResult EnemyManager::RegisterChain")
        self.assertIn("AddToDrawChain(&g_EnemyManagerDrawChain1, 5)", register)
        self.assertIn("AddToDrawChain(&g_EnemyManagerDrawChain2, 7)", register)
        self.assertIn("PspDrawEnemyP5WarmQueue(arg)", self.draw1)
        self.assertEqual(self.draw2.strip(), "{\n    return ActualOnDraw(arg, 2, 4);\n}")
        player = (ROOT / "src" / "Player.cpp").read_text(encoding="utf-8")
        self.assertIn("AddToDrawChain(mgr->drawChain1, 6)", player)

    def test_trails_use_enemy_unit_canonical_path(self) -> None:
        self.assertIn("enemy->trailFlags != 0", self.capture)
        queue_draw = function_body(self.source, "u32 PspDrawEnemyP5WarmQueue")
        canonical = function_body(self.source, "inline void PspDrawEnemyP5CanonicalOne")
        self.assertIn("kPspEnemyP5WarmCanonical", queue_draw)
        self.assertIn("PspDrawEnemyP5CanonicalOne", queue_draw)
        self.assertIn("EnemyManager::ActualOnDraw", canonical)
        self.assertIn("enemy->next = savedNext", canonical)
        self.assertIn("manager->enemyHead[headIndex] = savedHead", canonical)

    def test_preflight_is_atomic_and_whole_p5_falls_back(self) -> None:
        for required in (
            "recordIndex >= EnemyManager::kEnemyCapacity",
            "headVisits >= queue->headCounts[head]",
            "record.slotIndex != recordIndex",
            "record.headIndex != head",
            "headVisits != queue->headCounts[head]",
            "totalVisits == queue->p5RecordCount",
        ):
            self.assertIn(required, self.validate)
        ready = self.draw1.index("PspEnemyP5WarmQueueReady()")
        preflight = self.draw1.index("PspValidateEnemyP5WarmQueue", ready)
        fast = self.draw1.index("PspDrawEnemyP5WarmQueue", preflight)
        fallback = self.draw1.index("ActualOnDraw(arg, 0, 2)", fast)
        self.assertLess(ready, preflight)
        self.assertLess(preflight, fast)
        self.assertLess(fast, fallback)

    def test_restart_fixed30_pause_and_later_mutation_are_fail_closed(self) -> None:
        begin = function_body(self.source, "bool EnemyManager::PspBeginEnemyP5WarmQueue")
        ready = function_body(self.source, "bool EnemyManager::PspEnemyP5WarmQueueReady")
        remove = function_body(self.source, "i32 EnemyManager::RemoveAllEnemies")
        self.assertLess(begin.index("queue->published = 0u"), begin.index("recordCount = 0u"))
        self.assertIn("queue->mutationEpoch == this->pspEnemyMutationEpoch", ready)
        self.assertIn("PspMarkEnemyMutation();", remove)
        self.assertIn("PspMarkEnemyMutation();", function_body(self.header, "void PspTrackEnemySlot"))
        self.assertIn("PspMarkEnemyMutation();", function_body(self.header, "void PspForgetEnemySlot"))
        # No render mutation is moved into calc, so repeated calc (replay
        # restart/fixed30) just invalidates and republishes the newest queue.
        self.assertNotIn("DrawPspFastSprite", self.update)
        self.assertNotIn("SetRotationZ", self.update)

    def test_dense_log_proves_coverage_fallback_and_net_ab_inputs(self) -> None:
        for token in (
            "EQR%u",
            "EQF%u",
            "EVIS%llu",
            "EFAST%llu",
            "ECAN%llu",
            "(enemyP5).readyFrames == (dense).drawFrames",
            "(enemyP5).fallbackFrames == 0u",
            "(enemyP5).recordVisits > 0ull",
            "(enemyP5).fastEnemyDraws > 0ull",
        ):
            self.assertIn(token, self.renderer)
        self.assertIn("Th07PspTakeEnemyP5WarmWindow(&enemyP5)", self.renderer)
        self.assertIn("++gPspEnemyP5WarmWindow.readyFrames", self.draw1)
        self.assertIn("++gPspEnemyP5WarmWindow.fallbackFrames", self.draw1)


class PspEnemyP5WarmQueueDifferentialTests(unittest.TestCase):
    def test_four_head_topology_p5_output_and_trail_fallback_match_reference(self) -> None:
        compiler = shutil.which("g++") or shutil.which("c++")
        if compiler is None:
            self.skipTest("host C++ compiler is required")
        source = r"""
            #include <array>
            #include <cassert>
            #include <cstdint>
            #include <vector>
            constexpr uint16_t END=0xffff;
            struct Enemy { uint16_t slot; uint8_t head; bool trail; };
            struct Record { uint16_t next=END, slot=END; uint8_t head=0; bool trail=false; };
            struct Queue {
                std::array<uint16_t,4> heads{END,END,END,END};
                std::array<uint16_t,4> counts{};
                std::array<Record,16> records{};
                uint16_t p5=0; bool published=false;
            };
            static Queue capture(const std::vector<Enemy>& update) {
                Queue q;
                for (const auto& e:update) {
                    auto& r=q.records[e.slot]; r={q.heads[e.head],e.slot,e.head,e.trail};
                    q.heads[e.head]=e.slot; ++q.counts[e.head]; if(e.head<2) ++q.p5;
                }
                q.published=true; return q;
            }
            static bool validate(const Queue& q) {
                unsigned total=0;
                for(unsigned h=0;h<2;h++) {
                    unsigned n=0; auto i=q.heads[h];
                    while(i!=END) {
                        if(i>=q.records.size() || n>=q.counts[h] || total>=q.p5) return false;
                        const auto& r=q.records[i]; if(r.slot!=i || r.head!=h) return false;
                        i=r.next; ++n; ++total;
                    }
                    if(n!=q.counts[h]) return false;
                }
                return total==q.p5;
            }
            static std::vector<int> reference(const Queue& q) {
                std::vector<int> out;
                for(unsigned h=0;h<2;h++) for(auto i=q.heads[h];i!=END;i=q.records[i].next) {
                    const auto& r=q.records[i];
                    out.push_back(1000+r.slot); out.push_back(2000+r.slot);
                    out.push_back(3000+r.slot); if(r.trail) out.push_back(4000+r.slot);
                }
                return out;
            }
            static std::vector<int> warm(const Queue& q) {
                if(!q.published || !validate(q)) return reference(q);
                std::vector<int> out;
                for(unsigned h=0;h<2;h++) for(auto i=q.heads[h];i!=END;i=q.records[i].next) {
                    const auto& r=q.records[i];
                    // Both paths retain child0 -> primary -> child1; trails
                    // then use the complete per-enemy canonical tail.
                    out.push_back(1000+r.slot); out.push_back(2000+r.slot);
                    out.push_back(3000+r.slot); if(r.trail) out.push_back(4000+r.slot);
                }
                return out;
            }
            int main() {
                std::vector<Enemy> update={{0,0,false},{1,1,true},{2,2,false},
                                           {3,0,false},{4,3,true},{5,1,false}};
                auto q=capture(update); assert(validate(q)); assert(warm(q)==reference(q));
                // P5 reverses update order per head exactly like linked-list prepend.
                std::vector<int> want={1003,2003,3003,1000,2000,3000,
                                       1005,2005,3005,1001,2001,3001,4001};
                assert(warm(q)==want);
                // P7 records exist for four-head topology but never enter P5 output.
                for(int v:warm(q)) assert(v%1000!=2 && v%1000!=4);
                auto bad=q; bad.records[bad.heads[0]].next=bad.heads[0];
                assert(!validate(bad));
            }
        """
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "enemy_p5.cpp"
            exe = Path(tmp) / "enemy_p5"
            src.write_text(textwrap.dedent(source), encoding="utf-8")
            subprocess.run([compiler, "-std=c++17", "-O2", str(src), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    unittest.main()
