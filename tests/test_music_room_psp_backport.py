from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MUSIC_SOURCE = ROOT / "src" / "MusicRoom.cpp"
MUSIC_HEADER = ROOT / "src" / "MusicRoom.hpp"
TEXT_SOURCE = ROOT / "src" / "TextHelper.cpp"
GRAPHICS_SOURCE = ROOT / "psp" / "graphics" / "PspGuGraphics.cpp"


def shade_music_pixel_generic(
    pixel: tuple[int, int, int, int], pixel_index: int
) -> tuple[int, int, int, int]:
    """Reference the generic InvertAlpha path for the 1024x38 Music Room case."""
    r, g, b, a = pixel
    if a == 0:
        return 0, 0, 0, 0
    double_area = 1024 * 38 * 2
    gradient_index = pixel_index * 2
    if r >= b:
        r -= (r * gradient_index * 2) // double_area // 3
        g -= (g * gradient_index * 2) // double_area // 3
    else:
        b -= (b * gradient_index) // double_area // 2
        g -= (g * gradient_index) // double_area // 2
    return r, g, b, a


def shade_music_pixel_fast(
    pixel: tuple[int, int, int, int], pixel_index: int
) -> tuple[int, int, int, int]:
    """Reference the constant-divisor Music Room specialization."""
    r, g, b, a = pixel
    if a == 0:
        return 0, 0, 0, 0
    music_text_double_area = 1024 * 38 * 2
    gradient_index = pixel_index * 2
    if r >= b:
        r -= (r * gradient_index * 2) // music_text_double_area // 3
        g -= (g * gradient_index * 2) // music_text_double_area // 3
    else:
        b -= (b * gradient_index) // music_text_double_area // 2
        g -= (g * gradient_index) // music_text_double_area // 2
    return r, g, b, a


def synthetic_rgba(x: int, y: int) -> tuple[int, int, int, int]:
    return (
        (x * 17 + y * 29 + 3) & 0xFF,
        (x * 43 + y * 7 + 91) & 0xFF,
        (x * 5 + y * 61 + 127) & 0xFF,
        (x * 11 + y * 13 + 251) & 0xFF,
    )


def box_filter_reference(
    source_width: int,
    source_height: int,
    source_rect: tuple[int, int, int, int],
    destination_width: int,
    destination_height: int,
    use_horizontal_2x_fast_path: bool,
) -> list[tuple[int, int, int, int]]:
    """Execute the generic or specialized CopyTextBufferBoxFiltered arithmetic."""
    rect_x, rect_y, rect_width, rect_height = source_rect
    exact_horizontal_2x = rect_width == destination_width * 2
    unclipped_horizontal_2x = (
        exact_horizontal_2x
        and rect_x >= 0
        and rect_x + rect_width <= source_width
    )
    output: list[tuple[int, int, int, int]] = []
    for y in range(destination_height):
        source_y0 = rect_y + y * rect_height // destination_height
        source_y1 = rect_y + (
            ((y + 1) * rect_height + destination_height - 1)
            // destination_height
        )
        source_y0 = max(0, min(source_height - 1, source_y0))
        source_y1 = max(source_y0 + 1, min(source_height, source_y1))

        for x in range(destination_width):
            if use_horizontal_2x_fast_path and exact_horizontal_2x:
                source_x0 = rect_x + x * 2
                source_x1 = source_x0 + 2
            else:
                source_x0 = rect_x + x * rect_width // destination_width
                source_x1 = rect_x + (
                    ((x + 1) * rect_width + destination_width - 1)
                    // destination_width
                )
            source_x0 = max(0, min(source_width - 1, source_x0))
            source_x1 = max(source_x0 + 1, min(source_width, source_x1))

            sums = [0, 0, 0, 0]
            count = 0
            for source_y in range(source_y0, source_y1):
                for source_x in range(source_x0, source_x1):
                    pixel = synthetic_rgba(source_x, source_y)
                    for channel in range(4):
                        sums[channel] += pixel[channel]
                    count += 1

            sample_rows = source_y1 - source_y0
            if use_horizontal_2x_fast_path and unclipped_horizontal_2x:
                if sample_rows == 3:
                    divisor = 6
                elif sample_rows == 4:
                    divisor = 8
                else:
                    divisor = count
            else:
                divisor = count
            output.append(tuple(total // divisor for total in sums))
    return output


def swizzle_copy(source: bytes, width_bytes: int, height: int) -> bytes:
    """Host reference for PspGuGraphics::SwizzleCopy."""
    if width_bytes % 16 != 0 or height % 8 != 0:
        raise ValueError("a swizzle copy must contain complete 16x8-byte blocks")
    if len(source) != width_bytes * height:
        raise ValueError("source size does not match the declared rectangle")
    output = bytearray(len(source))
    width_blocks = width_bytes // 16
    for block_y in range(height // 8):
        for block_x in range(width_blocks):
            block_offset = (block_y * width_blocks + block_x) * 128
            row_offset = block_y * 8 * width_bytes + block_x * 16
            for y in range(8):
                output[block_offset + y * 16 : block_offset + (y + 1) * 16] = (
                    source[
                        row_offset + y * width_bytes :
                        row_offset + y * width_bytes + 16
                    ]
                )
    return bytes(output)


def swizzled_byte_offset(x_byte: int, y: int, width_bytes: int) -> int:
    width_blocks = width_bytes // 16
    block = (y // 8) * width_blocks + x_byte // 16
    return block * 128 + (y % 8) * 16 + x_byte % 16


def next_initial_description_state(state: int, logo_stopped: bool) -> tuple[int, bool]:
    none, wait_for_logo, wait_for_present, ready = range(4)
    if state == wait_for_logo and logo_stopped:
        return wait_for_present, False
    if state == wait_for_present:
        return ready, False
    if state == ready:
        return none, True
    return state, False


class MusicRoomSourceInvariantTest(unittest.TestCase):
    def test_state_uses_the_existing_tail_padding_byte(self) -> None:
        header = MUSIC_HEADER.read_text(encoding="utf-8")
        source = MUSIC_SOURCE.read_text(encoding="utf-8")
        self.assertIn("enum InitialDescriptionState : u8", source)
        states = (
            "INITIAL_DESCRIPTION_NONE",
            "INITIAL_DESCRIPTION_WAIT_FOR_LOGO",
            "INITIAL_DESCRIPTION_WAIT_FOR_STOPPED_PRESENT",
            "INITIAL_DESCRIPTION_READY",
        )
        positions = [source.index(state) for state in states]
        self.assertEqual(positions, sorted(positions))
        self.assertLess(header.index("u8 titleRendered[31]"),
                        header.index("u8 initialDescriptionState"))
        self.assertNotIn("descriptionRenderIdx", header)
        self.assertNotIn("descriptionRenderIdx", source)

        align4 = lambda value: (value + 3) & ~3
        self.assertEqual(align4(31), 32)
        self.assertEqual(align4(31 + 1), 32)

    def test_logo_frame_precedes_the_initial_comment_batch(self) -> None:
        source = MUSIC_SOURCE.read_text(encoding="utf-8")
        update = source[source.index("u32 MusicRoom::OnUpdate") :
                        source.index("u32 MusicRoom::OnDraw")]
        required = (
            "INITIAL_DESCRIPTION_WAIT_FOR_LOGO",
            "INITIAL_DESCRIPTION_WAIT_FOR_STOPPED_PRESENT",
            "INITIAL_DESCRIPTION_READY",
            "DrawMusicDescriptionBatched(arg, arg->selectedIdx)",
            "INITIAL_DESCRIPTION_NONE",
        )
        positions = [update.index(token) for token in required]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("arg->vm[0].isStopped", update)

        state = 1
        state, drew = next_initial_description_state(state, False)
        self.assertEqual((state, drew), (1, False))
        state, drew = next_initial_description_state(state, True)
        self.assertEqual((state, drew), (2, False))
        state, drew = next_initial_description_state(state, True)
        self.assertEqual((state, drew), (3, False))
        state, drew = next_initial_description_state(state, True)
        self.assertEqual((state, drew), (0, True))

    def test_selection_dispatches_audio_before_the_batched_comments(self) -> None:
        source = MUSIC_SOURCE.read_text(encoding="utf-8")
        selection = source[
            source.index("if (WAS_PRESSED_RAW(TH_BUTTON_SELECTMENU))") :
            source.index("if (WAS_PRESSED_RAW(TH_BUTTON_RETURNMENU))")
        ]
        positions = (
            selection.index("g_Supervisor.PlayAudio"),
            selection.index("g_SoundPlayer.ProcessQueues()"),
            selection.index("DrawMusicDescriptionBatched(this, this->selectedIdx)"),
        )
        self.assertEqual(positions, tuple(sorted(positions)))

        helper = source[source.index("void DrawMusicDescriptionBatched") :
                        source.index("} // namespace")]
        helper_positions = (
            helper.index("Th07PspBeginTextUploadBatch()"),
            helper.index("DrawMusicDescription(room, trackIdx)"),
            helper.index("Th07PspEndTextUploadBatch()"),
        )
        self.assertEqual(helper_positions, tuple(sorted(helper_positions)))

    def test_entry_batches_the_visible_titles_and_resets_static_reentry(self) -> None:
        source = MUSIC_SOURCE.read_text(encoding="utf-8")
        added = source[source.index("ZunResult MusicRoom::AddedCallback") :
                       source.index("ZunResult MusicRoom::DeletedCallback")]
        self.assertIn("arg->enableInput = 0", added)
        positions = (
            added.index("Th07PspBeginTextUploadBatch()"),
            added.index("DrawMusicTitle(arg, offset)"),
            added.index("Th07PspEndTextUploadBatch()"),
            added.index(
                "arg->initialDescriptionState = INITIAL_DESCRIPTION_WAIT_FOR_LOGO"
            ),
        )
        self.assertEqual(positions, tuple(sorted(positions)))
        self.assertIn("initialTitleStart + 10", added)


class TextHelperSourceInvariantTest(unittest.TestCase):
    def test_reused_work_surface_clears_only_the_requested_rectangle(self) -> None:
        source = TEXT_SOURCE.read_text(encoding="utf-8")
        allocate = source[source.index("bool TextHelper::AllocateBuffer") :
                          source.index("bool TextHelper::InvertAlpha")]
        reuse = allocate[:allocate.index("ReleaseBuffer();")]
        self.assertIn("SDL_Rect usedRect = {0, 0, width, height}", reuse)
        self.assertIn("SDL_FillRect(this->buffer, &usedRect, 0)", reuse)
        self.assertNotIn("SDL_FillRect(this->buffer, NULL, 0)", reuse)

        copy = source[source.index("bool TextHelper::CopyTextToTexture") :
                      source.index("ZunResult TextHelper::CreateTextBuffer")]
        self.assertNotIn("SDL_FillRect(outSurface", copy)

    def test_music_invert_alpha_specialization_keeps_integer_order(self) -> None:
        source = TEXT_SOURCE.read_text(encoding="utf-8")
        invert = source[source.index("bool TextHelper::InvertAlpha") :
                        source.index("static void CopyTextBufferBoxFiltered")]
        self.assertIn(
            "x == 0 && y == 0 && spriteWidth == 1024 && fontHeight == 38 && !param5",
            invert,
        )
        self.assertIn("constexpr i32 kMusicTextDoubleArea = 1024 * 38 * 2", invert)
        for expression in (
            "(r * i * 2) / kMusicTextDoubleArea / 3",
            "(g * i * 2) / kMusicTextDoubleArea / 3",
            "(b * i) / kMusicTextDoubleArea / 2",
            "(g * i) / kMusicTextDoubleArea / 2",
        ):
            self.assertIn(expression, invert)

    def test_music_invert_alpha_fast_path_is_bit_exact(self) -> None:
        pixels = (
            (0, 0, 0, 0),
            (255, 255, 255, 255),
            (255, 127, 0, 1),
            (0, 127, 255, 254),
            (128, 1, 127, 93),
            (127, 254, 128, 193),
            (1, 2, 1, 255),
            (1, 2, 2, 255),
        )
        for pixel_index in range(1024 * 38):
            for pixel in pixels:
                self.assertEqual(
                    shade_music_pixel_fast(pixel, pixel_index),
                    shade_music_pixel_generic(pixel, pixel_index),
                )

    def test_horizontal_2x_specialization_and_constant_divisors_are_present(self) -> None:
        source = TEXT_SOURCE.read_text(encoding="utf-8")
        filtered = source[source.index("static void CopyTextBufferBoxFiltered") :
                          source.index("bool TextHelper::CopyTextToTexture")]
        for token in (
            "const bool exactHorizontal2x",
            "const bool unclippedHorizontal2x",
            "sx0 = srcRect.x + x * 2",
            "sx1 = sx0 + 2",
            "sampleRows == 3",
            "sumR / 6u",
            "sampleRows == 4",
            "sumR >> 3",
        ):
            self.assertIn(token, filtered)

    def test_horizontal_2x_fast_path_is_bit_exact(self) -> None:
        cases = (
            # Exact Music Room dimensions; vertical spans exercise /6 and /8.
            (1030, 44, (3, 2, 1024, 38), 512, 16),
            # Keep exact 2x x bounds, but force the clipped divisor fallback.
            (1024, 42, (-1, 1, 1024, 38), 512, 16),
        )
        for source_width, source_height, rect, width, height in cases:
            with self.subTest(rect=rect):
                generic = box_filter_reference(
                    source_width, source_height, rect, width, height, False
                )
                fast = box_filter_reference(
                    source_width, source_height, rect, width, height, True
                )
                self.assertEqual(fast, generic)

    def test_glyph_flush_policy_is_split_by_model(self) -> None:
        source = TEXT_SOURCE.read_text(encoding="utf-8")
        self.assertNotIn("g_FontPointSize", source)
        render = source[source.index("void TextHelper::RenderTextToTextureBold") :
                        source.index("i32 TextHelper::GetLogicalStringWidth")]
        call = "TTF_SetFontSize(g_Font, fontSize)"
        # PSP-1000 keeps the unconditional per-row flush (heap fragmentation
        # protection).  64 MiB models keep the glyph cache: R7 hardware timing
        # showed the per-row flush costs ~0.5 s per Music Room text row.
        self.assertEqual(render.count(call), 2)
        font_guard = render.index("#if defined(TH07_PSP_1000)")
        font_else = render.index("#else", font_guard)
        font_end = render.index("#endif", font_else)
        psp1000_branch = render[font_guard:font_else]
        self.assertIn(call, psp1000_branch)
        guarded_branch = render[font_else:font_end]
        self.assertIn("g_CurrentFontSizeOwner != g_Font || g_CurrentFontSize != fontSize",
                      guarded_branch)
        self.assertIn(call, guarded_branch)
        self.assertNotIn("static i32 currentFontSize", render)
        self.assertNotIn("static TTF_Font *currentFont", render)
        self.assertLess(render.index("if (!g_Font)"),
                        render.index("#if defined(TH07_PSP_1000)"))


class PspGuTextAtlasBackportTest(unittest.TestCase):
    def test_batch_swap_restores_the_two_address_invariant(self) -> None:
        source = GRAPHICS_SOURCE.read_text(encoding="utf-8")
        begin = source[source.index("void BeginTextUploadBatch") :
                       source.index("void EndTextUploadBatch")]
        self.assertLess(begin.index("SubmitAndRestart()"),
                        begin.index("mTextUploadBatchActive = true"))

        end = source[source.index("void EndTextUploadBatch") :
                     source.index("void SetTextureSubImage")]
        positions = (
            end.index("sceKernelDcacheWritebackRange(writePixels, texture.bytes)"),
            end.index("std::swap(texture.pixels, texture.updatePixels)"),
            end.index("std::memcpy(texture.updatePixels, texture.pixels, texture.bytes)"),
            end.index("sceKernelDcacheWritebackRange(texture.updatePixels, texture.bytes)"),
            end.index("sceGuTexFlush()"),
        )
        self.assertEqual(positions, tuple(sorted(positions)))

        update = source[source.index("void SetTextureSubImage") :
                        source.index("void ReadPixels")]
        initialized = update.index("initializedAlternate = texture.updatePixels != nullptr")
        guarded_copy = update.index("if (initializedAlternate)", initialized)
        full_copy = update.index(
            "std::memcpy(texture.updatePixels, texture.pixels, texture.bytes)",
            guarded_copy,
        )
        self.assertLess(initialized, guarded_copy)
        self.assertLess(guarded_copy, full_copy)

    def test_swizzled_band_and_direct_copy_guards_are_present(self) -> None:
        source = GRAPHICS_SOURCE.read_text(encoding="utf-8")
        update = source[source.index("void SetTextureSubImage") :
                        source.index("void ReadPixels")]
        for token in (
            "const bool fullWidthSwizzledUpdate",
            "texture.textAtlas && texture.psm == GU_PSM_8888 && texture.swizzled",
            "dstLeft == 0 && dstRight == static_cast<int>(texture.storageWidth)",
            "const int publishTop = dstTop & ~7",
            "const int publishBottom = (dstBottom + 7) & ~7",
            "const bool swizzledBandPublish",
            "const bool directSwizzledBandCopy",
            "(dstTop & 7) == 0 && (dstBottom & 7) == 0",
            "texture.logicalWidth == texture.storageWidth",
            "texture.logicalHeight == texture.storageHeight",
            "xoffset == 0",
            "width == static_cast<int>(texture.storageWidth)",
            "SwizzleCopy(static_cast<unsigned char *>(destination)",
            "static_cast<unsigned int>(yoffset) * texture.storageWidth * 4u",
            "initializedAlternate ? 0u : dirtyOffset",
            "initializedAlternate ? texture.bytes : dirtyBytes",
        ):
            self.assertIn(token, update)

    def test_full_width_eight_row_swizzle_band_is_contiguous(self) -> None:
        width_bytes = 128
        storage_height = 64
        for dirty_top, dirty_bottom in ((0, 16), (18, 34), (31, 33), (58, 64)):
            with self.subTest(rows=(dirty_top, dirty_bottom)):
                publish_top = dirty_top & ~7
                publish_bottom = (dirty_bottom + 7) & ~7
                self.assertEqual(publish_top % 8, 0)
                self.assertEqual(publish_bottom % 8, 0)
                self.assertLessEqual(publish_bottom, storage_height)
                offset = publish_top * width_bytes
                size = (publish_bottom - publish_top) * width_bytes

                physical_band = {
                    swizzled_byte_offset(x_byte, y, width_bytes)
                    for y in range(publish_top, publish_bottom)
                    for x_byte in range(width_bytes)
                }
                self.assertEqual(physical_band, set(range(offset, offset + size)))
                dirty_bytes = {
                    swizzled_byte_offset(x_byte, y, width_bytes)
                    for y in range(dirty_top, dirty_bottom)
                    for x_byte in range(width_bytes)
                }
                self.assertLessEqual(dirty_bytes, physical_band)

    def test_direct_band_swizzle_matches_a_full_atlas_reswizzle(self) -> None:
        width_bytes = 64
        atlas_height = 32
        band_height = 16
        old_linear = bytes(
            (index * 17 + index // width_bytes * 31 + 9) & 0xFF
            for index in range(width_bytes * atlas_height)
        )
        new_band = bytes(
            (index * 47 + index // width_bytes * 13 + 5) & 0xFF
            for index in range(width_bytes * band_height)
        )
        for yoffset in (0, 8, 16):
            with self.subTest(yoffset=yoffset):
                expected_linear = bytearray(old_linear)
                linear_offset = yoffset * width_bytes
                expected_linear[
                    linear_offset : linear_offset + len(new_band)
                ] = new_band
                expected = swizzle_copy(bytes(expected_linear), width_bytes, atlas_height)

                actual = bytearray(swizzle_copy(old_linear, width_bytes, atlas_height))
                swizzled_band = swizzle_copy(new_band, width_bytes, band_height)
                swizzled_offset = yoffset * width_bytes
                actual[
                    swizzled_offset : swizzled_offset + len(swizzled_band)
                ] = swizzled_band
                self.assertEqual(bytes(actual), expected)


if __name__ == "__main__":
    unittest.main()
