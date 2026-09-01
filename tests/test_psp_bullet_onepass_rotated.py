from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE = "PSP_BULLET_ONEPASS_ROTATED"
MACRO = "TH07_PSP_BULLET_ONEPASS_ROTATED"


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


class PspBulletOnePassRotatedSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src" / "BulletManager.cpp").read_text(encoding="utf-8")
        cls.fast = function_body(cls.bullets, "PspDrawNormalAutoRotatedOnePass(")
        cls.on_draw = function_body(cls.bullets, "u32 BulletManager::OnDraw")

    def test_candidate_is_default_off_guarded_and_rotated_direct_dependent(self) -> None:
        self.assertIn(f"{FEATURE} ?= 0", self.makefile)
        start = self.makefile.index(f"ifeq ($({FEATURE}),1)")
        end = self.makefile.index("ifeq ($(PSP_ASCII_POPUP_BATCH),1)", start)
        block = self.makefile[start:end]
        self.assertIn(f"-D{MACRO}", block)
        self.assertIn("PSP-2000+", block)
        self.assertIn("ifneq ($(PSP_1000),0)", block)
        self.assertIn("ifneq ($(PSP_BULLET_ROTATED_DIRECT),1)", block)
        self.assertIn("ifneq ($(PSP_BULLET_AXIS_FAST),0)", block)
        self.assertIn("ifneq ($(PSP_BULLET_SNAPSHOT_EMITTER),0)", block)
        self.assertNotIn(
            "PSP_BULLET_ONEPASS_ROTATED and PSP_BULLET_UNIFIED_QUADS are mutually exclusive",
            block,
        )
        stamp = next(
            line for line in self.makefile.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn(f"$({FEATURE})", stamp)

    def test_named_and_release_build_roots_keep_candidate_off(self) -> None:
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

    def test_attribution_and_every_empty_timer_aa_reject_changed_frontend(self) -> None:
        self.assertRegex(
            self.makefile,
            rf"(?s)PSP_PERF_PROFILE\),ATTRIB\).*?{FEATURE}.*?PERF_ACCEPT",
        )
        self.assertRegex(
            self.makefile,
            rf"(?s)Empty-timer A/A calibration.*?"
            rf"ifneq \(\$\({FEATURE}\),0\)",
        )

    def test_implementation_is_compile_time_guarded(self) -> None:
        start = self.bullets.index("PspDrawNormalAutoRotatedOnePass(")
        # D2B adds nested position-source guards to the preceding static-proxy
        # function.  Find this feature's owning guard, not merely the nearest
        # (possibly nested) preprocessor directive.
        guard = self.bullets.rfind(f"#if defined({MACRO})", 0, start)
        self.assertNotEqual(guard, -1)
        self.assertIn(MACRO, self.bullets[guard:start])
        end = self.bullets.index("#endif", start)
        self.assertLess(start, end)

        call = self.on_draw.index("PspDrawNormalAutoRotatedOnePass(")
        call_guard = self.on_draw.rfind("#if", 0, call)
        self.assertIn(MACRO, self.on_draw[call_guard:call])

    def test_one_pass_route_preserves_original_bucket_walk_and_fallback(self) -> None:
        items = self.on_draw.index("g_ItemManager.OnDraw();")
        begin_rotated = self.on_draw.index("BeginPspRotatedBulletBatch();")
        viewport = self.on_draw.index("const ZunViewport &onePassViewport")
        bucket = re.search(r"for\s*\([^;]*=\s*0;[^;]*<\s*6;", self.on_draw)
        self.assertIsNotNone(bucket)
        assert bucket is not None
        call = self.on_draw.index("PspDrawNormalAutoRotatedOnePass(")
        fallback = self.on_draw.index("bullet->Draw();", call)
        advance = self.on_draw.index("bullet = bullet->next;", fallback)
        self.assertLess(items, begin_rotated)
        self.assertLess(begin_rotated, viewport)
        self.assertLess(viewport, bucket.start())
        self.assertLess(bucket.start(), call)
        self.assertLess(call, fallback)
        self.assertLess(fallback, advance)
        self.assertEqual(self.on_draw.count("PspDrawNormalAutoRotatedOnePass("), 1)
        self.assertNotIn("sort", self.on_draw[bucket.start() : advance])

    def test_viewport_cache_keeps_integer_add_then_float_conversion(self) -> None:
        for expression in (
            "static_cast<float>(onePassViewport.x)",
            "static_cast<float>(onePassViewport.y)",
            "static_cast<float>(onePassViewport.x + onePassViewport.width)",
            "static_cast<float>(onePassViewport.y + onePassViewport.height)",
        ):
            self.assertIn(expression, self.on_draw)

    def test_fast_predicate_is_the_narrow_stable_normal_case(self) -> None:
        state = self.fast.index("bullet->state != BULLET_NORMAL")
        vm = self.fast.index("AnmVm *vm = &bullet->sprites.spriteBullet;")
        predicate = self.fast.index("!vm->autoRotate")
        first_mutation = self.fast.index("vm->pos.x =")
        self.assertLess(state, vm)
        self.assertLess(vm, predicate)
        self.assertLess(predicate, first_mutation)
        for required in (
            "!bullet->pspRenderRotationValid",
            "bullet->pspRenderSourceAngle != bullet->angle",
            "bullet->pspRenderAngle == 0.0f",
        ):
            self.assertIn(required, self.fast[ predicate:first_mutation])
        self.assertNotIn("PspBulletRenderSinCos", self.fast)
        self.assertNotIn("AddNormalizeAngle", self.fast)

    def test_vm_side_effects_precede_reject_and_post_mutation_fallback(self) -> None:
        mutations = [
            self.fast.index("vm->pos.x ="),
            self.fast.index("vm->pos.y ="),
            self.fast.index("vm->pos.z = 0.05f;"),
            self.fast.index("vm->color.color ="),
            self.fast.index("vm->SetRotationZ(bullet->pspRenderAngle);"),
            self.fast.index("vm->updateRotation = 1;"),
        ]
        validation = self.fast.index("!vm->sprite")
        cull = self.fast.index("centerX + bound < viewportLeft")
        renderer_match = self.fast.index("const bool rendererStateMatches")
        post_fallback = self.fast.index("if (__builtin_expect(!rendererStateMatches")
        self.assertEqual(mutations, sorted(mutations))
        self.assertLess(max(mutations), validation)
        self.assertLess(validation, cull)
        self.assertLess(cull, renderer_match)
        self.assertLess(renderer_match, post_fallback)

    def test_renderer_match_gate_contains_every_call_causing_state(self) -> None:
        gate_start = self.fast.index("const bool rendererStateMatches")
        gate_end = self.fast.index("if (__builtin_expect(!rendererStateMatches", gate_start)
        gate = self.fast[gate_start:gate_end]
        for required in (
            "manager->currentTexture == texture",
            "manager->currentVertexShader == 1",
            "manager->pspSpriteBatchUsesPairs == 0",
            "manager->currentBlendMode",
            "vm->blendMode",
            "g_Supervisor.cfg.disableZBuffer",
            "manager->currentZWriteDisable",
            "vm->zWriteDisable",
        ):
            self.assertIn(required, gate)

        accepted = self.fast[gate_end:]
        for forbidden in (
            "Flush(",
            "SyncRenderState",
            "BindTexture",
            "DrawPspBullet",
            "DrawPspRotatedBullet",
            "PspBulletRenderSinCos",
        ):
            self.assertNotIn(forbidden, accepted)

    def test_fast_path_has_no_heap_io_logging_or_forced_sync(self) -> None:
        for forbidden in (
            "malloc(",
            "calloc(",
            "realloc(",
            "free(",
            "new ",
            "delete ",
            "std::vector",
            "fopen(",
            "fread(",
            "fwrite(",
            "sceIo",
            "th07_psp_boot_note",
            "sceGuSync",
            "sceGuFinish",
            "sceGuStart",
            "SubmitAndRestart",
            "sceKernelDcacheWriteback",
            "PreparePspBulletRenderRecord",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, self.fast)

    def test_four_vertices_keep_legacy_expression_and_byte_order(self) -> None:
        expected = (
            (0, "-halfWidth", "-halfHeight", "u0", "v0"),
            (1, "halfWidth", "-halfHeight", "u1", "v0"),
            (2, "-halfWidth", "halfHeight", "u0", "v1"),
            (3, "halfWidth", "halfHeight", "u1", "v1"),
        )
        first_corner = self.fast.index("const float localX0")
        for forbidden in ("Flush(", "SyncRenderState", "BindTexture"):
            self.assertNotIn(forbidden, self.fast[first_corner:])
        for corner, local_x, local_y, u, v in expected:
            x_decl = f"const float localX{corner} = {local_x};"
            y_decl = f"const float localY{corner} = {local_y};"
            x_expr = (
                f"float x{corner} = localX{corner} * cachedCos - "
                f"localY{corner} * cachedSin + posX + offsetX;"
            )
            y_expr = (
                f"float y{corner} = localX{corner} * cachedSin + "
                f"localY{corner} * cachedCos + posY + offsetY;"
            )
            write = (
                f"PspBulletOnePassWriteVertex(out[{corner}], x{corner}, y{corner}, "
                f"z, {u}, {v}, color);"
            )
            for required in (x_decl, y_decl, x_expr, y_expr, write):
                self.assertIn(required, self.fast)
        self.assertIn("manager->vertexBufferCurPtr += 4;", self.fast)
        self.assertIn("++manager->spritesToDraw;", self.fast)


class PspBulletOnePassRotatedDifferentialHarnessTests(unittest.TestCase):
    def test_narrow_one_pass_matches_reference_for_live_state_events_and_vertices(self) -> None:
        compiler = shutil.which("g++") or shutil.which("c++")
        if compiler is None:
            self.skipTest("host C++ compiler is required for the differential harness")

        source = r"""
            #include <array>
            #include <cmath>
            #include <cstddef>
            #include <cstdint>
            #include <cstdio>
            #include <cstdlib>
            #include <cstring>
            #include <limits>
            #include <vector>

            using u8 = std::uint8_t;
            using u16 = std::uint16_t;
            using u32 = std::uint32_t;

            constexpr float PI = 3.1415927410125732421875f;
            constexpr float TWO_PI = 6.283185482025146484375f;

            static u32 FloatBits(float value) {
                u32 bits = 0u;
                std::memcpy(&bits, &value, sizeof(bits));
                return bits;
            }

            struct Color { u32 value; };

            static u8 Channel(Color color, unsigned int shift) {
                return static_cast<u8>((color.value >> shift) & 0xffu);
            }

            static u8 MultiplyChannel(u8 source, u8 factor) {
                const u32 product = static_cast<u32>(source) * factor >> 7;
                return static_cast<u8>(product >= 256u ? 255u : product);
            }

            static Color MultiplyColor(Color source, Color factor) {
                Color result{0u};
                for (unsigned int shift : {0u, 8u, 16u, 24u}) {
                    result.value |= static_cast<u32>(MultiplyChannel(
                        Channel(source, shift), Channel(factor, shift))) << shift;
                }
                return result;
            }

            static u32 GuColor(Color color) {
                return (color.value & 0xff00ff00u) |
                       ((color.value & 0x00ff0000u) >> 16) |
                       ((color.value & 0x000000ffu) << 16);
            }

            struct Vertex {
                float u;
                float v;
                u32 color;
                float x;
                float y;
                float z;
            };
            static_assert(sizeof(Vertex) == 24, "PSP vertex layout drift");

            enum EventKind : u32 {
                EVENT_FLUSH = 1u,
                EVENT_BIND_TEXTURE,
                EVENT_SHADER,
                EVENT_BLEND,
                EVENT_DEPTH_MASK,
                EVENT_PAIR_MODE,
                EVENT_TRIG,
            };

            struct Event {
                u32 kind;
                u32 a;
                u32 b;
                u32 c;
            };
            static_assert(sizeof(Event) == 16, "event layout drift");

            struct Viewport { u32 x, y, width, height; };

            struct Sprite {
                float width;
                float height;
                float u0;
                float u1;
                float v0;
                float v1;
                int source;
            };

            struct Vm {
                u32 hasSprite;
                u32 visible;
                u32 active;
                u32 useColor2;
                u32 updateRotation;
                u32 anchor;
                u32 blendMode;
                u32 zWriteDisable;
                int autoRotate;
                float posX;
                float posY;
                float posZ;
                float scaleX;
                float scaleY;
                float rotationX;
                float rotationY;
                float rotationZ;
                float uvX;
                float uvY;
                Color color;
                Color color2;
                Sprite sprite;
            };

            enum BulletState : u16 {
                BULLET_INACTIVE = 0,
                BULLET_NORMAL = 1,
                BULLET_SPAWNING_FAST = 2,
                BULLET_SPAWNING_NORMAL = 3,
                BULLET_SPAWNING_SLOW = 4,
                BULLET_DESPAWN = 5,
                BULLET_END_ARRAY = 6,
            };

            enum VmIndex : std::size_t {
                VM_BULLET = 0,
                VM_SPAWN_FAST,
                VM_SPAWN_NORMAL,
                VM_SPAWN_SLOW,
                VM_DONUT,
            };

            struct Bullet {
                std::array<Vm, 5> vms;
                float posX;
                float posY;
                float posZ;
                float angle;
                float sourceAngle;
                float renderAngle;
                float renderSin;
                float renderCos;
                u32 rotationValid;
                u16 state;
                u16 reserved;
            };

            struct Manager {
                float arcadeX;
                float arcadeY;
                float offsetX;
                float offsetY;
                Viewport viewport;
                float cachedLeft;
                float cachedTop;
                float cachedRight;
                float cachedBottom;
                u32 colorMulEnabled;
                u32 disableZ;
                Color colorMul;
                int currentTexture;
                int currentShader;
                u32 currentBlend;
                u32 currentZWriteDisable;
                u32 pairMode;
                u32 unified;
                u32 unifiedGeneral;
                u32 forceQuads;
                u32 spritesToDraw;
                u32 renderStateChanges;
                u32 flushes;
                std::vector<Vertex> vertices;
                std::vector<Event> events;

                static int TextureForSource(int source) { return 101 + source * 17; }

                void BeginBatch() {
                    cachedLeft = static_cast<float>(viewport.x);
                    cachedTop = static_cast<float>(viewport.y);
                    cachedRight = static_cast<float>(viewport.x + viewport.width);
                    cachedBottom = static_cast<float>(viewport.y + viewport.height);
                    unifiedGeneral = 0u;
                }

                void Flush() {
                    if (spritesToDraw == 0u) return;
                    events.push_back({EVENT_FLUSH, pairMode, spritesToDraw, forceQuads});
                    spritesToDraw = 0u;
                    if (unified) forceQuads = 0u;
                    ++flushes;
                }

                void SyncState(const Vm &vm) {
                    if (currentBlend != vm.blendMode) {
                        Flush();
                        currentBlend = vm.blendMode;
                        events.push_back({EVENT_BLEND, currentBlend, 0u, 0u});
                    }
                    if (!disableZ && currentZWriteDisable != vm.zWriteDisable) {
                        Flush();
                        currentZWriteDisable = vm.zWriteDisable;
                        events.push_back(
                            {EVENT_DEPTH_MASK, currentZWriteDisable == 0u ? 1u : 0u,
                             0u, 0u});
                    }
                    ++renderStateChanges;
                }
            };

            struct Coverage {
                u32 acceptedDraw = 0u;
                u32 rejectSprite = 0u;
                u32 rejectVisible = 0u;
                u32 rejectActive = 0u;
                u32 rejectAlpha = 0u;
                u32 rejectCull = 0u;
                u32 preState = 0u;
                u32 preAuto = 0u;
                u32 preInvalid = 0u;
                u32 preAngle = 0u;
                u32 preZero = 0u;
                u32 postTexture = 0u;
                u32 postShader = 0u;
                u32 postPair = 0u;
                u32 postBlend = 0u;
                u32 postZ = 0u;
            };

            static float AddNormalizeAngle(float a, float b) {
                int iterations = 0;
                a += b;
                while (a > PI) {
                    a -= TWO_PI;
                    if (iterations++ > 16) break;
                }
                while (a < -PI) {
                    a += TWO_PI;
                    if (iterations++ > 16) break;
                }
                return a;
            }

            static void SinCosOracle(Manager &manager, float angle,
                                     float *outSin, float *outCos) {
                *outSin = static_cast<float>(std::sin(static_cast<double>(angle)));
                *outCos = static_cast<float>(std::cos(static_cast<double>(angle)));
                manager.events.push_back(
                    {EVENT_TRIG, FloatBits(angle), FloatBits(*outSin), FloatBits(*outCos)});
            }

            static void WriteVertex(std::vector<Vertex> &vertices, float x, float y,
                                    float z, float u, float v, Color color) {
                vertices.push_back({u, v, GuColor(color), x, y, z});
            }

            static bool IsValid(const Vm &vm) {
                return vm.hasSprite && vm.visible && vm.active &&
                       Channel(vm.color, 24u) != 0u;
            }

            static bool IsVisible(const Manager &manager, const Vm &vm,
                                  float halfWidth, float halfHeight,
                                  float left, float top, float right, float bottom) {
                const float centerX = vm.posX + manager.offsetX +
                                      ((vm.anchor & 1u) ? halfWidth : 0.0f);
                const float centerY = vm.posY + manager.offsetY +
                                      ((vm.anchor & 2u) ? halfHeight : 0.0f);
                const float bound = std::fabs(halfWidth) + std::fabs(halfHeight);
                return !(centerX + bound < left || centerY + bound < top ||
                         centerX - bound > right || centerY - bound > bottom);
            }

            static Color PrepareColor(const Manager &manager, const Vm &vm) {
                Color color = vm.useColor2 ? vm.color2 : vm.color;
                if (manager.colorMulEnabled) color = MultiplyColor(color, manager.colorMul);
                return color;
            }

            static void ReferenceTextureShaderState(Manager &manager, const Vm &vm) {
                const int texture = Manager::TextureForSource(vm.sprite.source);
                if (manager.currentTexture != texture) {
                    manager.currentTexture = texture;
                    manager.Flush();
                    manager.events.push_back({EVENT_BIND_TEXTURE,
                                              static_cast<u32>(texture), 0u, 0u});
                }
                if (manager.currentShader != 1) {
                    manager.Flush();
                    manager.currentShader = 1;
                    manager.events.push_back({EVENT_SHADER, 1u, 0u, 0u});
                }
            }

            static void ReferenceRotatedDraw(Manager &manager, const Vm &vm,
                                             float cachedSin, float cachedCos) {
                if (!IsValid(vm)) return;
                const float halfWidth = vm.sprite.width * vm.scaleX * 0.5f;
                const float halfHeight = vm.sprite.height * vm.scaleY * 0.5f;
                if (!IsVisible(manager, vm, halfWidth, halfHeight,
                               manager.cachedLeft, manager.cachedTop,
                               manager.cachedRight, manager.cachedBottom)) return;

                ReferenceTextureShaderState(manager, vm);
                const Color color = PrepareColor(manager, vm);
                const bool stateMatches =
                    manager.currentBlend == vm.blendMode &&
                    (manager.disableZ ||
                     manager.currentZWriteDisable == vm.zWriteDisable);
                if (stateMatches) {
                    ++manager.renderStateChanges;
                } else {
                    manager.SyncState(vm);
                }
                if (manager.pairMode != 0u) {
                    manager.Flush();
                    manager.pairMode = 0u;
                    manager.events.push_back({EVENT_PAIR_MODE, 0u, 0u, 0u});
                }
                if (manager.unified) {
                    manager.unifiedGeneral = 1u;
                    manager.forceQuads = 1u;
                }

                const float localX[4] = {
                    -halfWidth, halfWidth, -halfWidth, halfWidth};
                const float localY[4] = {
                    -halfHeight, -halfHeight, halfHeight, halfHeight};
                const float u[4] = {vm.sprite.u0 + vm.uvX, vm.sprite.u1 + vm.uvX,
                                    vm.sprite.u0 + vm.uvX, vm.sprite.u1 + vm.uvX};
                const float v[4] = {vm.sprite.v0 + vm.uvY, vm.sprite.v0 + vm.uvY,
                                    vm.sprite.v1 + vm.uvY, vm.sprite.v1 + vm.uvY};
                for (int corner = 0; corner < 4; ++corner) {
                    float x = localX[corner] * cachedCos - localY[corner] * cachedSin +
                              vm.posX + manager.offsetX;
                    float y = localX[corner] * cachedSin + localY[corner] * cachedCos +
                              vm.posY + manager.offsetY;
                    if (vm.anchor & 1u) x += halfWidth;
                    if (vm.anchor & 2u) y += halfHeight;
                    WriteVertex(manager.vertices, x, y, vm.posZ, u[corner], v[corner], color);
                }
                ++manager.spritesToDraw;
            }

            static void ReferenceGenericDraw(Manager &manager, const Vm &vm,
                                             bool hasCachedSinCos,
                                             float cachedSin, float cachedCos) {
                if (!IsValid(vm)) return;
                const float halfWidth = vm.sprite.width * vm.scaleX * 0.5f;
                const float halfHeight = vm.sprite.height * vm.scaleY * 0.5f;
                if (!IsVisible(manager, vm, halfWidth, halfHeight,
                               static_cast<float>(manager.viewport.x),
                               static_cast<float>(manager.viewport.y),
                               static_cast<float>(manager.viewport.x + manager.viewport.width),
                               static_cast<float>(manager.viewport.y + manager.viewport.height))) {
                    return;
                }

                float x[4]{};
                float y[4]{};
                if (vm.rotationZ == 0.0f) {
                    const float rawLeft = (vm.anchor & 1u) ? vm.posX
                                                          : vm.posX - halfWidth;
                    const float rawRight = (vm.anchor & 1u)
                                               ? vm.posX + halfWidth * 2.0f
                                               : vm.posX + halfWidth;
                    const float rawTop = (vm.anchor & 2u) ? vm.posY
                                                         : vm.posY - halfHeight;
                    const float rawBottom = (vm.anchor & 2u)
                                                ? vm.posY + halfHeight * 2.0f
                                                : vm.posY + halfHeight;
                    const float left = std::floor(rawLeft + manager.offsetX + 0.5f);
                    const float right = std::floor(rawRight + manager.offsetX + 0.5f);
                    const float top = std::floor(rawTop + manager.offsetY + 0.5f);
                    const float bottom = std::floor(rawBottom + manager.offsetY + 0.5f);
                    x[0] = x[2] = left;
                    x[1] = x[3] = right;
                    y[0] = y[1] = top;
                    y[2] = y[3] = bottom;
                } else {
                    float sine = cachedSin;
                    float cosine = cachedCos;
                    if (!hasCachedSinCos) {
                        SinCosOracle(manager, vm.rotationZ, &sine, &cosine);
                    }
                    const float localX[4] = {
                        -halfWidth, halfWidth, -halfWidth, halfWidth};
                    const float localY[4] = {
                        -halfHeight, -halfHeight, halfHeight, halfHeight};
                    for (int corner = 0; corner < 4; ++corner) {
                        x[corner] = localX[corner] * cosine - localY[corner] * sine +
                                    vm.posX + manager.offsetX;
                        y[corner] = localX[corner] * sine + localY[corner] * cosine +
                                    vm.posY + manager.offsetY;
                        if (vm.anchor & 1u) x[corner] += halfWidth;
                        if (vm.anchor & 2u) y[corner] += halfHeight;
                    }
                }

                ReferenceTextureShaderState(manager, vm);
                const Color color = PrepareColor(manager, vm);
                manager.SyncState(vm);
                const bool pairEligible =
                    vm.rotationZ == 0.0f && x[0] <= x[3] && y[0] <= y[3];
                if (manager.unified && !pairEligible) manager.unifiedGeneral = 1u;
                const bool usePairs = pairEligible &&
                                      (!manager.unified || !manager.unifiedGeneral);
                if (manager.pairMode != static_cast<u32>(usePairs)) {
                    manager.Flush();
                    manager.pairMode = static_cast<u32>(usePairs);
                    manager.events.push_back(
                        {EVENT_PAIR_MODE, manager.pairMode, 0u, 0u});
                }
                if (manager.unified && manager.unifiedGeneral) manager.forceQuads = 1u;

                const float u0 = vm.sprite.u0 + vm.uvX;
                const float u1 = vm.sprite.u1 + vm.uvX;
                const float v0 = vm.sprite.v0 + vm.uvY;
                const float v1 = vm.sprite.v1 + vm.uvY;
                WriteVertex(manager.vertices, x[0], y[0], vm.posZ, u0, v0, color);
                if (usePairs) {
                    WriteVertex(manager.vertices, x[3], y[3], vm.posZ, u1, v1, color);
                } else {
                    WriteVertex(manager.vertices, x[1], y[1], vm.posZ, u1, v0, color);
                    WriteVertex(manager.vertices, x[2], y[2], vm.posZ, u0, v1, color);
                    WriteVertex(manager.vertices, x[3], y[3], vm.posZ, u1, v1, color);
                }
                ++manager.spritesToDraw;
            }

            static Vm &SelectedVm(Bullet &bullet) {
                switch (bullet.state) {
                case BULLET_SPAWNING_FAST:
                    return bullet.vms[VM_SPAWN_FAST];
                case BULLET_SPAWNING_NORMAL:
                    return bullet.vms[VM_SPAWN_NORMAL];
                case BULLET_SPAWNING_SLOW:
                    return bullet.vms[VM_SPAWN_SLOW];
                case BULLET_DESPAWN:
                    return bullet.vms[VM_DONUT];
                default:
                    return bullet.vms[VM_BULLET];
                }
            }

            static void ReferenceBulletDraw(Bullet &bullet, Manager &manager) {
                Vm &vm = SelectedVm(bullet);
                vm.posX = manager.arcadeX + bullet.posX;
                vm.posY = manager.arcadeY + bullet.posY;
                vm.posZ = 0.05f;
                vm.color.value = (vm.color.value & 0xff000000u) | 0x00ffffffu;
                if (vm.autoRotate) {
                    if (!bullet.rotationValid || bullet.sourceAngle != bullet.angle) {
                        const float angle = AddNormalizeAngle(1.5707964f + bullet.angle,
                                                              0.0f);
                        SinCosOracle(manager, angle, &bullet.renderSin, &bullet.renderCos);
                        bullet.sourceAngle = bullet.angle;
                        bullet.renderAngle = angle;
                        bullet.rotationValid = 1u;
                    }
                    vm.rotationZ = bullet.renderAngle;
                    vm.updateRotation = 1u;
                    if (vm.rotationZ != 0.0f) {
                        ReferenceRotatedDraw(manager, vm,
                                             bullet.renderSin, bullet.renderCos);
                    } else {
                        ReferenceGenericDraw(manager, vm, true,
                                             bullet.renderSin, bullet.renderCos);
                    }
                } else {
                    ReferenceGenericDraw(manager, vm, false, 0.0f, 1.0f);
                }
            }

            static bool CandidateFast(Bullet &bullet, Manager &manager,
                                      Coverage &coverage) {
                if (bullet.state != BULLET_NORMAL) {
                    ++coverage.preState;
                    return false;
                }
                Vm &vm = bullet.vms[VM_BULLET];
                if (!vm.autoRotate) {
                    ++coverage.preAuto;
                    return false;
                }
                if (!bullet.rotationValid) {
                    ++coverage.preInvalid;
                    return false;
                }
                if (bullet.sourceAngle != bullet.angle) {
                    ++coverage.preAngle;
                    return false;
                }
                if (bullet.renderAngle == 0.0f) {
                    ++coverage.preZero;
                    return false;
                }

                vm.posX = manager.arcadeX + bullet.posX;
                vm.posY = manager.arcadeY + bullet.posY;
                vm.posZ = 0.05f;
                vm.color.value = (vm.color.value & 0xff000000u) | 0x00ffffffu;
                vm.rotationZ = bullet.renderAngle;
                vm.updateRotation = 1u;

                if (!vm.hasSprite) {
                    ++coverage.rejectSprite;
                    return true;
                }
                if (!vm.visible) {
                    ++coverage.rejectVisible;
                    return true;
                }
                if (!vm.active) {
                    ++coverage.rejectActive;
                    return true;
                }
                if (Channel(vm.color, 24u) == 0u) {
                    ++coverage.rejectAlpha;
                    return true;
                }

                const float halfWidth = vm.sprite.width * vm.scaleX * 0.5f;
                const float halfHeight = vm.sprite.height * vm.scaleY * 0.5f;
                if (!IsVisible(manager, vm, halfWidth, halfHeight,
                               manager.cachedLeft, manager.cachedTop,
                               manager.cachedRight, manager.cachedBottom)) {
                    ++coverage.rejectCull;
                    return true;
                }

                const int texture = Manager::TextureForSource(vm.sprite.source);
                if (manager.currentTexture != texture) {
                    ++coverage.postTexture;
                    return false;
                }
                if (manager.currentShader != 1) {
                    ++coverage.postShader;
                    return false;
                }
                if (manager.pairMode != 0u) {
                    ++coverage.postPair;
                    return false;
                }
                if (manager.currentBlend != vm.blendMode) {
                    ++coverage.postBlend;
                    return false;
                }
                if (!manager.disableZ &&
                    manager.currentZWriteDisable != vm.zWriteDisable) {
                    ++coverage.postZ;
                    return false;
                }

                const Color color = PrepareColor(manager, vm);
                ++manager.renderStateChanges;
                if (manager.unified) {
                    manager.unifiedGeneral = 1u;
                    manager.forceQuads = 1u;
                }

                const float halfWidth2 = vm.sprite.width * vm.scaleX * 0.5f;
                const float halfHeight2 = vm.sprite.height * vm.scaleY * 0.5f;
                const float u0 = vm.sprite.u0 + vm.uvX;
                const float u1 = vm.sprite.u1 + vm.uvX;
                const float v0 = vm.sprite.v0 + vm.uvY;
                const float v1 = vm.sprite.v1 + vm.uvY;
                const float z = vm.posZ;
                const float posX = vm.posX;
                const float posY = vm.posY;
                const float offsetX = manager.offsetX;
                const float offsetY = manager.offsetY;
                const u32 anchor = vm.anchor;
                const float cachedSin = bullet.renderSin;
                const float cachedCos = bullet.renderCos;

                const float localX0 = -halfWidth2;
                const float localY0 = -halfHeight2;
                float x0 = localX0 * cachedCos - localY0 * cachedSin + posX + offsetX;
                float y0 = localX0 * cachedSin + localY0 * cachedCos + posY + offsetY;
                if (anchor & 1u) x0 += halfWidth2;
                if (anchor & 2u) y0 += halfHeight2;
                WriteVertex(manager.vertices, x0, y0, z, u0, v0, color);

                const float localX1 = halfWidth2;
                const float localY1 = -halfHeight2;
                float x1 = localX1 * cachedCos - localY1 * cachedSin + posX + offsetX;
                float y1 = localX1 * cachedSin + localY1 * cachedCos + posY + offsetY;
                if (anchor & 1u) x1 += halfWidth2;
                if (anchor & 2u) y1 += halfHeight2;
                WriteVertex(manager.vertices, x1, y1, z, u1, v0, color);

                const float localX2 = -halfWidth2;
                const float localY2 = halfHeight2;
                float x2 = localX2 * cachedCos - localY2 * cachedSin + posX + offsetX;
                float y2 = localX2 * cachedSin + localY2 * cachedCos + posY + offsetY;
                if (anchor & 1u) x2 += halfWidth2;
                if (anchor & 2u) y2 += halfHeight2;
                WriteVertex(manager.vertices, x2, y2, z, u0, v1, color);

                const float localX3 = halfWidth2;
                const float localY3 = halfHeight2;
                float x3 = localX3 * cachedCos - localY3 * cachedSin + posX + offsetX;
                float y3 = localX3 * cachedSin + localY3 * cachedCos + posY + offsetY;
                if (anchor & 1u) x3 += halfWidth2;
                if (anchor & 2u) y3 += halfHeight2;
                WriteVertex(manager.vertices, x3, y3, z, u1, v1, color);

                ++manager.spritesToDraw;
                ++coverage.acceptedDraw;
                return true;
            }

            static void CandidateDispatch(Bullet &bullet, Manager &manager,
                                          Coverage &coverage) {
                if (!CandidateFast(bullet, manager, coverage)) {
                    ReferenceBulletDraw(bullet, manager);
                }
            }

            static u32 Next(u32 &state) {
                state = state * 1664525u + 1013904223u;
                return state;
            }

            static float RandomFloat(u32 &state, float low, float high) {
                const float unit = static_cast<float>(Next(state) >> 8) /
                                   static_cast<float>(0x00ffffffu);
                return low + (high - low) * unit;
            }

            static Vm RandomVm(u32 &rng, std::size_t salt) {
                Vm vm{};
                vm.hasSprite = 1u;
                vm.visible = 1u;
                vm.active = 1u;
                vm.useColor2 = static_cast<u32>((Next(rng) >> 3) & 1u);
                vm.updateRotation = Next(rng) & 1u;
                vm.anchor = static_cast<u32>(salt & 3u);
                vm.blendMode = static_cast<u32>((salt >> 1u) & 1u);
                vm.zWriteDisable = static_cast<u32>((salt >> 2u) & 1u);
                vm.autoRotate = static_cast<int>((salt % 4u) == 0u ? 1 : 0);
                vm.posX = RandomFloat(rng, -300.0f, 700.0f);
                vm.posY = RandomFloat(rng, -200.0f, 500.0f);
                vm.posZ = RandomFloat(rng, -2.0f, 2.0f);
                static constexpr float scales[] = {
                    -2.0f, -1.0f, -0.0f, 0.0f, 0.125f, 1.0f, 2.5f};
                vm.scaleX = scales[salt % 7u];
                vm.scaleY = scales[(salt * 3u + 1u) % 7u];
                vm.rotationX = RandomFloat(rng, -4.0f, 4.0f);
                vm.rotationY = RandomFloat(rng, -4.0f, 4.0f);
                vm.rotationZ = RandomFloat(rng, -3.0f, 3.0f);
                vm.uvX = RandomFloat(rng, -0.25f, 0.25f);
                vm.uvY = RandomFloat(rng, -0.25f, 0.25f);
                vm.color = {Next(rng) | 0x01000000u};
                vm.color2 = {Next(rng)};
                vm.sprite.width = RandomFloat(rng, 1.0f, 96.0f);
                vm.sprite.height = RandomFloat(rng, 1.0f, 96.0f);
                vm.sprite.u0 = RandomFloat(rng, -0.25f, 0.75f);
                vm.sprite.u1 = RandomFloat(rng, 0.25f, 1.25f);
                vm.sprite.v0 = RandomFloat(rng, -0.25f, 0.75f);
                vm.sprite.v1 = RandomFloat(rng, 0.25f, 1.25f);
                vm.sprite.source = static_cast<int>(salt % 7u);
                return vm;
            }

            static Bullet RandomBullet(u32 &rng, std::size_t salt) {
                Bullet bullet{};
                for (std::size_t i = 0; i < bullet.vms.size(); ++i) {
                    bullet.vms[i] = RandomVm(rng, salt * 7u + i);
                }
                bullet.posX = RandomFloat(rng, -280.0f, 600.0f);
                bullet.posY = RandomFloat(rng, -180.0f, 440.0f);
                bullet.posZ = RandomFloat(rng, -2.0f, 2.0f);
                bullet.angle = RandomFloat(rng, -PI, PI);
                bullet.sourceAngle = bullet.angle;
                bullet.renderAngle = AddNormalizeAngle(1.5707964f + bullet.angle, 0.0f);
                bullet.renderSin = static_cast<float>(
                    std::sin(static_cast<double>(bullet.renderAngle)));
                bullet.renderCos = static_cast<float>(
                    std::cos(static_cast<double>(bullet.renderAngle)));
                bullet.rotationValid = 1u;
                bullet.state = BULLET_NORMAL;
                return bullet;
            }

            static Manager StableManager(bool unified) {
                Manager manager{};
                manager.arcadeX = 32.0f;
                manager.arcadeY = 16.0f;
                manager.offsetX = -3.25f;
                manager.offsetY = 2.5f;
                manager.viewport = {0u, 0u, 480u, 272u};
                manager.colorMulEnabled = 1u;
                manager.disableZ = 0u;
                manager.colorMul = {0xd0c0b0a0u};
                manager.currentTexture = Manager::TextureForSource(2);
                manager.currentShader = 1;
                manager.currentBlend = 1u;
                manager.currentZWriteDisable = 0u;
                manager.pairMode = 0u;
                manager.unified = unified ? 1u : 0u;
                manager.spritesToDraw = 1u;
                manager.renderStateChanges = 9u;
                manager.BeginBatch();
                manager.vertices.push_back(
                    {0.0f, 0.0f, 0x11223344u, 1.0f, 2.0f, 3.0f});
                manager.vertices.push_back(
                    {1.0f, 0.0f, 0x11223344u, 2.0f, 2.0f, 3.0f});
                manager.vertices.push_back(
                    {0.0f, 1.0f, 0x11223344u, 1.0f, 3.0f, 3.0f});
                manager.vertices.push_back(
                    {1.0f, 1.0f, 0x11223344u, 2.0f, 3.0f, 3.0f});
                return manager;
            }

            static void MakeFastEligible(Bullet &bullet, const Manager &manager) {
                bullet.state = BULLET_NORMAL;
                Vm &vm = bullet.vms[VM_BULLET];
                vm.autoRotate = 1;
                vm.hasSprite = 1u;
                vm.visible = 1u;
                vm.active = 1u;
                vm.color.value |= 0x7f000000u;
                vm.sprite.source = 2;
                vm.blendMode = manager.currentBlend;
                vm.zWriteDisable = manager.currentZWriteDisable;
                vm.scaleX = 1.25f;
                vm.scaleY = 0.75f;
                bullet.angle = 0.375f;
                bullet.sourceAngle = bullet.angle;
                bullet.renderAngle = 1.9457964f;
                bullet.renderSin = static_cast<float>(
                    std::sin(static_cast<double>(bullet.renderAngle)));
                bullet.renderCos = static_cast<float>(
                    std::cos(static_cast<double>(bullet.renderAngle)));
                bullet.rotationValid = 1u;
                bullet.posX = 180.0f;
                bullet.posY = 100.0f;
            }

            [[noreturn]] static void Fail(const char *what, std::size_t run,
                                          std::size_t index) {
                std::fprintf(stderr, "%s at run=%zu index=%zu\n", what, run, index);
                std::exit(1);
            }

            static void Compare(const Bullet &referenceBullet,
                                const Bullet &candidateBullet,
                                const Manager &reference,
                                const Manager &candidate,
                                std::size_t run, std::size_t index) {
                if (std::memcmp(&referenceBullet, &candidateBullet,
                                sizeof(Bullet)) != 0) {
                    Fail("bullet/vm/cache bytes", run, index);
                }
                if (reference.vertices.size() != candidate.vertices.size())
                    Fail("vertex count", run, index);
                if (!reference.vertices.empty() &&
                    std::memcmp(reference.vertices.data(), candidate.vertices.data(),
                                reference.vertices.size() * sizeof(Vertex)) != 0)
                    Fail("vertex bytes", run, index);
                if (reference.events.size() != candidate.events.size())
                    Fail("event count", run, index);
                if (!reference.events.empty() &&
                    std::memcmp(reference.events.data(), candidate.events.data(),
                                reference.events.size() * sizeof(Event)) != 0)
                    Fail("event bytes", run, index);
                if (FloatBits(reference.cachedLeft) != FloatBits(candidate.cachedLeft) ||
                    FloatBits(reference.cachedTop) != FloatBits(candidate.cachedTop) ||
                    FloatBits(reference.cachedRight) != FloatBits(candidate.cachedRight) ||
                    FloatBits(reference.cachedBottom) != FloatBits(candidate.cachedBottom) ||
                    reference.currentTexture != candidate.currentTexture ||
                    reference.currentShader != candidate.currentShader ||
                    reference.currentBlend != candidate.currentBlend ||
                    reference.currentZWriteDisable != candidate.currentZWriteDisable ||
                    reference.pairMode != candidate.pairMode ||
                    reference.unifiedGeneral != candidate.unifiedGeneral ||
                    reference.forceQuads != candidate.forceQuads ||
                    reference.spritesToDraw != candidate.spritesToDraw ||
                    reference.renderStateChanges != candidate.renderStateChanges ||
                    reference.flushes != candidate.flushes)
                    Fail("renderer state", run, index);
            }

            static u32 HashBytes(const void *data, std::size_t size) {
                const u8 *bytes = static_cast<const u8 *>(data);
                u32 hash = 2166136261u;
                for (std::size_t i = 0; i < size; ++i) {
                    hash = (hash ^ bytes[i]) * 16777619u;
                }
                return hash;
            }

            static void RunIsolated(Bullet input, Manager initial,
                                    Coverage &coverage, std::size_t index) {
                Bullet referenceBullet = input;
                Bullet candidateBullet = input;
                Manager reference = initial;
                Manager candidate = initial;
                ReferenceBulletDraw(referenceBullet, reference);
                CandidateDispatch(candidateBullet, candidate, coverage);
                Compare(referenceBullet, candidateBullet, reference, candidate, 0u, index);
            }

            static void CheckCoverage(const Coverage &c) {
                const u32 counts[] = {
                    c.acceptedDraw, c.rejectSprite, c.rejectVisible, c.rejectActive,
                    c.rejectAlpha, c.rejectCull, c.preState, c.preAuto,
                    c.preInvalid, c.preAngle, c.preZero, c.postTexture,
                    c.postShader, c.postPair, c.postBlend, c.postZ,
                };
                for (std::size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
                    if (counts[i] == 0u) Fail("coverage", 999u, i);
                }
            }

            int main() {
                if (GuColor({0xa1b2c3d4u}) != 0xa1d4c3b2u)
                    Fail("GU color golden", 0u, 0u);

                u32 rng = 0x6d2b79f5u;
                Coverage coverage{};
                std::size_t directed = 0u;
                for (bool unified : {false, true}) {
                    Manager manager = StableManager(unified);
                    Bullet base = RandomBullet(rng, directed);
                    MakeFastEligible(base, manager);

                    RunIsolated(base, manager, coverage, directed++); // accepted

                    Bullet bullet = base;
                    bullet.state = BULLET_SPAWNING_FAST;
                    bullet.vms[VM_SPAWN_FAST].autoRotate = 1;
                    RunIsolated(bullet, manager, coverage, directed++);

                    bullet = base;
                    bullet.vms[VM_BULLET].autoRotate = 0;
                    RunIsolated(bullet, manager, coverage, directed++);

                    bullet = base;
                    bullet.rotationValid = 0u;
                    RunIsolated(bullet, manager, coverage, directed++);

                    bullet = base;
                    bullet.sourceAngle = std::nextafter(
                        bullet.angle, std::numeric_limits<float>::infinity());
                    RunIsolated(bullet, manager, coverage, directed++);

                    bullet = base;
                    bullet.renderAngle = -0.0f;
                    RunIsolated(bullet, manager, coverage, directed++);

                    bullet = base;
                    bullet.vms[VM_BULLET].hasSprite = 0u;
                    RunIsolated(bullet, manager, coverage, directed++);

                    bullet = base;
                    bullet.vms[VM_BULLET].visible = 0u;
                    RunIsolated(bullet, manager, coverage, directed++);

                    bullet = base;
                    bullet.vms[VM_BULLET].active = 0u;
                    RunIsolated(bullet, manager, coverage, directed++);

                    bullet = base;
                    bullet.vms[VM_BULLET].color.value &= 0x00ffffffu;
                    RunIsolated(bullet, manager, coverage, directed++);

                    bullet = base;
                    bullet.posX = -10000.0f;
                    RunIsolated(bullet, manager, coverage, directed++);

                    Manager changed = manager;
                    changed.currentTexture = Manager::TextureForSource(6);
                    RunIsolated(base, changed, coverage, directed++);

                    changed = manager;
                    changed.currentShader = 0;
                    RunIsolated(base, changed, coverage, directed++);

                    changed = manager;
                    changed.pairMode = 1u;
                    RunIsolated(base, changed, coverage, directed++);

                    changed = manager;
                    changed.currentBlend ^= 1u;
                    RunIsolated(base, changed, coverage, directed++);

                    changed = manager;
                    changed.currentZWriteDisable ^= 1u;
                    changed.disableZ = 0u;
                    RunIsolated(base, changed, coverage, directed++);

                    // A z mismatch is intentionally accepted when the z buffer is disabled.
                    changed.disableZ = 1u;
                    RunIsolated(base, changed, coverage, directed++);

                    // Primary alpha gates visibility even when color2 is selected.
                    bullet = base;
                    bullet.vms[VM_BULLET].useColor2 = 1u;
                    bullet.vms[VM_BULLET].color2.value &= 0x00ffffffu;
                    RunIsolated(bullet, manager, coverage, directed++);
                }

                // Stateful mixed streams prove that batching order and pending renderer
                // state remain identical, not merely isolated bullet geometry.
                for (std::size_t run = 0; run < 24u; ++run) {
                    Manager reference = StableManager((run & 1u) != 0u);
                    reference.arcadeX = RandomFloat(rng, -64.0f, 64.0f);
                    reference.arcadeY = RandomFloat(rng, -64.0f, 64.0f);
                    reference.offsetX = RandomFloat(rng, -32.0f, 32.0f);
                    reference.offsetY = RandomFloat(rng, -32.0f, 32.0f);
                    reference.disableZ = static_cast<u32>((run >> 1u) & 1u);
                    Manager candidate = reference;
                    for (std::size_t index = 0; index < 192u; ++index) {
                        Bullet input = RandomBullet(rng, run * 193u + index);
                        MakeFastEligible(input, reference);
                        Vm &vm = input.vms[VM_BULLET];
                        switch (index % 16u) {
                        case 1:
                            input.state = static_cast<u16>(index % 7u);
                            break;
                        case 2:
                            vm.autoRotate = 0;
                            break;
                        case 3:
                            input.rotationValid = 0u;
                            break;
                        case 4:
                            input.sourceAngle = -input.angle;
                            break;
                        case 5:
                            input.renderAngle = 0.0f;
                            break;
                        case 6:
                            vm.hasSprite = 0u;
                            break;
                        case 7:
                            input.posY = 9000.0f;
                            break;
                        case 8:
                            vm.sprite.source = (vm.sprite.source + 1) % 7;
                            break;
                        case 9:
                            vm.blendMode ^= 1u;
                            break;
                        case 10:
                            vm.zWriteDisable ^= 1u;
                            break;
                        case 11:
                            vm.anchor = static_cast<u32>(index & 3u);
                            vm.scaleX = -1.0f;
                            vm.scaleY = 2.5f;
                            break;
                        case 12:
                            input.sourceAngle = -0.0f;
                            input.angle = 0.0f; // equality must hit despite sign bit
                            break;
                        case 13:
                            input.rotationValid = 0xffffffffu;
                            break;
                        default:
                            break;
                        }

                        Bullet referenceBullet = input;
                        Bullet candidateBullet = input;
                        ReferenceBulletDraw(referenceBullet, reference);
                        CandidateDispatch(candidateBullet, candidate, coverage);
                        Compare(referenceBullet, candidateBullet, reference, candidate,
                                run + 1u, index);
                    }
                }

                // Delayed-side-effect gate: keep the same Bullet/VM/cache
                // objects alive across hundreds of frames. A side effect that
                // happens to be invisible in the accepting frame will still
                // perturb a later fallback or cache refresh, and the per-frame
                // whole-array hash catches that divergence immediately.
                for (bool unified : {false, true}) {
                    constexpr std::size_t kPersistentBullets = 32u;
                    std::array<Bullet, kPersistentBullets> referenceBullets{};
                    std::array<Bullet, kPersistentBullets> candidateBullets{};
                    Manager reference = StableManager(unified);
                    Manager candidate = reference;
                    for (std::size_t index = 0; index < kPersistentBullets; ++index) {
                        referenceBullets[index] = RandomBullet(rng, 9000u + index);
                        MakeFastEligible(referenceBullets[index], reference);
                        referenceBullets[index].posX = 24.0f + static_cast<float>(index * 11u);
                        referenceBullets[index].posY = 28.0f + static_cast<float>(index * 5u);
                        candidateBullets[index] = referenceBullets[index];
                    }

                    for (std::size_t frame = 0; frame < 360u; ++frame) {
                        reference.vertices.clear();
                        reference.events.clear();
                        reference.spritesToDraw = 0u;
                        reference.BeginBatch();
                        candidate.vertices.clear();
                        candidate.events.clear();
                        candidate.spritesToDraw = 0u;
                        candidate.BeginBatch();

                        for (std::size_t index = 0; index < kPersistentBullets; ++index) {
                            Bullet &referenceBullet = referenceBullets[index];
                            Bullet &candidateBullet = candidateBullets[index];
                            const float dx = static_cast<float>(static_cast<int>(index % 5u) - 2) * 0.125f;
                            const float dy = static_cast<float>(static_cast<int>(index % 7u) - 3) * 0.0625f;
                            referenceBullet.posX += dx;
                            referenceBullet.posY += dy;
                            candidateBullet.posX += dx;
                            candidateBullet.posY += dy;

                            if ((frame + index * 3u) % 47u == 0u) {
                                referenceBullet.angle = AddNormalizeAngle(
                                    referenceBullet.angle, 0.03125f);
                                candidateBullet.angle = AddNormalizeAngle(
                                    candidateBullet.angle, 0.03125f);
                            }
                            if ((frame + index) % 83u == 0u) {
                                referenceBullet.vms[VM_BULLET].visible ^= 1u;
                                candidateBullet.vms[VM_BULLET].visible ^= 1u;
                            }
                            if ((frame + index * 5u) % 109u == 0u) {
                                referenceBullet.state = BULLET_SPAWNING_FAST;
                                candidateBullet.state = BULLET_SPAWNING_FAST;
                            } else if (referenceBullet.state != BULLET_NORMAL) {
                                referenceBullet.state = BULLET_NORMAL;
                                candidateBullet.state = BULLET_NORMAL;
                            }

                            ReferenceBulletDraw(referenceBullet, reference);
                            CandidateDispatch(candidateBullet, candidate, coverage);
                            Compare(referenceBullet, candidateBullet, reference, candidate,
                                    100u + static_cast<std::size_t>(unified),
                                    frame * kPersistentBullets + index);
                        }

                        const u32 referenceFrameHash = HashBytes(
                            referenceBullets.data(), sizeof(referenceBullets));
                        const u32 candidateFrameHash = HashBytes(
                            candidateBullets.data(), sizeof(candidateBullets));
                        if (referenceFrameHash != candidateFrameHash)
                            Fail("persistent per-frame Bullet/VM/cache hash",
                                 100u + static_cast<std::size_t>(unified), frame);
                    }
                }

                CheckCoverage(coverage);
                return 0;
            }
        """

        with tempfile.TemporaryDirectory() as temporary:
            temporary_path = Path(temporary)
            cpp = temporary_path / "onepass_rotated_differential.cpp"
            executable = temporary_path / "onepass_rotated_differential"
            cpp.write_text(textwrap.dedent(source), encoding="utf-8")
            subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fno-fast-math",
                    "-ffp-contract=off",
                    str(cpp),
                    "-o",
                    str(executable),
                ],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            subprocess.run(
                [str(executable)],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )


if __name__ == "__main__":
    unittest.main()
