# Credits / References

## 原作とデコンパイル

- [上海アリス幻樂団](https://www16.big.or.jp/~zun/) / ZUN — 東方妖々夢 ～ Perfect Cherry Blossomの原作。
  原作データは本プロジェクトに含まれません。
- [some100/th07](https://github.com/some100/th07) — 東方妖々夢 1.00bのデコンパイル。
  本移植は[portable branch](https://github.com/some100/th07/tree/portable)のコミット`7e2ccb4`を直接の土台にしています。
- [GensokyoClub/th06](https://github.com/GensokyoClub/th06) — 共通するエンジン構造、型、命名、挙動の参照元。
- [GensokyoClub/th08](https://github.com/GensokyoClub/th08) — LZSS/PBG系処理と後続エンジン実装の参照元。

## PSP実装の直接参照

- [kan8223-dotcom/th06-psp-native](https://github.com/kan8223-dotcom/th06-psp-native) —
  PSP向けGE描画、VFPU、ME音声、入力、電源管理、原作データ分離、ログ、配布構成の基準。
- [kan8223-dotcom/th06_ps3](https://github.com/kan8223-dotcom/th06_ps3) —
  TH06のプラットフォーム分離と、別機種向け移植時の描画・音声・ファイル境界の参照。
- [kan8223-dotcom/psp-pmdvis](https://github.com/kan8223-dotcom/psp-pmdvis) @ `18fb0b1` —
  実機で稼働済みのMECC初期化、SC/ME共有メモリ、キャッシュ同期の基準。
- [mcidclan/psp-media-engine-safe-task](https://github.com/mcidclan/psp-media-engine-safe-task) @
  `e8c2d8b` — M-cid（m-c/d）氏によるPSP Media Engine Safe Task Library。PSP-1000の
  BGM ringをME local eDRAMへ移すため、同ライブラリのMIST方式を使用しています。これにより
  Main RAMを正味393,088 bytes（383.875 KiB、約384 KiB）回収し、PSP-1000で発生していた
  OOMを防ぐことができました。
  ライセンスは同repositoryの[MIT License](https://github.com/mcidclan/psp-media-engine-safe-task/blob/main/LICENSE.md)を
  参照してください。
- [mcidclan/psp-media-engine-custom-core](https://github.com/mcidclan/psp-media-engine-custom-core) @
  `7dbf492` — M-cid（m-c/d）氏によるPSP Media Engine Custom Core（MECC）とME core mapper。
  MIST task内のnative ME関数と機種別mappingに使用しています。vendored copyのライセンスは
  `psp/third_party/me-custom-core/LICENSE.md`を参照してください。

## SDK・検証環境

- [PSPDEV/PSPSDK](https://github.com/pspdev/pspsdk) — PSP homebrew SDK、libGU/libGUM、VFPUおよびPSP API。
- [PPSSPP](https://github.com/hrydgard/ppsspp) — 高速な回帰試験に使用。最終判定はPSP実機です。
- SDL2、SDL2_image、SDL2_ttf、FreeType、libpng、libjpeg、zlib、bzip2 —
  portable engineおよびPSPビルドで使用するライブラリ。
- Noto Sans JP — 日本語テキスト表示。配布フォントのライセンスは`licenses/NotoSansJP/OFL.txt`を
  参照してください。

本プロジェクトについて、上記の原作者・上流・参照先へ問い合わせないでください。
これらのプロジェクトは本移植のサポート窓口ではありません。
