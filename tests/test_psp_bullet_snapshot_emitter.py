from __future__ import annotations

import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FEATURE_MACRO = "TH07_PSP_BULLET_SNAPSHOT_EMITTER"
MAKE_FEATURE = "PSP_BULLET_SNAPSHOT_EMITTER"


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


def matching_function_bodies(source: str, name_pattern: str) -> list[str]:
    """Return ordinary C/C++ function definitions whose declaration names match."""
    bodies: list[str] = []
    pattern = re.compile(
        rf"^[^\n;#]*{name_pattern}[^\n;]*\([^;{{}}]*\)\s*(?:const\s*)?\{{",
        re.MULTILINE,
    )
    for match in pattern.finditer(source):
        opening = source.index("{", match.start())
        depth = 0
        for index in range(opening, len(source)):
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
                if depth == 0:
                    bodies.append(source[opening : index + 1])
                    break
        else:
            raise AssertionError(f"unterminated matching function at {match.start()}")
    return bodies


class PspBulletSnapshotEmitterSourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.bullets = (ROOT / "src" / "BulletManager.cpp").read_text(encoding="utf-8")
        cls.bullets_h = (ROOT / "src" / "BulletManager.hpp").read_text(encoding="utf-8")
        cls.anm = (ROOT / "src" / "AnmManager.cpp").read_text(encoding="utf-8")
        cls.anm_h = (ROOT / "src" / "AnmManager.hpp").read_text(encoding="utf-8")
        cls.records_h = (ROOT / "src" / "PspBulletRender.hpp").read_text(
            encoding="utf-8"
        )
        cls.draw = function_body(cls.bullets, "u32 BulletManager::OnDraw")
        cls.feature_functions = matching_function_bodies(
            cls.bullets + "\n" + cls.anm,
            r"(?:PreparePspBulletRenderRecord|DrawPspBulletRecords)",
        )

    def test_make_feature_is_explicit_reversible_and_psp2000plus_only(self) -> None:
        self.assertIn(f"{MAKE_FEATURE} ?= 0", self.makefile)
        self.assertIn(f"-D{FEATURE_MACRO}", self.makefile)
        self.assertRegex(
            self.makefile,
            rf"{MAKE_FEATURE}[^\n]*(?:PSP-2000\+|PSP-1000)|"
            rf"(?:PSP-2000\+|PSP-1000)[^\n]*{MAKE_FEATURE}",
        )
        stamp = next(
            line for line in self.makefile.splitlines() if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn(f"$({MAKE_FEATURE})", stamp)
        # Every named release/profile recipe must opt out until this validation
        # increment has independently passed on the PSP-3000.
        self.assertGreaterEqual(self.makefile.count(f"{MAKE_FEATURE}=0"), 5)

    def test_psp1000_axis_fast_and_m3_combinations_are_rejected(self) -> None:
        feature_block = self.makefile[
            self.makefile.index(f"ifeq ($({MAKE_FEATURE}),1)") : self.makefile.index(
                "ifeq ($(PSP_ASCII_POPUP_BATCH),1)"
            )
        ]
        self.assertIn("ifneq ($(PSP_1000),0)", feature_block)
        self.assertIn("ifneq ($(PSP_BULLET_AXIS_FAST),0)", feature_block)
        self.assertRegex(self.makefile, r"(?i)(?:M3.*snapshot|snapshot.*M3)")
        self.assertGreaterEqual(
            self.makefile.count(f"ifneq ($({MAKE_FEATURE}),0)"),
            2,
            "both M3 attribution and M3 empty-timer profiles must reject I2b",
        )

    def test_production_state_is_compiled_out_of_psp1000(self) -> None:
        combined = (
            self.bullets
            + "\n"
            + self.bullets_h
            + "\n"
            + self.anm
            + "\n"
            + self.anm_h
            + "\n"
            + self.records_h
        )
        guards = re.findall(r"^#if[^\n]*BULLET_SNAPSHOT_EMITTER[^\n]*$", combined, re.MULTILINE)
        self.assertTrue(guards, "snapshot code has no compile-time feature guard")
        self.assertIn(f"ifeq ($({MAKE_FEATURE}),1)", self.makefile)
        # The Make rejection above plus explicit flag-off standard recipes means
        # the feature macro (and therefore every guarded declaration/storage
        # object) is absent from the PSP-1000 translation units.
        psp1000_recipe = self.makefile[
            self.makefile.index("psp1000-build:") : self.makefile.index(
                "psp2000plus-build:"
            )
        ]
        self.assertIn(f"{MAKE_FEATURE}=0", psp1000_recipe)

    def test_record_contract_is_one_cache_line_and_pod_data_only(self) -> None:
        self.assertIn(f"#if defined({FEATURE_MACRO})", self.records_h)
        self.assertIn("struct alignas(16) PspBulletRenderRecord", self.records_h)
        self.assertIn("static_assert(sizeof(PspBulletRenderRecord) == 64", self.records_h)
        for forbidden in ("AnmVm *", "Bullet *", "void *", "std::vector", "std::string"):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, self.records_h)

    def test_draw_owner_order_remains_laser_then_item_then_bullets(self) -> None:
        laser = self.draw.index("laser = arg->lasers;")
        laser_loop = self.draw.index("for (i = 0; i < 64; i++, laser++)", laser)
        items = self.draw.index("g_ItemManager.OnDraw();", laser_loop)
        feature = self.draw.index(FEATURE_MACRO, items)
        self.assertLess(laser, laser_loop)
        self.assertLess(laser_loop, items)
        self.assertLess(items, feature)

    def test_candidate_and_legacy_both_use_bucket_linked_list_order(self) -> None:
        bucket_loop = re.search(r"for\s*\([^;]*=\s*0;[^;]*<\s*6;", self.draw)
        self.assertIsNotNone(bucket_loop)
        assert bucket_loop is not None
        traversal = self.draw[bucket_loop.start() :]
        self.assertRegex(traversal, r"bulletsPtrs\s*\[")
        self.assertRegex(traversal, r"bullet\s*=\s*bullet->next")

        feature = traversal.index(FEATURE_MACRO)
        feature_else = traversal.index("#else", feature)
        feature_end = traversal.index("#endif", feature_else)
        candidate = traversal[feature:feature_else]
        legacy = traversal[feature_else:feature_end]
        self.assertIn("PreparePspBulletRenderRecord", candidate)
        self.assertIn("DrawPspBulletRecords", candidate)
        self.assertIn("bullet->Draw();", legacy)
        self.assertNotIn("bullet->Draw();", candidate)
        for forbidden in ("sort(", "stable_sort(", "pspActiveBulletBits", "BulletAt("):
            self.assertNotIn(forbidden, candidate)

    def test_snapshot_uses_a_fixed_64_record_tile(self) -> None:
        combined = self.bullets + "\n" + self.bullets_h + "\n" + self.records_h
        self.assertRegex(combined, r"kPspBulletRenderTileSize\s*=\s*64u\s*;")
        self.assertRegex(
            combined,
            r"PspBulletRenderRecord\s+\w+\s*\[\s*kPspBulletRenderTileSize\s*\]",
        )

    def test_snapshot_hot_functions_have_no_heap_io_or_forced_sync(self) -> None:
        self.assertTrue(self.feature_functions, "no snapshot production functions found")
        feature_code = "\n".join(self.feature_functions)
        for forbidden in (
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
            "sceGuSync",
            "SubmitAndRestart",
            "ReadPixels",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, feature_code)

    def test_record_creation_preserves_bullet_draw_vm_and_rotation_side_effects(self) -> None:
        feature_code = "\n".join(self.feature_functions)
        for required in (
            "BULLET_SPAWNING_FAST",
            "BULLET_SPAWNING_NORMAL",
            "BULLET_SPAWNING_SLOW",
            "BULLET_DESPAWN",
            "spriteBullet",
            "spriteSpawnEffectDonut",
            "vm->pos.x",
            "vm->pos.y",
            "vm->pos.z",
            "vm->color.color",
            "vm->autoRotate",
            "pspRenderRotationValid",
            "pspRenderSourceAngle",
            "pspRenderSin",
            "pspRenderCos",
            "vm->SetRotationZ",
            "vm->updateRotation = 1",
        ):
            with self.subTest(required=required):
                self.assertIn(required, feature_code)

    def test_tile_dispatch_does_not_force_a_renderer_flush(self) -> None:
        first_record = self.draw.index("PreparePspBulletRenderRecord")
        final_dispatch = self.draw.rindex("DrawPspBulletRecords")
        dispatch = self.draw[first_record:final_dispatch]
        self.assertIn("renderRecordCount == kPspBulletRenderTileSize", dispatch)
        self.assertNotRegex(dispatch, r"(?:->|\.)Flush\s*\(")
        self.assertNotIn("ResetVertexBuffer", dispatch)

    def test_batch_emitter_consumes_records_in_order_without_boundary_flush(self) -> None:
        emitter = function_body(
            self.anm,
            "void AnmManager::DrawPspBulletRecords",
        )
        self.assertRegex(
            emitter,
            r"for\s*\(u32\s+recordIndex\s*=\s*0;\s*recordIndex\s*<\s*count;"
            r"\s*\+\+recordIndex\)",
        )
        self.assertIn("records[recordIndex]", emitter)
        for forbidden in ("sort(", "stable_sort(", "ResetVertexBuffer"):
            self.assertNotIn(forbidden, emitter)

        loop = emitter.index("for (u32 recordIndex")
        first_vertex = emitter.index("WritePspSpriteVertex", loop)
        last_append = emitter.rindex("++this->spritesToDraw;")
        # State/texture changes may flush within the loop. Merely entering or
        # leaving a 64-record tile must not become a new renderer boundary.
        self.assertNotIn("this->Flush()", emitter[:loop])
        self.assertNotIn("this->Flush()", emitter[last_append:])
        self.assertLess(loop, first_vertex)


class PspBulletSnapshotEmitterSemanticHarnessTests(unittest.TestCase):
    def test_bucket_order_survives_64_record_tile_boundaries(self) -> None:
        compiler = shutil.which("g++") or shutil.which("c++")
        if not compiler:
            self.skipTest("host C++ compiler is required for the semantic harness")

        source = r"""
            #include <array>
            #include <cassert>
            #include <cstddef>
            #include <vector>

            struct Node { int id; Node *next; };
            struct Record { int id; int bucket; };

            struct Sink {
                std::vector<Record> records;
                unsigned int tileCalls = 0;
                unsigned int forcedFlushes = 0;

                void Emit(const Record *tile, unsigned int count) {
                    ++tileCalls;
                    records.insert(records.end(), tile, tile + count);
                    // A tile is scratch ownership, not a render-state boundary.
                }
            };

            static std::vector<Record> Legacy(Node *const heads[6]) {
                std::vector<Record> result;
                for (int bucket = 0; bucket < 6; ++bucket)
                    for (Node *node = heads[bucket]; node; node = node->next)
                        result.push_back({node->id, bucket});
                return result;
            }

            static Sink Snapshot(Node *const heads[6]) {
                constexpr unsigned int kTile = 64;
                std::array<Record, kTile> tile{};
                unsigned int used = 0;
                Sink sink;
                for (int bucket = 0; bucket < 6; ++bucket) {
                    for (Node *node = heads[bucket]; node; node = node->next) {
                        tile[used++] = {node->id, bucket};
                        if (used == kTile) {
                            sink.Emit(tile.data(), used);
                            used = 0;
                        }
                    }
                }
                if (used) sink.Emit(tile.data(), used);
                return sink;
            }

            int main() {
                std::array<Node, 129> nodes{};
                Node *heads[6]{};
                Node *tails[6]{};
                // Deliberately cross bucket boundaries inside both full tiles.
                for (int i = 0; i < 129; ++i) {
                    const int bucket = (i < 17) ? 0 : (i < 63) ? 2 : (i < 120) ? 4 : 5;
                    nodes[i] = {1000 - i * 7, nullptr};
                    if (!heads[bucket]) heads[bucket] = &nodes[i];
                    else tails[bucket]->next = &nodes[i];
                    tails[bucket] = &nodes[i];
                }
                const auto legacy = Legacy(heads);
                const auto snapshot = Snapshot(heads);
                assert(snapshot.records.size() == legacy.size());
                for (std::size_t i = 0; i < legacy.size(); ++i) {
                    assert(snapshot.records[i].id == legacy[i].id);
                    assert(snapshot.records[i].bucket == legacy[i].bucket);
                }
                assert(snapshot.tileCalls == 3); // 64 + 64 + 1
                assert(snapshot.forcedFlushes == 0);

                Node *empty[6]{};
                const auto emptySnapshot = Snapshot(empty);
                assert(emptySnapshot.records.empty());
                assert(emptySnapshot.tileCalls == 0);
                return 0;
            }
        """
        with tempfile.TemporaryDirectory() as temporary:
            temporary_path = Path(temporary)
            cpp = temporary_path / "i2b_semantic.cpp"
            executable = temporary_path / "i2b_semantic"
            cpp.write_text(textwrap.dedent(source), encoding="utf-8")
            subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-O2",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
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

    def test_record_emitter_matches_legacy_vertices_and_state_events(self) -> None:
        compiler = shutil.which("g++") or shutil.which("c++")
        if not compiler:
            self.skipTest("host C++ compiler is required for the differential harness")

        source = r"""
            #include <algorithm>
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
            using u32 = std::uint32_t;

            struct Color { u32 value; };

            static u8 Channel(Color color, unsigned int shift) {
                return static_cast<u8>((color.value >> shift) & 0xffu);
            }

            static u8 MultiplyChannel(u8 source, u8 factor) {
                u32 value = static_cast<u32>(source) * factor >> 7;
                return static_cast<u8>(value >= 256u ? 255u : value);
            }

            static Color MultiplyColor(Color source, Color factor) {
                Color result{0u};
                for (unsigned int shift : {0u, 8u, 16u, 24u}) {
                    result.value |= static_cast<u32>(
                        MultiplyChannel(Channel(source, shift), Channel(factor, shift))) << shift;
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
            static_assert(sizeof(Event) == 12, "state event model drift");

            struct Manager {
                float offsetX = 0.0f;
                float offsetY = 0.0f;
                float viewportX = 0.0f;
                float viewportY = 0.0f;
                float viewportWidth = 480.0f;
                float viewportHeight = 272.0f;
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

                void Flush() {
                    if (spritesToDraw == 0u) return;
                    events.push_back({EVENT_FLUSH, pairMode ? 1 : 0, spritesToDraw});
                    spritesToDraw = 0u;
                }
            };

            struct Vm {
                bool hasSprite = true;
                bool visible = true;
                bool active = true;
                bool useColor2 = false;
                bool cachedSinCos = false;
                float posX = 0.0f;
                float posY = 0.0f;
                float posZ = 0.05f;
                float width = 16.0f;
                float height = 16.0f;
                float scaleX = 1.0f;
                float scaleY = 1.0f;
                float rotationZ = 0.0f;
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

            enum RecordFlags : u32 {
                RECORD_DRAWABLE = 1u << 0,
                RECORD_CACHED_SINCOS = 1u << 1,
                RECORD_BLEND_ADD = 1u << 2,
                RECORD_ZWRITE_DISABLE = 1u << 3,
                RECORD_ANCHOR_SHIFT = 4,
                RECORD_ANCHOR_MASK = 3u << RECORD_ANCHOR_SHIFT,
            };

            struct alignas(16) Record {
                float posX, posY, posZ;
                float halfWidth, halfHeight;
                float rotationZ, sine, cosine;
                float u0, u1, v0, v1;
                Color color;
                int sourceFileIndex;
                u32 flags;
            };
            static_assert(sizeof(Record) == 64, "record model drift");

            static float RenderFloor(float value) {
                if (!std::isfinite(value) || value < -2147483520.0f ||
                    value > 2147483520.0f) {
                    return std::floor(value);
                }
                return static_cast<float>(std::floor(value));
            }

            static void RenderSinCos(float angle, float &sine, float &cosine) {
                sine = std::sin(angle);
                cosine = std::cos(angle);
            }

            static void LegacyDraw(Manager &manager, const Vm &vm) {
                if (!vm.hasSprite || !vm.visible || !vm.active || Channel(vm.color, 24u) == 0u)
                    return;

                const float halfWidth = vm.width * vm.scaleX * 0.5f;
                const float halfHeight = vm.height * vm.scaleY * 0.5f;
                const float centerX = vm.posX + manager.offsetX +
                                      ((vm.anchor & 1u) ? halfWidth : 0.0f);
                const float centerY = vm.posY + manager.offsetY +
                                      ((vm.anchor & 2u) ? halfHeight : 0.0f);
                const float bound = std::fabs(halfWidth) + std::fabs(halfHeight);
                if (centerX + bound < manager.viewportX ||
                    centerY + bound < manager.viewportY ||
                    centerX - bound > manager.viewportX + manager.viewportWidth ||
                    centerY - bound > manager.viewportY + manager.viewportHeight)
                    return;

                float x[4];
                float y[4];
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
                    const float left = RenderFloor(rawLeft + manager.offsetX + 0.5f);
                    const float right = RenderFloor(rawRight + manager.offsetX + 0.5f);
                    const float top = RenderFloor(rawTop + manager.offsetY + 0.5f);
                    const float bottom = RenderFloor(rawBottom + manager.offsetY + 0.5f);
                    x[0] = x[2] = left;
                    x[1] = x[3] = right;
                    y[0] = y[1] = top;
                    y[2] = y[3] = bottom;
                } else {
                    float sine;
                    float cosine;
                    if (vm.cachedSinCos) {
                        sine = vm.sine;
                        cosine = vm.cosine;
                    } else {
                        RenderSinCos(vm.rotationZ, sine, cosine);
                    }
                    const float localX[4] = {-halfWidth, halfWidth, -halfWidth, halfWidth};
                    const float localY[4] = {-halfHeight, -halfHeight, halfHeight, halfHeight};
                    for (int corner = 0; corner < 4; ++corner) {
                        x[corner] = localX[corner] * cosine - localY[corner] * sine +
                                    vm.posX + manager.offsetX;
                        y[corner] = localX[corner] * sine + localY[corner] * cosine +
                                    vm.posY + manager.offsetY;
                        if (vm.anchor & 1u) x[corner] += halfWidth;
                        if (vm.anchor & 2u) y[corner] += halfHeight;
                    }
                }

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
                Color color = vm.useColor2 ? vm.color2 : vm.color;
                if (manager.colorMulEnabled) color = MultiplyColor(color, manager.colorMul);

                if (manager.currentBlendMode != vm.blendMode) {
                    manager.Flush();
                    manager.currentBlendMode = vm.blendMode;
                    manager.events.push_back(
                        {EVENT_BLEND, static_cast<int>(vm.blendMode), 0u});
                }
                if (!manager.disableZBuffer &&
                    manager.currentZWriteDisable != vm.zWriteDisable) {
                    manager.Flush();
                    manager.currentZWriteDisable = vm.zWriteDisable;
                    manager.events.push_back(
                        {EVENT_DEPTH_MASK, vm.zWriteDisable == 0u ? 1 : 0, 0u});
                }
                ++manager.renderStateChanges;

                const bool usePairs = vm.rotationZ == 0.0f && x[0] <= x[3] && y[0] <= y[3];
                if (manager.pairMode != usePairs) {
                    manager.Flush();
                    manager.pairMode = usePairs;
                    manager.events.push_back({EVENT_PAIR_MODE, usePairs ? 1 : 0, 0u});
                }
                manager.vertices.push_back({vm.u0, vm.v0, color.value,
                                            x[0], y[0], vm.posZ});
                if (usePairs) {
                    manager.vertices.push_back({vm.u1, vm.v1, color.value,
                                                x[3], y[3], vm.posZ});
                } else {
                    manager.vertices.push_back({vm.u1, vm.v0, color.value,
                                                x[1], y[1], vm.posZ});
                    manager.vertices.push_back({vm.u0, vm.v1, color.value,
                                                x[2], y[2], vm.posZ});
                    manager.vertices.push_back({vm.u1, vm.v1, color.value,
                                                x[3], y[3], vm.posZ});
                }
                ++manager.spritesToDraw;
            }

            static Record BuildRecord(const Vm &vm) {
                Record record{};
                u32 flags = vm.cachedSinCos ? RECORD_CACHED_SINCOS : 0u;
                record.flags = 0u;
                if (!vm.hasSprite || !vm.visible || !vm.active ||
                    Channel(vm.color, 24u) == 0u)
                    return record;

                record.posX = vm.posX;
                record.posY = vm.posY;
                record.posZ = vm.posZ;
                record.halfWidth = vm.width * vm.scaleX * 0.5f;
                record.halfHeight = vm.height * vm.scaleY * 0.5f;
                record.rotationZ = vm.rotationZ;
                record.sine = vm.cachedSinCos ? vm.sine : 0.0f;
                record.cosine = vm.cachedSinCos ? vm.cosine : 0.0f;
                record.u0 = vm.u0;
                record.u1 = vm.u1;
                record.v0 = vm.v0;
                record.v1 = vm.v1;
                record.color = vm.useColor2 ? vm.color2 : vm.color;
                record.sourceFileIndex = vm.sourceFileIndex;
                flags |= RECORD_DRAWABLE;
                flags |= (vm.anchor << RECORD_ANCHOR_SHIFT) & RECORD_ANCHOR_MASK;
                if (vm.blendMode) flags |= RECORD_BLEND_ADD;
                if (vm.zWriteDisable) flags |= RECORD_ZWRITE_DISABLE;
                record.flags = flags;
                return record;
            }

            static void RecordDraw(Manager &manager, const Record &record) {
                if (!(record.flags & RECORD_DRAWABLE)) return;

                const u32 anchor =
                    (record.flags & RECORD_ANCHOR_MASK) >> RECORD_ANCHOR_SHIFT;
                const float centerX = record.posX + manager.offsetX +
                                      ((anchor & 1u) ? record.halfWidth : 0.0f);
                const float centerY = record.posY + manager.offsetY +
                                      ((anchor & 2u) ? record.halfHeight : 0.0f);
                const float bound = std::fabs(record.halfWidth) +
                                    std::fabs(record.halfHeight);
                if (centerX + bound < manager.viewportX ||
                    centerY + bound < manager.viewportY ||
                    centerX - bound > manager.viewportX + manager.viewportWidth ||
                    centerY - bound > manager.viewportY + manager.viewportHeight)
                    return;

                float x[4];
                float y[4];
                if (record.rotationZ == 0.0f) {
                    const float rawLeft = (anchor & 1u) ? record.posX
                                                        : record.posX - record.halfWidth;
                    const float rawRight = (anchor & 1u)
                                               ? record.posX + record.halfWidth * 2.0f
                                               : record.posX + record.halfWidth;
                    const float rawTop = (anchor & 2u) ? record.posY
                                                       : record.posY - record.halfHeight;
                    const float rawBottom = (anchor & 2u)
                                                ? record.posY + record.halfHeight * 2.0f
                                                : record.posY + record.halfHeight;
                    const float left = RenderFloor(rawLeft + manager.offsetX + 0.5f);
                    const float right = RenderFloor(rawRight + manager.offsetX + 0.5f);
                    const float top = RenderFloor(rawTop + manager.offsetY + 0.5f);
                    const float bottom = RenderFloor(rawBottom + manager.offsetY + 0.5f);
                    x[0] = x[2] = left;
                    x[1] = x[3] = right;
                    y[0] = y[1] = top;
                    y[2] = y[3] = bottom;
                } else {
                    float sine;
                    float cosine;
                    if (record.flags & RECORD_CACHED_SINCOS) {
                        sine = record.sine;
                        cosine = record.cosine;
                    } else {
                        RenderSinCos(record.rotationZ, sine, cosine);
                    }
                    const float localX[4] = {-record.halfWidth, record.halfWidth,
                                             -record.halfWidth, record.halfWidth};
                    const float localY[4] = {-record.halfHeight, -record.halfHeight,
                                             record.halfHeight, record.halfHeight};
                    for (int corner = 0; corner < 4; ++corner) {
                        x[corner] = localX[corner] * cosine - localY[corner] * sine +
                                    record.posX + manager.offsetX;
                        y[corner] = localX[corner] * sine + localY[corner] * cosine +
                                    record.posY + manager.offsetY;
                        if (anchor & 1u) x[corner] += record.halfWidth;
                        if (anchor & 2u) y[corner] += record.halfHeight;
                    }
                }

                const int texture = Manager::TextureForSource(record.sourceFileIndex);
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
                Color color = record.color;
                if (manager.colorMulEnabled) color = MultiplyColor(color, manager.colorMul);

                const u32 blendMode =
                    (record.flags & RECORD_BLEND_ADD) ? 1u : 0u;
                if (manager.currentBlendMode != blendMode) {
                    manager.Flush();
                    manager.currentBlendMode = blendMode;
                    manager.events.push_back(
                        {EVENT_BLEND, static_cast<int>(blendMode), 0u});
                }
                const u32 zWriteDisable =
                    (record.flags & RECORD_ZWRITE_DISABLE) ? 1u : 0u;
                if (!manager.disableZBuffer &&
                    manager.currentZWriteDisable != zWriteDisable) {
                    manager.Flush();
                    manager.currentZWriteDisable = zWriteDisable;
                    manager.events.push_back(
                        {EVENT_DEPTH_MASK, zWriteDisable == 0u ? 1 : 0, 0u});
                }
                ++manager.renderStateChanges;

                const bool usePairs = record.rotationZ == 0.0f &&
                                      x[0] <= x[3] && y[0] <= y[3];
                if (manager.pairMode != usePairs) {
                    manager.Flush();
                    manager.pairMode = usePairs;
                    manager.events.push_back({EVENT_PAIR_MODE, usePairs ? 1 : 0, 0u});
                }
                manager.vertices.push_back({record.u0, record.v0, color.value,
                                            x[0], y[0], record.posZ});
                if (usePairs) {
                    manager.vertices.push_back({record.u1, record.v1, color.value,
                                                x[3], y[3], record.posZ});
                } else {
                    manager.vertices.push_back({record.u1, record.v0, color.value,
                                                x[1], y[1], record.posZ});
                    manager.vertices.push_back({record.u0, record.v1, color.value,
                                                x[2], y[2], record.posZ});
                    manager.vertices.push_back({record.u1, record.v1, color.value,
                                                x[3], y[3], record.posZ});
                }
                ++manager.spritesToDraw;
            }

            [[noreturn]] static void Fail(const char *what, int caseNumber) {
                std::fprintf(stderr, "differential mismatch: %s case=%d\n", what, caseNumber);
                std::exit(1);
            }

            static void CompareManagers(const Manager &legacy, const Manager &candidate,
                                        int caseNumber) {
                if (legacy.vertices.size() != candidate.vertices.size())
                    Fail("vertex count", caseNumber);
                if (!legacy.vertices.empty() &&
                    std::memcmp(legacy.vertices.data(), candidate.vertices.data(),
                                legacy.vertices.size() * sizeof(Vertex)) != 0)
                    Fail("vertex bytes", caseNumber);
                if (legacy.events.size() != candidate.events.size())
                    Fail("state/flush event count", caseNumber);
                if (!legacy.events.empty() &&
                    std::memcmp(legacy.events.data(), candidate.events.data(),
                                legacy.events.size() * sizeof(Event)) != 0)
                    Fail("state/flush event bytes", caseNumber);
                if (legacy.currentTexture != candidate.currentTexture ||
                    legacy.currentVertexShader != candidate.currentVertexShader ||
                    legacy.currentBlendMode != candidate.currentBlendMode ||
                    legacy.currentZWriteDisable != candidate.currentZWriteDisable ||
                    legacy.pairMode != candidate.pairMode ||
                    legacy.spritesToDraw != candidate.spritesToDraw)
                    Fail("final render state", caseNumber);
                if (legacy.renderStateChanges != candidate.renderStateChanges)
                    Fail("renderStateChanges", caseNumber);
            }

            static void RunCase(const Manager &initial, const std::vector<Vm> &vms,
                                int caseNumber) {
                Manager legacy = initial;
                Manager candidate = initial;
                for (const Vm &vm : vms) LegacyDraw(legacy, vm);

                std::vector<Record> tile;
                tile.reserve(64u);
                for (const Vm &vm : vms) {
                    tile.push_back(BuildRecord(vm));
                    if (tile.size() == 64u) {
                        for (const Record &record : tile) RecordDraw(candidate, record);
                        tile.clear();
                    }
                }
                for (const Record &record : tile) RecordDraw(candidate, record);
                CompareManagers(legacy, candidate, caseNumber);
            }

            static u32 randomState = 0x6d2b79f5u;
            static u32 RandomU32() {
                randomState ^= randomState << 13;
                randomState ^= randomState >> 17;
                randomState ^= randomState << 5;
                return randomState;
            }
            static bool RandomBool() { return (RandomU32() & 1u) != 0u; }
            static float RandomFloat(float low, float high) {
                const float unit = static_cast<float>(RandomU32() & 0xffffu) / 65535.0f;
                return low + (high - low) * unit;
            }

            static Manager RandomManager() {
                Manager manager;
                manager.offsetX = RandomFloat(-40.0f, 40.0f);
                manager.offsetY = RandomFloat(-30.0f, 30.0f);
                manager.viewportX = 32.0f;
                manager.viewportY = 16.0f;
                manager.viewportWidth = 384.0f;
                manager.viewportHeight = 448.0f;
                manager.colorMulEnabled = RandomBool();
                manager.disableZBuffer = RandomBool();
                manager.colorMul = {RandomU32()};
                manager.currentTexture = RandomBool()
                                             ? Manager::TextureForSource(
                                                   static_cast<int>(RandomU32() % 4u))
                                             : -1;
                manager.currentVertexShader = static_cast<int>(RandomU32() % 2u);
                manager.currentBlendMode = RandomU32() % 2u;
                manager.currentZWriteDisable = RandomU32() % 2u;
                manager.pairMode = RandomBool();
                manager.spritesToDraw = RandomU32() % 4u;
                manager.renderStateChanges = RandomU32() % 7u;
                return manager;
            }

            static Vm RandomVm() {
                Vm vm;
                vm.hasSprite = (RandomU32() % 19u) != 0u;
                vm.visible = (RandomU32() % 17u) != 0u;
                vm.active = (RandomU32() % 13u) != 0u;
                vm.useColor2 = RandomBool();
                vm.cachedSinCos = RandomBool();
                vm.posX = RandomFloat(-700.0f, 900.0f);
                vm.posY = RandomFloat(-700.0f, 900.0f);
                vm.posZ = RandomFloat(0.01f, 0.99f);
                vm.width = RandomFloat(1.0f, 96.0f);
                vm.height = RandomFloat(1.0f, 96.0f);
                static constexpr float scales[] = {-2.0f, -1.0f, -0.25f, -0.0f,
                                                    0.0f, 0.25f, 1.0f, 2.0f};
                vm.scaleX = scales[RandomU32() % (sizeof(scales) / sizeof(scales[0]))];
                vm.scaleY = scales[RandomU32() % (sizeof(scales) / sizeof(scales[0]))];
                static constexpr float rotations[] = {
                    0.0f, -0.0f, 0.125f, -0.75f, 1.5707964f, 3.1415927f};
                vm.rotationZ =
                    rotations[RandomU32() % (sizeof(rotations) / sizeof(rotations[0]))];
                RenderSinCos(vm.rotationZ, vm.sine, vm.cosine);
                vm.u0 = RandomFloat(-1.0f, 1.0f);
                vm.u1 = RandomFloat(0.0f, 2.0f);
                vm.v0 = RandomFloat(-1.0f, 1.0f);
                vm.v1 = RandomFloat(0.0f, 2.0f);
                vm.color = {RandomU32() | 0x01000000u};
                if ((RandomU32() % 23u) == 0u) vm.color.value &= 0x00ffffffu;
                vm.color2 = {RandomU32()};
                vm.sourceFileIndex = static_cast<int>(RandomU32() % 4u);
                vm.anchor = RandomU32() % 4u;
                vm.blendMode = RandomU32() % 2u;
                vm.zWriteDisable = RandomU32() % 2u;
                return vm;
            }

            static void RunDirectedCases() {
                std::vector<Vm> vms;
                const std::array<float, 5> scales{{-1.0f, -0.0f, 0.0f, 0.5f, 1.5f}};
                const std::array<float, 4> rotations{{0.0f, -0.0f, 0.25f, 1.5707964f}};
                int ordinal = 0;
                for (u32 anchor = 0; anchor < 4u; ++anchor) {
                    for (float scale : scales) {
                        for (float rotation : rotations) {
                            for (bool cached : {false, true}) {
                                Vm vm;
                                vm.anchor = anchor;
                                vm.scaleX = scale;
                                vm.scaleY = scales[(ordinal + 2) % scales.size()];
                                vm.rotationZ = rotation;
                                vm.cachedSinCos = cached;
                                RenderSinCos(rotation, vm.sine, vm.cosine);
                                vm.posX = (ordinal % 7 == 0) ? -1000.0f :
                                          (ordinal % 11 == 0) ? 1000.0f : 180.5f;
                                vm.posY = (ordinal % 13 == 0) ? -1000.0f : 150.5f;
                                vm.useColor2 = (ordinal & 1) != 0;
                                vm.color = {static_cast<u32>(0x80010203u + ordinal)};
                                vm.color2 = {static_cast<u32>(0x40f0e0d0u - ordinal)};
                                vm.blendMode = static_cast<u32>(ordinal & 1);
                                vm.zWriteDisable = static_cast<u32>((ordinal >> 1) & 1);
                                vm.sourceFileIndex = ordinal % 4;
                                vm.u0 = ordinal * 0.001f;
                                vm.u1 = vm.u0 + 0.0625f;
                                vm.v0 = ordinal * -0.0005f;
                                vm.v1 = vm.v0 + 0.125f;
                                vms.push_back(vm);
                                ++ordinal;
                            }
                        }
                    }
                }
                // Conservative-cull strict inequalities: equality remains
                // drawable, the adjacent representable value is rejected.
                Vm leftEdge;
                leftEdge.width = leftEdge.height = 16.0f; // bound = 16
                leftEdge.posX = 32.0f - 16.0f - 3.25f;
                leftEdge.posY = 120.0f;
                vms.push_back(leftEdge);
                leftEdge.posX = std::nextafter(leftEdge.posX,
                                               -std::numeric_limits<float>::infinity());
                vms.push_back(leftEdge);
                Vm rightEdge;
                rightEdge.width = rightEdge.height = 16.0f;
                rightEdge.posX = 32.0f + 384.0f + 16.0f - 3.25f;
                rightEdge.posY = 120.0f;
                vms.push_back(rightEdge);
                rightEdge.posX = std::nextafter(rightEdge.posX,
                                                std::numeric_limits<float>::infinity());
                vms.push_back(rightEdge);
                Vm topEdge;
                topEdge.width = topEdge.height = 16.0f;
                topEdge.posX = 180.0f;
                topEdge.posY = 16.0f - 16.0f + 7.75f;
                vms.push_back(topEdge);
                topEdge.posY = std::nextafter(topEdge.posY,
                                              -std::numeric_limits<float>::infinity());
                vms.push_back(topEdge);
                Vm bottomEdge;
                bottomEdge.width = bottomEdge.height = 16.0f;
                bottomEdge.posX = 180.0f;
                bottomEdge.posY = 16.0f + 448.0f + 16.0f + 7.75f;
                vms.push_back(bottomEdge);
                bottomEdge.posY = std::nextafter(bottomEdge.posY,
                                                 std::numeric_limits<float>::infinity());
                vms.push_back(bottomEdge);
                // Explicit rejected records exercise all four early-out flags.
                for (int field = 0; field < 4; ++field) {
                    Vm vm;
                    if (field == 0) vm.hasSprite = false;
                    if (field == 1) vm.visible = false;
                    if (field == 2) vm.active = false;
                    if (field == 3) vm.color.value &= 0x00ffffffu;
                    vms.push_back(vm);
                }
                Manager manager;
                manager.offsetX = 3.25f;
                manager.offsetY = -7.75f;
                manager.viewportX = 32.0f;
                manager.viewportY = 16.0f;
                manager.viewportWidth = 384.0f;
                manager.viewportHeight = 448.0f;
                manager.colorMulEnabled = true;
                manager.colorMul = {0x90e080c0u};
                manager.currentTexture = Manager::TextureForSource(3);
                manager.currentVertexShader = 0;
                manager.currentBlendMode = 1u;
                manager.currentZWriteDisable = 1u;
                manager.pairMode = false;
                manager.spritesToDraw = 2u;
                RunCase(manager, vms, 1);
            }

            static void TestNonFiniteFloorFallback() {
                const float nan = std::numeric_limits<float>::quiet_NaN();
                const float positive = std::numeric_limits<float>::infinity();
                const float negative = -std::numeric_limits<float>::infinity();
                if (!std::isnan(RenderFloor(nan))) Fail("NaN floor fallback", -1);
                if (!std::isinf(RenderFloor(positive)) || RenderFloor(positive) < 0.0f)
                    Fail("+Inf floor fallback", -1);
                if (!std::isinf(RenderFloor(negative)) || RenderFloor(negative) > 0.0f)
                    Fail("-Inf floor fallback", -1);
                if (RenderFloor(2147483648.0f) != 2147483648.0f)
                    Fail("large finite floor fallback", -1);
            }

            int main() {
                RunDirectedCases();
                for (int caseNumber = 2; caseNumber < 502; ++caseNumber) {
                    Manager manager = RandomManager();
                    const unsigned int count = 1u + RandomU32() % 193u;
                    std::vector<Vm> vms;
                    vms.reserve(count);
                    for (unsigned int index = 0; index < count; ++index)
                        vms.push_back(RandomVm());
                    RunCase(manager, vms, caseNumber);
                }
                TestNonFiniteFloorFallback();
                return 0;
            }
        """

        with tempfile.TemporaryDirectory() as temporary:
            temporary_path = Path(temporary)
            cpp = temporary_path / "i2b_differential.cpp"
            executable = temporary_path / "i2b_differential"
            cpp.write_text(textwrap.dedent(source), encoding="utf-8")
            compile_result = subprocess.run(
                [
                    compiler,
                    "-std=c++17",
                    "-O2",
                    "-fno-fast-math",
                    "-ffp-contract=off",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    str(cpp),
                    "-o",
                    str(executable),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run_result = subprocess.run(
                [str(executable)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            self.assertEqual(run_result.returncode, 0, run_result.stderr)


if __name__ == "__main__":
    unittest.main()
