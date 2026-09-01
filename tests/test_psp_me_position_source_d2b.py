import pathlib
import re
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = ROOT / "psp" / "audio_me.h"
SOURCE = ROOT / "psp" / "audio_me.c"
HARNESS = ROOT / "tests" / "me_position_source_d2b_harness.c"
BASELINE_HARNESS = ROOT / "tests" / "me_position_source_baseline_harness.c"


def function_body(text: str, name: str) -> str:
    start = text.index(name)
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError(f"unterminated function: {name}")


class PspMePositionSourceD2bTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_wire_abi_is_versioned_and_frozen(self):
        self.assertIn(
            'TH07_PSP_ME_RENDER_LIST_LAYOUT_VERSION = 0x4c4c3032u, // "LL02"',
            self.header,
        )
        self.assertIn(
            'TH07_PSP_ME_RENDER_POSITION_SOURCE_VERSION = 0x50533031u, // "PS01"',
            self.header,
        )
        for field in (
            "kind", "ownerBasePhys", "ownerBytes", "slotStrideBytes",
            "validBitsPhys", "fullGenerationBasePhys",
            "publishManagerSerialBasePhys", "publishCalcSerialBasePhys",
            "expectedManagerSerial", "expectedCalcSerial",
        ):
            self.assertRegex(self.header, rf"unsigned int {field};")

    def test_header_layout_compiles_on_host(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = pathlib.Path(tmp) / "d2b_abi"
            subprocess.run(
                [
                    "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT), str(HARNESS), "-o", str(binary),
                ],
                check=True,
                cwd=ROOT,
            )
            subprocess.run([str(binary)], check=True)

    def test_feature_off_keeps_ll01_128_byte_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = pathlib.Path(tmp) / "direct_list_baseline_abi"
            subprocess.run(
                [
                    "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT), str(BASELINE_HARNESS), "-o", str(binary),
                ],
                check=True,
                cwd=ROOT,
            )
            subprocess.run([str(binary)], check=True)

    def test_mixed_source_rule_is_fail_closed(self):
        body = function_body(
            self.source, "me_render_stream_position_load_xy(")
        clear = body.index("(validBefore & slotBit) == 0u")
        aos_x = body.index("layout->bulletPosXOffset", clear)
        valid_after_clear = body.index("validAfter", aos_x)
        header = body.index("me_render_stream_position_header_matches", clear)
        generation = body.index("generationBefore", header)
        soa_x = body.index("source->posXBasePhys", generation)
        bracket = body.index("generationAfter", soa_x)
        self.assertLess(clear, aos_x)
        self.assertLess(aos_x, valid_after_clear)
        self.assertLess(valid_after_clear, header)
        self.assertLess(header, generation)
        self.assertLess(generation, soa_x)
        self.assertLess(soa_x, bracket)
        self.assertIn("generationBefore != canonicalGeneration", body)
        self.assertIn("managerBefore != source->expectedManagerSerial", body)
        self.assertIn("calcBefore != source->expectedCalcSerial", body)
        self.assertIn("(validAfter & slotBit) != 0u", body)

    def test_direct_list_has_no_unversioned_position_read(self):
        body = function_body(
            self.source, "me_render_stream_reconstruct_list_record(")
        self.assertIn("me_render_stream_position_load_xy(", body)
        prefix = body[: body.index("me_render_stream_position_load_xy(")]
        self.assertNotIn("bulletPosXOffset);", prefix)
        self.assertNotIn("bulletPosYOffset);", prefix)
        self.assertIn(
            "posXBits, posYBits, 0u, 0x3f800000u", body,
            "D2B must preserve direct-list Z=0 semantics",
        )

    def test_source_layout_rejects_aliases_and_unknown_abi(self):
        body = function_body(
            self.source, "me_render_stream_position_source_valid(")
        self.assertIn("source->version !=", body)
        self.assertIn("source->bytes != sizeof(*source)", body)
        self.assertIn("source->flags != 0u", body)
        self.assertIn("me_render_stream_position_subrange_valid", body)
        self.assertIn("me_render_ranges_overlap", body)
        self.assertIn("source->publishManagerSerialBasePhys == 0u", body)
        self.assertIn("source->expectedManagerSerial == 0u", body)

    def test_real_me_selftest_covers_hit_cold_and_stale(self):
        direct = function_body(
            self.source, "selftest_render_stream_direct_list(")
        self.assertIn("me_render_position_selftest_use_soa", direct)
        self.assertIn("posXBits[0] = float_bits(9000.0f)", direct)
        self.assertIn("generation[0] = 10u", direct)
        self.assertIn("TH07_PSP_ME_RENDER_STREAM_RESULT_RECORD", direct)
        self.assertIn("staleCompletion.outputBytes != 0u", direct)
        self.assertIn("MERW STREAM DIRECT PS01 PASS", direct)


if __name__ == "__main__":
    unittest.main()
