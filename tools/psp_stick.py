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
  python3 tools/psp_stick.py status               # EBOOT SHA・台帳名・ログ一覧
  python3 tools/psp_stick.py pull-log [--tag TAG] # 現行ログをartifactsへ読取専用回収
  python3 tools/psp_stick.py deploy FILE --expect SHA8 [--note TEXT]
  python3 tools/psp_stick.py restore NAME         # 台帳(known_builds.json)から復旧

Fable/Codex共通ルール: スティック操作はこのツール経由のみ。生のpowershell直叩き禁止。
"""
import argparse
import datetime
import json
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LEDGER = os.path.join(REPO, "tools", "psp_known_builds.json")
SIG = r"PSP\GAME\TH07SHIKI"
EBOOT = r"PSP\GAME\TH07SHIKI\EBOOT.PBP"


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


def cmd_status(_args):
    d = find_psp()
    sha = sha256_win(f"{d}:\\{EBOOT}")
    print(f"EBOOT.PBP SHA256 = {sha}")
    print(f"台帳同定: {ledger_name(sha)}")
    logs = list_logs(d)
    active = active_log_name(logs)
    print("=== ルートの.LOG（FAT時刻順。PSP時計は選択根拠にしない）===")
    for l in logs:
        mark = "  <-- ACTIVE（ソース契約一致）" if l["name"] == active else ""
        print(f"  {l['name']}  {l['bytes']}B  {l['mtime']}{mark}")


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
    target = f"{d}:\\{EBOOT}"
    cur = sha256_win(target)
    print(f"現行EBOOT = {cur[:16]}… ({ledger_name(cur)})")
    expect = (getattr(args, "expect", None) or "").upper()
    if expect and not cur.startswith(expect):
        print(f"ABORT: 現行SHAが--expect({expect})と不一致。並行デプロイの疑い。")
        sys.exit(5)
    ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    backup_dir = os.path.join(REPO, "artifacts", "stick_backups")
    os.makedirs(backup_dir, exist_ok=True)
    backup_name = f"EBOOT.PRE-{ts}.{cur[:8]}.PBP"
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
    print(f"投入: {os.path.basename(src)}")
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
    print(f"台帳復旧: {args.name} <- {e['pc_path']}")
    cmd_deploy(A(), src=e["pc_path"], note=e.get("note", ""))


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("find")
    sub.add_parser("status")
    p = sub.add_parser("pull-log")
    p.add_argument("--tag")
    p = sub.add_parser("deploy")
    p.add_argument("file")
    p.add_argument("--expect")
    p.add_argument("--note")
    p = sub.add_parser("restore")
    p.add_argument("name")
    args = ap.parse_args()
    if args.cmd == "find":
        find_psp()
    elif args.cmd == "status":
        cmd_status(args)
    elif args.cmd == "pull-log":
        cmd_pull_log(args)
    elif args.cmd == "deploy":
        cmd_deploy(args)
    elif args.cmd == "restore":
        cmd_restore(args)


if __name__ == "__main__":
    main()
