#!/usr/bin/env python3
"""git コミット前ガード — 原作データ・生成アセットの混入を検出して拒否する。

検出対象（このパイプラインが生成・使用する全アーティファクト種別）:
  1. PBP で ICON0/PIC1/SND0 スロットが非空のもの（XMB化EBOOT）
  2. THTX マジックを含むファイル（ANMテクスチャ抜き出し）
  3. RIFF/WAVE で ATRAC3 (format tag 0x0270) のもの（SND0.AT3）
  4. PBG4 マジック（th07.dat そのもの）
  5. 生 thbgm.dat（先頭16バイト目からの PCM、サイズ 400MB 超で判定）
  6. 危険ファイル名: ICON0.PNG / PIC1.PNG / SND0.AT3 / th07.dat / thbgm.dat

使い方（pre-commit フックから）:
  git diff --cached --name-only -z | xargs -0 python3 check_no_original_assets.py
非ゼロ終了 = コミット拒否。
"""
import struct, sys, os

BAD_NAMES = {'icon0.png', 'pic1.png', 'pic0.png', 'snd0.at3', 'icon1.pmf',
             'th07.dat', 'thbgm.dat'}

def check(path):
    name = os.path.basename(path).lower()
    if name in BAD_NAMES:
        return f'禁止ファイル名: {name}'
    try:
        size = os.path.getsize(path)
        head = open(path, 'rb').read(4096)
    except (OSError, IsADirectoryError):
        return None
    if head[:4] == b'PBG4':
        return 'PBG4アーカイブ (th07.dat系)'
    if b'\0' not in head:
        try:
            head.decode('utf-8')
            return None  # テキストファイル（ソースコード等）は対象外
        except UnicodeDecodeError:
            pass
    if size > 400 * 1024 * 1024:
        return '400MB超の巨大ファイル (thbgm.dat疑い)'
    if head[:4] == b'\0PBP':
        offs = struct.unpack_from('<8I', head, 8)
        # ICON0(1) / PIC0(3) / PIC1(4) / SND0(5) スロットが非空なら XMB 化 EBOOT
        sizes = [offs[i + 1] - offs[i] for i in range(7)] + [size - offs[7]]
        if sizes[1] or sizes[3] or sizes[4] or sizes[5]:
            return 'XMBアセット入りEBOOT.PBP'
    if b'THTX' in head:
        return 'THTXテクスチャ'
    if head[:4] == b'RIFF' and head[8:12] == b'WAVE':
        i = head.find(b'fmt ')
        if i >= 0 and struct.unpack_from('<H', head, i + 8)[0] == 0x0270:
            return 'ATRAC3 (SND0系)'
    return None

def main():
    bad = []
    for p in sys.argv[1:]:
        r = check(p)
        if r:
            bad.append((p, r))
    if bad:
        print('コミット拒否 — 原作データ/生成アセットが含まれています:', file=sys.stderr)
        for p, r in bad:
            print(f'  {p}: {r}', file=sys.stderr)
        sys.exit(1)

if __name__ == '__main__':
    main()
