#!/usr/bin/env python3
"""PSPメモステ操作の唯一の公式経路（コント根絶ツール）。

設計原則:
 1. ドライブ文字を引数に取らない。毎回全ドライブを走査し、
    内容署名 <X>:\\PSP\\GAME\\TH07SHIKI で同定する。決め打ちは構造的に不可能。
 2. 「見つからない」と言う時は必ず全走査表を出力する。根拠なき NO_D 禁止。
 3. ログ回収はルートの *.LOG を全列挙し、実行ファイル側のログ名契約と
    一致する現行ログだけを扱う。壊れたPSP時計/FAT時刻からは推測しない。
 4. deploy は 現行SHA照合(guard)→タイムスタンプ退避→copy→readback照合 を
    1コマンドで不可分に行う。手作業の工程抜けを構造的に排除。

使い方 (WSLから):
  python3 tools/psp_stick.py find                 # PSP同定（全走査表つき）
  python3 tools/psp_stick.py status [--app APP]   # 指定appのEBOOT SHA・台帳名・ログ一覧
  python3 tools/psp_stick.py replay-status        # 両appのリプレイ名・SHA一覧（読取のみ）
  python3 tools/psp_stick.py pull-log [--tag TAG] # 現行ログをartifactsへ読取専用回収
  python3 tools/psp_stick.py deploy FILE --expect SHA8 [--app TH07SHIKI_NOME] [--note TEXT]
  python3 tools/psp_stick.py install-replay FILE --expect SHA256 --app TH07SHIKI [--app TH07SHIKI_NOME]
  python3 tools/psp_stick.py install-font /mnt/c/.../msgothic-subset.ttf
  python3 tools/psp_stick.py restore NAME         # 台帳(known_builds.json)から復旧

Fable/Codex共通ルール: スティック操作はこのツール経由のみ。生のpowershell直叩き禁止。
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LEDGER = os.path.join(REPO, "tools", "psp_known_builds.json")
SIG = r"PSP\GAME\TH07SHIKI"
EBOOT = r"PSP\GAME\TH07SHIKI\EBOOT.PBP"
APP_ALLOWLIST = ("TH07SHIKI", "TH07SHIKI_NOME")
REPLAY_APP_ALLOWLIST = APP_ALLOWLIST
FONT_APP_ALLOWLIST = ("TH07SHIKI",)
FONT_BASENAME = "msgothic-subset.ttf"


class FontInstallError(RuntimeError):
    """A guarded local-font installation could not be completed safely."""


def app_eboot(app):
    if app not in APP_ALLOWLIST:
        raise ValueError(f"許可されていないPSPアプリ: {app}")
    return rf"PSP\GAME\{app}\EBOOT.PBP"


def ps(cmd):
    wrapped = "$ErrorActionPreference='Stop'; " + cmd
    r = subprocess.run(
        ["powershell.exe", "-NoProfile", "-Command", wrapped],
        capture_output=True, text=True, timeout=300)
    if r.returncode != 0:
        detail = (r.stderr or r.stdout).replace("\r", "").strip()
        raise RuntimeError(f"PowerShell失敗: {detail}")
    return r.stdout.replace("\r", "")


def scan():
    out = ps(
        "Get-PSDrive -PSProvider FileSystem | ForEach-Object {"
        " $L=$_.Name;"
        " $sig = Test-Path -LiteralPath ($L+':\\" + SIG.replace("\\", "\\\\") + "');"
        " '{0}|{1:N1}|{2:N1}|{3}' -f $L, ($_.Used/1GB), ($_.Free/1GB), $sig }")
    rows = []
    for line in out.strip().splitlines():
        parts = line.split("|")
        if len(parts) == 4:
            rows.append({"drive": parts[0], "usedGB": parts[1],
                         "freeGB": parts[2], "psp": parts[3] == "True"})
    return rows


def print_scan(rows):
    print("=== 全ドライブ走査 ===")
    for r in rows:
        mark = "  <-- PSP (TH07SHIKI署名)" if r["psp"] else ""
        print(f"  {r['drive']}: used={r['usedGB']}GB free={r['freeGB']}GB{mark}")


def find_psp(quiet=False):
    rows = scan()
    hits = [r for r in rows if r["psp"]]
    if not quiet or not hits:
        print_scan(rows)
    if not hits:
        print("RESULT: PSP未検出（上の走査表が根拠。これを添えずに『見えない』と報告してはならない）")
        sys.exit(3)
    if len(hits) > 1:
        print("RESULT: 署名一致が複数。手動確認要:", [r["drive"] for r in hits])
        sys.exit(4)
    d = hits[0]["drive"]
    print(f"RESULT: PSP={d}: (free {hits[0]['freeGB']}GB)")
    return d


def sha256_win(path_win):
    out = ps(f"(Get-FileHash -Algorithm SHA256 -LiteralPath '{path_win}').Hash")
    return out.strip().upper()


def sha256_local(path):
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def _path_is_within(path, directory):
    """Return true when *path* resolves inside *directory*, including symlinks."""
    try:
        return os.path.commonpath((os.path.realpath(path),
                                   os.path.realpath(directory))) == \
            os.path.realpath(directory)
    except ValueError:
        return False


def validate_font_source(path):
    """Validate the private, repo-external local-font artifact."""
    source = os.path.realpath(os.path.abspath(path))
    if os.path.basename(source) != FONT_BASENAME:
        raise FontInstallError(
            f"入力ファイル名は {FONT_BASENAME} に固定すること")
    if not os.path.isfile(source):
        raise FontInstallError(f"フォント投入元が存在しない: {source}")
    if _path_is_within(source, REPO):
        raise FontInstallError(
            "フォント成果物はrepo内から投入できない"
            "（repo外で生成すること）")
    return source


def _wsl_windows_path(path):
    """Convert an absolute /mnt/<drive>/ path without invoking PowerShell."""
    match = re.fullmatch(r"/mnt/([A-Za-z])(?:/(.*))?", os.path.abspath(path))
    if not match:
        raise FontInstallError(
            "PowerShellから読める /mnt/<drive>/... のフォントを指定すること")
    tail = (match.group(2) or "").replace("/", "\\")
    return f"{match.group(1).upper()}:\\{tail}"


def _ps_literal(value):
    """Quote one PowerShell literal path (single quotes are doubled)."""
    return "'" + str(value).replace("'", "''") + "'"


def install_font_transaction(source, target, temp, source_sha,
                             backup_for_sha, *, exists, copy_file,
                             digest, atomic_replace, remove_file):
    """Install one font through a verified temporary file.

    Paths are opaque to this routine.  Production supplies PowerShell-backed
    operations; tests supply ordinary filesystem operations over a fake stick.
    The old target is copied to a verified timestamped sibling before the
    same-directory temp file is atomically renamed into place.
    """
    expected = source_sha.upper()
    if not re.fullmatch(r"[0-9A-F]{64}", expected):
        raise FontInstallError("投入元SHA-256が不正")
    if exists(temp):
        raise FontInstallError(f"一時パスが既に存在する: {temp}")

    backup = None
    old_sha = None
    if exists(target):
        old_sha = digest(target).upper()
        backup = backup_for_sha(old_sha)
        if exists(backup):
            raise FontInstallError(f"退避先が既に存在する: {backup}")
        copy_file(target, backup)
        if digest(backup).upper() != old_sha:
            if exists(backup):
                remove_file(backup)
            raise FontInstallError("旧フォントの退避SHA不一致")

    copy_file(source, temp)
    if digest(temp).upper() != expected:
        if exists(temp):
            remove_file(temp)
        raise FontInstallError("一時copy SHA不一致")

    try:
        atomic_replace(temp, target)
    except Exception as exc:
        if exists(temp):
            remove_file(temp)
        raise FontInstallError(f"最終rename失敗: {exc}") from exc

    readback = digest(target).upper()
    if readback != expected:
        raise FontInstallError("最終readback SHA不一致")
    return {
        "source_sha": expected,
        "readback_sha": readback,
        "old_sha": old_sha,
        "backup": backup,
    }


def load_ledger():
    if os.path.exists(LEDGER):
        with open(LEDGER, encoding="utf-8") as f:
            return json.load(f)
    return {}


def ledger_name(sha):
    for name, e in load_ledger().items():
        if isinstance(e, dict) and e.get("sha256", "").upper() == sha:
            return name
    return "UNKNOWN(台帳未登録)"


def list_logs(d):
    out = ps(
        f"Get-ChildItem -LiteralPath '{d}:\\' -File -Filter '*.LOG' | "
        "Sort-Object LastWriteTime -Descending | ForEach-Object {"
        " '{0}|{1}|{2}' -f $_.Name, $_.Length,"
        " $_.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss') }")
    logs = []
    for line in out.strip().splitlines():
        p = line.split("|")
        if len(p) == 3:
            logs.append({"name": p[0], "bytes": int(p[1]), "mtime": p[2]})
    return logs


def list_replays(d, app):
    """List exact replay files and hashes for one allowlisted app.

    This is deliberately read-only: the PowerShell fragment contains only
    Test-Path, Get-ChildItem and Get-FileHash.  JSON is used so an unexpected
    filename cannot corrupt delimiter-based parsing.
    """
    app_eboot(app)  # validate against the same fixed allowlist as deploy
    app_dir = f"{d}:\\PSP\\GAME\\{app}"
    replay_dir = f"{app_dir}\\replay"
    out = ps(
        f"$appExists=Test-Path -LiteralPath '{app_dir}';"
        f" $replayExists=Test-Path -LiteralPath '{replay_dir}';"
        " $items=@();"
        " if ($replayExists) {"
        f"  $items=@(Get-ChildItem -LiteralPath '{replay_dir}' -File -Filter '*.rpy' |"
        "   Sort-Object Name | ForEach-Object {"
        "    [PSCustomObject]@{name=$_.Name; bytes=[int64]$_.Length;"
        "     sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash}"
        "   })"
        " };"
        " [PSCustomObject]@{appExists=$appExists;"
        "  replayDirExists=$replayExists; items=@($items)} |"
        " ConvertTo-Json -Depth 3 -Compress")
    result = json.loads(out)
    items = result.get("items") or []
    if isinstance(items, dict):
        items = [items]
    return {
        "appExists": bool(result.get("appExists")),
        "replayDirExists": bool(result.get("replayDirExists")),
        "items": items,
    }


def active_log_name(logs):
    """Derive the live log name from the program's source contract.

    PSP clocks and archived logs make FAT timestamps unsuitable for selecting
    the current log.  Do not guess: read the path used by the program and then
    require exactly one enumerated root log to match it.
    """
    source = os.path.join(REPO, "psp", "fileio.cpp")
    try:
        with open(source, encoding="utf-8") as f:
            text = f.read()
    except OSError:
        return None
    match = re.search(r'gBootLog\[[^]]+\]\s*=\s*"(?:ms0:/)?([^"/]+\.LOG)"',
                      text, re.IGNORECASE)
    if not match:
        return None
    wanted = match.group(1).casefold()
    hits = [entry["name"] for entry in logs
            if entry["name"].casefold() == wanted]
    return hits[0] if len(hits) == 1 else None


def cmd_status(args):
    d = find_psp()
    app = getattr(args, "app", None) or "TH07SHIKI"
    sha = sha256_win(f"{d}:\\{app_eboot(app)}")
    print(f"EBOOT.PBP ({app}) SHA256 = {sha}")
    print(f"台帳同定: {ledger_name(sha)}")
    logs = list_logs(d)
    active = active_log_name(logs)
    print("=== ルートの.LOG（FAT時刻順。PSP時計は選択根拠にしない）===")
    for l in logs:
        mark = "  <-- ACTIVE（ソース契約一致）" if l["name"] == active else ""
        print(f"  {l['name']}  {l['bytes']}B  {l['mtime']}{mark}")


def cmd_replay_status(_args):
    """Report replay identities for both A/B apps without changing the stick."""
    d = find_psp()
    print("◆◆◆ リプレイ一覧（読取のみ／推測なし） ◆◆◆")
    for app in APP_ALLOWLIST:
        report = list_replays(d, app)
        print(f"[{app}]")
        if not report["appExists"]:
            print("  APPディレクトリなし")
            continue
        if not report["replayDirExists"]:
            print("  replayディレクトリなし")
            continue
        if not report["items"]:
            print("  .rpyなし")
            continue
        for item in report["items"]:
            print(f"  {item['name']}  {int(item['bytes'])}B  SHA256={item['sha256'].upper()}")
    print("（読み取りのみ。スティック側は無変更）")


def cmd_pull_log(args):
    d = find_psp()
    logs = list_logs(d)
    if not logs:
        print("RESULT: ルートに.LOGなし")
        sys.exit(3)
    active = active_log_name(logs)
    if not active:
        print("RESULT: 現行ログをソース契約から一意同定できない。時刻では推測しない。")
        for entry in logs:
            print(f"  {entry['name']}  {entry['bytes']}B  {entry['mtime']}")
        sys.exit(7)
    newest = next(entry for entry in logs if entry["name"] == active)
    ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    tag = args.tag or "PULL"
    dest_rel = f"artifacts/TH07PSP_BOOT.{tag}.{ts}.LOG"
    dest_win = "C:" + REPO.replace("/mnt/c", "").replace("/", "\\") + \
        "\\" + dest_rel.replace("/", "\\")
    ps(f"Copy-Item -LiteralPath '{d}:\\{newest['name']}' -Destination '{dest_win}'")
    sha = sha256_win(dest_win)
    print(f"回収: {newest['name']} ({newest['bytes']}B, {newest['mtime']})")
    print(f"  -> {dest_rel}")
    print(f"  SHA256 = {sha}")
    print("（読み取りのみ。スティック側は無変更）")


def cmd_deploy(args, src=None, note=None):
    src = src or args.file
    if not os.path.exists(src):
        print(f"ERROR: 投入元が存在しない: {src}")
        sys.exit(2)
    src_sha = subprocess.run(["sha256sum", src], capture_output=True,
                             text=True).stdout.split()[0].upper()
    d = find_psp()
    app = getattr(args, "app", None) or "TH07SHIKI"
    target = f"{d}:\\{app_eboot(app)}"
    cur = sha256_win(target)
    print(f"現行EBOOT ({app}) = {cur[:16]}… ({ledger_name(cur)})")
    expect = (getattr(args, "expect", None) or "").upper()
    if expect and not cur.startswith(expect):
        print(f"ABORT: 現行SHAが--expect({expect})と不一致。並行デプロイの疑い。")
        sys.exit(5)
    ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_dir = os.path.join(REPO, "artifacts", "stick_backups")
    os.makedirs(backup_dir, exist_ok=True)
    backup_name = f"EBOOT.{app}.PRE-{ts}.{cur[:8]}.PBP"
    backup_path = os.path.join(backup_dir, backup_name)
    backup_win = "C:" + backup_path.replace("/mnt/c", "").replace("/", "\\")
    src_win = "C:" + os.path.abspath(src).replace("/mnt/c", "").replace("/", "\\")
    ps(f"Copy-Item -LiteralPath '{target}' -Destination '{backup_win}' -Force")
    backup_sha = sha256_win(backup_win)
    if backup_sha != cur:
        print(f"ABORT: PC退避SHA不一致: {backup_sha} / {cur}")
        sys.exit(8)
    ps(f"Copy-Item -LiteralPath '{src_win}' -Destination '{target}' -Force")
    rb = sha256_win(target)
    ok = rb == src_sha
    print(f"PC退避: artifacts/stick_backups/{backup_name} ({backup_sha[:16]}…)")
    print(f"投入: {app} <- {os.path.basename(src)}")
    print(f"readback = {rb[:16]}… / 期待 = {src_sha[:16]}… : "
          f"{'一致 OK' if ok else '不一致 ***FAIL***'}")
    if note:
        print(f"内容: {note}")
    print("報告書式: PC実装=済 / メモステ=投入済(readback一致) / 実機=未確認")
    sys.exit(0 if ok else 6)


def cmd_restore(args):
    led = load_ledger()
    e = led.get(args.name)
    if not e:
        print(f"ERROR: 台帳に'{args.name}'なし。登録済み: {list(led)}")
        sys.exit(2)
    class A:
        expect = None
        app = "TH07SHIKI"
    print(f"台帳復旧: {args.name} <- {e['pc_path']}")
    cmd_deploy(A(), src=e["pc_path"], note=e.get("note", ""))


def cmd_install_replay(args):
    """Install one explicitly identified replay beside selected EBOOTs.

    This deliberately does not accept globs or copy a replay library.  The
    caller must provide the exact source hash and every destination app.  An
    existing same-name replay is backed up to the PC before replacement, and
    a temporary on-stick copy is hash-checked before the final rename.
    """
    src = os.path.abspath(args.file)
    if not os.path.isfile(src):
        print(f"ERROR: リプレイ投入元が存在しない: {src}")
        sys.exit(2)

    name = os.path.basename(src)
    if (name != args.name if args.name else False):
        print("ERROR: --nameはパスを含まない投入ファイル名と一致させること")
        sys.exit(2)
    if args.name:
        name = args.name
    if not re.fullmatch(r"[A-Za-z0-9_.-]+\.rpy", name, re.IGNORECASE):
        print(f"ERROR: 安全でないリプレイ名: {name}")
        sys.exit(2)

    expected = args.expect.upper()
    if not re.fullmatch(r"[0-9A-F]{64}", expected):
        print("ERROR: --expectにはSHA-256全64桁が必要")
        sys.exit(2)
    src_sha = subprocess.run(
        ["sha256sum", src], capture_output=True, text=True, check=True
    ).stdout.split()[0].upper()
    if src_sha != expected:
        print(f"ABORT: 投入元SHA不一致: {src_sha} / 期待 {expected}")
        sys.exit(5)

    apps = args.app
    if not apps:
        print("ERROR: --appを1つ以上明示すること")
        sys.exit(2)
    if len(set(apps)) != len(apps):
        print("ERROR: --appの重複指定")
        sys.exit(2)
    bad_apps = [app for app in apps if app not in REPLAY_APP_ALLOWLIST]
    if bad_apps:
        print(f"ERROR: 許可されていないapp: {bad_apps}")
        sys.exit(2)

    d = find_psp()
    missing = []
    for app in apps:
        exists = ps(
            f"Test-Path -LiteralPath '{d}:\\PSP\\GAME\\{app}'"
        ).strip()
        if exists != "True":
            missing.append(app)
    if missing:
        print(f"ABORT: 対象appが存在しない: {missing}（何も書き込んでいない）")
        sys.exit(5)

    src_win = "C:" + src.replace("/mnt/c", "").replace("/", "\\")
    ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_dir = os.path.join(REPO, "artifacts", "stick_backups", "replay")
    os.makedirs(backup_dir, exist_ok=True)

    results = []
    for app in apps:
        replay_dir = f"{d}:\\PSP\\GAME\\{app}\\replay"
        target = f"{replay_dir}\\{name}"
        temp = f"{target}.NEW"
        ps(f"New-Item -ItemType Directory -Force -Path '{replay_dir}' | Out-Null")

        exists = ps(f"Test-Path -LiteralPath '{target}'").strip() == "True"
        backup_rel = None
        if exists:
            old_sha = sha256_win(target)
            backup_name = f"{app}.{name}.{ts}.{old_sha[:8]}.rpy"
            backup_path = os.path.join(backup_dir, backup_name)
            backup_win = "C:" + backup_path.replace("/mnt/c", "").replace("/", "\\")
            ps(f"Copy-Item -LiteralPath '{target}' -Destination '{backup_win}' -Force")
            if sha256_win(backup_win) != old_sha:
                print(f"ABORT: 既存リプレイのPC退避SHA不一致: {app}/{name}")
                sys.exit(8)
            backup_rel = os.path.relpath(backup_path, REPO)

        ps(f"Copy-Item -LiteralPath '{src_win}' -Destination '{temp}' -Force")
        temp_sha = sha256_win(temp)
        if temp_sha != src_sha:
            ps(f"Remove-Item -LiteralPath '{temp}' -Force -ErrorAction SilentlyContinue")
            print(f"ABORT: 一時copy SHA不一致: {app}/{name}")
            sys.exit(6)
        ps(f"Move-Item -LiteralPath '{temp}' -Destination '{target}' -Force")
        readback = sha256_win(target)
        if readback != src_sha:
            print(f"ABORT: 最終readback SHA不一致: {app}/{name}")
            sys.exit(6)
        results.append((app, readback, backup_rel))

    print(f"投入元: {name} ({os.path.getsize(src)}B)")
    print(f"SHA256 = {src_sha}")
    for app, readback, backup_rel in results:
        suffix = f" / 旧版退避={backup_rel}" if backup_rel else " / 旧版なし"
        print(f"投入: {app}\\replay\\{name} readback={readback} 一致 OK{suffix}")
    print("報告書式: PC検証=済 / メモステ=リプレイ投入済(readback一致) / 実機メニュー確認=未")


def cmd_install_font(args):
    """Install the private local subset font beside the main EBOOT only."""
    try:
        source = validate_font_source(args.file)
        source_win = _wsl_windows_path(source)
        source_sha = sha256_local(source)
    except (FontInstallError, OSError) as exc:
        print(f"ERROR: {exc}")
        sys.exit(2)

    app = getattr(args, "app", None) or "TH07SHIKI"
    if app not in FONT_APP_ALLOWLIST:
        print(f"ERROR: フォント投入先はTH07SHIKIのみ: {app}")
        sys.exit(2)

    # Source validation deliberately precedes drive discovery: an unsafe font
    # path must not cause even a read-only USB scan, much less a write.
    drive = find_psp()
    app_dir = f"{drive}:\\PSP\\GAME\\{app}"
    if ps(f"Test-Path -LiteralPath {_ps_literal(app_dir)} -PathType Container").strip() != "True":
        print(f"ABORT: 対象appが存在しない: {app}（何も書き込んでいない）")
        sys.exit(5)

    token = datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    target = f"{app_dir}\\{FONT_BASENAME}"
    temp = f"{target}.NEW-{token}"

    def win_exists(path):
        return ps(f"Test-Path -LiteralPath {_ps_literal(path)} -PathType Leaf").strip() == "True"

    def win_copy(source_path, destination_path):
        ps("Copy-Item -LiteralPath " + _ps_literal(source_path) +
           " -Destination " + _ps_literal(destination_path))

    def win_remove(path):
        ps("Remove-Item -LiteralPath " + _ps_literal(path) +
           " -Force -ErrorAction SilentlyContinue")

    def win_atomic_replace(source_path, destination_path):
        # Both names are siblings on the Memory Stick.  Move-Item performs the
        # final same-volume rename only after the temporary copy's SHA matches.
        ps("Move-Item -LiteralPath " + _ps_literal(source_path) +
           " -Destination " + _ps_literal(destination_path) + " -Force")

    def backup_for_sha(old_sha):
        return (f"{app_dir}\\msgothic-subset.PRE-{token}."
                f"{old_sha[:8]}.ttf")

    try:
        result = install_font_transaction(
            source_win, target, temp, source_sha, backup_for_sha,
            exists=win_exists,
            copy_file=win_copy,
            digest=sha256_win,
            atomic_replace=win_atomic_replace,
            remove_file=win_remove)
    except (FontInstallError, RuntimeError) as exc:
        print(f"ABORT: {exc}")
        sys.exit(6)

    backup = result["backup"]
    backup_note = (f" / 旧版退避={backup}" if backup else " / 旧版なし")
    print(f"投入元: {source} ({os.path.getsize(source)}B)")
    print(f"SHA256 = {source_sha}")
    print(f"投入: {app}\\{FONT_BASENAME} "
          f"readback={result['readback_sha']} 一致 OK{backup_note}")
    print("報告書式: PC検証=済 / メモステ=フォント投入済(readback一致) / "
          "実機フォント選択確認=未")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("find")
    p = sub.add_parser("status")
    p.add_argument("--app", choices=APP_ALLOWLIST, default="TH07SHIKI")
    sub.add_parser("replay-status")
    p = sub.add_parser("pull-log")
    p.add_argument("--tag")
    p = sub.add_parser("deploy")
    p.add_argument("file")
    p.add_argument("--expect")
    p.add_argument("--app", choices=APP_ALLOWLIST, default="TH07SHIKI")
    p.add_argument("--note")
    p = sub.add_parser("restore")
    p.add_argument("name")
    p = sub.add_parser("install-replay")
    p.add_argument("file")
    p.add_argument("--expect", required=True)
    p.add_argument("--app", action="append", choices=REPLAY_APP_ALLOWLIST)
    p.add_argument("--name")
    p = sub.add_parser("install-font")
    p.add_argument("file")
    p.add_argument("--app", choices=FONT_APP_ALLOWLIST, default="TH07SHIKI")
    args = ap.parse_args()
    if args.cmd == "find":
        find_psp()
    elif args.cmd == "status":
        cmd_status(args)
    elif args.cmd == "replay-status":
        cmd_replay_status(args)
    elif args.cmd == "pull-log":
        cmd_pull_log(args)
    elif args.cmd == "deploy":
        cmd_deploy(args)
    elif args.cmd == "restore":
        cmd_restore(args)
    elif args.cmd == "install-replay":
        cmd_install_replay(args)
    elif args.cmd == "install-font":
        cmd_install_font(args)


if __name__ == "__main__":
    main()
