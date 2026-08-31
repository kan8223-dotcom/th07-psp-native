import hashlib
import importlib.util
import json
import pathlib
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL_PATH = ROOT / "tools" / "psp_stick.py"


def load_tool():
    spec = importlib.util.spec_from_file_location("psp_stick", TOOL_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class PspStickToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tool = load_tool()
        cls.source = TOOL_PATH.read_text(encoding="utf-8")

    def test_scan_discovers_by_signature_instead_of_drive_argument(self):
        captured = []

        def fake_ps(command):
            captured.append(command)
            return "C|100.0|20.0|False\nD|7.0|1.0|True\nH|4.0|4.0|False\n"

        with mock.patch.object(self.tool, "ps", side_effect=fake_ps):
            rows = self.tool.scan()
        self.assertEqual([row["drive"] for row in rows if row["psp"]], ["D"])
        self.assertIn("Get-PSDrive -PSProvider FileSystem", captured[0])
        self.assertIn(self.tool.SIG.replace("\\", "\\\\"), captured[0])
        self.assertNotIn("add_argument(\"--drive\"", self.source)

    def test_active_log_uses_source_contract_not_fat_timestamp(self):
        logs = [
            {"name": "TH06PSP_BOOT.LOG", "bytes": 99, "mtime": "2099-01-01"},
            {"name": "TH07PSP_BOOT.LOG", "bytes": 42, "mtime": "2011-01-01"},
        ]
        self.assertEqual(self.tool.active_log_name(logs), "TH07PSP_BOOT.LOG")

    def test_active_log_refuses_ambiguous_casefold_match(self):
        logs = [
            {"name": "TH07PSP_BOOT.LOG", "bytes": 1, "mtime": "2011-01-01"},
            {"name": "th07psp_boot.log", "bytes": 1, "mtime": "2011-01-01"},
        ]
        self.assertIsNone(self.tool.active_log_name(logs))

    def test_powershell_failures_are_not_silently_accepted(self):
        failed = mock.Mock(returncode=1, stdout="", stderr="copy failed\r\n")
        with mock.patch.object(self.tool.subprocess, "run", return_value=failed):
            with self.assertRaisesRegex(RuntimeError, "copy failed"):
                self.tool.ps("Copy-Item invalid")

    def test_deploy_contract_backs_up_to_pc_before_target_copy(self):
        body_start = self.source.index("def cmd_deploy(")
        body_end = self.source.index("\ndef cmd_restore(", body_start)
        body = self.source[body_start:body_end]
        backup_copy = "Copy-Item -LiteralPath '{target}' -Destination '{backup_win}'"
        target_copy = "Copy-Item -LiteralPath '{src_win}' -Destination '{target}'"
        self.assertIn('"artifacts", "stick_backups"', body)
        self.assertLess(body.index(backup_copy), body.index(target_copy))
        self.assertLess(body.index("backup_sha != cur"), body.index(target_copy))
        self.assertIn("rb == src_sha", body)

    def test_rid22_restore_artifact_exists_and_matches_ledger(self):
        ledger = json.loads(
            (ROOT / "tools" / "psp_known_builds.json").read_text(
                encoding="utf-8"
            )
        )
        rid22 = ledger["RID22-METER"]
        artifact = pathlib.Path(rid22["pc_path"])
        self.assertTrue(artifact.is_file())
        digest = hashlib.sha256(artifact.read_bytes()).hexdigest().upper()
        self.assertEqual(digest, rid22["sha256"])


if __name__ == "__main__":
    unittest.main()
