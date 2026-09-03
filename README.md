# th07-psp-native

**日本語** | [English](README_EN.md)

> [!IMPORTANT]
> **現在の配布版`v1.0.0-rc1`は最終テスター版（正式stable公開前のプレリリース）です。**
> 入手先は[GitHub Releases](https://github.com/kan8223-dotcom/th07-psp-native/releases)です。
> 全機種共通の単一EBOOT（初期SHA-256 `822E0A4C43AC84509A25AF16D921B0BB9BCB1C4597DCDBB315E9583D5E92FAD4`）が起動時に
> PSP-1000用profileとPSP-2000/3000/Go用profileを自動選択します。対応CFWはARK-5のみで、
> PSP-2000/3000/Goでは`Use Extra Memory = Max`が必須です。
>
> 初期EBOOTのXMBアイコンと背景は完全透明です。利用者自身の正しい`th07.dat`と`thbgm.dat`を
> 初回起動で検証できた場合だけ、実機上でアイコンと背景をlocal生成します。生成後のEBOOTは
> 原作由来画像を含むため、共有・再配布しないでください。

東方妖々夢 ～ Perfect Cherry Blossom 1.00bをPSP上でネイティブ動作させる、非公式の移植です。
Windows版をエミュレーションするものではありません。

> [!WARNING]
> このリポジトリと配布物には、東方妖々夢の原作データおよび原作から生成した画像を一切含みません。
> 動作には、利用者自身が所有するPC版東方妖々夢 1.00bのインストールフォルダが必要です。

## 特徴

- [some100/th07のportable branch](https://github.com/some100/th07/tree/portable)を基にした、
  PSP向けネイティブ移植です。
- 1つのEBOOTが起動時にPSPの型番を判定し、PSP-1000用32MB profileまたは
  PSP-2000/3000/Go用64MB profileを自動選択します。機種別EBOOTを選ぶ必要はありません。
- PSPSDKのlibGU/libGUMからPSPのGraphics Engine（GE）へ描画します。
- VFPU対応libGUMとPSP向け数値処理を使用します。
- m-c/dの[Media Engine Custom Core（MECC）](https://github.com/mcidclan/psp-media-engine-custom-core)を使い、
  実機ではMedia Engine（ME）にPCM音声ミキシングを担当させます。利用できない場合は
  メインCPU（SC）へ安全にフォールバックします。
- PSP-1000の省メモリ音声profileでは、M-cid（m-c/d）氏の
  [PSP Media Engine Safe Task](https://github.com/mcidclan/psp-media-engine-safe-task)が提供する
  MIST方式を利用します。BGM ringをME local eDRAMへ移してMain RAMを正味393,088 bytes
  （383.875 KiB）回収し、32MB環境のOOMを防ぎます。
- 原作の論理解像度640x480を保ち、PSPの480x272へ4:3表示または全画面引き伸ばしで出力します。
- 設定、スコア、リプレイは原作フォルダへ書かず、EBOOTと同じ場所へ保存します。
- 正式ZIPはPSP-2000/3000/Go用のfull Noto fontと、PSP-1000用の同じOFL Noto由来
  1,190文字subsetを両方同梱します。

## 正式統合版の状態

現在は、XMB上のBeta／tester表記を外した単一EBOOTの最終テスター／プレリリースを
GitHub Releasesで公開しています。正式stableへの昇格は、対象実機での受入結果を根拠に別途判断します。
PCまたはPPSSPPだけの結果を実機受入とは扱いません。

PSP-1000のリプレイ同期に関する旧い「未確認」記述は、2026-09-03の実機受入によって更新されました。
EBOOT SHA-256
`18cf0136de1525ef6b0eca4fca5bc2415a0a65875d8c0d88d53a9a509a94c365`を物理PSP-1000で使用し、
固定Lunatic外部リプレイ`th7_udLUNA.rpy`が1面から6面、幽々子撃破、Replay選択画面への復帰まで
同期完走しました。当該boot logにはCFW名とversionが記録されていないため、このrunのCFWは未確定です。
今後の正式サポート条件はARK-5へ限定します。固定リプレイ、EBOOT、実機ログのhashは[更新履歴](CHANGELOG.md)と
[受入anchor](release-anchors/psp1000-e480-hw-pass-20260903/README.md)に固定しています。

この合格は上記の固定リプレイに対するものです。任意の外部リプレイ、全難易度、全機体を一括して
保証するものではありません。identityまたは予約容量の契約を満たさないPSP-1000リプレイは、敵を
黙って欠落させず`REPLAY INVALID`を記録して中止します。

> [!IMPORTANT]
> 対応CFWはARK-5のみです。PSP-2000、PSP-3000、PSP Goでは、ARK-5の
> `Use Extra Memory`を起動前に必ず`Max`へ設定してください。`Default`、`Off`、または
> `always, highmem, off`のままではサポート対象外で、起動前に終了する場合があります。
> PSP-1000には追加Main RAMがないためMax設定は適用されませんが、CFWはARK-5を使用します。

> [!TIP]
> 重い弾幕や後半面で安定性を優先する場合は、プレイ中にSELECTを押して固定30fpsモードを使用できます。
> 描画を30fpsへ固定しても、ゲーム進行、入力、BGM、SEの速度は変わりません。

## インストール

必要なもの:

- ARK-5でhomebrewを起動できるPSP-1000/2000/3000/Go
- GitHub Releasesにある最終テスター／プレリリース統合版ZIP（全機種共通）
- 利用者自身が所有するPC版東方妖々夢 1.00bのインストールフォルダ
- USB接続またはカードリーダー

一番簡単な手順:

現在の最終テスター／プレリリース統合版は、次の手順で導入します。

1. ARK-5を導入します。PSP-2000/3000/Goでは対象アプリの`Use Extra Memory`を`Max`へ設定します。
2. 配布ZIPを展開します。
3. ZIP内の`TH07PSP`フォルダを、メモリースティックまたは内蔵ストレージの`PSP/GAME/`へコピーします。
4. PCにある東方妖々夢のインストールフォルダを、フォルダごと`TH07PSP`の中へコピーします。
5. コピーした原作フォルダの名前を、半角英数字の`th7`へ変更します。すでに`th7`がある場合は、
   原作フォルダそのものではなく、その**中身**を`th7`直下へコピーします。
6. XMBから`東方妖々夢 ～ Perfect Cherry Blossom.`を起動します。

最終的な配置は次の形です。DATを展開したり、日本語名の原作ファイルを個別に選別したりする必要はありません。

```text
PSP/GAME/TH07PSP/
├── EBOOT.PBP
├── NotoSansJP-Regular.ttf
├── msgothic-subset.ttf
├── README.md
├── CREDITS.md
├── LICENSE
├── licenses/
└── th7/                    ← 利用者が自分でコピーした原作フォルダ
    ├── th07.dat
    ├── thbgm.dat
    ├── 東方妖々夢.exe     ← 名前はそのままで構いません
    └── その他の原作ファイル
```

2つのfontはどちらも必須です。PSP-1000 payloadは264,288-byteの
`msgothic-subset.ttf`、PSP-2000/3000/Go payloadは4,491,696-byteの
`NotoSansJP-Regular.ttf`を使用します。前者はruntime互換のfilenameにすぎず、内部も字形も
OFL-licensed Noto Sans CJK JP 2.004由来で、Microsoft MS Gothicではありません。Windowsから
作ったprivate subsetで置き換えたり再配布したりしないでください。生成・hash・licenseの契約は
[正式配布font仕様](docs/PSP_RELEASE_FONTS.md)に記録しています。

EBOOTは`th07.dat`と`thbgm.dat`のheaderおよび1.00bのfile sizeを確認します。
対応する1.00bのsizeはそれぞれ23,829,135 bytesと444,516,656 bytesです。
別versionや不足したデータでは起動しません。

> [!WARNING]
> `th7`の中へ原作フォルダをもう一度入れないでください。
> `TH07PSP/th7/東方妖々夢/th07.dat`のように一段深い配置にすると、統合launcherと
> model別runtimeの起動までは成功しても、runtimeが`original TH07 1.00b data not found`を
> 記録して終了します。`th07.dat`と`thbgm.dat`は必ず`TH07PSP/th7/`直下へ置いてください。

### 初回のXMB表示とローカル生成物

配布直後のEBOOTは、ICON0とPIC1に生成ツール製の完全透明な中立placeholderだけを持ち、XMB上では
サムネイルも背景も表示しません。正しい`th07.dat`と`thbgm.dat`を
初回起動で検証できた場合だけ、利用者のPSP上でXMB用ICON0とPIC1を生成し、次にXMBへ表示された時から
画像が現れます。生成ツールだけを配布し、生成済み画像をリポジトリやZIPへ収録することはありません。
このローカル更新経路は実機で安全な更新と次回XMB表示まで確認済みです。現在の配布物は
最終テスター／プレリリースであり、この確認だけを正式stable公開の宣言とは扱いません。

ローカル生成後の`EBOOT.PBP`には利用者所有の原作から派生した画像が入ります。
そのEBOOT、生成画像、PSP-1000の`title01.psp1000.cache`を再配布したり、不具合報告へ添付したり
しないでください。モデルに応じて生成される`TH07RUNTIME.PBP`は、削除しても次回起動時に再生成されます。

設定、スコア、新規リプレイはEBOOTの隣に作成されます。主な生成物は`th07.cfg`、`score.dat`、
`replay/`です。

### 別の原作データ配置

推奨配置は`PSP/GAME/TH07PSP/th7/`です。容量や管理上の理由がある場合は、原作folderを
`ms0:`またはPSP Goの`ef0:`直下へ置くこともできます。launcherとmodel別runtimeはそれぞれ、
起動device直下とその1階層下、および`PSP/GAME/`の兄弟folderにある`th7`を非再帰で探索します。
どの配置でも、探索対象folderの**直下**に正しい`th07.dat`と`thbgm.dat`の組が必要です。
その中へさらに原作folderを入れ子にしても探索しません。全機種で同じ規則を使います。

### 起動しない場合

次を順に確認してください。

1. CFWがARK-5である。
2. PSP-2000/3000/Goでは`Use Extra Memory`が`Max`である。
3. `EBOOT.PBP`が`PSP/GAME/TH07PSP/`にある。
4. `NotoSansJP-Regular.ttf`と`msgothic-subset.ttf`を削除・置換していない。
5. `TH07PSP/th7/th07.dat`と`TH07PSP/th7/thbgm.dat`が**直下に**あり、さらに原作folderの
   中へ入れ子になっていない。
6. 原作が東方妖々夢1.00bである。

`TH07PSP_BOOT.LOG`に`original TH07 1.00b data not found`と出る場合は、まず手順5の配置を
確認してください。統合launcherからmodel別runtimeへの切替に成功した後でも、この配置違いで
runtimeだけが終了することがあります。

ARK-5の設定はMemory Stick交換やARK環境の入れ直しで失われる場合があります。以前起動していても、
storageを交換した後は`Max`を再確認してください。`always, highmem, off`が残っている場合の安全な
修正方法は[ARK-5設定手順](docs/ARK5_HIGH_MEMORY.md)にあります。付属snippetを
`SETTINGS.TXT`全体へ上書きしてはいけません。

起動・面移動の記録は起動device直下の`TH07PSP_BOOT.LOG`へ出ます。不具合を再現したら、ゲームを
もう一度起動する前にログをPCへ退避してください。報告にはログ、PSPの正確な型番、ARK-5のversion、
ゲームmode・難易度・面・再現操作、使用EBOOTのSHA-256を添えてください。原作データや原作由来の
ローカル生成物は添付しないでください。

## 操作

- 方向キー / アナログパッド: 移動、項目選択
- ×: ショット、決定、会話送り
- ○: ボム、キャンセル
- □ または L/R: 低速移動
- △: 会話スキップ
- START: ポーズ
- SELECT: 通常60fps / ゲーム進行速度を保つ固定30fpsモードの切り替え
- HOME/PSボタン: PSPの終了・中断menu

原作のwindow表示設定は`4:3 FIT`（362x272）、fullscreen設定は`FULL STRETCH`（480x272）として扱います。

## 現在の制限

- PPSSPPで正常でも、Memory Stick I/O、ARK-5、ME、32/64MB RAMの挙動は実機と異なる場合があります。
- 重い弾幕、Effect、後半面の3D背景では処理落ちが残ります。これはゲーム進行を遅くする原作準拠の
  frame skipです。必要に応じて固定30fpsを使用してください。
- セーブデータと任意の外部リプレイは、更新前にbackupしてください。2026-09-03の同期受入範囲は
  上記の固定Lunaticリプレイです。
- Music Roomと一部の表示修正には、PPSSPP合格後も代表実機で再確認が必要な経路があります。
- 同梱するNoto由来`msgothic-subset.ttf`のPC監査は合格していますが、このexact hashを使う
  PSP-1000実機の4面および固定Lunatic 1～6面runは正式stable昇格前のgateとして残っています。

詳細は[既知の不具合](docs/KNOWN_ISSUES.md)と[更新履歴](CHANGELOG.md)を参照してください。

## 含まれないもの

- 東方妖々夢のEXE、DAT、画像、音楽、SE、その他の原作データ
- 原作から生成済みのXMB画像またはそれを埋め込んだEBOOT
- 原作または開発中に作成したリプレイ（`.rpy`）
- 設定、スコア、logなどのuser data
- Microsoftのfont（`msgothic-subset.ttf`という互換filenameの同梱物はMicrosoft fontではなく、
  OFL Noto Sans CJK JP由来です）
- 自動play、無限残機、MAX powerなどを有効にした開発用EBOOT

## 非公式プロジェクトについて

本プロジェクトは上海アリス幻樂団およびZUN氏による公式製品ではなく、承認・支援・保証を受けたものでも
ありません。本プロジェクトについて、上海アリス幻樂団、ZUN氏、some100、GensokyoClub、m-c/d、
PSPDEV、PPSSPP、または参照先の作者・保守担当者へ問い合わせないでください。

質問や不具合報告の窓口は、このリポジトリ自身のIssuesです。原作製品やCFWの入手方法、
原作データの共有に関する質問は扱いません。

## ビルド

PSPDEV/PSPSDKが`/usr/local/pspdev`に導入済みで、CMakeが使える環境で実行します。
同梱したMECCもsourceからbuildします。正式archiveのfont再生成監査には
`fontTools==4.62.1`が必要です。

```sh
make -j"$(nproc)" all
make psp1000-build
make psp2000plus-build
make release
```

`make release`は固定済みの両profileとclean buildした統合launcherから、local候補archive
`dist/th07-psp-native-v1.0.0.zip`を1つ作ります。このcommandだけではGitHub tagやReleaseを
発行しません。初期EBOOTのXMB画像slotが規定の完全透明placeholderと一致すること、
モデル別runtimeのhash、原作データ・user data・診断EBOOT・原作由来画像が混入していないことを
release auditで検査します。加えて両Noto fontのsource/license/output hash、1,190文字cmap、
`kern`のみのlayout契約を再生成で検査します。個別profile targetは開発と回帰確認用であり、
機種別ZIPは配布しません。

開発用の面直行buildは明示した場合だけ有効です。配布物には使用しません。

```sh
make PSP_DIRECT_GAME=1 PSP_DIRECT_STAGE=5 -j"$(nproc)"
```

## 参照・謝辞

参考元のdecompile、TH06 PSP移植、MECC、MISTを含む実装出所は
[CREDITS.md](CREDITS.md)へURLと用途を記載しています。

## ライセンス

本体はbaseのsome100/th07と同じくCC0 1.0 Universalです。全文は[LICENSE](LICENSE)を参照してください。
同梱third-partyとfontは各licenseに従います。原作データの権利は本licenseの対象外です。
