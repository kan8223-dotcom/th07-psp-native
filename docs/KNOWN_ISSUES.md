# 既知の不具合・未検証項目

この文書は現在の正式統合版候補だけを扱います。v0.1.x Beta当時の修正履歴、旧機種別EBOOT、
古いtester報告は[CHANGELOG.md](../CHANGELOG.md)を参照してください。

## 公開前の実機gate

- 正式統合版は、1つの外側EBOOTが起動時にPSP modelを判定し、PSP-1000用32MB runtimeまたは
  PSP-2000/3000/Go用64MB runtimeを選びます。model 0はPSP-1000の実機受入済みE480 runtime、
  model 1以上は保守的なPSP-2000+ runtimeを使用します。
- 内側runtimeの個別実機実績はありますが、統合した外側EBOOT自体はPSP-1000と代表PSP-2000+の
  両方でboot、model選択、全run、終了復帰を再確認するまで公開合格ではありません。
- 初期配布EBOOTのICON0/PIC1は、XMB上で画像を表示しない完全透明な中立placeholderです。
  原作data検証後のlocal XMB画像生成と固定slot更新は、PBP headerと実行payloadを一切変更せず、
  実行中PBPを安全に更新できること、失敗・電源断後に中立EBOOTから再起動できることまで確認して
  初めてrelease gate合格とします。生成後EBOOTは原作由来画像を含むため再配布禁止です。

## CFWとMain RAM

- 対応CFWはARK-5のみです。別CFWで動作した過去報告は、正式版のsupport条件に含めません。
- PSP-2000、PSP-3000、PSP Goでは`Use Extra Memory = Max`が必須です。`Default`、`Off`、
  `always, highmem, off`の環境は非対応で、gameの`main()`へ入る前に終了する場合があります。
- ARK-5設定はMemory Stick交換やARK再導入で失われる場合があります。storage交換後は同じ本体でも
  `Max`を再設定してください。安全な変更手順は[ARK-5設定](ARK5_HIGH_MEMORY.md)にあります。
- PSP-1000には追加Main RAMがないためMax設定は適用されませんが、ARK-5は必要です。

## PSP-1000リプレイ同期

- 2026-09-03、EBOOT SHA-256
  `18CF0136DE1525EF6B0ECA4FCA5BC2415A0A65875D8C0D88D53A9A509A94C365`を物理PSP-1000で使用し、
  固定Lunatic外部リプレイ`th7_udLUNA.rpy`（72,308 bytes、SHA-256
  `D6B6634FB12DBA2DF5084D04DB05612FC681735DBC0D035A42A52143DFFB498F`）が1面から6面、
  幽々子撃破、Replay選択画面への復帰まで同期完走しました。
- 実機log SHA-256は
  `00FAB988A1430A08D5F67CD76CD98AB535E4B032E7E03A15004ECA3C5DC13611`です。固定replay epoch内の
  `REPLAY INVALID`、fatal、crash、arena/pool確保失敗は0件でした。当該logにCFW名・versionは
  含まれないため、このrunのCFWは未確定です。今後の正式support条件は別にARK-5へ限定します。
- 旧版で報告された「PSP-1000のほぼ全replayが同期しない」「upstreamとのbuild条件差を調査中」
  「幽々子後にReplay選択へ戻れない」は、この固定replayに関する現在の状態ではありません。
  現行READMEの警告からは削除し、保証へ切り替えた根拠をCHANGELOGに残しました。
- 合格範囲は上記固定replayです。任意replay、内蔵demo、全難易度、全機体を一括保証するものでは
  ありません。PSP-1000ではreplay identityと面別Enemy予約量を検証し、契約外なら敵を黙って
  欠落させず`REPLAY INVALID`を記録して中止します。
- save dataと任意replayはupdate前にbackupしてください。不具合報告には作成元version、難易度、
  機体、replay SHA-256、使用EBOOT SHA-256を添えてください。

## 性能

- 重い弾幕、Effect、後半面の3D背景では、PSPのframe deadlineを超えて原作準拠の処理落ちが
  発生します。平均FPSだけでなく、同一replayのwindow対応とVSync MISSで判定します。
- 固定30fps modeはgame logic、input、BGM、SEの速度を保ったまま描画負荷の余裕を増やします。
  play中にSELECTで切り替えられます。
- PSP Goのoverclock実測は開発・性能調査の資料であり、正式版の動作要件や安定性保証ではありません。
  本体固有のOC上限をrelease既定値にしません。

## 実機再確認が残る経路

- Music Roomの背景cache、段階的text更新、曲変更、title復帰はPPSSPPで合格していますが、
  代表実機で入室・複数曲再生・退出を通すrelease gateが必要です。
- 会話立ち絵下端の1px clip、PSP-1000縮小atlasのhalf-texel補正など一部の描画修正は、
  PPSSPP画像比較後の代表実機目視確認をrelease checklistに残しています。
- HOME suspend/resume、save/reload、Result、Ending、title復帰はmodel profileごとに実機確認します。
  一方のmodelの合格を他方の証拠として扱いません。

## Debug build

- `TH07_PSP_PERF_DIAG`、面直行、自動play、無限残機、強制MAX power、observer、telemetry付きEBOOTは
  診断専用です。正式releaseへ混入させません。
- debug buildだけがPracticeのclear数gateを開く場合があります。`clrd`、`pscr`、`score.dat`を
  release buildが変更してstageをunlockすることはありません。

## Logと不具合報告

- `TH07PSP_BOOT.LOG`は起動deviceのrootに出て、次回起動で上書きされます。問題を再現したら
  再起動前にPCへ退避してください。
- 報告にはboot log、正確なPSP model、ARK-5 version、game mode・難易度・stage・再現操作、
  EBOOT SHA-256を含めてください。
- 原作DAT、BGM、画像、SE、原作由来cache、local XMB画像、生成後EBOOTは添付しないでください。
- PPSSPP合格だけでは、Memory Stick I/O、ARK-5、ME、32/64MB RAMの実機挙動を保証できません。
