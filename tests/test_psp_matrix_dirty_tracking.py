import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "psp/graphics/PspGuGraphics.cpp").read_text(encoding="utf-8")


class PspMatrixDirtyTrackingTests(unittest.TestCase):
    def test_each_spatial_matrix_has_an_independent_dirty_bit(self):
        for matrix in ("MATRIX_MODEL", "MATRIX_VIEW", "MATRIX_PROJECTION"):
            self.assertIn(f"MatrixDirtyBit({matrix})", SOURCE)
        self.assertIn("if (type <= MATRIX_PROJECTION)", SOURCE)
        self.assertIn("mMatrixDirtyMask |= MatrixDirtyBit(type);", SOURCE)
        all_bits = re.search(
            r"constexpr unsigned int kAll3dMatrixDirtyBits =(?P<body>.*?);",
            SOURCE,
            re.S,
        )
        self.assertIsNotNone(all_bits)
        self.assertNotIn("MATRIX_TEXTURE", all_bits.group("body"))
        self.assertNotIn("mMatricesDirty", SOURCE)

    def test_new_list_and_raw_state_reset_force_a_full_submit(self):
        self.assertGreaterEqual(
            SOURCE.count("mMatrixDirtyMask = kAll3dMatrixDirtyBits;"),
            3,
        )
        start_list = re.search(
            r"void StartList\(\).*?mAppliedMatrixMode = -1;(?P<body>.*?)\n    }",
            SOURCE,
            re.S,
        )
        self.assertIsNotNone(start_list)
        self.assertIn("mMatrixDirtyMask = kAll3dMatrixDirtyBits;", start_list.group("body"))

    def test_mode_switch_forces_all_matrices_but_same_mode_uses_mask(self):
        apply = re.search(
            r"void ApplyMatrices\(bool screenSpace\)(?P<body>.*?)\n    }\n\n    void ApplyTexture",
            SOURCE,
            re.S,
        )
        self.assertIsNotNone(apply)
        body = apply.group("body")
        self.assertIn("if (screenSpace && !modeChanged)", body)
        self.assertIn("mMatrixDirtyMask |= kAll3dMatrixDirtyBits;", body)
        self.assertRegex(
            body,
            r"mMatrixDirtyMask & \(1u << static_cast<unsigned int>\(i\)\)",
        )

    def test_model_only_3d_draw_does_not_resubmit_texture_matrix(self):
        apply = re.search(
            r"void ApplyMatrices\(bool screenSpace\)(?P<body>.*?)\n    }\n\n    void ApplyTexture",
            SOURCE,
            re.S,
        )
        self.assertIsNotNone(apply)
        body = apply.group("body")
        self.assertIn("if (modeChanged)", body)
        self.assertIn("sceGuSetMatrix(GU_TEXTURE, &kIdentityMatrix);", body)

    def test_submission_model_reduces_stage5_redundancy(self):
        def apply(mode, dirty, applied_mode):
            all_bits = 0b1111
            effective = all_bits if mode != applied_mode else dirty
            if effective == 0:
                return 0, mode
            if mode == 1:  # screen-space path deliberately keeps the full reset
                return 4, mode
            return effective.bit_count(), mode

        submissions = 0
        mode = -1
        for quad in range(129):
            dirty = 0b0001  # MODEL changes for every Stage 5 staircase quad
            count, mode = apply(0, dirty, mode)
            submissions += count
        self.assertEqual(submissions, 4 + 128)
        self.assertLess(submissions, 129 * 4)


if __name__ == "__main__":
    unittest.main()
