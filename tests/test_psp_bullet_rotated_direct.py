from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE_MACRO = "TH07_PSP_BULLET_ROTATED_DIRECT"
MAKE_FEATURE = "PSP_BULLET_ROTATED_DIRECT"


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
    match = re.search(r"^[A-Za-z0-9_.-]+:", makefile[start + len(target) + 1 :], re.MULTILINE)
    if match is None:
        return makefile[start:]
    return makefile[start : start + len(target) + 1 + match.start()]


class PspBulletRotatedDirectSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src" / "BulletManager.cpp").read_text(encoding="utf-8")
        cls.anm = (ROOT / "src" / "AnmManager.cpp").read_text(encoding="utf-8")
        cls.anm_h = (ROOT / "src" / "AnmManager.hpp").read_text(encoding="utf-8")
        cls.bullet_draw = function_body(cls.bullets, "void Bullet::Draw()")
        cls.manager_draw = function_body(cls.bullets, "u32 BulletManager::OnDraw")
        cls.begin_batch = function_body(
            cls.anm, "void AnmManager::BeginPspRotatedBulletBatch()"
        )
        cls.direct = function_body(
            cls.anm, "AnmManager::DrawPspRotatedBullet("
        )

    def test_feature_is_default_off_reversible_and_psp2000plus_only(self) -> None:
        self.assertIn(f"{MAKE_FEATURE} ?= 0", self.makefile)
        feature_start = self.makefile.index(f"ifeq ($({MAKE_FEATURE}),1)")
        feature_end = self.makefile.index("ifeq ($(PSP_ASCII_POPUP_BATCH),1)", feature_start)
        feature_block = self.makefile[feature_start:feature_end]
        self.assertIn(f"-D{FEATURE_MACRO}", feature_block)
        self.assertIn("ifneq ($(PSP_1000),0)", feature_block)
        self.assertIn("ifneq ($(PSP_BULLET_AXIS_FAST),0)", feature_block)
        self.assertIn("ifneq ($(PSP_BULLET_SNAPSHOT_EMITTER),0)", feature_block)
        self.assertNotIn(
            "PSP_BULLET_ROTATED_DIRECT and PSP_BULLET_UNIFIED_QUADS are mutually exclusive",
            feature_block,
        )
        self.assertRegex(feature_block, r"PSP-2000\+")

        stamp = next(
            line for line in self.makefile.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn(f"$({MAKE_FEATURE})", stamp)

    def test_standard_and_release_recipes_keep_candidate_off(self) -> None:
        # Standalone profile builds must not silently promote an unaccepted A/B
        # bit.  The formal package uses exact hardware/rollback anchors instead
        # of rebuilding either payload from a dirty research worktree.
        for target in (
            "psp1000-build",
            "psp2000plus-build",
            "psp2000plus-shikigami-build",
            "psp3000-mecc-bgm384k-build",
            "psp3000-mecc-audio4m-build",
        ):
            with self.subTest(target=target):
                self.assertIn(f"{MAKE_FEATURE}=0", recipe_body(self.makefile, target))
        self.assertIn("release-build: psp-unified-build", self.makefile)
        unified = recipe_body(self.makefile, "psp-unified-build")
        self.assertIn("$(PSP_RELEASE_1000_ANCHOR)", unified)
        self.assertIn("$(PSP_RELEASE_2000PLUS_ANCHOR)", unified)
        self.assertNotIn(f"{MAKE_FEATURE}=1", unified)

    def test_attribution_profiles_reject_the_changed_boundary(self) -> None:
        self.assertRegex(
            self.makefile,
            r"(?s)PSP_PERF_PROFILE\),ATTRIB\).*?"
            r"ifneq \(\$\(PSP_BULLET_ROTATED_DIRECT\),0\).*?"
            r"rotated-direct A/B",
        )
        self.assertRegex(
            self.makefile,
            r"(?s)ifneq \(\$\(PSP_BULLET_ROTATED_DIRECT\),0\).*?"
            r"Empty-timer A/A calibration requires PSP_BULLET_ROTATED_DIRECT=0",
        )

    def test_declarations_and_implementation_are_compile_time_guarded(self) -> None:
        self.assertIn(f"#if defined({FEATURE_MACRO})", self.anm_h)
        self.assertIn("void BeginPspRotatedBulletBatch();", self.anm_h)
        self.assertIn("ZunResult DrawPspRotatedBullet", self.anm_h)
        direct_start = self.anm.index("void AnmManager::BeginPspRotatedBulletBatch()")
        guard = self.anm.rfind("#if", 0, direct_start)
        self.assertIn(FEATURE_MACRO, self.anm[guard:direct_start])

    def test_auto_rotate_side_effects_are_committed_before_direct_dispatch(self) -> None:
        direct = self.bullet_draw.index("DrawPspRotatedBullet")
        for required in (
            "pspRenderRotationValid",
            "pspRenderSourceAngle",
            "pspRenderSin",
            "pspRenderCos",
            "pspRenderAngle",
            "vm->SetRotationZ(this->pspRenderAngle);",
            "vm->updateRotation = 1;",
        ):
            with self.subTest(required=required):
                self.assertLess(self.bullet_draw.index(required), direct)

        route = self.bullet_draw[
            self.bullet_draw.rfind(f"#if defined({FEATURE_MACRO})", 0, direct) :
            self.bullet_draw.index("#else", direct)
        ]
        self.assertIn("if (vm->rotation.z != 0.0f)", route)
        self.assertIn("DrawPspRotatedBullet", route)
        self.assertIn("DrawPspBullet", route)
        self.assertLess(route.index("DrawPspRotatedBullet"), route.index("else"))
        self.assertEqual(self.bullet_draw.count("DrawPspRotatedBullet"), 1)

        non_auto = self.bullet_draw[self.bullet_draw.index("else", direct) :]
        self.assertIn("g_AnmManager->DrawPspBullet(vm);", non_auto)

    def test_viewport_cache_begins_after_items_and_before_bucket_traversal(self) -> None:
        items = self.manager_draw.index("g_ItemManager.OnDraw();")
        begin = self.manager_draw.index("BeginPspRotatedBulletBatch();")
        bucket = re.search(r"for\s*\([^;]*=\s*0;[^;]*<\s*6;", self.manager_draw)
        self.assertIsNotNone(bucket)
        assert bucket is not None
        self.assertLess(items, begin)
        self.assertLess(begin, bucket.start())
        guard = self.manager_draw.rfind("#if", 0, begin)
        self.assertIn(FEATURE_MACRO, self.manager_draw[guard:begin])
        self.assertEqual(self.manager_draw.count("BeginPspRotatedBulletBatch();"), 1)

    def test_viewport_cache_preserves_integer_add_then_float_conversion(self) -> None:
        self.assertIn("const ZunViewport &viewport = g_Supervisor.viewport;", self.begin_batch)
        self.assertIn("static_cast<float>(viewport.x)", self.begin_batch)
        self.assertIn("static_cast<float>(viewport.y)", self.begin_batch)
        self.assertIn("static_cast<float>(viewport.x + viewport.width)", self.begin_batch)
        self.assertIn("static_cast<float>(viewport.y + viewport.height)", self.begin_batch)
        for forbidden in ("malloc(", "new ", "sceIo", "fopen(", "sceGuSync"):
            self.assertNotIn(forbidden, self.begin_batch)

    def test_direct_hot_path_has_no_heap_io_or_forced_ge_sync(self) -> None:
        hot = self.begin_batch + "\n" + self.direct
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
            "ReadPixels",
            "ResetVertexBuffer",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, hot)

    def test_cull_uses_only_cached_batch_viewport(self) -> None:
        cull = self.direct[
            self.direct.index("const float centerX") :
            self.direct.index("const GfxTextureHandle texture")
        ]
        for boundary in (
            "gPspRotatedViewportLeft",
            "gPspRotatedViewportTop",
            "gPspRotatedViewportRight",
            "gPspRotatedViewportBottom",
        ):
            self.assertIn(boundary, cull)
        self.assertNotIn("g_Supervisor.viewport", cull)
        self.assertIn("const float bound = fabsf(halfWidth) + fabsf(halfHeight);", cull)

    def test_renderer_calls_finish_before_direct_corner_math(self) -> None:
        corner = self.direct.index("const float localX0")
        for state_operation in (
            "BindTexture",
            "SyncRenderState",
            "this->pspSpriteBatchUsesPairs = 0",
        ):
            self.assertLess(self.direct.index(state_operation), corner)
        self.assertNotIn("this->Flush()", self.direct[corner:])
        self.assertNotIn("SyncRenderState", self.direct[corner:])
        self.assertNotIn("float x[4]", self.direct)
        self.assertNotIn("float y[4]", self.direct)
        self.assertNotIn("PspRenderSinCos", self.direct)

    def test_all_four_corner_expressions_keep_legacy_float_order(self) -> None:
        for alias in (
            "const float posX = vm->pos.x;",
            "const float posY = vm->pos.y;",
            "const float offsetX = this->offset.x;",
            "const float offsetY = this->offset.y;",
            "const u32 anchor = vm->anchor;",
        ):
            self.assertIn(alias, self.direct)
        expected_locals = (
            (0, "-halfWidth", "-halfHeight", "u0", "v0"),
            (1, "halfWidth", "-halfHeight", "u1", "v0"),
            (2, "-halfWidth", "halfHeight", "u0", "v1"),
            (3, "halfWidth", "halfHeight", "u1", "v1"),
        )
        for corner, local_x, local_y, u, v in expected_locals:
            with self.subTest(corner=corner):
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
                    f"WritePspSpriteVertex(out[{corner}], x{corner}, y{corner}, "
                    f"z, {u}, {v}, color);"
                )
                for required in (x_decl, y_decl, x_expr, y_expr, write):
                    self.assertIn(required, self.direct)
                start = self.direct.index(x_decl)
                end = self.direct.index(write, start)
                block = self.direct[start:end]
                self.assertLess(block.index(x_expr), block.index("x" + str(corner) + " += halfWidth"))
                self.assertLess(block.index(y_expr), block.index("y" + str(corner) + " += halfHeight"))

        self.assertIn("this->vertexBufferCurPtr += 4;", self.direct)
        self.assertIn("++this->spritesToDraw;", self.direct)
        for reassociation in ("baseX", "baseY", "widthCos", "heightSin", "wx", "hy"):
            self.assertNotIn(reassociation, self.direct)


class PspBulletRotatedDirectDifferentialHarnessTests(unittest.TestCase):
    def test_legacy_and_direct_match_vertices_state_and_events(self) -> None:
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
            #include <vector>

            using u8 = std::uint8_t;
            using u32 = std::uint32_t;

            struct Color { u32 value; };

            static u8 Channel(Color color, unsigned int shift) {
                return static_cast<u8>((color.value >> shift) & 0xffu);
            }

            static u8 MultiplyChannel(u8 source, u8 factor) {
                const u32 value = static_cast<u32>(source) * factor >> 7;
                return static_cast<u8>(value >= 256u ? 255u : value);
            }

            static Color MultiplyColor(Color source, Color factor) {
                Color result{0u};
                for (unsigned int shift : {0u, 8u, 16u, 24u}) {
                    result.value |= static_cast<u32>(MultiplyChannel(
                        Channel(source, shift), Channel(factor, shift))) << shift;
                }
                return result;
            }

            struct Vertex {
                float u;
                float v;
                u32 color;
                float x;
                float y;
                float z;
            };
            static_assert(sizeof(Vertex) == 24, "PSP vertex model drift");

            enum EventKind {
                EVENT_FLUSH = 1,
                EVENT_BIND_TEXTURE,
                EVENT_SHADER,
                EVENT_BLEND,
                EVENT_DEPTH_MASK,
                EVENT_PAIR_MODE,
            };

            struct Event {
                int kind;
                int value;
                u32 count;
            };
            static_assert(sizeof(Event) == 12, "event model drift");

            struct Viewport { u32 x, y, width, height; };

            struct Manager {
                float offsetX = 0.0f;
                float offsetY = 0.0f;
                Viewport viewport{0u, 0u, 480u, 272u};
                float cachedLeft = 0.0f;
                float cachedTop = 0.0f;
                float cachedRight = 0.0f;
                float cachedBottom = 0.0f;
                bool colorMulEnabled = false;
                bool disableZBuffer = false;
                Color colorMul{0xffffffffu};
                int currentTexture = -1;
                int currentVertexShader = 0;
                u32 currentBlendMode = 0u;
                u32 currentZWriteDisable = 0u;
                bool pairMode = false;
                u32 spritesToDraw = 0u;
                u32 renderStateChanges = 0u;
                std::vector<Vertex> vertices;
                std::vector<Event> events;

                static int TextureForSource(int source) { return 101 + source * 17; }

                void BeginBatch() {
                    cachedLeft = static_cast<float>(viewport.x);
                    cachedTop = static_cast<float>(viewport.y);
                    cachedRight = static_cast<float>(viewport.x + viewport.width);
                    cachedBottom = static_cast<float>(viewport.y + viewport.height);
                }

                void Flush() {
                    if (spritesToDraw == 0u) return;
                    events.push_back({EVENT_FLUSH, pairMode ? 1 : 0, spritesToDraw});
                    spritesToDraw = 0u;
                }

                void SyncState(u32 blendMode, u32 zWriteDisable) {
                    if (currentBlendMode != blendMode) {
                        Flush();
                        currentBlendMode = blendMode;
                        events.push_back({EVENT_BLEND, static_cast<int>(blendMode), 0u});
                    }
                    if (!disableZBuffer && currentZWriteDisable != zWriteDisable) {
                        Flush();
                        currentZWriteDisable = zWriteDisable;
                        events.push_back(
                            {EVENT_DEPTH_MASK, zWriteDisable == 0u ? 1 : 0, 0u});
                    }
                    ++renderStateChanges;
                }
            };

            struct Vm {
                bool hasSprite = true;
                bool visible = true;
                bool active = true;
                bool useColor2 = false;
                float posX = 0.0f;
                float posY = 0.0f;
                float posZ = 0.05f;
                float width = 16.0f;
                float height = 16.0f;
                float scaleX = 1.0f;
                float scaleY = 1.0f;
                float rotationZ = 0.5f;
                float sine = 0.0f;
                float cosine = 1.0f;
                float u0 = 0.0f;
                float u1 = 1.0f;
                float v0 = 0.0f;
                float v1 = 1.0f;
                Color color{0xffffffffu};
                Color color2{0xffffffffu};
                int sourceFileIndex = 0;
                u32 anchor = 0u;
                u32 blendMode = 0u;
                u32 zWriteDisable = 0u;
            };

            static void CommonState(Manager &manager, const Vm &vm, Color &color,
                                    bool direct) {
                const int texture = Manager::TextureForSource(vm.sourceFileIndex);
                if (manager.currentTexture != texture) {
                    manager.currentTexture = texture;
                    manager.Flush();
                    manager.events.push_back({EVENT_BIND_TEXTURE, texture, 0u});
                }
                if (manager.currentVertexShader != 1) {
                    manager.Flush();
                    manager.currentVertexShader = 1;
                    manager.events.push_back({EVENT_SHADER, 1, 0u});
                }
                color = vm.useColor2 ? vm.color2 : vm.color;
                if (manager.colorMulEnabled) color = MultiplyColor(color, manager.colorMul);

                const bool stateMatches =
                    manager.currentBlendMode == vm.blendMode &&
                    (manager.disableZBuffer ||
                     manager.currentZWriteDisable == vm.zWriteDisable);
                if (direct && stateMatches) {
                    ++manager.renderStateChanges;
                } else {
                    manager.SyncState(vm.blendMode, vm.zWriteDisable);
                }
                if (manager.pairMode) {
                    manager.Flush();
                    manager.pairMode = false;
                    manager.events.push_back({EVENT_PAIR_MODE, 0, 0u});
                }
            }

            static bool ValidateAndLegacyCull(const Manager &manager, const Vm &vm,
                                              float halfWidth, float halfHeight) {
                if (!vm.hasSprite || !vm.visible || !vm.active ||
                    Channel(vm.color, 24u) == 0u) return false;
                const float centerX = vm.posX + manager.offsetX +
                                      ((vm.anchor & 1u) ? halfWidth : 0.0f);
                const float centerY = vm.posY + manager.offsetY +
                                      ((vm.anchor & 2u) ? halfHeight : 0.0f);
                const float bound = std::fabs(halfWidth) + std::fabs(halfHeight);
                return !(centerX + bound < static_cast<float>(manager.viewport.x) ||
                         centerY + bound < static_cast<float>(manager.viewport.y) ||
                         centerX - bound > static_cast<float>(manager.viewport.x +
                                                              manager.viewport.width) ||
                         centerY - bound > static_cast<float>(manager.viewport.y +
                                                              manager.viewport.height));
            }

            static bool ValidateAndDirectCull(const Manager &manager, const Vm &vm,
                                              float halfWidth, float halfHeight) {
                if (!vm.hasSprite || !vm.visible || !vm.active ||
                    Channel(vm.color, 24u) == 0u) return false;
                const float centerX = vm.posX + manager.offsetX +
                                      ((vm.anchor & 1u) ? halfWidth : 0.0f);
                const float centerY = vm.posY + manager.offsetY +
                                      ((vm.anchor & 2u) ? halfHeight : 0.0f);
                const float bound = std::fabs(halfWidth) + std::fabs(halfHeight);
                return !(centerX + bound < manager.cachedLeft ||
                         centerY + bound < manager.cachedTop ||
                         centerX - bound > manager.cachedRight ||
                         centerY - bound > manager.cachedBottom);
            }

            static void LegacyDraw(Manager &manager, const Vm &vm) {
                const float halfWidth = vm.width * vm.scaleX * 0.5f;
                const float halfHeight = vm.height * vm.scaleY * 0.5f;
                if (!ValidateAndLegacyCull(manager, vm, halfWidth, halfHeight)) return;

                float x[4];
                float y[4];
                const float localX[4] = {-halfWidth, halfWidth, -halfWidth, halfWidth};
                const float localY[4] = {-halfHeight, -halfHeight, halfHeight, halfHeight};
                for (int corner = 0; corner < 4; ++corner) {
                    x[corner] = localX[corner] * vm.cosine - localY[corner] * vm.sine +
                                vm.posX + manager.offsetX;
                    y[corner] = localX[corner] * vm.sine + localY[corner] * vm.cosine +
                                vm.posY + manager.offsetY;
                    if (vm.anchor & 1u) x[corner] += halfWidth;
                    if (vm.anchor & 2u) y[corner] += halfHeight;
                }

                Color color{};
                CommonState(manager, vm, color, false);
                manager.vertices.push_back({vm.u0, vm.v0, color.value,
                                            x[0], y[0], vm.posZ});
                manager.vertices.push_back({vm.u1, vm.v0, color.value,
                                            x[1], y[1], vm.posZ});
                manager.vertices.push_back({vm.u0, vm.v1, color.value,
                                            x[2], y[2], vm.posZ});
                manager.vertices.push_back({vm.u1, vm.v1, color.value,
                                            x[3], y[3], vm.posZ});
                ++manager.spritesToDraw;
            }

            static void DirectDraw(Manager &manager, const Vm &vm) {
                const float halfWidth = vm.width * vm.scaleX * 0.5f;
                const float halfHeight = vm.height * vm.scaleY * 0.5f;
                if (!ValidateAndDirectCull(manager, vm, halfWidth, halfHeight)) return;

                Color color{};
                CommonState(manager, vm, color, true);

                const float localX0 = -halfWidth;
                const float localY0 = -halfHeight;
                float x0 = localX0 * vm.cosine - localY0 * vm.sine +
                           vm.posX + manager.offsetX;
                float y0 = localX0 * vm.sine + localY0 * vm.cosine +
                           vm.posY + manager.offsetY;
                if (vm.anchor & 1u) x0 += halfWidth;
                if (vm.anchor & 2u) y0 += halfHeight;
                manager.vertices.push_back({vm.u0, vm.v0, color.value,
                                            x0, y0, vm.posZ});

                const float localX1 = halfWidth;
                const float localY1 = -halfHeight;
                float x1 = localX1 * vm.cosine - localY1 * vm.sine +
                           vm.posX + manager.offsetX;
                float y1 = localX1 * vm.sine + localY1 * vm.cosine +
                           vm.posY + manager.offsetY;
                if (vm.anchor & 1u) x1 += halfWidth;
                if (vm.anchor & 2u) y1 += halfHeight;
                manager.vertices.push_back({vm.u1, vm.v0, color.value,
                                            x1, y1, vm.posZ});

                const float localX2 = -halfWidth;
                const float localY2 = halfHeight;
                float x2 = localX2 * vm.cosine - localY2 * vm.sine +
                           vm.posX + manager.offsetX;
                float y2 = localX2 * vm.sine + localY2 * vm.cosine +
                           vm.posY + manager.offsetY;
                if (vm.anchor & 1u) x2 += halfWidth;
                if (vm.anchor & 2u) y2 += halfHeight;
                manager.vertices.push_back({vm.u0, vm.v1, color.value,
                                            x2, y2, vm.posZ});

                const float localX3 = halfWidth;
                const float localY3 = halfHeight;
                float x3 = localX3 * vm.cosine - localY3 * vm.sine +
                           vm.posX + manager.offsetX;
                float y3 = localX3 * vm.sine + localY3 * vm.cosine +
                           vm.posY + manager.offsetY;
                if (vm.anchor & 1u) x3 += halfWidth;
                if (vm.anchor & 2u) y3 += halfHeight;
                manager.vertices.push_back({vm.u1, vm.v1, color.value,
                                            x3, y3, vm.posZ});
                ++manager.spritesToDraw;
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

            [[noreturn]] static void Fail(const char *what, std::size_t run,
                                          std::size_t index) {
                std::fprintf(stderr, "%s at run=%zu index=%zu\n", what, run, index);
                std::exit(1);
            }

            static void Compare(const Manager &legacy, const Manager &direct,
                                std::size_t run, std::size_t index) {
                if (legacy.vertices.size() != direct.vertices.size())
                    Fail("vertex count", run, index);
                if (!legacy.vertices.empty() &&
                    std::memcmp(legacy.vertices.data(), direct.vertices.data(),
                                legacy.vertices.size() * sizeof(Vertex)) != 0)
                    Fail("vertex bytes", run, index);
                if (legacy.events.size() != direct.events.size())
                    Fail("event count", run, index);
                if (!legacy.events.empty() &&
                    std::memcmp(legacy.events.data(), direct.events.data(),
                                legacy.events.size() * sizeof(Event)) != 0)
                    Fail("event bytes", run, index);
                if (legacy.currentTexture != direct.currentTexture ||
                    legacy.currentVertexShader != direct.currentVertexShader ||
                    legacy.currentBlendMode != direct.currentBlendMode ||
                    legacy.currentZWriteDisable != direct.currentZWriteDisable ||
                    legacy.pairMode != direct.pairMode ||
                    legacy.spritesToDraw != direct.spritesToDraw ||
                    legacy.renderStateChanges != direct.renderStateChanges)
                    Fail("renderer state", run, index);
            }

            static Manager MakeManager(u32 &rng, std::size_t run) {
                Manager manager;
                manager.offsetX = RandomFloat(rng, -64.0f, 64.0f);
                manager.offsetY = RandomFloat(rng, -64.0f, 64.0f);
                manager.viewport = {
                    Next(rng) % 33u, Next(rng) % 25u,
                    320u + Next(rng) % 321u, 240u + Next(rng) % 241u};
                manager.colorMulEnabled = (run & 1u) != 0u;
                manager.disableZBuffer = (run & 2u) != 0u;
                manager.colorMul = {Next(rng) | 0x01010101u};
                manager.currentTexture = (run % 3u == 0u) ? -1 :
                                         Manager::TextureForSource(static_cast<int>(run % 5u));
                manager.currentVertexShader = (run & 4u) ? 1 : 0;
                manager.currentBlendMode = static_cast<u32>((run >> 1u) & 1u);
                manager.currentZWriteDisable = static_cast<u32>((run >> 2u) & 1u);
                manager.pairMode = (run & 8u) != 0u;
                manager.spritesToDraw = static_cast<u32>(run % 4u);
                return manager;
            }

            static Vm MakeVm(u32 &rng, std::size_t run, std::size_t index,
                             const Manager &manager) {
                Vm vm;
                vm.width = RandomFloat(rng, 1.0f, 96.0f);
                vm.height = RandomFloat(rng, 1.0f, 96.0f);
                static constexpr float scales[] = {
                    -3.0f, -1.0f, -0.0f, 0.0f, 0.125f, 1.0f, 2.75f};
                vm.scaleX = scales[(run + index) % (sizeof(scales) / sizeof(scales[0]))];
                vm.scaleY = scales[(run * 3u + index * 5u) %
                                   (sizeof(scales) / sizeof(scales[0]))];
                vm.anchor = static_cast<u32>(index & 3u);
                vm.rotationZ = RandomFloat(rng, -6.0f, 6.0f);
                if (vm.rotationZ == 0.0f) vm.rotationZ = 0.25f;
                vm.sine = std::sin(vm.rotationZ);
                vm.cosine = std::cos(vm.rotationZ);
                vm.posX = RandomFloat(rng, -220.0f,
                                      static_cast<float>(manager.viewport.x +
                                                         manager.viewport.width) + 220.0f);
                vm.posY = RandomFloat(rng, -220.0f,
                                      static_cast<float>(manager.viewport.y +
                                                         manager.viewport.height) + 220.0f);
                vm.posZ = RandomFloat(rng, 0.001f, 0.99f);
                vm.u0 = RandomFloat(rng, -0.25f, 0.75f);
                vm.u1 = RandomFloat(rng, 0.25f, 1.25f);
                vm.v0 = RandomFloat(rng, -0.25f, 0.75f);
                vm.v1 = RandomFloat(rng, 0.25f, 1.25f);
                vm.color = {Next(rng) | 0x01000000u};
                vm.color2 = {Next(rng)};
                vm.useColor2 = (Next(rng) & 1u) != 0u;
                vm.sourceFileIndex = static_cast<int>((run + index * 3u) % 7u);
                vm.blendMode = static_cast<u32>((index / 3u + run) & 1u);
                vm.zWriteDisable = static_cast<u32>((index / 5u + run) & 1u);

                // Exercise exact strict-inequality boundaries and both sides.
                const float halfWidth = vm.width * vm.scaleX * 0.5f;
                const float halfHeight = vm.height * vm.scaleY * 0.5f;
                const float bound = std::fabs(halfWidth) + std::fabs(halfHeight);
                switch (index % 16u) {
                case 0:
                    vm.posX = static_cast<float>(manager.viewport.x) - bound -
                              manager.offsetX;
                    break;
                case 1:
                    vm.posX = static_cast<float>(manager.viewport.x) - bound -
                              manager.offsetX - 0.25f;
                    break;
                case 2:
                    vm.posX = static_cast<float>(manager.viewport.x +
                                                 manager.viewport.width) + bound -
                              manager.offsetX;
                    break;
                case 3:
                    vm.posX = static_cast<float>(manager.viewport.x +
                                                 manager.viewport.width) + bound -
                              manager.offsetX + 0.25f;
                    break;
                case 4:
                    vm.posY = static_cast<float>(manager.viewport.y) - bound -
                              manager.offsetY;
                    break;
                case 5:
                    vm.posY = static_cast<float>(manager.viewport.y) - bound -
                              manager.offsetY - 0.25f;
                    break;
                case 6:
                    vm.posY = static_cast<float>(manager.viewport.y +
                                                 manager.viewport.height) + bound -
                              manager.offsetY;
                    break;
                case 7:
                    vm.posY = static_cast<float>(manager.viewport.y +
                                                 manager.viewport.height) + bound -
                              manager.offsetY + 0.25f;
                    break;
                default:
                    break;
                }

                if (index % 41u == 0u) vm.visible = false;
                if (index % 43u == 0u) vm.active = false;
                if (index % 47u == 0u) vm.hasSprite = false;
                if (index % 53u == 0u) vm.color.value &= 0x00ffffffu;
                return vm;
            }

            int main() {
                u32 rng = 0x6d2b79f5u;
                constexpr std::size_t runs = 96u;
                constexpr std::size_t casesPerRun = 128u;
                for (std::size_t run = 0; run < runs; ++run) {
                    Manager legacy = MakeManager(rng, run);
                    Manager direct = legacy;
                    direct.BeginBatch();

                    const float expectedLeft = static_cast<float>(direct.viewport.x);
                    const float expectedTop = static_cast<float>(direct.viewport.y);
                    const float expectedRight = static_cast<float>(direct.viewport.x +
                                                                   direct.viewport.width);
                    const float expectedBottom = static_cast<float>(direct.viewport.y +
                                                                    direct.viewport.height);
                    if (std::memcmp(&direct.cachedLeft, &expectedLeft, sizeof(float)) != 0 ||
                        std::memcmp(&direct.cachedTop, &expectedTop, sizeof(float)) != 0 ||
                        std::memcmp(&direct.cachedRight, &expectedRight, sizeof(float)) != 0 ||
                        std::memcmp(&direct.cachedBottom, &expectedBottom, sizeof(float)) != 0)
                        Fail("viewport cache", run, 0u);

                    for (std::size_t index = 0; index < casesPerRun; ++index) {
                        const Vm vm = MakeVm(rng, run, index, legacy);
                        LegacyDraw(legacy, vm);
                        DirectDraw(direct, vm);
                        Compare(legacy, direct, run, index);
                    }
                }
                return 0;
            }
        """

        with tempfile.TemporaryDirectory() as temporary:
            temporary_path = Path(temporary)
            cpp = temporary_path / "rotated_direct_differential.cpp"
            executable = temporary_path / "rotated_direct_differential"
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
