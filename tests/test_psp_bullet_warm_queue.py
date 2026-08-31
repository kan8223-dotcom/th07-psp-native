from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE = "PSP_BULLET_WARM_QUEUE"
MACRO = "TH07_PSP_BULLET_WARM_QUEUE"


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


class PspBulletWarmQueueSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.source = (ROOT / "src" / "BulletManager.cpp").read_text(encoding="utf-8")
        cls.header = (ROOT / "src" / "BulletManager.hpp").read_text(encoding="utf-8")
        cls.window = (ROOT / "src" / "GameWindow.cpp").read_text(encoding="utf-8")
        cls.replay_source = (ROOT / "src" / "ReplayManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.replay_header = (ROOT / "src" / "ReplayManager.hpp").read_text(
            encoding="utf-8"
        )
        cls.psp_graphics = (
            ROOT / "psp" / "graphics" / "PspGuGraphics.cpp"
        ).read_text(encoding="utf-8")
        cls.fileio = (ROOT / "psp" / "fileio.cpp").read_text(encoding="utf-8")
        cls.capture = function_body(cls.source, "PspCaptureBulletWarmRecord(")
        cls.ready = function_body(cls.source, "PspBulletWarmQueueReady(")
        cls.prepared_draw = function_body(cls.source, "PspDrawBulletWarmRecord(")
        cls.update = function_body(cls.source, "u32 BulletManager::OnUpdate")
        cls.draw = function_body(cls.source, "u32 BulletManager::OnDraw")

    def test_feature_is_default_off_stamped_and_psp2000plus_only(self) -> None:
        self.assertIn(f"{FEATURE} ?= 0", self.makefile)
        start = self.makefile.index(f"ifeq ($({FEATURE}),1)")
        end = self.makefile.index("ifeq ($(PSP_BULLET_QUIESCENT_ANM),1)", start)
        block = self.makefile[start:end]
        self.assertIn(f"-D{MACRO}", block)
        self.assertIn("PSP-2000+", block)
        self.assertIn("ifneq ($(PSP_1000),0)", block)
        for dependency in (
            "PSP_BULLET_ROTATED_DIRECT),1",
            "PSP_BULLET_UNIFIED_QUADS),1",
            "PSP_BULLET_ONEPASS_ROTATED),1",
        ):
            self.assertIn(dependency, block)
        for conflict in (
            "PSP_BULLET_HOT_PREFETCH",
            "PSP_BULLET_SNAPSHOT_EMITTER",
            "PSP_BULLET_QUIESCENT_ANM",
        ):
            self.assertIn(f"{FEATURE} and {conflict} are mutually exclusive", block)
        stamp = next(
            line for line in self.makefile.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn(f"$({FEATURE})", stamp)

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

    def test_attribution_and_empty_timer_profiles_reject_queue(self) -> None:
        self.assertRegex(
            self.makefile,
            rf"(?s)PSP_PERF_PROFILE\),ATTRIB\).*?ifneq \(\$\({FEATURE}\),0\)",
        )
        self.assertIn(
            f"Empty-timer A/A calibration requires {FEATURE}=0", self.makefile
        )
        self.assertIn(
            "defined(TH07_PSP_PERF_ACCEPT) && "
            "defined(TH07_PSP_BULLET_WARM_QUEUE)",
            self.fileio,
        )
        combined = self.fileio.index('return "WARMD";')
        dense = self.fileio.index('return "DENSE";')
        warm = self.fileio.index('return "WARMQ";')
        self.assertLess(combined, dense)
        self.assertLess(dense, warm)
        self.assertIn('return "WARMQ";', self.fileio)

    def test_single_stage_allocation_is_aligned_compact_owned_and_released(self) -> None:
        self.assertIn("static_assert(sizeof(PspBulletWarmRecord) == 80", self.source)
        self.assertIn("sizeof(PspBulletWarmQueue) <= 128u * 1024u", self.source)
        ensure = function_body(self.source, "bool BulletManager::PspEnsureBulletWarmQueue")
        release = function_body(self.source, "void BulletManager::PspReleaseBulletWarmQueue")
        deleted = function_body(self.source, "ZunResult BulletManager::DeletedCallback")
        initialize = function_body(self.source, "void BulletManager::Initialize")
        self.assertEqual(ensure.count("memalign("), 1)
        self.assertIn("memalign(64, sizeof(PspBulletWarmQueue))", ensure)
        self.assertNotIn("malloc(", self.capture)
        self.assertIn("std::free(this->pspBulletWarmQueue)", release)
        self.assertIn("PspReleaseBulletWarmQueue();", deleted)
        self.assertIn("void *warmQueue = this->pspBulletWarmQueue;", initialize)
        self.assertIn("this->pspBulletWarmQueue = warmQueue;", initialize)
        self.assertIn("published = 0u", initialize)

    def test_commit_gate_covers_warmup_fixed30_and_pending_toggle(self) -> None:
        gate = function_body(self.window, "bool Th07PspCanCommitBulletWarmQueue")
        self.assertIn("g_GameWindow.curFrame >= 0", gate)
        self.assertIn("!g_PspFixed30Fps", gate)
        self.assertIn("!WAS_PRESSED_RAW(TH_BUTTON_FPS_TOGGLE)", gate)
        self.assertIn("!ReplayManager::MayRestartCalcChainAfterBulletUpdate()", gate)
        begin = function_body(self.source, "PspBeginBulletWarmQueue(")
        self.assertLess(begin.index("queue->published = 0u"), begin.index("Th07PspCanCommit"))

    def test_replay_restart_gate_is_broad_before_later_gui_and_replay_jobs(self) -> None:
        self.assertIn(
            "static bool MayRestartCalcChainAfterBulletUpdate();",
            self.replay_header,
        )
        restart_gate = function_body(
            self.replay_source,
            "bool ReplayManager::MayRestartCalcChainAfterBulletUpdate()",
        )
        for required in (
            "g_ReplayManager == NULL",
            "!g_ReplayManager->IsDemo()",
            "!g_GameManager.notInMenu",
            "g_Gui.HasCurrentMsgIdx()",
            "g_GameManager.replayStage == 2",
            "!g_EnemyManager.HasActiveBoss()",
        ):
            self.assertIn(required, restart_gate)
        self.assertNotIn("frameId %", restart_gate)
        self.assertNotIn("IsDialogueSkippable", restart_gate)
        restart_sites = [
            path
            for path in (ROOT / "src").glob("*.cpp")
            if "return CHAIN_CALLBACK_RESULT_RESTART_FROM_FIRST_JOB"
            in path.read_text(encoding="utf-8")
        ]
        self.assertEqual(restart_sites, [ROOT / "src" / "ReplayManager.cpp"])

    def test_capture_is_in_update_hot_tail_and_preserves_six_bucket_order(self) -> None:
        timers = self.update.index("bullet->timer1++;")
        capture = self.update.index("PspCaptureBulletWarmRecord(")
        pointer_link = self.update.index("bullet->next = arg->bulletsPtrs")
        self.assertLess(timers, capture)
        self.assertLess(capture, pointer_link)
        self.assertIn("record.nextIndex = queue->heads[collisionType];", self.capture)
        self.assertIn("queue->heads[collisionType] = static_cast<u16>(slotIndex);", self.capture)
        self.assertIn("writtenBits", self.capture)

    def test_strong_capture_commits_exact_vm_side_effects_but_not_live_draw_inputs(self) -> None:
        for required in (
            "bullet->state != BULLET_NORMAL",
            "!vm->autoRotate",
            "vm->pos.x =",
            "vm->pos.y =",
            "vm->pos.z = 0.05f",
            "vm->color.color =",
            "PspBulletRenderSinCos",
            "vm->SetRotationZ",
            "vm->updateRotation = 1",
            "record.baseX[corner]",
            "record.baseY[corner]",
            "record.bound = fabsf(halfWidth) + fabsf(halfHeight)",
        ):
            self.assertIn(required, self.capture)
        for forbidden in (
            "manager->offset",
            "colorMulEnabled",
            "g_Supervisor.viewport",
            "viewportLeft",
            "viewportRight",
        ):
            self.assertNotIn(forbidden, self.capture)

    def test_prepared_draw_uses_live_globals_and_never_reads_bullet_or_vm(self) -> None:
        for required in (
            "manager->offset.x",
            "manager->offset.y",
            "manager->colorMulEnabled",
            "viewportLeft",
            "viewportRight",
            "record.bound",
            "manager->textures[record.sourceFileIndex]",
        ):
            self.assertIn(required, self.prepared_draw)
        for forbidden in ("Bullet *", "AnmVm", "BulletAt(", "bullet->", "vm->"):
            self.assertNotIn(forbidden, self.prepared_draw)
        mismatch = self.prepared_draw.index("!rendererStateMatches")
        self.assertIn("return false;", self.prepared_draw[mismatch:])

    def test_invalid_or_mutated_publication_selects_whole_legacy_loop(self) -> None:
        self.assertIn("queue->published = 0u", self.source)
        self.assertIn("writtenCount == queue->recordCount", self.source)
        self.assertIn("queue->mutationEpoch != manager->pspBulletMutationEpoch", self.ready)
        self.assertNotIn("while (", self.ready)
        # I-ME2 keeps the GE-consume declaration outside its fallback block,
        # while the macro-off branch retains the accepted declaration site for
        # byte-identical legacy builds.  The preprocessor selects exactly one.
        self.assertEqual(self.draw.count("PspBulletWarmQueueReady(arg)"), 2)
        self.assertIn(
            "defined(TH07_PSP_ME_RENDER_GE_CONSUME)", self.draw
        )
        self.assertIn(
            "!defined(TH07_PSP_ME_RENDER_GE_CONSUME)", self.draw
        )
        ready = self.draw.index(
            "const bool pspBulletWarmQueueReady = PspBulletWarmQueueReady(arg)"
        )
        legacy = self.draw.index("bullet = arg->bulletsPtrs[i];", ready)
        self.assertLess(ready, legacy)
        self.assertIn("if (pspBulletWarmQueueReady)", self.draw[ready:legacy])
        self.assertIn("else", self.draw[ready:legacy])
        self.assertIn("arg->BulletAt(recordIndex)->Draw();", self.draw[ready:legacy])

    def test_dense_run_proves_every_measured_frame_used_the_queue(self) -> None:
        self.assertIn("warmQueueReadyFrames", self.header)
        self.assertIn("warmQueueFallbackFrames", self.header)
        self.assertIn("++gPspDenseSliceWindow.warmQueueReadyFrames", self.draw)
        self.assertIn("++gPspDenseSliceWindow.warmQueueFallbackFrames", self.draw)
        self.assertIn(
            '#define TH07_PSP_DENSE_WARM_FORMAT " WQR%u WQF%u"',
            self.psp_graphics,
        )
        dense_validation = function_body(
            self.psp_graphics,
            "void ReportPerfWindow(unsigned long long geEndUs)",
        )
        self.assertIn("TH07_PSP_DENSE_WARM_VALID(dense)", dense_validation)
        self.assertIn("TH07_PSP_DENSE_WARM_ZERO(dense)", dense_validation)
        self.assertIn(
            "(dense).warmQueueReadyFrames == (dense).drawFrames",
            self.psp_graphics,
        )
        self.assertIn("(dense).warmQueueFallbackFrames == 0u", self.psp_graphics)

    def test_bulk_and_bitmap_mutations_advance_epoch(self) -> None:
        for signature in (
            "void BulletManager::RemoveAllBullets",
            "i32 BulletManager::DespawnBullets",
            "void BulletManager::RemoveBulletsInRadius",
            "void BulletManager::StopBulletMovement",
        ):
            self.assertIn("PspMarkBulletMutation();", function_body(self.source, signature))
        track = function_body(self.header, "void PspTrackBulletSlot")
        forget = function_body(self.header, "void PspForgetBulletSlot")
        self.assertIn("PspMarkBulletMutation();", track)
        self.assertIn("PspMarkBulletMutation();", forget)


class PspBulletWarmQueueDifferentialHarnessTests(unittest.TestCase):
    def test_order_live_state_gates_and_fallbacks_match_reference(self) -> None:
        compiler = shutil.which("g++") or shutil.which("c++")
        if compiler is None:
            self.skipTest("host C++ compiler is required")

        source = r"""
            #include <array>
            #include <cassert>
            #include <cmath>
            #include <cstdint>
            #include <cstring>
            #include <vector>

            constexpr uint16_t END = 0xffff;
            constexpr uint32_t PREP = 1, DRAWABLE = 2;
            struct Color { uint8_t b,g,r,a; };
            struct Bullet {
                uint16_t slot, bucket;
                bool eligible, drawable;
                float px,py,hw,hh,s,c;
                uint32_t color;
            };
            struct Record {
                float bx[4], by[4], u[4], v[4];
                float px,py,hw,hh,bound;
                uint32_t color, flags;
                uint16_t next, slot;
            };
            struct Queue {
                std::array<uint16_t,6> heads{};
                std::array<Record,16> records{};
                uint32_t count=0, epoch=0;
                bool published=false;
            };
            struct Live {
                float ox,oy,l,t,r,b;
                Color mul;
                bool colorMul, rendererMatch;
            };
            struct Vertex { float x,y; uint32_t color; };

            static uint8_t mul(uint8_t x, uint8_t f) {
                uint32_t v=(uint32_t(x)*f)>>7; return v>=256?255:uint8_t(v);
            }
            static uint32_t apply(uint32_t raw, const Live& live) {
                if (!live.colorMul) return raw;
                Color c; std::memcpy(&c,&raw,4);
                c.r=mul(c.r,live.mul.r); c.g=mul(c.g,live.mul.g);
                c.b=mul(c.b,live.mul.b); c.a=mul(c.a,live.mul.a);
                std::memcpy(&raw,&c,4); return raw;
            }
            static bool gate(int curFrame, bool fixed30, bool pending) {
                return curFrame>=0 && !fixed30 && !pending;
            }
            static Queue capture(const std::vector<Bullet>& updateOrder, bool allocated,
                                 int curFrame, bool fixed30, bool pending, uint32_t epoch) {
                Queue q; q.heads.fill(END);
                if (!allocated || !gate(curFrame,fixed30,pending)) return q;
                for (const Bullet& b: updateOrder) {
                    Record& r=q.records[b.slot]; r.slot=b.slot;
                    r.next=q.heads[b.bucket]; q.heads[b.bucket]=b.slot; ++q.count;
                    if (!b.eligible) continue;
                    r.flags=PREP|(b.drawable?DRAWABLE:0); r.px=b.px; r.py=b.py;
                    r.hw=b.hw; r.hh=b.hh; r.bound=std::fabs(b.hw)+std::fabs(b.hh);
                    r.color=b.color;
                    const float lx[4]={-b.hw,b.hw,-b.hw,b.hw};
                    const float ly[4]={-b.hh,-b.hh,b.hh,b.hh};
                    for(int i=0;i<4;i++) {
                        r.bx[i]=lx[i]*b.c-ly[i]*b.s+b.px;
                        r.by[i]=lx[i]*b.s+ly[i]*b.c+b.py;
                        r.u[i]=float(i&1); r.v[i]=float(i>>1);
                    }
                }
                q.epoch=epoch; q.published=true; return q;
            }
            static std::vector<uint16_t> order(const Queue& q) {
                std::vector<uint16_t> out;
                for(int b=0;b<6;b++) for(uint16_t i=q.heads[b];i!=END;i=q.records[i].next)
                    out.push_back(i);
                return out;
            }
            static bool culled(float px,float py,float hw,float hh,float bound,
                               const Live& l) {
                float cx=px+l.ox+hw, cy=py+l.oy+hh;
                return cx+bound<l.l || cy+bound<l.t || cx-bound>l.r || cy-bound>l.b;
            }
            static std::vector<Vertex> reference(const Bullet& b,const Live& l) {
                std::vector<Vertex> out;
                if(!b.drawable || culled(b.px,b.py,b.hw,b.hh,std::fabs(b.hw)+std::fabs(b.hh),l))
                    return out;
                const float lx[4]={-b.hw,b.hw,-b.hw,b.hw};
                const float ly[4]={-b.hh,-b.hh,b.hh,b.hh};
                for(int i=0;i<4;i++) {
                    float x=lx[i]*b.c-ly[i]*b.s+b.px+l.ox; x+=b.hw;
                    float y=lx[i]*b.s+ly[i]*b.c+b.py+l.oy; y+=b.hh;
                    out.push_back({x,y,apply(b.color,l)});
                }
                return out;
            }
            static bool prepared(const Record& r,const Live& l,std::vector<Vertex>& out) {
                if(!(r.flags&PREP)) return false;
                if(!(r.flags&DRAWABLE) || culled(r.px,r.py,r.hw,r.hh,r.bound,l)) return true;
                if(!l.rendererMatch) return false;
                for(int i=0;i<4;i++)
                    out.push_back({r.bx[i]+l.ox+r.hw,r.by[i]+l.oy+r.hh,apply(r.color,l)});
                return true;
            }
            static bool same(const std::vector<Vertex>& a,const std::vector<Vertex>& b) {
                return a.size()==b.size() &&
                    (a.empty() || std::memcmp(a.data(),b.data(),a.size()*sizeof(Vertex))==0);
            }
            int main() {
                std::vector<Bullet> bullets={
                    {2,1,true,true,30,40,3,5,.5f,.8660254f,0xff4080c0},
                    {7,0,false,true,4,6,2,2,0,1,0xffffffff},
                    {5,1,true,true,500,500,3,5,.5f,.8660254f,0xff102030},
                };
                Queue q=capture(bullets,true,0,false,false,9);
                std::vector<uint16_t> expected={7,5,2};
                assert(order(q)==expected); // six buckets and reverse insertion preserved

                Live live{2.25f,-3.5f,0,0,384,448,{128,96,160,128},true,true};
                std::vector<Vertex> got;
                assert(prepared(q.records[2],live,got));
                assert(same(got,reference(bullets[0],live))); // shake + colorMul
                got.clear();
                assert(prepared(q.records[5],live,got) && got.empty()); // live cull

                live.rendererMatch=false; got.clear();
                assert(!prepared(q.records[2],live,got)); // caller must canonical-fallback slot 2
                assert(!prepared(q.records[7],live,got)); // noneligible canonical fallback

                assert(!capture(bullets,false,0,false,false,1).published); // allocation fail
                assert(!capture(bullets,true,-1,false,false,1).published); // warmup
                assert(!capture(bullets,true,0,true,false,1).published);   // fixed30
                assert(!capture(bullets,true,0,false,true,1).published);  // pending toggle
                assert(q.published && q.epoch==9);
                uint32_t liveEpoch=10;
                bool wholeFramePrepared=q.published && q.epoch==liveEpoch;
                assert(!wholeFramePrepared); // mutation mismatch: no partial prepared draw
                return 0;
            }
        """
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "warm_queue_harness.cpp"
            exe = Path(tmp) / "warm_queue_harness"
            src.write_text(textwrap.dedent(source), encoding="utf-8")
            subprocess.run(
                [compiler, "-std=c++17", "-O2", str(src), "-o", str(exe)],
                check=True,
                capture_output=True,
                text=True,
            )
            subprocess.run([str(exe)], check=True, capture_output=True, text=True)


if __name__ == "__main__":
    unittest.main()
