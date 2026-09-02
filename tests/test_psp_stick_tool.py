import hashlib
import importlib.util
import json
import pathlib
import shutil
import tempfile
import unittest
from unittest import mock
from contextlib import redirect_stdout
import io


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
        self.assertIn("app_eboot(app)", body)
        self.assertIn('or "TH07SHIKI"', body)

    def test_deploy_app_is_allowlisted_and_cli_defaults_to_main(self):
        self.assertEqual(
            self.tool.app_eboot("TH07SHIKI_NOME"),
            r"PSP\GAME\TH07SHIKI_NOME\EBOOT.PBP",
        )
        with self.assertRaises(ValueError):
            self.tool.app_eboot("TH07SHIKI_EVIL")
        self.assertIn(
            'p.add_argument("--app", choices=APP_ALLOWLIST, default="TH07SHIKI")',
            self.source,
        )

    def test_deploy_full_main_hash_disambiguates_multiple_devices(self):
        complete = "A" * 64
        with mock.patch.object(
                self.tool, "find_psp_by_eboot", return_value="D") as by_hash, \
                mock.patch.object(self.tool, "find_psp") as unqualified:
            self.assertEqual(
                self.tool.select_psp_for_deploy("TH07SHIKI", complete), "D"
            )
        by_hash.assert_called_once_with(complete)
        unqualified.assert_not_called()

        with mock.patch.object(self.tool, "find_psp", return_value="H") as scan, \
                mock.patch.object(self.tool, "find_psp_by_eboot") as by_hash:
            self.assertEqual(
                self.tool.select_psp_for_deploy("TH07SHIKI", "A5C0"), "H"
            )
            self.assertEqual(
                self.tool.select_psp_for_deploy("TH07SHIKI_NOME", complete),
                "H",
            )
        self.assertEqual(scan.call_count, 2)
        by_hash.assert_not_called()

    def test_alt_app_install_is_fixed_guarded_and_staged(self):
        body_start = self.source.index("def cmd_install_alt_app(")
        body_end = self.source.index("\ndef cmd_deploy(", body_start)
        body = self.source[body_start:body_end]
        self.assertEqual(self.tool.ALT_APP_SOURCE, "TH07SHIKI")
        self.assertEqual(self.tool.ALT_APP_TARGET, "TH07SHIKI_NOME")
        self.assertEqual(self.tool.ALT_APP_REPLAY_BASENAME,
                         "th7_udLUNA.rpy")
        self.assertIn("find_psp_by_pair(expected[\"main EBOOT\"]", body)
        self.assertIn("全SHA guardは64桁必須", body)
        self.assertIn("Test-Path -LiteralPath", body)
        self.assertIn("main EBOOT/wrapper/replay SHA guard不一致", body)
        self.assertIn("source_manifest", body)
        self.assertIn("alternate_resource_manifest", body)
        self.assertIn("free_bytes", body)
        clone = '"Copy-Item -LiteralPath " + _ps_literal(source_dir)'
        replace = '"Copy-Item -LiteralPath " + _ps_literal(wrapper_source_win)'
        commit = '"Move-Item -LiteralPath " + _ps_literal(temp_dir)'
        self.assertLess(body.index(clone), body.index(replace))
        self.assertLess(body.index(replace), body.index(commit))
        self.assertIn("sha256_win(temp_replay)", body)
        self.assertIn("sha256_win(target_replay)", body)
        self.assertIn("main app changed during alternate install", body)
        self.assertIn("Remove-Item -LiteralPath", body)
        self.assertNotIn("-Destination " + '" + _ps_literal(source_dir)', body)

    def test_alt_app_cli_requires_all_complete_hash_guards(self):
        self.assertIn('sub.add_parser("install-alt-app")', self.source)
        for option in (
                "--expect-main-eboot", "--expect-main-wrapper",
                "--expect-new-eboot", "--expect-new-wrapper",
                "--expect-replay"):
            self.assertIn(f'p.add_argument("{option}", required=True)',
                          self.source)

    def test_status_reads_selected_allowlisted_app(self):
        args = mock.Mock(app="TH07SHIKI_NOME")
        output = io.StringIO()
        with mock.patch.object(self.tool, "find_psp", return_value="D"), \
                mock.patch.object(self.tool, "sha256_win",
                                  return_value="A" * 64) as sha, \
                mock.patch.object(self.tool, "ledger_name",
                                  return_value="AB-SC"), \
                mock.patch.object(self.tool, "list_logs", return_value=[]), \
                redirect_stdout(output):
            self.tool.cmd_status(args)
        sha.assert_called_once_with(
            r"D:\PSP\GAME\TH07SHIKI_NOME\EBOOT.PBP"
        )
        self.assertIn("EBOOT.PBP (TH07SHIKI_NOME)", output.getvalue())
        self.assertIn(
            'p.add_argument("--app", choices=APP_ALLOWLIST, default="TH07SHIKI")',
            self.source,
        )

    def test_list_replays_is_read_only_and_parses_json(self):
        payload = json.dumps({
            "appExists": True,
            "replayDirExists": True,
            "items": [{
                "name": "th7_udAB.rpy",
                "bytes": 1234,
                "sha256": "b" * 64,
            }],
        })
        with mock.patch.object(self.tool, "ps", return_value=payload) as ps:
            result = self.tool.list_replays("D", "TH07SHIKI")
        command = ps.call_args.args[0]
        self.assertIn("Get-ChildItem", command)
        self.assertIn("Get-FileHash -Algorithm SHA256", command)
        for mutator in ("Copy-Item", "Move-Item", "Remove-Item", "New-Item"):
            self.assertNotIn(mutator, command)
        self.assertEqual(result["items"][0]["name"], "th7_udAB.rpy")
        self.assertEqual(result["items"][0]["sha256"], "b" * 64)

    def test_replay_status_always_lists_both_allowlisted_apps(self):
        calls = []

        def fake_list(_drive, app):
            calls.append(app)
            return {
                "appExists": True,
                "replayDirExists": True,
                "items": [{
                    "name": f"{app}.rpy",
                    "bytes": 7,
                    "sha256": "c" * 64,
                }],
            }

        output = io.StringIO()
        with mock.patch.object(self.tool, "find_psp", return_value="D"), \
                mock.patch.object(self.tool, "list_replays",
                                  side_effect=fake_list), \
                redirect_stdout(output):
            self.tool.cmd_replay_status(mock.Mock())
        self.assertEqual(calls, list(self.tool.APP_ALLOWLIST))
        self.assertIn("TH07SHIKI.rpy", output.getvalue())
        self.assertIn("TH07SHIKI_NOME.rpy", output.getvalue())
        self.assertIn("読み取りのみ", output.getvalue())
        self.assertIn('sub.add_parser("replay-status")', self.source)

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

    def test_replay_install_is_exact_file_only_and_hash_guarded(self):
        body_start = self.source.index("def cmd_install_replay(")
        body_end = self.source.index("\ndef main(", body_start)
        body = self.source[body_start:body_end]
        self.assertIn("[0-9A-F]{64}", body)
        self.assertIn("src_sha != expected", body)
        self.assertNotIn("*", body)
        self.assertIn("REPLAY_APP_ALLOWLIST", body)
        self.assertIn(".NEW", body)
        self.assertLess(body.index("temp_sha = sha256_win(temp)"),
                        body.index("Move-Item -LiteralPath '{temp}'"))
        self.assertIn("readback != src_sha", body)

    def test_replay_install_cli_requires_hash_and_explicit_app(self):
        self.assertIn('sub.add_parser("install-replay")', self.source)
        self.assertIn('p.add_argument("--expect", required=True)', self.source)
        self.assertIn('p.add_argument("--app", action="append"', self.source)

    @staticmethod
    def _local_sha(path):
        return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest().upper()

    def test_font_transaction_replaces_through_verified_temp_and_backs_up(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            source_dir = root / "private-source"
            app_dir = root / "fake-stick" / "PSP" / "GAME" / "TH07SHIKI"
            source_dir.mkdir()
            app_dir.mkdir(parents=True)
            source = source_dir / self.tool.FONT_BASENAME
            target = app_dir / self.tool.FONT_BASENAME
            temp = app_dir / (self.tool.FONT_BASENAME + ".NEW-test")
            source.write_bytes(b"new-private-font")
            target.write_bytes(b"old-private-font")
            old_sha = self._local_sha(target)

            def backup_for_sha(digest):
                return app_dir / (
                    f"msgothic-subset.PRE-20260901-120000.{digest[:8]}.ttf"
                )

            result = self.tool.install_font_transaction(
                source, target, temp, self._local_sha(source), backup_for_sha,
                exists=lambda path: pathlib.Path(path).exists(),
                copy_file=shutil.copyfile,
                digest=self._local_sha,
                atomic_replace=lambda src, dst: pathlib.Path(src).replace(dst),
                remove_file=lambda path: pathlib.Path(path).unlink())

            self.assertEqual(target.read_bytes(), b"new-private-font")
            self.assertFalse(temp.exists())
            backup = pathlib.Path(result["backup"])
            self.assertEqual(backup.parent, app_dir)
            self.assertEqual(backup.read_bytes(), b"old-private-font")
            self.assertEqual(result["old_sha"], old_sha)
            self.assertEqual(result["readback_sha"], self._local_sha(source))

    def test_font_transaction_new_install_needs_no_backup(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            source = root / "source" / self.tool.FONT_BASENAME
            target = root / "fake-stick" / "PSP" / "GAME" / \
                "TH07SHIKI" / self.tool.FONT_BASENAME
            source.parent.mkdir()
            target.parent.mkdir(parents=True)
            source.write_bytes(b"first-private-font")
            temp = pathlib.Path(str(target) + ".NEW-test")

            result = self.tool.install_font_transaction(
                source, target, temp, self._local_sha(source),
                lambda digest: target.with_name(
                    f"msgothic-subset.PRE-test.{digest[:8]}.ttf"),
                exists=lambda path: pathlib.Path(path).exists(),
                copy_file=shutil.copyfile,
                digest=self._local_sha,
                atomic_replace=lambda src, dst: pathlib.Path(src).replace(dst),
                remove_file=lambda path: pathlib.Path(path).unlink())

            self.assertIsNone(result["backup"])
            self.assertEqual(target.read_bytes(), source.read_bytes())

    def test_font_transaction_bad_temp_sha_preserves_old_target(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            source = root / "source" / self.tool.FONT_BASENAME
            target = root / "fake-stick" / "PSP" / "GAME" / \
                "TH07SHIKI" / self.tool.FONT_BASENAME
            source.parent.mkdir()
            target.parent.mkdir(parents=True)
            source.write_bytes(b"new-private-font")
            target.write_bytes(b"old-private-font")
            temp = pathlib.Path(str(target) + ".NEW-test")

            def corrupting_copy(src, dst):
                shutil.copyfile(src, dst)
                if pathlib.Path(dst) == temp:
                    pathlib.Path(dst).write_bytes(b"corrupt")

            with self.assertRaisesRegex(self.tool.FontInstallError,
                                        "一時copy SHA不一致"):
                self.tool.install_font_transaction(
                    source, target, temp, self._local_sha(source),
                    lambda digest: target.with_name(
                        f"msgothic-subset.PRE-test.{digest[:8]}.ttf"),
                    exists=lambda path: pathlib.Path(path).exists(),
                    copy_file=corrupting_copy,
                    digest=self._local_sha,
                    atomic_replace=lambda src, dst: pathlib.Path(src).replace(dst),
                    remove_file=lambda path: pathlib.Path(path).unlink())

            self.assertEqual(target.read_bytes(), b"old-private-font")
            self.assertFalse(temp.exists())

    def test_font_source_policy_rejects_repo_and_wrong_basename(self):
        repo_font = ROOT / "private" / self.tool.FONT_BASENAME
        with mock.patch.object(self.tool.os.path, "isfile", return_value=True):
            with self.assertRaisesRegex(self.tool.FontInstallError, "repo内"):
                self.tool.validate_font_source(str(repo_font))
        with tempfile.TemporaryDirectory() as tmp:
            wrong = pathlib.Path(tmp) / "other.ttf"
            wrong.write_bytes(b"not-a-font")
            with self.assertRaisesRegex(self.tool.FontInstallError,
                                        self.tool.FONT_BASENAME):
                self.tool.validate_font_source(str(wrong))

    def test_font_install_is_main_app_only_and_uses_signature_scan(self):
        body_start = self.source.index("def cmd_install_font(")
        body_end = self.source.index("\ndef main(", body_start)
        body = self.source[body_start:body_end]
        self.assertEqual(self.tool.FONT_APP_ALLOWLIST, ("TH07SHIKI",))
        self.assertIn("drive = find_psp()", body)
        self.assertNotIn("TH07SHIKI_NOME", body)
        self.assertIn("msgothic-subset.PRE-", body)
        self.assertIn("atomic_replace=win_atomic_replace", body)
        self.assertIn('sub.add_parser("install-font")', self.source)
        self.assertIn(
            'p.add_argument("--app", choices=FONT_APP_ALLOWLIST, default="TH07SHIKI")',
            self.source,
        )

    def test_unsafe_font_source_stops_before_drive_scan(self):
        args = mock.Mock(file=str(ROOT / "private" /
                                  self.tool.FONT_BASENAME),
                         app="TH07SHIKI")
        with mock.patch.object(self.tool.os.path, "isfile", return_value=True), \
                mock.patch.object(self.tool, "find_psp") as find_psp, \
                redirect_stdout(io.StringIO()), \
                self.assertRaises(SystemExit) as raised:
            self.tool.cmd_install_font(args)
        self.assertEqual(raised.exception.code, 2)
        find_psp.assert_not_called()


if __name__ == "__main__":
    unittest.main()
