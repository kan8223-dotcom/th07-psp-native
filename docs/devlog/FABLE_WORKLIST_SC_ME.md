# SC/ME 配席表（RID29世代+A1-MOVE実装中 時点）

更新規約: プロセッサ間で仕事が移動する増分が実機合格したら、この表と
FABLE_ADVICE_CHECKLIST.md を同時更新する。最終更新: 2026-08-31 Fable。

## SC（Allegrex 333MHz + VFPU）

権威（SC固定契約・不可侵）:
- ECL実行・敵AI・敵update／弾の生成消滅・特殊弾・ECL介入弾
- 当たり判定/グレイズ判定とcommit／アイテム生成・回収判定・得点/PIV
- 自機update・ショット誘導・命中・ダメージ／RNG・スコア・全ゲーム状態

描画:
- 全レイヤ走査・GE状態管理・GEコマンド組立/発行（MEはGE不可触）
- 弾: ME run表に従うsubmit + VM副作用commit
- 背景一式（VM更新・行列・頂点/コマンド生成）※積み荷候補
- 敵・自機・Effect layer2・GUI・文字・popup（SCバッチ版）
- Item描画のsuffix（ME予算あふれ分の受け皿）

音響・システム:
- BGMストリーミング＋SEミキシング（税0.5〜1.0ms。A5: 除算除去→VFPU化予定）
- 入力・swap・ロードI/O・SHIKIGAMI打電・perf計測・使用率メーター

## ME（333MHz、VFPU無し）

- ✅ 安定NORMAL弾 compact update（移動・境界・証明可能陰性衝突分類）
- ✅ 弾頂点生成（ONEPASS展開・run表→GE直接消費）
- ✅ Item描画prefix（予算器80%・前frame≥85% veto）… RID29〜
- 🔨 Item吸引・移動更新（A1-MOVE、弾ジョブへ統合実装中）
- ✅ 起動セルフテスト群・CP0サイクル自己計測

待機列（prefix/suffix分配器の積み荷順）:
1. Effect layer0/3 復帰（M0階段を踏んでから）
2. 自機ショット描画準備（PL計測が先）
3. 得点数字
4. 背景の走査・行列・run表組立
5. Item生成の候補化

永遠に来ないもの: ECL/AI・判定commit・スコア・回収・音声mix・GE発行

## GE（参考）

全実描画・3D T&L・表示。eDRAM上下フル稼働（FB×2/Z/立ち絵プール2048K）。
tail 0.2〜0.6ms——唯一余裕のある部署。
