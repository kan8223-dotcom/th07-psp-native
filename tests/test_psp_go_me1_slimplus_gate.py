from __future__ import annotations

import hashlib
import importlib.util
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def target_recipe(makefile: str, target: str) -> str:
    start = makefile.index(f"{target}:")
    match = re.search(r"\n(?=[A-Za-z0-9_.-]+(?:\s+[^\n:]*)?:)", makefile[start + 1 :])
    return makefile[start:] if match is None else makefile[start : start + 1 + match.start()]


def load_builder():
    path = ROOT / "tools/build_ge4_slimplus_wrapper.py"
    spec = importlib.util.spec_from_file_location("ge4_slimplus_builder", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class PspGoMe1SlimPlusGateContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        cls.audio = (ROOT / "psp/audio_me.c").read_text(encoding="utf-8")
        cls.main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        cls.ge4 = (ROOT / "psp/ge4_game_bridge.cpp").read_text(encoding="utf-8")
        cls.builder = load_builder()

    def test_candidate_is_opt_in_and_profile_stamped(self) -> None:
        self.assertIn("PSP_SLIMPLUS_ME_GATE ?= 0", self.makefile)
        self.assertIn("-DTH07_PSP_SLIMPLUS_ME_GATE", self.makefile)
        stamp = next(
            line
            for line in self.makefile.splitlines()
            if line.startswith("PROFILE_STAMP :=")
        )
        self.assertIn("$(PSP_SLIMPLUS_ME_GATE)", stamp)
        for required in (
            "PSP_SLIMPLUS_ME_GATE requires the AUDIO4M custom-core profile",
            "PSP_SLIMPLUS_ME_GATE is not valid for PSP-1000",
        ):
            self.assertIn(required, self.makefile)

    def test_candidate_keeps_exact_go_v021_a7_contract(self) -> None:
        recipe = target_recipe(self.makefile, "pspgo-me1-slimplus-build")
        expected = {
            "PSP_SLIMPLUS_ME_GATE": "1",
            "PSP_1000": "0",
            "PSP_SHIKIGAMI": "1",
            "PSP_MECC_AUDIO_4M": "1",
            "PSP_RID30_AB_ME_UV16": "0",
            "PSP_RID30_AB_ME_XYZ16": "0",
            "PSP_RID30_AB_ME_C1_GE_EXPERIMENT": "0",
            "PSP_RID30_AB_ME_TRUSTED_SEED_AUTHORITY": "0",
            "PSP_RID30_AB_ME_SEED_SOA": "0",
            "PSP_RID30_AB_ME_POSITION_SOA_SHADOW": "0",
            "PSP_RID30_AB_ME_TITLE_WORKSPACE": "1",
            "PSP_RID30_AB_ME_TITLE_TRANSIENT": "0",
            "PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP": "1",
            "PSP_RID30_AB_ME_LOCAL_FONT_SUBSET": "1",
            "PSP_RID30_AB_ME_FONT_TAIL_ARCHIVE": "1",
            "PSP_RID30_AB_ME_BUILD_ID": "0x260901adu",
        }
        for variable, value in expected.items():
            with self.subTest(variable=variable):
                self.assertIn(f"{variable}={value}", recipe)
        self.assertIn("PSP_RID30_AB_ME_TITLE='TH07 PSP v0.2.1-beta'", recipe)
        self.assertIn("psp3000-rid30-ab-me-build", recipe)

    def test_mecc_and_merw_share_positive_model_gate(self) -> None:
        self.assertIn("ME_BGM_MINIMUM_MODEL = 1", self.audio)
        self.assertIn("model < ME_BGM_MINIMUM_MODEL", self.audio)
        self.assertIn("ME_BGM_REQUIRED_MODEL = 3", self.audio)
        self.assertIn("model != ME_BGM_REQUIRED_MODEL", self.audio)
        self.assertIn("ME_BGM_REQUIRED_TABLE = 2", self.audio)
        self.assertIn("MECC GATE PASS MODEL %d (SLIM+)", self.audio)
        self.assertIn("MECC GATE OFF MODEL %d (SLIM+ MIN 1)", self.audio)
        self.assertIn("MERW M0 PASS (SLIM+ GATE; SELFTEST PASS)", self.main)
        self.assertIn("MERW M0 OFF (ME INIT R0; SEE PRIOR MECC REASON)", self.main)
        self.assertIn("MERW M0A FAILED -> STOP / COLD REBOOT", self.main)

    def test_ge4_changes_only_model_conjunct_in_user_bridge(self) -> None:
        self.assertIn("kMinimumModel = 1", self.ge4)
        self.assertIn("model < kMinimumModel", self.ge4)
        self.assertIn("kRequiredModel = 3", self.ge4)
        self.assertIn("model != kRequiredModel", self.ge4)
        for unchanged_gate in (
            "base != kExpectedEdramBase",
            "hwSize != kFourMiB",
            "sizeBefore != kTwoMiB",
            "sizeAfter != kFourMiB",
            "power-lock-uncertain",
            "post-Set4 sync",
        ):
            self.assertIn(unchanged_gate, self.ge4)
        self.assertIn("GE4 GATE PASS M%d", self.ge4)
        self.assertIn("GE4 GATE OFF M%d/MIN1", self.ge4)
        self.assertIn("GE4 ACTIVE Slim+", self.ge4)

    def test_kernel_wrapper_is_exact_frozen_blob_plus_two_gate_words(self) -> None:
        source_line = next(
            line
            for line in self.makefile.splitlines()
            if line.startswith("GE4_PROVEN_PRX_SOURCE := ")
        )
        source = (ROOT / source_line.split(":=", 1)[1].strip()).resolve()
        base = source.read_bytes()
        candidate = self.builder.derive(base)

        offset = self.builder.MODEL_GATE_OFFSET
        width = len(self.builder.MODEL3_GATE)
        self.assertEqual(base[offset : offset + width], self.builder.MODEL3_GATE)
        self.assertEqual(candidate[offset : offset + width], self.builder.SLIMPLUS_GATE)
        self.assertEqual(base[:offset], candidate[:offset])
        self.assertEqual(base[offset + width :], candidate[offset + width :])
        self.assertEqual(
            int.from_bytes(base[offset : offset + 4], "little"), 0x24030003
        )
        self.assertEqual(
            int.from_bytes(base[offset + 4 : offset + 8], "little"), 0x5443000F
        )
        self.assertEqual(
            int.from_bytes(candidate[offset : offset + 4], "little"), 0
        )
        self.assertEqual(
            int.from_bytes(candidate[offset + 4 : offset + 8], "little"),
            0x5840000F,
        )
        self.assertEqual(
            hashlib.sha256(candidate).hexdigest(), self.builder.CANDIDATE_SHA256
        )

    def test_kernel_wrapper_refuses_any_unfrozen_input(self) -> None:
        altered = bytearray(b"\0" * self.builder.BASE_SIZE)
        with self.assertRaisesRegex(ValueError, "base SHA-256"):
            self.builder.derive(bytes(altered))


if __name__ == "__main__":
    unittest.main()
