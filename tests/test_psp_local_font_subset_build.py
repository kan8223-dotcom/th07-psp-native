from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAKEFILE = ROOT / "Makefile"
AUDIT = ROOT / "tools" / "release_audit.sh"


def target_body(makefile: str, target: str, next_marker: str) -> str:
    start = makefile.index(target + ":")
    return makefile[start : makefile.index(next_marker, start)]


class PspLocalFontSubsetBuildTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.makefile = MAKEFILE.read_text(encoding="utf-8")

    def test_profile_is_opt_in_file_backed_on_psp1000_and_stamped(self) -> None:
        self.assertIn("PSP_LOCAL_FONT_SUBSET ?= 0", self.makefile)
        self.assertIn("ifeq ($(PSP_LOCAL_FONT_SUBSET),1)", self.makefile)
        self.assertNotIn("PSP_LOCAL_FONT_SUBSET is PSP-2000+ only", self.makefile)
        self.assertIn("-DTH07_PSP_LOCAL_FONT_SUBSET", self.makefile)
        stamp = self.makefile[
            self.makefile.index("PROFILE_STAMP :=") :
            self.makefile.index(".PHONY: FORCE_PROFILE")
        ]
        self.assertIn("$(PSP_LOCAL_FONT_SUBSET)", stamp)

    def test_psp1000_font_ram_measurement_pair_changes_only_selection(self) -> None:
        control = target_body(
            self.makefile,
            "psp1000-font-heap-control-build",
            "psp1000-font-heap-subset-build:",
        )
        subset = target_body(
            self.makefile,
            "psp1000-font-heap-subset-build",
            "psp2000plus-build:",
        )
        for body in (control, subset):
            self.assertIn("PSP_1000=1", body)
            self.assertIn("PSP_FONT_HEAP_DIAG=1", body)
            self.assertIn("psp1000-build", body)
        self.assertIn("PSP_LOCAL_FONT_SUBSET=0", control)
        self.assertIn("PSP_LOCAL_FONT_SUBSET=1", subset)
        self.assertIn(
            "PSP_LOCAL_FONT_SUBSET=$(PSP_LOCAL_FONT_SUBSET)",
            target_body(self.makefile, "psp1000-build", "# PC/PPSSPP-only A/B pair"),
        )

    def test_a6v4_changes_only_the_local_font_gate_over_a6v3_contract(self) -> None:
        base = target_body(
            self.makefile,
            "psp3000-rid30-ab-me-build",
            "# Current accepted ME A/B build",
        )
        self.assertIn(
            "PSP_LOCAL_FONT_SUBSET=$(PSP_RID30_AB_ME_LOCAL_FONT_SUBSET)", base
        )

        a6v4 = target_body(
            self.makefile,
            "psp3000-rid30-a6v4-local-font-subset-build",
            "# SC member of the pair",
        )
        for token in (
            "PSP_RID30_AB_ME_TITLE_WORKSPACE=1",
            "PSP_RID30_AB_ME_TITLE_TRANSIENT=0",
            "PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=1",
            "PSP_RID30_AB_ME_LOCAL_FONT_SUBSET=1",
            "PSP_RID30_AB_ME_BUILD_ID=0x260901a9u",
            "psp3000-rid30-ab-me-build",
        ):
            self.assertIn(token, a6v4)

    def test_known_a6v3_remains_subset_off_by_default(self) -> None:
        a6v3 = target_body(
            self.makefile,
            "psp3000-rid30-a6v3-title-font-hole-swap-build",
            "# A6v4 keeps",
        )
        self.assertNotIn("LOCAL_FONT_SUBSET", a6v3)
        self.assertIn("PSP_RID30_AB_ME_BUILD_ID=0x260901a8u", a6v3)

    def test_a6v4_wave_dash_candidate_is_uniquely_identified(self) -> None:
        candidate = target_body(
            self.makefile,
            "psp3000-rid30-a6v4-cp932-wave-dash-build",
            "# SC member of the pair",
        )
        for token in (
            "PSP_RID30_AB_ME_TITLE_WORKSPACE=1",
            "PSP_RID30_AB_ME_TITLE_FONT_HOLE_SWAP=1",
            "PSP_RID30_AB_ME_LOCAL_FONT_SUBSET=1",
            "PSP_RID30_AB_ME_BUILD_ID=0x260901aau",
            "TH07 RID30 A6V4W CP932 WAVE",
        ):
            self.assertIn(token, candidate)

    def test_proprietary_subset_cannot_enter_release_archive(self) -> None:
        audit = AUDIT.read_text(encoding="utf-8")
        self.assertIn(r"msgothic[^/]*\.(ttc|ttf|otf)", audit)


if __name__ == "__main__":
    unittest.main()
