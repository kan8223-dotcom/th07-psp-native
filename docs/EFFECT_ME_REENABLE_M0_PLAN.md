# Effect layer 0/3 ME復帰: 専用M0階段

状態: **PC設計・実コード監査完了 / コア実装なし / D:投入なし / 実機確認なし**。

この文書は、I-ME8で隔離したEffect layer 0/3のME描画準備を、安全に再開する
ための実装契約である。比較元はRID30 A1-MOVEに固定する。C1 16bit頂点、C2
record痩身、その他の性能増分とは混ぜない。この文書自体はbuild/deployを許可しない。

## 1. 監査で確定した現在地

### 1.1 過去ログから言えること

旧I-ME8 ALL-INはEffectだけの単独試験ではない。Effect、trusted seed、lean cache、
GUI tileを同時に有効化しており、`artifacts/TH07PSP_BOOT.IME8.STARTUPFAIL.LOG`
は汎用M0A完了後で止まっている。Effect固有の開始・完了点は記録されていない。

一方、Effectをcompile outしたI-ME8Rもlean cache有効のままdirect-list selftest中に
停止した。さらに、その後のItem診断で「SCが同じMain RAM物理アドレスを書き換えた
連続jobに対し、MEのcached aliasが旧next値を再読する」問題が実機で確定し、RID29は
可変Item入力だけをKSEG uncached volatile読みに変えて起動合格した。

したがって、過去ログからの正しい結論は次の2点だけである。

- Effectが停止原因だったとは断定できない。
- 現Effect経路はItemで確定したalias事故への修正をまだ受けておらず、再投入不可。

旧`psp3000-me-render-i8-allin-build`は`PSP_ME_RENDER_LEAN_CACHE_OWNERSHIP=1`
を含み、現在のMakefileはこの設定をhardware-rejectedとしてbuild自体を拒否する。
復帰profileの土台には使わない。

### 1.2 再利用できる実装

次の骨格は設計上そのまま再利用できる。

| 部位 | 現行実装 | 判定 |
|---|---|---|
| SC post-update capture | `EffectManager::PspPrepareMeEffectRenderStream` | 再利用可。layer 0/3のリスト、slot generation、active bit、`vm.pos`副作用、SC sin/cosを確定する |
| 128-byte descriptor | `PspBuildMeEffectRenderLayout` / `Th07PspMeRenderEffectLayout` | 再利用可。408 slot、head/tail、serial/count、pool identityを記述する |
| ME structural validation | `me_render_stream_effect_layout_valid` | 再利用可。live値をdereferenceせず範囲・ABI・重複を検査する現在の方針を維持する |
| record再構成とexpand | `me_render_stream_reconstruct_effect_record` / `me_render_stream_expand_kernel` | 演算・primitive/cull部分は再利用可。ただしlive read aliasは全面修正必須 |
| Effect transaction | `process_render_stream_on_me` | 再利用可。layer 0/3を一つのoptional transactionとしてrollbackする |
| READY検証 | `PspValidateMeEffectRenderStream` / `me_render_stream_effect_completion_valid` | 再利用可。Item/Bulletとは独立にEffectを拒否できる |
| 表示順 | `PspConsumeMeEffectRenderStream` | 再利用可。`layer0 ME -> layer2 canonical -> Flush -> layer3 ME`を維持する |
| output cache fence | submit/retire/GE promotionのI-ME7 full-pool fence | 再利用必須。狭域化・lean化は禁止 |
| 既存Python test | `tests/test_psp_me_effect_render_stream.py` | 構造確認として再利用可。ただし実データ、連続job、cache aliasを一切実行していない |

### 1.3 実装前に必ず直す箇所

`psp/audio_me.c`のEffect live traversalは現在、以下をすべて
`0x80000000 | physical`のcached aliasから読んでいる。

- active bitmap
- `Effect`本体の`inUseFlag`、`is2D`、`next`
- slot generation（開始bracketと終了bracketの両方）
- `AnmVm`のpos、flags、color、sprite pointer、rotation、scale、UV scroll
- SCが書いたsin/cos sidecar
- prepare serial、prepared serial、prepared layer counts

加えて、次EffectとVMへprefetchを出している。これらは全て毎frame更新される
SC authorityであり、RID29のItem修正と同じく**uncached volatile限定**に変える。

実装はItem専用名を増殖させず、例えば次の共有helperへ整理する。

- `me_render_stream_live_uncached(phys)` -> `0x40000000 | phys`
- `me_render_stream_live_load_u32(...)`
- `me_render_stream_live_load_u8(...)`

Effect/VM/sidecar/authorityにcached aliasとprefetchを残してはならない。対して、stage
assetとしてjob中不変のsprite metadataとrepresentative tableだけは現在どおりcached
alias + prefetchを維持してよい。sprite pointerそのものはmutable VMからuncachedで読む。

generationはlive recordの読み始めと読み終わりにuncached volatileで二度読みし、同値で
なければそのrecordをrejectする。compilerがbracketをCSE/hoistできない形をPython testと
Allegrex disassemblyの両方で確認する。

## 2. 不変条件

1. ゲーム状態、Effect update、生成/消滅、RNG、ECL、当たり判定はSC権威のまま。
2. 対象は描画準備だけ。layer 1/2や2D EffectをMEへ移さない。
3. layer 0とlayer 3は一つのtransaction。片方だけ採用しない。
4. 可視順は必ず **layer 0 -> layer 2 canonical -> layer 3**。
5. layer 0をGEへ1命令でもsubmitした後にcanonical fallbackへ戻らない。全検証を最初の
   submit前に完了させる。
6. layer 2後は必ず`g_AnmManager->Flush()`してからlayer 3 direct runsをsubmitする。
7. Effectが不合格ならそのframeのlayer 0/2/3を全てSC canonicalで描く。Item/Bullet MEは
   独立して継続する。
8. SCはcommand publish前にlive authorityをwritebackする。MEは最初のlive readより前に
   full dcache invalidateを行う。その後のmutable readはuncached volatileだけを使う。
9. output vertex/run poolはI-ME7のfull-pool writeback-invalidate契約を維持する。
10. Effect失敗時のrollbackは共有auxiliary cache lineを考慮し、Effectだけの狭域
    invalidateを行わない。現行どおりauxiliary全域を捨ててItemを再構築する。
11. PSP-1000は対象外。PSP-3000/model 3のみ。Go/model 4もこのM0では対象外。
12. C1/C2/背景/自機shot/音声の変更を同じRIDへ混ぜない。

## 3. runtime gateと降格設計

compile defineだけでgameplayを有効にしない。新たに
`gMeEffectRenderEnabled`相当のatomic runtime gateを設け、boot時は0とする。

推奨状態は次の通り。

| 状態 | 意味 |
|---|---|
| `UNAVAILABLE` | compile外またはME無効 |
| `TESTING_SYNTHETIC` | synthetic M0中 |
| `WAIT_REAL_CAPTURE` | gameplayは全Effect SC、最初の適格な実Effect frameを待つ |
| `TESTING_COMMAND` | real captureのshadow command中。まだ全Effect SC |
| `ENABLED` | 全M0合格後だけjobへEffect flagを付け、consumeを許可 |
| `SAFE_FALLBACK` | Effect固有のclean failure。以後process lifetime中は全Effect SC |
| `COMMON_FATAL` | mailbox/stack/timeout等でlate ME writer不在を証明できない |

`BulletManager`のjob buildと`Th07PspTryConsumeMeEffectStream`は両方runtime gateを検査する。
gateが0なら、Effect layout/version/flagをjobへ載せず、`EffectManager::OnDraw`は元の
canonical 0/2/3を通る。

以下はEffect固有のsafe failureとして扱える。

- 完了済みcommandのEffect record/layout/hash/run/vertex不一致
- layer count、generation、endpoint、blend分類、capacity、guardのclean reject
- READY時のEffect authority mismatch（まだEffect run未submit）
- M0 readback mismatch

この場合はEffect gateだけを0にし、全stream slotをME idle時に再初期化した後、
Effect flagなしのRID30相当Item/Bullet command-10 selftestを再実行する。再試験合格なら
ゲームを起動し、boot noteとSHIKIGAMIに
`EFFECT_ME=OFF / SAFE_FALLBACK / BULLET_ME=ON / ITEM_ME=<state>`を出す。

timeout、worker/stack fault、command ownership不明、FCR31破壊のようにlate writer不在を
証明できない場合だけはEffect単独降格を装ってはならない。これは共通ME fatalであり、
既存のfail-stopを維持する。

runtime中のEffect semantic rejectは、そのframeを全Effect canonicalへ戻し、Item/Bulletを
継続する。最初のclean Effect mismatchでprocess lifetimeのEffect gateを閉じる。
「失敗を毎frame繰り返してMEを浪費する」運用はしない。

## 4. 専用M0階段

新しいdefault-off変数を`PROFILE_STAMP`へ追加する。

```text
PSP_ME_EFFECT_M0_LEVEL ?= 0   # 0,1,2,3,4のみ
```

各levelは累積だが、build target/RID/SHAは別にする。Effectコードをcompileするprofileでも
level 4合格前はruntime gateを上げない。旧ALL-IN targetは使用しない。

### 4.0 予約build targetと研究RID

実装時の名前と識別子を以下に固定する。比較元は全levelで
`psp3000-a1-item-motion-build` / RID30 (`0x26083130u`)。C1/C2、lean cache、
その他の性能増分は全て0に固定し、Effect M0 level以外を混ぜない。

| 階段 | 予約Make target | `PSP_ME_EFFECT_M0_LEVEL` | 予約build ID / RID | gameplay Effect consume |
|---|---|---:|---|---|
| E1 synthetic | `psp3000-effect-m0-e1-synthetic-build` | 1 | `0x260901e1u` | 常にOFF |
| E2 real capture | `psp3000-effect-m0-e2-real-capture-build` | 2 | `0x260901e2u` | 常にOFF |
| E3 command | `psp3000-effect-m0-e3-command-build` | 3 | `0x260901e3u` | 常にOFF |
| E4 consume | `psp3000-effect-m0-e4-consume-build` | 4 | `0x260901e4u` | E1〜E3合格後のみgate可 |

これらは**将来のMake targetとRIDの予約**であり、現時点でtarget自体は
Makefileに実装しない。予約RIDは既存RID30、C1 (`0x260831c1u`〜
`0x260831c3u`)、C2 (`0x260831d1u`〜`0x260831dfu`)と衝突しない。

**成果物のPC buildも、D:/H:への投入も、実機起動もこの予約では
許可されない。** 各階段の投入は、PC gate合格後に田中さんからその
1件に対する明示的な「いれて」が出た場合のみ可。H:は別途明示依頼が
なければ常に対象外とし、PSP-1000 buildも禁止する。

### M0-E1: synthetic（PC + boot、consumeなし）

目的はEffect ABI、SC oracle、bounds、atomic rollbackを実ゲーム状態から分離して証明する
こと。64-byte aligned fixtureに408個のsynthetic Effect、generation、active bitmap、
sin/cos、serial/count、sprite/representativeを置き、前後guardを付ける。

必須case:

| case | layer 0 | layer 3 | 追加条件 |
|---|---:|---:|---|
| empty | 0 | 0 | head/tail=0、serialは有効。output/run=0 |
| one-L0 | 1 | 0 | non-additive、axis pair |
| one-L3 | 0 | 1 | additive、rotated quad |
| one-each | 1 | 1 | 二層のrun/vertex offsetを検査 |
| max-L0 | 408 | 0 | pool上限、全slot一意 |
| max-L3 | 0 | 408 | pool上限、全slot一意 |
| max-mixed | 204 | 204 | 二層合計408、source/state/run境界を交互化 |
| aux-boundary | Effect合計408 | - | Item count 692なら合計1100で合格、693ならsubmit前reject |

fixtureにはvisible/invisible、alpha 0、anchor 0..3、color/color2、z-write on/off、
rotated/non-rotated、viewport内/境界外、sprite切替を含める。SC oracleはproductionの
ME reconstruct/pack helperを呼ばず、record hash、vertex bytes、run descriptorsを独立に
生成する。

異常caseも一回のbootで総なめする: duplicate/cycle、wrong tail、generation変化、active
bit落ち、inUse=0、is2D=1、layer 0 additive、layer 3 non-additive、bad sprite、serial/count
変化、output/run overflow。全異常はEffect出力0、Effect result reject、Item/Bullet領域と
全guard不変でなければならない。

### M0-E2: real capture（gameplay shadow、commandなし）

`EffectManager::OnUpdate`が4本のcanonical listを作り終えた直後、最初の適格frameで一度
だけSC captureを行う。ここではME commandもGE consumeも行わない。

検査内容:

1. layer 0/3をcanonical順に走査し、全slotのpool範囲、重複、active、generation、inUse、
   is2D、blend分類、head/tail/countを検査する。
2. canonical Drawが観測可能に書く`vm.pos`とSC sin/cosを通常prepareと同じ順で確定する。
3. SC独立oracle recordを作り、layoutの全32 wordsとrecord hashを保存する。
4. capture後にgeneration/serial/count/head/tailを再確認する。
5. このframeの実描画は必ずcanonical 0/2/3とする。

0件frameは`WAIT_REAL_CAPTURE`を維持してよい。実Effectが現れるまでゲームを止めたり、
起動失敗にしたりしない。

### M0-E3: command（synthetic + frozen real capture、consumeなし）

M0-E1の0/1/max fixtureを実ME command-10へ通す。続いて、M0-E2と同じpost-update点で
real captureを凍結し、そのgame threadを止めたままEffect-only shadow commandを完了
させる。Bullet/Item/Effectの実描画は全てcanonicalでよく、性能値には使わない。

ME completionの次をSC oracleと完全一致させる。

- layer別record count/hash
- layer別vertex count/bytes/hash
- layer別run countと全descriptor
- primitive、source、logical layer、blend、z-write、firstVertex
- output/run guards、FCR31、token/version/serial/generation

#### 連続job alias試験

同じfixture物理アドレスを移設せず、少なくともA -> B -> Cを連続submitする。

- A: 同じhead slotをlayer 0の1件、`next=0`、generation G、position/color A。
- B: 同じheadの`next`を第2slotへ変更し、layer 3へ分類変更、generation G+1、
  position/color/sprite/sin/cos/serial/countも変更。
- C: 同じheadを再び1件へ短縮し、generation G+2、別値に変更。

各job前にSC cached writesをpublishし、MEは毎job新しいuncached live値を読む。BがAの
`next=0`、CがBのnext/VM/serialを再利用した時点で不合格とする。これはRID26/27のIR
staleをEffect ABIで直接再現する回帰試験である。

cache順序は次に固定する。

1. SCがfixture/live authorityをcached aliasで更新。
2. `sceKernelDcacheWritebackAll()`でlive authorityをpublish。
3. SCがoutput/run全poolを`WritebackInvalidateRange`してMEへ所有権移譲。
4. MEがlayoutをlocal copy後、live pointerを辿る前に
   `meLibDcacheWritebackInvalidateAll()`。
5. mutable Effect/VM/sidecar/authorityはuncached volatile、immutable sprite/repsだけcached。
6. MEがvertex/runの実extentをwritebackし、SC retireが同extentをinvalidate。
7. GEへはまだ渡さずREADYをrelease。次jobで同じlive物理アドレスを書き換える。

### M0-E4: consume（実機GE、最後の段）

E1-E3が全合格したprofileだけruntime gateを上げる。最初の可視runより前にjob、READY、
Effect authority、全run/vertex範囲を検証する。consume順は次のコード形から変えない。

```text
validate all Effect0/3
begin one GE owner (必要なときだけ)
submit layer-0 ME run range
draw layer-2 canonical
flush canonical layer-2 queue
submit layer-3 ME run range
priority-10 Item/Bulletが同じtoken ownerを継続
```

0件、layer 0だけ、layer 3だけ、両方ありを実機で確認する。synthetic maxはE3で
capacity/bytesを証明し、E4ではisolated surfaceへcanonicalとME版を描いてcolor/depth
readbackを完全一致させる。最低でも次を含める。

- axis GU_SPRITESとrotated indexed quad
- layer 0 non-additive、layer 3 additive
- layer 2 billboardが両者の間にある重なりsentinel
- z-write enabled/disabled、alpha/color2、viewport edge
- display-list restartをlayer 2 flush付近へ強制したcase

order traceは観測専用とし、最初のlayer 0 submit後に失敗し得るassertを置かない。
PC source testでcall順を固定し、実機はframebuffer/depth readbackとtrace `0,2,3`で確認する。

## 5. PC testとbinary audit

既存`tests/test_psp_me_effect_render_stream.py`を拡張し、別に
`tests/test_psp_me_effect_reenable_m0.py`と独立SC oracle harnessを追加する。

最低限の自動gate:

1. Effect mutable入力の全phys fieldがshared uncached helperを通り、Effect/VM/nextへの
   cached alias/prefetchがゼロ。sprite/repsだけcachedである。
2. generation開始/終了bracketとserial/count finishがvolatile loadである。
3. 0/1/408、mixed、1100境界、異常matrix、A->B->C連続jobのoracle一致。
4. Effect failure時にlayer 0/3のvertex/runが両方0となり、Itemは再構築、Bullet結果は独立。
5. runtime gate OFFではjob version/flag/layout、prepare walk、Effect consumeが一切発生せず、
   RID30 canonical OnDrawへ戻る。
6. clean Effect M0 failure -> Effect OFF -> slot reset -> EffectなしItem/Bullet selftest PASSの
   降格経路。Effect不一致でcold rebootしない。
7. common timeout/stack/mailbox faultはsafe fallback扱いされない。
8. consumeの全validationが最初のsubmitより前、順序が0 -> 2 -> Flush -> 3。
9. Make profileはRID30 + Effect M0だけ。lean=0、C1=0、C2=0、PSP_1000=0。
10. full Python suite、PSP-3000 build、Allegrex binary audit。PSP-1000 buildは禁止。

binary auditでは、mutable physからKSEG cached aliasを作る`or 0x80000000`がEffect
reconstruct/finishに残っていないこと、KSEG uncached `0x40000000`とvolatile `lw/lbu`、
full cache fences、distinct Effect stream version/RID、runtime gate branchを確認する。

## 6. 実装対象と変更量の見立て

| ファイル | 必要作業 |
|---|---|
| `psp/audio_me.c` | shared uncached live helper、Effect reconstruct/finish修正、synthetic fixture/連続job M0、Effect diag/runtime gate、clean降格 |
| `psp/audio_me.h` | M0/diag state・summary API、必要ならdiagnostic hash fields。production ABIを増やす場合はversion更新 |
| `src/EffectManager.cpp/.hpp` | real SC oracle capture、runtime gateに従うprepare/consume、canonical fallback、order telemetry |
| `src/BulletManager.cpp` | gate ON時だけEffect layout/flagをpublish、shadow command比較、Effect reject後もItem/Bulletを継続 |
| `Makefile` | `PSP_ME_EFFECT_M0_LEVEL`、profile stamp、RID30固定のE1-E4 target、旧ALL-IN不使用 |
| `tests/test_psp_me_effect_render_stream.py` | uncached、降格、order、atomicityの構造gateを追加 |
| `tests/test_psp_me_effect_reenable_m0.py` + harness | 0/1/max、異常matrix、連続job、独立oracle |

現行Effect ABI、layout、expand、atomic rollback、READY/consumeの大部分は再利用できる。
主要な新規作業は「mutable live readのuncached化」「Effect専用M0」「runtime gateと降格」
であり、Effect描画アルゴリズムの書き直しは不要である。

## 7. 昇格条件

Effectを性能候補と呼べるのは次を全て満たした後だけ。

- E1-E4が順番に合格し、途中levelを飛ばしていない。
- synthetic/real/連続jobでmismatch、guard、protocol、FCR31 faultが全0。
- framebuffer/depthがcanonical RID30と完全一致。
- Effect不合格注入でタイトル/ゲームが起動し、全Effect SC、Item/Bullet ME継続を確認。
- 同一Lunatic replayでリプレイhash、スコア/PIV、Effect数・順序、PCM CRCが一致。
- SHIKIGAMIで`EFFECT_ME=ON`または`SAFE_FALLBACK`をUSBなしに判定可能。
- その後のPERF-ACCEPT runでSC時間とME deadline missが悪化しない。

M0合格前のD:投入、旧I-ME8 ALL-IN再利用、lean cache再有効化、EffectとC1/C2の同時判定は
すべて禁止する。
