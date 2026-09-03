# PSPローカル MS Gothic subset（旧診断専用）

> **正式配布との境界:** 正式統合版の`msgothic-subset.ttf`は同じruntime互換filenameを
> 使用しますが、中身は再配布可能なOFL Noto Sans CJK JP派生物です。この文書のMicrosoft
> font生成物とは別物です。Microsoft派生物をformal `TH07PSP`へ上書きせず、repo・release・
> bug reportへ絶対に入れないでください。正式版は
> [PSP_RELEASE_FONTS.md](PSP_RELEASE_FONTS.md)のexact hashだけを受け入れます。

## 目的

PSP実機で使うMS Gothicを、妖々夢のstockデータが実際に表示する文字だけへ
縮小する。フォントはEBOOTへ埋め込まず、EBOOTと同じディレクトリの
`msgothic-subset.ttf`から読む。

MS Gothicおよびそこから作ったsubsetは再配布しない。生成物はrepo・release・
配布zipへ入れず、ユーザー本人のWindowsフォントからローカル生成する。

## 文字集合

`tools/th07_stock_font_profile.py`がローカルのstock `th07.dat`とソースを解析し、
次を合成する。

- 会話 `msg1.dat`〜`msg8.dat`
- スペル名 `ecldata1.ecl`〜`ecldata8.ecl` のopcode 90
- Music Roomの曲名・コメント
- 全エンディング
- メニュー、リザルト、スコア表示などの静的UI文字列
- ASCII printable全95文字
- 名前入力 `g_AlphabetList` の全96セル（94 unique文字）

stock集合は1,190 codepoints、固定SHA-256は
`da81e0e1a2b8b5d44c135d2ac43f3f91a90ce684c62b985206992e3855a90aa4`。
名前入力は観測済みの名前ではなく、プレイヤーが選べるcharset全体を必須にする。
追跡する`src/Th07FontCoverage.hpp`には数値codepointだけを置き、原作文面やfontを
含めない。

## ローカル生成

WSLからの例:

```sh
python3 tools/build_local_msgothic_subset.py \
  --archive artifacts/th07.analysis.tmp.dat \
  --font /mnt/c/Windows/Fonts/msgothic.ttc \
  --output /mnt/c/Users/kan82/AppData/Local/TH07PSP/msgothic-subset.ttf \
  --force
```

`fontTools`とPillowがローカル開発依存。出力先はrepo外、basenameは
`msgothic-subset.ttf`に固定される。manifestとcoverage reportも同じ場所へ出る。

次の投入経路は過去の隔離した開発profileを再現する場合だけに使用する。正式統合版の
`TH07PSP`には使用しない。

```sh
python3 tools/psp_stick.py install-font \
  /mnt/c/Users/kan82/AppData/Local/TH07PSP/msgothic-subset.ttf
```

`install-font`を使用する場合は旧開発環境を`--app TH07SHIKI`等で明示し、formal
`TH07PSP`を対象にしない。
既存fontはメモステ上へ時刻・旧SHA付きで退避し、同一ディレクトリの一時copyを
SHA検証してからrenameし、最後にreadback SHAを確認する。生のドライブ文字指定や
repo内fontからの投入は拒否する。

2026-09-01のstock実測:

- 出力: 300,364 bytes
- SHA-256: `cd21262fb7a1cf7b8539ecca8fb69563943f28b69fc307f76bde718892fc196c`
- coverage: 1,190 / 1,190、欠字0
- cmap / hmtx / glyph instruction / outline: 全1,190文字一致
- 28 / 30 / 32px: 計3,570描画でpixelとmetricsが元MS Gothicと一致
- 2回生成したTTF、manifest、coverage reportはすべてbyte-identical

subset時にEBDT/EBLCは落ちるが、このゲームの実使用30pxとwarmup 28pxを含む
28/30/32pxでは全件一致している。

### CP932波ダッシュ

stockデータの`81 60`はWindows CP932では`U+FF5E`（～）だが、従来のPSP用
strict Shift-JIS表は`U+301C`へ変換していた。棚卸しとMS Gothic subsetは正しく
`U+FF5E`を保持しているため、A6v4Wでは変換器側だけをWindows互換へ補正する。
フォント文字数・サイズ・SHAはA6v4から変わらない。

## PSP実行時の契約

`TH07_PSP_LOCAL_FONT_SUBSET`有効時の候補順は次の通り。

1. `msgothic-subset.ttf`
2. `msgothic.ttc`
3. `NotoSansJP-Regular.ttf`

候補を開くたびに、追跡済み1,190 codepointsを
`TTF_GlyphIsProvided32`で全件検査する。1字でも欠けた候補は丸ごと閉じて次へ進み、
文字列単位・文字単位の混在fallbackはしない。選ばれたTTC face indexは、Main RAM
promotionと両方のfile demotionでも保持する。boot logの`FONT COVERAGE`行で
候補名、face、提供数、最初の欠字を確認できる。

stock以外の改造テキストを使う場合は`--chars`で追加文字を明示するか、完全版fontへ
fallbackする。coverage不完全なsubsetを部分的に使うことは禁止する。

## 旧開発ビルドと履歴

`make psp3000-rid30-a6v4-local-font-subset-build`はA6v3の
FONT/TITLE共有arenaを維持し、local subset gateを追加する。subset実読込サイズの直後を
64-byte境界へ切り上げ、残ったtailへ1,536KiBのstage text cacheが収まる場合は同じarenaを
借用する。このためA6v3で別途必要だったstage text cacheの1,536KiB確保はA6v4では消える。
tail接続中はFONTからTITLEへ遷移できず、必ずtext cacheをdetachしてから所有権を返す。
容量不足・状態不整合時は従来の独立mallocへfail-closedする。この旧profileはPSP-1000で
拒否する。Microsoft由来の生成fontは現在もEBOOT・formal ZIPへ同梱されない。

2026-09-01現在:

- PC実装・テスト・PSP-3000 build・バイナリ監査: 合格
- 全テスト: 745 / 745 合格
- EBOOT SHA-256: `4e32a4a1f51b42e832e74bc2049376f818b6ea55b70215ca5352113de0c54653`
- D:投入: 済（EBOOT・fontともreadback SHA一致）
- 実機確認: 合格。stage 1〜5を通過し、5面終了後のtitle01再読込とFONT復帰まで成功
- 実機ログ: `artifacts/TH07PSP_BOOT.A6V4-FONT-SUBSET-RUN1.20260901-203100.LOG`
  （SHA-256 `5275f24f0a8bbeed941324a7f40cbc7dc38aa1058f3bab7fa28b00bc6ee5ac95`）

## 次段候補: 事前ラスタライズA8 atlas

実コード監査では、stock経路の実フォントサイズは30px（warmup 28px）、最終uploadは
512x16 RGBAで統一されている。ただしbit一致するatlas化は、最終16px文字を並べるだけ
では成立しない。size 30・bold後の未着色A8 coverageとbearing/advance/baseline等を保持し、
現行どおり行全体を合成してから4方向outline、本文、絶対座標依存alpha処理、38→16の
box filterを通す必要がある。

- 見積容量: 約1.05〜1.60MiB + metadata（A8）
- RGBA atlas: 約4.0〜6.2MiBのため不採用
- A4/1bit: coverage量子化でpixel一致不能のため不採用
- 既存96-row profileではFreeType部分は約0.70秒/3.11秒。atlas化だけの理論短縮は
  約23%で、outline/blit/filter/storeは残る
- 全stock隣接pairと名前入力94文字の全8,836 pairでrow hash一致を証明する必要がある

このA8案は旧private-font profileの検討記録としてのみ残す。formal PSP-1000経路は
264,288-byteのOFL Noto派生subsetへ移行しており、追加実装は別の監査済み増分とする。
