# TH07 PSP 最終60FPSアーキテクチャ設計書 — Rev.3.3（自己完結版）

Date: 2026-08-29 / Author: Fable (design-only, no code)
Rev.3.3 changelog: M2/M3実機確定入力を反映（§3.4新設）。M3上位分解は676/676窓
閉包で**有効**、M3詳細phaseは583/676窓閉包失敗で**無効のまま**（単一補正係数では
閉包とVM非負を両立不可＝係数修正禁止）。M2絶対値は計測負荷で約1.6倍膨張して
いたため相対比較専用に格下げ。増分の優先順位を確定入力で更新（I3最優先、
弾側はI2b主軸）。BGMゲート/I1は実機合格（§3.4）。
Rev.3.2 changelog: I8の4点修正（seedFrames可変化と81920の満額限定、file unlock
段の追加、Detach/解放のowner契約整合、I/O主張の限定）、I1呼出位置の固定
（GameManager.cpp:897、gameplay state設定902行目より前）、M3にBU閉包gate追加、
§1.1のFB/Z旧値を544/272KiBへ統一。
Rev.3.1 changelog: (1)J*の所属を訂正（J*⊂C、DRAW系⊂R、BG⊂ST⊂R）、S5内訳を
実DRAW行(log:1278)で再計算（掲載計13.0、未ログ1.1ms） (2)M2境界をswap費用込みに
訂正、`mFrameBlockingGeUs`二重計上の排除（GE加算はpost-vblank tailのみ）、
M2/M3に数値gate追加 (3)guardを「gameplay-entry headroom検査」に再定義し7段階順序へ、
owner契約とTextHelper自前free移管を明記 (4)BGM seedのpublish transaction固定・
別FILE handle・clamp、I8はUND=0複数run (5)最終出荷gateの分離と診断4profile化。

判定原則: クリティカル仕事量は`CPU+GE`。`VB`不加算。内訳の所属は
**J3/J7/J8/J10/J11/J12 ⊂ C（calc job）**、**PERF DRAWのST/EN/PL/FX/BU/GUI ⊂ R**、
**BG ⊂ ST ⊂ R**（`PspGuGraphics.cpp:3812`付近のjob分類に基づく。外側加算禁止）。
リプレイ中telemetry FPS欄は不使用。PERFの「peak」=120フレーム窓平均の最大
（per-frame最大ではない、§3.1）。

---

## 0. 設計対象

- **一次対象: PSP-3000 / model 3**（`ge4_game_bridge.cpp`の`kRequiredModel = 3`）。
  本書の予算・合格条件はこの構成のみに適用。
- 二次profile: PSP-2000/Go（未実証）。GE bridge非活性＝立ち絵Main RAM fallback
  （最大1536KiB、§4.1に専用行）。60FPSコミット外。合格条件は起動と機能維持のみ。
- PSP-1000: §8の独立profile。

---

## 1. リソース所有マップ

### 1.1 現状（R21C実測）

| リソース | 容量 | 所有者 | 状態 |
|---|---:|---|---|
| Main RAM user partition | ~55MiB上限(ARK Max) | 静的23.94MiB+newlib15-17MiB | high memory有効実証済み |
| BGMリング | 384KiB | SoundPlayer(Main RAM) | 出荷経路。underrun 1回/走 |
| SFX | ~1.6MiB | SoundPlayer(Main RAM) | 出荷経路 |
| スプライト頂点 | §1.3 | AnmManager | Main RAM静的アレナ主経路 |
| GE下位2MiB | FB×2 544K+Z 272K+surface≤512K+staging512K+gap≥208K | レンダラ | 実証済み |
| GE上位2MiB | §1.4 | portrait cache他 | s4/5/6全hash PASS |
| ME eDRAM 4MiB | — | **UNUSED** | 出荷不変条件#8 |
| Scratchpad 16KiB | — | 未使用 | I5候補（タイル設計必須） |

### 1.2 目標状態マップ

| リソース | 目標所有者 | 変更点 |
|---|---|---|
| Main RAM追加分 | §4.5 Optional RAM budget owner（唯一の確保者） | テキストキャッシュ/BGM先読み/ANM先読み/診断を優先度管理 |
| GE下位/上位 | 現状のまま | 変更なし（§1.4のspare 376KiBのみ将来枠） |
| ME eDRAM | **UNUSED継続** | 変更なし（研究profileのみ§4.4） |
| Scratchpad | I5採用時のみエミッタタイルバッファ | タイル設計合格が条件 |

### 1.3 頂点経路の実態

主経路は`src/AnmManager.hpp:398`
`alignas(16) Th07PspSpriteVertex spriteVertexBuffer[49152]`（Main RAM静的アレナ）:

```
CPU front arena（spriteVertexBuffer前方へCPUが頂点書込）
  → deferred GE arena（同バッファ末尾384KiB、GEへ引き渡す領域）
  → D-cache writeback（FlushDeferredSpriteDraw時。CPU書込のGE可視化フェンス）
  → GE read（DIRECT list参照。フレーム末尾syncで完了保証）
  → 次フレーム再利用（sync後にのみ書込可）
sceGuGetMemory はアレナ不足時のfallback（主経路ではない）
```

### 1.4 GE上位2MiBの実配置（PspGuGraphics.cpp:61付近の定数に基づく）

| 区画 | 容量 |
|---|---:|
| portrait予約（live上限） | 1536KiB |
| low-res color（封印機能の予約枠） | 68KiB |
| low-res depth（同上） | 68KiB |
| 残余spare | 376KiB |

---

## 2. フレームフェーズスケジュール

```
t=0                                    ~CPUend             vblank(16.7)
CPU: |input|calc|draw: front arena→deferred arena→writeback→list構築|-(idle)-|
GE :        |DIRECT追走 …………………………………………… tail(0.7-2.4ms)|
ME :  idle（出荷: eDRAM UNUSED / JOBS=0。オーディオはSCスレッドのMain RAM経路）
      Finish → WaitVblank →(GE排水)→ sync(≈0) → swap → StartList
```

- フェンス: (a)フレーム末尾sync（vblank後、通常即時） (b)`SubmitAndRestart`
  （list溢れ時のみ） (c)deferred flushのdcache writeback。
- フレーム中の追加`sceGuSync`/`sceGeDrawSync`禁止。
- **現行計測境界の欠陥（M2で修正必須）**: フレーム先頭の
  `StartList`/`PreserveLatestPlayfield`/`ClearPillarboxes`は`mFrameStartUs`打刻より
  前に実行され、末尾`sceGuFinish`は`cpuEnd`打刻より後（PspGuGraphics.cpp:2937/2978
  付近）。最終per-frame判定の境界は**前フレームGE完了後・swap前を開始点**とし
  （swap直後開始では`sceGuSwapBuffers()`自体が漏れる。開始点を動かせない場合は
  swap費用を独立項として計上）、終了点はFinish完了後とする。
- **`mFrameBlockingGeUs`の二重計上排除（M2で修正必須）**: `SubmitAndRestart`の
  ブロッキング時間（PspGuGraphics.cpp:3773付近）はCPU壁時計に既に含まれる。
  新指標の`CPU+GE`におけるGE加算は**post-vblank tailのみ**とし、
  `mFrameBlockingGeUs`を加算しない（参考値として別掲は可）。
- フルダブルバッファリスト: GPU-bound窓（CPU<16.7かつCPU+GE>16.7）が実測される
  まで保留。

---

## 3. 予算と指標

### 3.1 指標定義（最終合格条件）

| 指標 | 最終合格条件 |
|---|---|
| per-frame CPU+GE 最大値（§2の修正境界） | ≤16.7ms（診断OFF相当ビルド） |
| per-frame CPU+GE p99 | ≤15.7ms（jitter予備1.0ms） |
| >16.7msフレーム数 / VSync miss数 | stage4-6全域で0（会話・スペル開始を含む） |
| 分布 | ヒストグラムをRAMログへ（M2で実装） |

作業目標は窓平均≤15.7ms。§3.5の過渡数値は**I1の中間gateであり最終条件ではない**。
最終条件は上記の全域0 miss（キャッシュヒット時の会話フレームは通常フレーム+32K
アップロード程度で16.7msに収まる設計。M2実測で収まらない構造が判明した場合は
免除区間を設けず、設計へ差し戻して再承認を得る）。

### 3.2 実測予算（閉じた形式。J*⊂C、DRAW系⊂R、BG⊂ST⊂R）

| 行 | S4 dense avg | S5 hotspot窓(log:1277-1278) | S6 late窓(log:2476-2477) |
|---|---:|---:|---:|
| calcチェーン C（J3/7/8/10/11/12を内包） | 5.3 | 5.8 | 3.9 |
| drawチェーン R | 10.0 | 14.1 | 17.2 |
| 　R内訳: PERF DRAW掲載計(ST/EN/PL/FX/BU/GUI) | （M2で閉包） | **13.0** (=2.6+0.6+1.7+0.0+0.4+0.1+0.2+6.5+0.9) | **5.3** (=0.5+0.2+0+0+0.8+2.5+1.3) |
| 　　（参考: BG⊂ST） | — | BG2.8⊂ST計3.2 | BG0.4⊂ST計0.7 |
| 　R内訳: **未ログ分**（未掲載priority＋chain overhead） | （M2で閉包） | **1.1** | **11.9** |
| 外側残差（audio/system/入力等。=CPU−C−R） | **3.4** | **0.3** | **0.1** |
| **CPU計（=実測）** | **18.7** | **20.2** | **21.2** |
| GE tail | 0.0-0.5 | 0.0 | 0.0 |

- S6 late窓の11.9msは**Rのうちログに出ていない部分**（現行PERF DRAW行は
  priority 3,4,5,6,7,8,9,10,12のみ掲載）。分解はM2の仕事。なおS6 lateの
  BU=2.5msは弾0でも発生（BUはレーザー/アイテムを含む）。
- S4はDRAW行の代表窓を本書執筆時点で特定していないため内訳を空欄とし、
  M2の全priority閉包で埋める（C/R/外側残差と合計は実測）。

参考観測（結論に使用しない）: S6 late窓は`M50.8 D99 VI10391`。ただしS5には
`M137.0でCPU7.9ms`の窓（log:1055）があり、**行列submit回数は原因候補の一つに
過ぎない**（回数であり時間ではない）。

### 3.3 目標予算（仮説H。M系合格まで確定予算に不算入）

| | 現状(窓) | 仮説削減(H) | 目標(窓) |
|---|---:|---:|---:|
| S4 dense | 18.7-19.7 | I2a/b −2〜3、I4 −0.4 | **15.3〜17.3**（M3合格まで未確定） |
| S5 hotspot | 19.6-20.2 | 未確定 | **（空欄。M2/M3後に記入）** |
| S6 opening | 23.1 | 未確定 | **（空欄。同上）** |
| S6 late | 21.2 | I2/I5/I6は効果0（弾0）。I3+I4でも最大18.5 | **未達。M2の11.9ms分解まで目標値を提示しない** |

### 3.4 M2/M3実機確定入力（2026-08-29夜、次フェーズの予算根拠）

計測の有効性判定（Codex最終判定、ログ`artifacts/final60-m3cal-bgmgate-real-run-20260829/`）:

| 計測 | 判定 | 使い方 |
|---|---|---|
| M3上位分解（laser/item/bullet callback別） | **676/676窓閉包・有効** | 絶対値として予算に使用可 |
| M3詳細phase（VM/VD/CR/ST/VS/RP/DC） | **583/676窓閉包失敗・無効** | 仮説のみ。係数補正による確定は禁止（閉包とVM非負を単一係数で両立不可） |
| M2 owner別draw時間 | owner判定は有効 | **絶対値は計測負荷で約1.6倍膨張**——相対比較専用 |
| BGMロード待機 / I1文字キャッシュ | 実機合格 | 曲頭同期OK／hit=82/90/120, miss=0, coverage100% |

確定した数値（M3上位・有効）:

- 弾経路ピーク（窓平均）: **S4=7.70ms / S5=6.92ms / S6=9.24ms**
- S6後半のP11得点ポップアップ: owner判定有効（M2相対で10.2〜16.6ms、絶対値は1.6倍膨張込みなので実効≈6〜10ms級と推定——診断OFF相当での再確認が必要）
- 会話停止: 1.2-1.5秒→117〜166ms（約90%減。中間gate100ms・最終16.7msは未達）
- 音声UND=0/FATAL=0、ME eDRAM未使用維持

これによる増分方針の確定:

1. **I3（得点ポップアップのバッチ化）が最優先**
2. 弾側は**I2b（描画順保存snapshot/emitter＋state削減＋cull/rotation一括化）を主軸**
   とし、I2a（descriptor事前計算）は従属（詳細phaseが無効のためVM/VD配分は未確定）
3. M3詳細phaseの再計測は任意: 固定1/32抽出→**無相関抽出＋信頼区間**へ再設計し、
   弾の多い1ステージのみで実施（4-6面完走の再依頼はしない）
4. 最終性能判定は診断OFF相当ビルド（§10 RELEASE/PERF-ACCEPT）で行う

### 3.5 過渡（**I1の中間gate**。最終条件は§3.1の全域0 miss）

| 事象 | 現状 | 中間gate |
|---|---:|---:|
| 会話CPU窓 | 73-118ms | ≤20ms |
| 最大フレーム事象 | 1.1-1.35s | ≤0.1s |
| S6ボスBGM遷移underrun | 1回/走 | 0（I8） |

---

## 4. メモリ設計

### 4.1 Main RAM（PSP-3000一次対象）

| プール | cap | 優先度 | 寿命 |
|---|---:|---:|---|
| OOM guard（§4.5ライフサイクル） | 2MiB | 0 | ロード区間内のみ保持 |
| テキストキャッシュ | 1536→768→384→256K | 1 | ステージ |
| BGM遷移先読み | 320KiB(5×64KiB) | 2 | ロード〜遷移seedまで |
| stage ANM先読み | ≤2MiB | 3 | ステージ |
| 二次profile: 立ち絵fallback | 1536K | 二次対象のみ | ステージ |

診断ログは**stage ownerに含めない**: 既存512KiBバッファはprocess寿命の静的BSS
（`psp/fileio.cpp:43`付近）であり、§10の診断profile別予算で管理する。

### 4.2 GE eDRAM容量/寿命/所有者

| 領域 | cap | 所有者 | 寿命 |
|---|---:|---|---|
| 下位: FB×2 | 544KiB | レンダラ | フレーム交互 |
| 下位: Z | 272KiB | レンダラ | フレーム |
| 下位: surface cache | ≤512KiB | レンダラ | 常駐 |
| 下位: 未割当gap | ≥208KiB | 未割当 | — |
| 下位: portrait staging | 512KiB | portrait cache | アップロード時 |
| 上位: portrait予約 | 1536KiB | portrait cache | ステージ（player分跨ぎ常駐） |
| 上位: low-res予約枠（封印） | 136KiB | 封印機能 | — |
| 上位: spare | 376KiB | 未割当 | — |

### 4.3 ME eDRAM

| 領域 | 出荷 | 研究profile（出荷外） |
|---|---|---|
| 全4MiB | **UNUSED / JOBS=0**（不変条件#8） | §4.4の非権威ミラーのみ |

### 4.4 ME研究profile（define分離・出荷判断に不算入）

非権威ミラー限定（全ブロックにMain RAMマスター、チェックサム照合失敗は無言で
マスターへ）。1 defineで全除去可能。CRCフック常設。pause/resume・境界ノイズの
実績があるため、音声・ゲーム正しさに関与する位置への配置を禁止。

### 4.5 Optional RAM budget owner（唯一の確保者）

**guardの意味論**: guardは「**gameplay entry時点で2MiBの連続headroomが存在する
ことの検査**」である。解放後は通常mallocが消費し得るため「プレイ中常時2MiB」は
主張しない。常時保証が必要になった場合は、**保持型emergency reserve
（必須allocation失敗時に明示APIで解放→一度だけ再試行）**を別途導入する
（本Revではreserve=導入せず。導入時は本節を改訂して再承認）。

ステージ境界の順序（この順以外を禁止）:

1. 旧consumerをDisable/Detach（borrowed pointerの使用停止を確認）
2. 旧poolを解放
3. guard 2MiBを実mallocで確保（失敗→全optional OFF、現行経路で続行）
4. 新poolを優先度順にラダー確保
5. **pre-render/prefetch/coverage判定まで完了させる**（I1のFreeType
   pre-render等はこの段階。guard解放より先に完了必須）
6. 失敗poolをプール単位で全量rollback
7. **gameplayへ入る直前にguardを解放**（entry headroom検査の成立）

owner契約:
- **確保・解放はownerのみ**が行う。consumerは**borrowed pointerのみ**を持ち、
  自前freeを禁止。解放はDetach後にownerが実施。
- 既存の`TextHelper`は自前freeを行う（`src/TextHelper.cpp:856`付近）。
  **I1でowner管理へ移管すること（未移管のまま併用すると二重解放の余地）**。
- プレイ中のoptional追加確保は禁止。teardownは優先度逆順。
- 判定への`sceKernelTotalFreeMemSize`系/`pspSdkTotalFreeUserMemSize`使用禁止。
- 結果はboot note 1行（`optram guard=ok text=1536K bgmpre=320K anm=0`）。

---

## 5. ステージロード先読みとミスポリシー

### 5.1 許可区間

ステージ境界ロード、リザルト、Music Room入場。会話中は不許可。

### 5.2 I1: テキストキャッシュ

- gate: §4.5 owner経由の実確保ラダーへ置換。
- **呼出位置移設を含む（位置固定）**: 現状`src/Gui.cpp:697`の`PreRenderStageText()`は
  `src/GameManager.cpp:869`より先に走る。移設先は
  **`src/GameManager.cpp:897`（gameplay state設定の902行目より前）に固定**する
  （「869より後/ロード末尾」という幅のある指定を廃止）。
- 合格: `full=0 / runtime miss=0 / expected key coverage=100%`。全量が収まらない
  capでは**キャッシュ丸ごとOFF**（部分キャッシュ運用禁止）。
- **BGM開始順序（Rev.3.2追補、実機で確認された穴）**: ステージBGMの再生開始
  （AUDIO_STARTの実効、またはconsumerの解放）は**§4.5 step 7（guard解放）以降の
  gameplay entry直前**に置く。事前生成などのロード作業中にBGM再生位置を進めては
  ならない（M3+I1fix実機runで「ロード中に曲が先行し、リプレイ開始時点で曲が
  途中から」という位置ずれを確認）。修正後の合格条件: ステージ開始フレームと
  曲先頭の対応がPC版と同一（リプレイで検証）。

### 5.3 I8: BGM遷移先読み（generation結合設計）

前提事実: `ReopenBGM()`（SoundPlayerPsp.cpp:1741付近）はgenerationを更新するため、
ロード時のgenerationのままではバッファは無効になる。設計:

1. ステージロード時、ボス曲を**track/archive identity**（archiveパス＋
   `fmt.startOffset`）をキーに320KiB（5×64KiB）読み込み保持。
   **読み込みは専用の別FILE handleで行い、再生中ストリームのFILE位置を
   一切動かさない**。`totalLength < 320KiB`の曲はclamp（それも不可なら
   現行経路fallback）。generationには結合しない。
2. 遷移時、`ReopenBGM()`内でidentity一致を確認したら、以下の
   **publish transaction順を固定**して新generationへ結合する:

   ```
   playing=false → generation更新 → file lock/旧read排水 → ResetRing
   → identity/長さ検証（validSeedBytes確定: 満額320KiB or clamp値）
   → ringへcopy → FILE位置/gTrackCursor前進
   → gWriteFrame = seedFrames (= validSeedBytes / 4) をrelease-store
     （81920は320KiB満額時のみ）
   → file unlock
   → prefetchバッファをconsumerがDetach（実解放は§4.5 owner APIが実施）
   → playing=true
   ```

3. identity不一致/バッファ無し/検証失敗は現行ストリーミングへ（機能喪失なし）。
4. Stop/suspend/teardown時: consumer（producer/feeder/DACスレッド）の停止確認後、
   consumerがDetach→ownerが解放。suspend復帰後はバッファ無効（現行経路のみ）。
5. **I8が追加する同期readはロード区間のみ**（既存のBGMストリーミング読みは
   現行どおり継続する。「ゲーム中MS readゼロ」はI8の主張範囲外）。
6. **I8の合格条件はUND=0（複数runで再現）**。§10 audio行の「baseline非回帰
   （UND=1容認）」はI8には適用しない。

### 5.4 ミスポリシー

キャッシュ未ヒット=現行経路（外観/タイミング悪化ゼロ保証）。
新規のゲームプレイ同期MS I/O禁止（§10のカウンタで検証）。

---

## 6. 不変条件（完全版）

1. 描画品質・エフェクト・弾数・当たり判定・更新レート・音質・テキスト外観/内容/
   タイミング・ステージ挙動を変更しない。30FPS固定化で解決しない。
2. vsyncオーバーラップ経路を維持。フレーム中の追加syncを禁止。
3. GE立ち絵cache（s4:1408K hash5/5、s5:1280K 4/4、s6:1408K 5/5、fault全0）を維持。
4. BGM=Main RAM 384Kリング、SFX=Main RAMを出荷経路として維持。
5. SHIKIGAMI telemetry（wireサイズ凍結）を維持。
6. Music Room修正・stage3ブラー・リザルト演出・スペル名32K化を維持。
7. PSP-1000は独立profile（§8）。2000+のプール確保/プローブを一切実行しない。
8. 出荷ビルドのME eDRAM使用ゼロ（JOBS=0）。
9. 凍結sidecar（`ge4wrap_texv1.prx`/`kcall.prx`）はbyte同一維持。
10. 診断はRAMバッファ→走行後flush。ホットパスのMS書込禁止。
11. 配布はUSB直copy＋退避＋readback SHA。OTA禁止。
12. リプレイ中telemetry FPS欄を性能根拠に使わない。性能はPERF/ヒストグラムの
    壁時計値のみ。
13. 対象機種の予算主張はPSP-3000/model 3に限る。
14. calc側とdraw側の削減を同一増分で二重計上しない。
15. 予算表は実測合計と常に一致（BG/J*をRの外側へ再加算しない。未帰属は
    「R内未ログ分」「外側残差」として明示）。
16. 仮説(H)の削減値を確定予算・合格判定に使用しない。

### 維持する実証済み経路の一覧

vsyncオーバーラップ / GE立ち絵cache一式（bridge・staging・hash readback・
PREWARM COMPLETE診断）/ Main RAM BGMリング＋SFX / SHIKIGAMI（送信スレッド・
schema凍結）/ Music Roomのglyphキャッシュ修正とバンド公開 / stage3ブラー /
リザルト演出 / スペル名32Kアップロード / RAMバッファ診断ログ /
モノリシック弾描画経路（`PSP_BULLET_AXIS_FAST=0`）。

---

## 7. 増分の依存DAG

```
M1 (R20 OFF vs R21C 再現A/B) ────────────────────┐
M2 (draw帰属の完全化+境界修正+ヒストグラム) ──┬→ I3 (内容はM2結果で定義) ─→ S6 late追補増分
M3 (エミッタ内訳のPSP実測分解) ─→ I2a → I2b ──┘
                     └────────→ I2c (calc側。別増分・別計上)
I1 (テキストキャッシュ) …… 独立（owner=§4.5に依存）
I4 (行列submitキャッシュ/clear範囲) …… 独立・小粒
I5 (Scratchpadタイル) …… I2b合格後。必須設計項: 1要素byte数/16KiB内タイル発数/
    タイル→Main RAM書き戻し費用の実測
I6 (VFPUバッチ) …… I2c合格後。数値等価性/16Bアライン/ABI所有/fallback率<1%実証。
    旧axis-fast(fallback支配+0.16ms)の再利用禁止
I7 (フルダブルバッファ) …… GPU-bound窓の実測まで凍結。I8/I9をブロックしない
I8 (BGM先読み) …… §4.5 ownerに依存。I1と独立
I9 (ANM先読み) …… §4.5 ownerに依存。他と独立
```

### M2の設計（4段階＋数値gate）

1. **全draw priorityをcallback owner単位でログ**（現行の9個掲載から全priorityへ。
   priority 0/1/2/11/13-17を含む）
2. **全job合計＋chain overhead＝R を閉じる**。閉包許容誤差gate:
   **|R−Σ| ≤ max(0.2ms, Rの2%) /窓**。超過は計測設計不合格
3. 最大ownerの内部だけを**排他的カテゴリ**で分解:
   pack / 行列submit / state同期 / deferred flush / dcache writeback
4. **空タイマーのA/A計測**で計測負荷を校正。gate: **A/A差 ≤0.2ms/frame**。
   超過時はタイマー粒度を落として再設計
追加要件:
- §2の計測境界修正（前フレームGE完了後・swap前開始〜Finish後。swap費用計上）
- `mFrameBlockingGeUs`をGEへ再加算しない（post-vblank tailのみ）
- per-frame CPU+GEヒストグラム（RAMログ）
- **性能の主張は詳細タイマーを外した軽量buildで再測定した値のみ**で行う
  （ATTRIB profileの数値は帰属専用、§10）

### M3の設計

エミッタ経路の分離計時: linked-list走査 / VM・descriptor参照 / culling・回転 /
state同期 / 頂点store / deferred repack / dcache writeback。
母集団注意: BU窓平均6.5msと窓内最大1024発は別母集団（S5代表窓は平均~736発
≈8.8µs/call）。回転弾は4頂点96B、BUにはレーザー/アイテムを含む。
PCハーネスは正当性検証専用（PSP性能予測に不使用）。
数値gate（M2と同一方針）: **空タイマーA/A差 ≤0.2ms/frame**、
**BU閉包 |BU−各phase合計| ≤ max(0.2ms, BU×2%)**、性能主張は
軽量build再測定値のみ、**(H)→確定昇格は複数runで再現した場合のみ**。

### I2の分割

| # | 内容 | 計上先 |
|---|---|---|
| I2a | immutable descriptor事前計算 | draw |
| I2b | 描画順序を保存するrender snapshot/emitter | draw |
| I2c | calc側SoA（必要ならVFPU） | calc（J12）。drawと二重計上禁止 |

---

## 8. PSP-1000 profile（完全規則）

1. `TH07_PSP_1000`によるコンパイル時分離を維持。
2. 新規プール（テキストキャッシュ/先読み/Scratchpadタイル/ヒストグラム拡張）は
   **すべてコンパイル時除外**。実行時プローブも行わない。
3. 既存の実証経路を維持: 共有arena（`psp1000_arena`）/ u8 SFX / タイトルキャッシュ /
   MIST音声 / 毎行glyphフラッシュ（ヒープ断片化対策）。
4. 診断はRAMバッファ128KiB版（実装済み）を維持。
5. I2系エミッタの1000適用は別増分・別検証とし、60FPS計画のクリティカルパスに
   含めない。
6. volatileメモリ+4MiBは、採用時にOSK/utility非使用の確認とpause/resume実機検証を
   前提条件とする（現時点では不採用）。

---

## 9. 失敗/フォールバック規則（完全版）

1. プール確保失敗→ラダー降段→プール単位全量rollback→現行経路（機能喪失ゼロ）。
2. guard確保失敗→全optional OFF。
3. エミッタ系（I2/I3/I5/I6）はdefineでOFF→現行モノリシック経路へ即復帰。
4. 一致検証はPC先行（正当性のみ）＋PSP実機ハッシュ（§10）合格後に採用。
5. 実機投入前に「入る/維持/失われる」一覧を提出。喪失があれば投入しない。
6. 各増分はビルドID+1・退避・readback SHA・同一Lunaticリプレイ計測の定型に従う。
7. M系計測はゲーム動作に影響しないことをバイナリ監査と§10 simulation hashで確認。

---

## 10. 受入マトリクス（実機、同一Lunaticリプレイ、複数run）

| 領域 | 検証内容 | 合格 |
|---|---|---|
| simulation | RNG・弾/敵/player状態・collision・scoreの毎フレームhash（RAMログ） | 基準ビルドと全フレーム一致 |
| render | PSP上のvertex/state streamハッシュ＋選択フレームframebuffer readback | 一致（PCピクセルhash単独は不合格根拠にしない） |
| text | cached/uncachedの32KiB surface一致、miss=0、full=0、coverage=100% | 全条件 |
| audio | PCM CRC・総サンプル数・曲切替位置・UND/FATAL・pause/resume往復 | 基準一致、UND regressionなし |
| I/O | フェーズラベル付きreadカウンタ | ゲームプレイ中の新規同期read=0 |
| 性能 | §3.1のper-frame指標（修正境界）をstage4-6複数runで | 全stage非回帰。最終は診断OFF相当で全域0 miss |

### gateの階層（混同禁止）

- 中間gate（増分単位）: §3.5の過渡値、各Iの個別削減目標。
- **最終性能gate**: §3.1。
- **最終出荷gate**: §3.1 ＋ **本表（§10）全行合格** ＋ **§6全不変条件** ＋
  **lost=0**（機能喪失ゼロの一覧提出）。

### 診断profileの分離（build/run別）

容量根拠: stage4-6は約81,360フレームあり、64bit simulation hashだけで約651KiB。
既存512KiB静的バッファ（process寿命BSS、`fileio.cpp:43`）には収まらないため、
検証は目的別のbuild/runに分離する。**各profileでバッファoverflow/dropは即不合格**。

| profile | 内容 | RAMログ予算 | 性能主張 |
|---|---|---:|---|
| ATTRIB | M2/M3の帰属タイマー | 512KiB | **禁止**（帰属専用） |
| CORRECTNESS | simulation/render/audio hash | 1MiB（高メモリ機のみ、静的BSS拡張） | 禁止 |
| PERF-ACCEPT | 最小タイマー＋ヒストグラム＋missカウンタ | 128KiB | ここの値のみ |
| RELEASE | 診断完全OFF | 0 | 最終確認run |

---

## 11. 次のアクション（レビュー判定を反映）

1. **M1: GO（着手可）**: R20 OFF vs R21C再現A/B 1組
2. **M2/M3: 本Rev.3.1承認後にGO**（§7の4段階＋境界/二重計上修正＋数値gate＋
   profile分離を実装）
3. **I1/I8/Optional owner: §4.5と§5.3の修正済み設計に基づき、M系と独立に
   レビュー承認後着手可**
4. **最終60FPS設計の再承認**: M2/M3結果でS5/S6予算・I3・VFPU(I6)・I9を確定して
   から
