from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE = "PSP_BULLET_STATIC_PROXY"
MACRO = "TH07_PSP_BULLET_STATIC_PROXY"


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


class PspBulletStaticProxySourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.source = (ROOT / "src" / "BulletManager.cpp").read_text(
            encoding="utf-8"
        )
        cls.header = (ROOT / "src" / "BulletManager.hpp").read_text(
            encoding="utf-8"
        )
        cls.ecl = (ROOT / "src" / "EnemyEclInstr.cpp").read_text(
            encoding="utf-8"
        )
        cls.graphics = (
            ROOT / "psp" / "graphics" / "PspGuGraphics.cpp"
        ).read_text(encoding="utf-8")
        cls.fileio = (ROOT / "psp" / "fileio.cpp").read_text(encoding="utf-8")
        cls.sync = function_body(cls.source, "PspSyncBulletStaticProxy(")
        cls.rebuild = function_body(cls.source, "PspRebuildBulletStaticProxy(")
        cls.draw_proxy = function_body(cls.source, "PspDrawBulletStaticProxy(")
        cls.draw = function_body(cls.source, "u32 BulletManager::OnDraw")

    def test_guard_is_perf_accept_only_and_accepted_stack_dependent(self) -> None:
        self.assertIn(f"{FEATURE} ?= 0", self.makefile)
        start = self.makefile.index(f"ifeq ($({FEATURE}),1)")
        end = self.makefile.index("ifeq ($(PSP_ENEMY_P5_WARM_QUEUE),1)", start)
        block = self.makefile[start:end]
        for required in (
            "ifneq ($(PSP_1000),0)",
            "ifneq ($(PSP_PERF_DIAG),1)",
            "ifneq ($(PSP_PERF_PROFILE),PERF_ACCEPT)",
            "PSP_BULLET_ROTATED_DIRECT),1",
            "PSP_BULLET_UNIFIED_QUADS),1",
            "PSP_BULLET_ONEPASS_ROTATED),1",
            "PSP_BULLET_WARM_QUEUE",
            "PSP_BULLET_SNAPSHOT_EMITTER",
            "PSP_BULLET_HOT_PREFETCH",
            "PSP_ENEMY_P5_WARM_QUEUE",
            f"-D{MACRO}",
        ):
            self.assertIn(required, block)
        stamp = next(
            line
            for line in self.makefile.splitlines()
            if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn(f"$({FEATURE})", stamp)

    def test_release_roots_and_psp1000_explicitly_keep_proxy_off(self) -> None:
        for target in (
            "psp1000-build",
            "psp2000plus-build",
            "psp2000plus-shikigami-build",
            "psp3000-mecc-bgm384k-build",
            "psp3000-mecc-audio4m-build",
        ):
            with self.subTest(target=target):
                self.assertIn(f"{FEATURE}=0", recipe_body(self.makefile, target))
        slot = self.header.index("u16 pspStaticProxySlot;")
        guard = self.header.rfind("#if", 0, slot)
        self.assertIn(MACRO, self.header[guard:slot])

    def test_pool_is_single_aligned_stage_allocation_and_fail_closed(self) -> None:
        self.assertIn("struct alignas(64) PspBulletStaticProxyPool", self.source)
        self.assertIn("sizeof(PspBulletStaticProxyRecord) == 80", self.source)
        self.assertIn("sizeof(PspBulletStaticProxyIdentity) == 40", self.source)
        self.assertIn("sizeof(PspBulletStaticProxyPool) <= 128u * 1024u", self.source)
        ensure = function_body(
            self.source, "bool BulletManager::PspEnsureBulletStaticProxyPool"
        )
        release = function_body(
            self.source, "void BulletManager::PspReleaseBulletStaticProxyPool"
        )
        deleted = function_body(self.source, "ZunResult BulletManager::DeletedCallback")
        self.assertIn("memalign(64, sizeof(PspBulletStaticProxyPool))", ensure)
        self.assertIn("return false;", ensure)
        self.assertIn("std::free(this->pspBulletStaticProxyPool)", release)
        self.assertIn("PspReleaseBulletStaticProxyPool();", deleted)

    def test_calc_stable_frame_updates_only_dynamic_record_position(self) -> None:
        for forbidden in (
            "vm->pos.x =",
            "vm->pos.y =",
            "vm->pos.z =",
            "vm->color.color =",
            "vm->SetRotationZ",
            "vm->updateRotation =",
            "staticSignature",
        ):
            self.assertNotIn(forbidden, self.sync)
        self.assertIn("record.posX =", self.sync)
        self.assertIn("record.posY =", self.sync)
        self.assertIn("if (!prepared || angleChanged)", self.sync)
        self.assertNotIn("memset", self.sync)
        self.assertNotIn("malloc", self.sync)

    def test_rebuild_is_mutation_or_angle_only_and_publishes_flags_last(self) -> None:
        self.assertIn("record.flags = 0u;", self.rebuild)
        self.assertIn("vm->currentInstruction", self.rebuild)
        self.assertIn("PspCaptureBulletStaticProxyIdentity", self.rebuild)
        self.assertIn("record.localX[corner]", self.rebuild)
        self.assertIn("record.localY[corner]", self.rebuild)
        self.assertLess(
            self.rebuild.index("record.flags = 0u;"),
            self.rebuild.rindex("record.flags = flags"),
        )

    def test_draw_live_gate_precedes_every_observable_vm_mutation(self) -> None:
        gate_items = [
            "bullet->state != BULLET_NORMAL",
            "pool->publishedMutationEpoch",
            "record.generation != pool->generations[slot]",
            "PspBulletStaticProxyIdentityMatches",
            "record.posX",
            "record.sourceAngleBits",
        ]
        mutation = self.draw_proxy.index("vm->pos.x = expectedPosX;")
        for item in gate_items:
            self.assertLess(self.draw_proxy.index(item), mutation)
        ordered = [
            self.draw_proxy.index("vm->pos.x ="),
            self.draw_proxy.index("vm->pos.y ="),
            self.draw_proxy.index("vm->pos.z = 0.05f"),
            self.draw_proxy.index("vm->color.color ="),
            self.draw_proxy.index("vm->SetRotationZ"),
            self.draw_proxy.index("vm->updateRotation = 1"),
        ]
        self.assertEqual(ordered, sorted(ordered))

    def test_exact_identity_replaces_collision_prone_hash(self) -> None:
        identity = function_body(
            self.source, "PspBulletStaticProxyIdentityMatches("
        )
        for field in (
            "identity.sprite == vm->sprite",
            "identity.currentInstruction == vm->currentInstruction",
            "identity.scaleXBits",
            "identity.scaleYBits",
            "identity.uvXBits",
            "identity.uvYBits",
            "identity.baseColor",
            "identity.renderFlags",
            "identity.activeSpriteIdx",
            "identity.autoRotate",
        ):
            self.assertIn(field, identity)
        self.assertNotIn("hash", identity.lower())
        self.assertNotIn("staticSignature", self.source)

    def test_use_color2_still_tracks_canonical_color_alpha_gate(self) -> None:
        flags = function_body(
            self.source, "PspBulletStaticProxyRenderFlags("
        )
        base_color = function_body(
            self.source, "PspBulletStaticProxyBaseColor("
        )
        self.assertIn("vm->color.bytes.a", flags)
        self.assertIn("<< 24u", flags)
        self.assertIn("vm->useColor2", base_color)
        self.assertIn("vm->color2.color", base_color)

    def test_proxy_precedes_onepass_without_reordering_bucket_walk(self) -> None:
        bucket = self.draw.index("for (i = 0; i < 6; i++)")
        proxy = self.draw.index("PspTryBulletStaticProxy(", bucket)
        onepass = self.draw.index("PspDrawNormalAutoRotatedOnePass(", proxy)
        canonical = self.draw.index("bullet->Draw();", onepass)
        advance = self.draw.index("bullet = bullet->next;", canonical)
        self.assertLess(bucket, proxy)
        self.assertLess(proxy, onepass)
        self.assertLess(onepass, canonical)
        self.assertLess(canonical, advance)
        self.assertNotIn("sort", self.draw[bucket:advance])

    def test_all_known_sprite_writers_invalidate_through_wrapper(self) -> None:
        direct = re.findall(
            r"SetActiveSprite\s*\(&bullet->sprites\.spriteBullet", self.source + self.ecl
        )
        self.assertEqual(len(direct), 1)  # the wrapper implementation itself
        self.assertGreaterEqual(self.ecl.count("SetActiveBulletSprite("), 4)
        assign = function_body(self.source, "void Bullet::AssignTypeSprites")
        stop = function_body(self.source, "void BulletManager::StopBulletMovement")
        self.assertIn("PspInvalidateBulletStaticProxy(this);", assign)
        self.assertIn("SetActiveBulletSprite(", stop)
        invalidate = function_body(
            self.source, "void BulletManager::PspInvalidateBulletStaticProxy"
        )
        self.assertIn("++pool->generations[slot]", invalidate)
        self.assertIn("PspMarkBulletStaticProxyMutation();", invalidate)

    def test_post_calc_bulk_mutations_advance_manager_epoch(self) -> None:
        for signature in (
            "void BulletManager::RemoveAllBullets",
            "i32 BulletManager::DespawnBullets",
            "void BulletManager::RemoveBulletsInRadius",
            "void BulletManager::StopBulletMovement",
        ):
            self.assertIn(
                "PspMarkBulletStaticProxyMutation();",
                function_body(self.source, signature),
            )
        update = function_body(self.source, "u32 BulletManager::OnUpdate")
        self.assertIn("pool->publishedMutationEpoch", update)

    def test_dense_counters_preserve_existing_closure_without_new_timers(self) -> None:
        for field in (
            "staticProxyReadyFrames",
            "staticProxyFallbackFrames",
            "staticProxyVisitHits",
            "staticProxyCanonicalFallbacks",
        ):
            self.assertIn(field, self.header)
            self.assertIn(field, self.source)
            self.assertIn(field, self.graphics)
        self.assertIn('return "SPRXD";', self.fileio)
        self.assertIn("TH07_PSP_DENSE_STATIC_PROXY_VALID", self.graphics)
        self.assertIn(
            "dense.bulletVisits == dense.onePassAccepts +", self.graphics
        )
        proxy_region = self.source[
            self.source.index("PspSyncBulletStaticProxy(") :
            self.source.index("// Consume the stable NORMAL+autoRotate case")
        ]
        self.assertNotIn("sceKernelGetSystemTime", proxy_region)


class PspBulletStaticProxyDifferentialHarnessTests(unittest.TestCase):
    def test_vertices_state_and_vm_mutations_match_reference(self) -> None:
        compiler = shutil.which("g++") or shutil.which("c++")
        if compiler is None:
            self.skipTest("host C++ compiler is required")

        source = r'''
            #include <array>
            #include <cassert>
            #include <cmath>
            #include <cstdint>
            #include <cstring>
            #include <random>

            struct Vm { float x,y,z,rot; uint32_t color; bool update; };
            struct Vertex { float u,v,x,y,z; uint32_t color; };
            struct Record {
                float lx[4],ly[4],u0,u1,v0,v1,px,py,hw,hh;
                uint32_t color, angleBits, flags; uint16_t gen,reserved;
            };
            static uint32_t bits(float v) { uint32_t x; std::memcpy(&x,&v,4); return x; }
            static void mutate(Vm& vm,float px,float py,float angle) {
                vm.x=px; vm.y=py; vm.z=0.05f;
                vm.color=(vm.color&0xff000000u)|0x00ffffffu;
                vm.rot=angle; vm.update=true;
            }
            static std::array<Vertex,4> reference(Vm& vm,float px,float py,float hw,float hh,
                                                   float s,float c,float ox,float oy,
                                                   uint32_t anchor,float angle) {
                mutate(vm,px,py,angle);
                std::array<Vertex,4> out{};
                float xx[4]={-hw,hw,-hw,hw}, yy[4]={-hh,-hh,hh,hh};
                float uu[4]={.1f,.9f,.1f,.9f}, vv[4]={.2f,.2f,.8f,.8f};
                for(int i=0;i<4;i++) {
                    float x=xx[i]*c-yy[i]*s+px+ox;
                    float y=xx[i]*s+yy[i]*c+py+oy;
                    if(anchor&1)x+=hw; if(anchor&2)y+=hh;
                    out[i]={uu[i],vv[i],x,y,.05f,vm.color};
                }
                return out;
            }
            static Record build(float px,float py,float hw,float hh,float s,float c,
                                uint32_t anchor,float angle,uint16_t generation) {
                Record r{}; float xx[4]={-hw,hw,-hw,hw}, yy[4]={-hh,-hh,hh,hh};
                for(int i=0;i<4;i++) { r.lx[i]=xx[i]*c-yy[i]*s; r.ly[i]=xx[i]*s+yy[i]*c; }
                r.u0=.1f;r.u1=.9f;r.v0=.2f;r.v1=.8f;r.px=px;r.py=py;r.hw=hw;r.hh=hh;
                r.color=0xffffffffu;r.angleBits=bits(angle);r.flags=1u|(anchor<<2);r.gen=generation;
                return r;
            }
            static std::array<Vertex,4> proxy(Vm& vm,const Record&r,float ox,float oy,float angle,
                                               uint16_t generation,bool normal,bool epoch) {
                assert(normal&&epoch&&r.gen==generation&&r.angleBits==bits(angle));
                mutate(vm,r.px,r.py,angle);
                std::array<Vertex,4> out{}; float uu[4]={r.u0,r.u1,r.u0,r.u1};
                float vv[4]={r.v0,r.v0,r.v1,r.v1}; uint32_t a=(r.flags>>2)&3;
                for(int i=0;i<4;i++) {
                    float x=r.lx[i]+r.px+ox, y=r.ly[i]+r.py+oy;
                    if(a&1)x+=r.hw; if(a&2)y+=r.hh;
                    out[i]={uu[i],vv[i],x,y,.05f,vm.color};
                }
                return out;
            }
            int main() {
                std::mt19937 rng(0x53505258u);
                std::uniform_real_distribution<float> f(-400.f,400.f), a(-6.2f,6.2f);
                for(int n=0;n<20000;n++) {
                    float px=f(rng),py=f(rng),hw=std::fabs(f(rng))*.1f,hh=std::fabs(f(rng))*.1f;
                    float angle=a(rng),s=std::sin(angle),c=std::cos(angle),ox=f(rng),oy=f(rng);
                    uint32_t anchor=rng()&3; Vm vr{0,0,9,1,0x7f123456u,false},vp=vr;
                    Record rec=build(px,py,hw,hh,s,c,anchor,angle,uint16_t(n));
                    auto x=reference(vr,px,py,hw,hh,s,c,ox,oy,anchor,angle);
                    auto y=proxy(vp,rec,ox,oy,angle,uint16_t(n),true,true);
                    assert(std::memcmp(x.data(),y.data(),sizeof(x))==0);
                    assert(std::memcmp(&vr,&vp,sizeof(Vm))==0);
                }
            }
        '''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "proxy.cpp"
            exe = Path(tmp) / "proxy"
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
