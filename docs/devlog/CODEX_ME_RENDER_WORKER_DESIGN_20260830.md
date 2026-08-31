# Codex統合設計: PSP-3000 ME Render Worker

日付: 2026-08-30  
状態: **設計のみ。コード変更・ビルド・メモステ投入・実機確認は未実施**  
対象基準: accepted DENSE（ROTATED_DIRECT + UNIFIED_QUADS + ONEPASS、SPRXDは不採用）  
対象機: PSP-3000 / model 3。PSP-1000・2000・Goは本設計の性能コミット外。

この文書は `FABLE_ME_OFFLOAD_DESIGN_20260830.md` Rev.1を実コードと実機ログで再監査し、
採用可能な部分を統合した実装前設計である。

## 0. 結論

MEの最も効果的で、かつゲームの正しさを壊しにくい使い方は、**追加RAMとしてではなく、
確定済みの弾描画snapshotからGE用頂点列を作る非同期ワーカー**として使うこと。

- SCに残す: 弾の移動、ECL/ANM script、当たり判定、graze、消去、item/sound、RNG、
  linked-list publication、`AnmVm`への可観測な書込み、renderer state、GE submit。
- MEへ渡す: immutableなPOD snapshotからのcull、quad座標計算、色計算、UV付与、
  native 24-byte頂点packing、順序を変えないrun記述子生成。
- 共有データは全てMain RAM。**ME eDRAMは上位・下位とも0 byteのまま**。
- SCはME完了を待たない。期限までに未完、不一致、異常なら、そのフレーム全体を
  accepted DENSE経路で描く。
- 実際に1フレーム古い頂点を描く方式、MEからのGE命令発行、live `Bullet` / `AnmVm`
  pointer共有は採用しない。

効果の現時点の正直な見積は次のとおり。

| 段階 | W15 critical削減見積 | W15 19.962msからの見込 | 判定 |
|---|---:|---:|---|
| copy版（最初の性能増分） | **1.3〜2.4ms (H)** | 17.6〜18.7ms | 体感改善は見込めるが60固定には不足 |
| direct-submit版 | **2.0〜3.3ms (H)** | 16.7〜18.0ms | 上振れ時だけ60圏、固定の保証はない |
| direct + 後段の純粋update補助 | **未算定** | 未算定 | 非blocking dependency設計がないため予算へ入れない |

`(H)`は仮説値。最初の実機microbenchでsnapshot、cache、ME kernel、締切余裕を
別々に測るまで、リリース性能の主張には使わない。

## 1. 実測の土台

### 1.1 Stage 6 DENSE実測

根拠:

- `artifacts/final60-dense-slice-run-20260830/README.md`
- `artifacts/final60-dense-slice-run-20260830/current/TH07PSP_BOOT.DENSE.LOG`

| 窓 | critical | CPU | 弾update | 弾draw | 弾合計 | 弾数相当/frame | miss/120 |
|---|---:|---:|---:|---:|---:|---:|---:|
| W12 | 15.510 | 14.051 | 2.473 | 2.919 | 5.392 | 522.7 | 39 |
| W13 | 16.228 | 14.719 | 2.564 | 3.240 | 5.804 | 576.7 | 48 |
| W14 | 18.932 | 18.323 | 3.708 | 3.883 | 7.591 | 689.9 | 94 |
| W15 | 19.962 | 19.395 | 4.289 | 5.074 | 9.363 | 884.3 | 98 |

W15はGE tailが約0.57msで、主律速はCPU。従ってME並列化の方向自体は正しい。
ただし、弾draw 5.074msを全部消せるわけではない。

### 1.2 楽観参照値と、既に測れた移動量

他の全費用が変わらず弾drawだけが仮に0になった場合の楽観的な参照floorは次のとおり。

| 窓 | critical - 弾draw | 楽観参照上の余裕 |
|---|---:|---:|
| W12 | 12.591ms | 大 |
| W13 | 12.988ms | 大 |
| W14 | 15.049ms | 60圏 |
| W15 | 14.888ms | 60圏 |

これは実装見積でも厳密な数学上限でもない。並列化に伴うbus競合、snapshot、GE/SCの律速移動を
含まない「弾draw欄だけを差し引いた値」である。

WARM_QUEUE実機runでは、80-byte/発の事前計算によってdrawからupdateへ移った量が
W12/13/14/15でそれぞれ1.472 / 1.738 / 1.907 / 2.752msだった。一方、そのcapture費が
ほぼ同量増え、critical改善は最大0.203msに留まった。

この移動量には純粋なcorner計算だけでなく、live Bullet/VM参照、SC VFPU sin/cos、render cache更新、
VM副作用の前倒しが含まれる。従って2.752msを「MEへそのまま移せるgeometry量」とは扱わない。

この結果は二つを示す。

1. 1.5〜2.8ms級のdraw側work bundleは実在する。
2. SC上で別queueへ書き直すだけでは相殺される。MEで本当に重ねて隠し、recordを小さくし、
   draw側の二重検査を避けない限り勝てない。

### 1.3 MEの速さを過大評価しない

`psp-me-bench`のPSP-3000/333MHz実測は、PIとMANDELでSC単独からDUALへ約2.0倍、
PRIMEで約2.9倍。ただしDUALは仕事をSCとMEへ半分ずつ分け、遅い側の時間を採る測定である。
従って一般的な浮動小数点処理で「ME単体がSCの3倍」なのではなく、**MEは概ねSC 1個分、
二つを完全に重ねられれば合計が約2倍**と読むのが妥当。

この設計の利得源はMEの単体速度ではなく、Present/VBlankと次フレームdraw priority 0〜9に
ME処理を重ねることにある。

## 2. Fable Rev.1の採否

| Fable案 | 判定 | 統合設計での扱い |
|---|---|---|
| 弾calcを触らずdraw準備だけMEへ | 採用 | 最重要境界として固定 |
| 新しいsprite expand job | 採用・再設計 | 既存同期APIとは別のasync begin/probe/collectにする |
| Main RAM snapshot / Main RAM頂点 | 採用 | GEがME eDRAMを読めないため必須 |
| bit一致のCORRECTNESS gate | 採用 | vertex hashだけでなくsimulation/FB/audio/lifecycleも含める |
| 最初に合成jobを測る | 採用 | FPU一致、cache、deadline slackも同時に測る |
| 128発×複数jobで音声jobを挟む | 棄却 | 現accepted版はBGM/SFXともSC、ME JOBS=0。1 frame 1 jobの方が往復が少ない |
| `sceGuCallList`予約スロット | 棄却 | GU direct listの消費中に未完成領域を予約する必要がなく、安全性も証明されていない |
| 20B/発snapshot | 保留 | exact stateには不足する可能性が高い。32/48/64Bを実測して決める |
| 上位ME eDRAMを短寿命scratchに使用 | 棄却 | 上下とも実機破損・聴感noise済みで、田中さんの全面不使用判断に反する |
| W15 -3.2〜3.6msを主見積 | 下方修正 | Fable値はCPU、本文値はcriticalで同一指標ではない。採否用レンジはcopy 1.3〜2.4 / direct 2.0〜3.3ms |
| audio mailbox待ちが主制約 | 訂正 | ME音声jobはない。SC音声threadとMain RAM bus競合、UND=0は引き続き検査 |

## 3. 現行コード上の境界

### 3.1 現accepted profileのME状態

`PSP_MECC_AUDIO_4M=1`でも `TH07_PSP_BGM_MAIN_RAM` と
`TH07_PSP_SFX_MAIN_RAM` が定義され、`SoundPlayerPsp.cpp`はcustom coreを起動しない。
実機telemetryも `ME_EDRAM=UNUSED / JOBS=0 / MAXWAIT=0us`。

したがってrender workerは既存音声jobへの追加ではなく、**新しくMEのprocess-wide ownershipを
開始する機能**になる。`SoundPlayer::Release()`が現在ME shutdownを所有しているため、実装時は
lifecycleを音声クラスから独立したserviceへ移す必要がある。

### 3.2 既存vertex APIは性能経路に使えない

`th07_psp_me_vertex_pack()`は次の構造。

- SCが入力cacheをwriteback
- 1個のmailboxへcommandをpublish
- SCが最大6ms poll/wait
- MEがMain RAMの専用arenaへpack
- 完了後にSCへ戻る

これは起動selftestとM0の部品には使えるが、同期waitのため本番並列化には使えない。
本番APIは `submit()` / `try_complete()` / `retire()` に分け、draw hot pathは一度のacquire loadだけで
未完を判定する。

現workerはidle時にもuncached mailboxをbusy-pollする。jobがないtitle/menu区間でMain RAM busを
無駄に叩かないよう、render worker loopには読み出し間隔を広げるNOP backoffを入れ、M-ME0Bでは
「ME起動・job 0」のA/Bも測る。割込み駆動化は既存coreに証拠がないため前提にしない。

### 3.3 FPU/VFPU

現TH07 workerはfloatをopaqueなIEEE-754 bit列としてpackするだけで、ME上の浮動小数点ABIを
契約していない。独立した `psp-me-bench` ではscalar FPUが実機動作済みだが、TH07 workerと
同じprologue、FCR31、compiler options、cache/lifecycle下でのbit一致は未証明。

- scalar FPU: M0でbit一致を証明できた場合だけ使用。
- VFPU: context save/restoreと実機契約がないため本設計では使用禁止。
- scalar FPUが不一致なら、単純interleaveだけでは削減不足と判断し、MERW本実装はNO-GO。

## 4. フレームpipeline

PSP版 `GameWindow::Render()` は「draw → calc → Present」の順である。従ってcalcで確定した状態は
次のdrawで表示される。ここを利用すれば、見た目を1フレーム遅らせずにMEへ時間を与えられる。

```text
frame N
SC:  draw(N-1) -> calc(N) -> post-chain capture/publish -> Present/VBlank
ME:                                      [ expand vertices for N ]

frame N+1
SC:  draw priority 0..9 ------------------------------> bullet priority 10
ME:  [continue if needed] ---------------------------> READY deadline
SC:                                                    ready: consume
                                                       not ready: DENSE fallback
```

### 4.1 publish位置

`RunCalcChain()` の非zero returnだけを完了根拠にしてはいけない。`BREAK`も1を返し、priority 2等で
Bulletへ到達せず抜ける場合がある。calc priority 17より後ろに専用のpriority 18
`ME_RENDER_CALC_COMPLETE` sentinelを置き、そのsentinelまで到達したpassだけにcompletion generationを付ける。
priority 17の`RESTART_FROM_FIRST_JOB`が起きたpassや途中`BREAK`はsentinelへ到達しないためpublish不可。

最初の配線では既存の `g_SoundPlayer.ProcessQueues()` の順序を変えず、sentinel済みgenerationを
GameWindow側で確認してからPresent前にpublishする。固定30時は、calc後に決まる
`g_PspDrawNextFrame`を見て**次のRenderが実際にdrawするstateだけ**をpublishし、update-only frameの
obsolete jobで単一workerを塞がない。音声queue処理との並列化は、意味論と効果を別途測ってからの
後段候補とする。

実装段階は二つに分ける。

1. correctness版: post-chainで6本のcanonical bullet listを再走査してsnapshot化。
2. 性能版: priority 12のhot loopでcompact recordを仮captureし、post-chainで
   manager/stage mutation epochが不変の場合だけpublish。priority 13以降でclear等があれば全破棄。

後者は二重scanを避けられるが、correctness版で意味論を確定してから入れる。

### 4.2 consume位置

Bullet draw priority 10のlaser/item描画後、bullet listを描く直前。

- READY、frameSeq、stage/manager epoch、global signature、count/boundsを一括検査。
- 未完なら**待たずに**accepted DENSE（ONEPASS → canonical `Bullet::Draw`）へ戻る。
- 有効ならrecord順に現行と同じ`AnmVm`副作用をSCがcommitする。
- GE state変更とdraw command発行はSCだけが行う。
- 実際のlist順、6 bucket順、z順、UQのprimitive形式は変更しない。

本設計は**final-coordinate + draw時global signature方式**に固定する。post-calc時点の
offset/colorMul/viewport/clip等をMEが最終頂点へ焼き、priority 10で全globalがbitwise同一かを一括比較する。
同一とは仮定せず、不一致はframe全体fallback。`eligible / published / ready / consumed /
signatureDrop`を別々に数え、ready率だけでなく対象窓の**consumed coverage 100%**を性能版のgateにする。
offset非依存outputはdraw時にSC patchが必要で完成native頂点という目的と矛盾するため、別案として残さない。

I-ME2までの採用単位はframe全体とする。未対応recordが1件でもあればそのframeは全体fallback。
W15にはONEPASS fallbackが4.2%程度あるため、NORMAL+autoRotateだけを対象にすると実用上ほぼ毎frame
fallbackになり得る。性能版へ進む前にspawn/despawn、non-auto、negative scale、bottom clip、
invisible/cullを含む実際の全VM roleをgeneric snapshotで表現し、
対象窓の**frame coverage 100%**を満たすこと。部分的なME/SC混在描画は別増分なしに導入しない。

## 5. データ所有権

### 5.1 triple buffer

direct-submitまで考えると最低3 slot必要。

```text
FREE -> SC_BUILD -> ME_RUNNING -> READY_SC -> GE_IN_FLIGHT -> FREE
                         late deadline: LATE_IGNORED -> DONE -> FREE
                         hard hang/corruption: QUARANTINED -> poison/STOP
```

- A: GEが現在読んでいるoutput
- B: MEが次draw用outputを作る
- C: SCが次snapshotを組む、またはfree

deadlineに遅れただけのslotはDONE確認後に通常freeする。protocol timeout/hangしたslotだけは
workerの停止/ackまで再利用もfreeもしない。stage teardown、HOME、suspend要求は
`cancel -> ack/STOP -> GE fence -> free`の順。証明できない場合はfeatureをcold bootまでOFFにする。

### 5.2 概算RAM

| 領域 | 仮サイズ | 3 slot |
|---|---:|---:|
| input record 48B × 1024 | 48KiB | 144KiB |
| output worst-case 4頂点 × 24B × 1024 | 96KiB | 288KiB |
| header/status/run table | 8〜16KiB | 24〜48KiB |
| 合計 | — | **約456〜480KiB** |

static descriptor表とguardを含めても512〜640KiB程度を想定。high memory profileでは許容範囲だが、
optional RAM budgetの専用ownerからprocess寿命の単一poolとして確保し、失敗時は全量rollbackして
feature OFFにする。stage descriptorだけをstage epochで作り直し、triple buffer本体は面ごとにfreeしない。

### 5.3 input header / record

headerの必須項目:

- `frameSeq`, `targetDrawSeq`, `stageEpoch`, `managerEpoch`
- record数、6 bucket境界、feature/version、input/output容量
- viewport、Anm offset、colorMul、global color、UQ/render configのbitwise signature
- payload CRC/hash（CORRECTNESS/診断のみ。release authorityにはしない）

recordはpointerを含めず、次をPOD値で持つ。

- stable slot index + **u32 slot generation**
- draw時に選ばれるVM role/state
- pos、cached sin/cos、scale/half-size、UV/scroll、z、base color
- visible/active/alpha/anchor/useColor2
- texture/source index、blend、zwrite、primitive flags

最終byte数は32/48/64BをM0で比較して決定。20Bを先に仕様化しない。

auto-rotateでsource angleが変わった場合、現DENSEはSC VFPUの`PspBulletRenderSinCos()`を呼び、
`pspRenderSourceAngle/Angle/Sin/Cos/RotationValid`を更新する。ME scalar FPUで同じtrig結果が出るとは
仮定しない。snapshot publish時にSCがこのrender cacheをcanonicalな関数で確定してsin/cos bitをrecordへ
入れ、そのSC費を`snapshotUs`へ計上する。`AnmVm::pos/color/rotation/updateRotation`の可観測書込みは
引き続きdraw時に元順序で行う。

### 5.4 cache規律

- SC input single-writer → 64-byte aligned range writeback → release publish
- ME acquire → input invalidate → output single-writer
- ME output writeback → release READY
- SC acquire → output invalidate（SCが読むcopy版のみ）
- direct GE版はGEが読むMain RAM範囲をwriteback済みのまま保持
- SCとMEが同じcache lineを同時にdirtyにしない

control/statusとpayload先頭は別々の64-byte cache lineへ置く。SCとMEの双方で、pool内物理Main RAM
範囲、64-byte alignment、`count * stride`のoverflow、input/output非overlap、各capacity、frame/epoch、
command/versionを検査する。MEにはMMUがないため、SCだけの検査を信用して任意addressへ書かせない。

deadline-lateとprotocol hangは区別する。priority 10に間に合わないだけならSCはDENSE fallbackし、
slotをDONEまで隔離するがworkerは止めない。hard watchdogを越えるcommand/status破損・真のhangだけを
poison対象とし、STOP/guard確認/cold reboot規則へ進む。

## 6. ME kernel

1 frameを1 jobとして投入し、内部で64-record tileを順次処理する。128発ごとのmailbox command分割はしない。

1. header/bounds/version検査
2. input recordをcanonical orderで読む
3. visible/cull判定
4. scalar FPUで現ONEPASS/canonicalと同じ演算順のcornerを生成
5. 現行と同じ8-bit color multiply、UV、zをnative vertexへpack
6. 順序を変えずstate run tableを作る（sort/grouping禁止）
7. output全体をwriteback
8. count/hash/statusを書き、最後にREADYをrelease publish

outputは常時4頂点固定ではない。accepted UQは、最初のgeneral quadより前のaxis spriteを
`GU_SPRITES`の2頂点runで保持し、generalへ入った後は4頂点indexed quadへ移る。MEはこの遷移、
2/4頂点数、run/flush境界をexactに記録し、SCは同じprimitiveでsubmitする。96KiBは安全側の
worst-case容量であって、出力形式の固定を意味しない。

MEはengine関数、allocator、FreeType、SDL、sceGu、sound、live pointerへ触れない。stackは既存guarded
Main RAM 8KiBの制約内に収め、再帰・大型local arrayを禁止する。

## 7. 段階実装DAG

### 7.0 A/B基準の固定

2026-08-30時点のD:本体には棄却済みSPRXD
（XMB PBP SHA-256 `4e91222b97f35a1877f1e70acc37d14560176b8750a80d25515d49711a13e604`）が
残っている。M-ME0候補を作る前に、比較元をaccepted DENSE+UDPへ戻してreadback確認すること。

accepted基準:

- XMB PBP SHA-256: `c9bb2c98420ebcaa1d16741d2fa6a1361327cb4539e2b4f73407a978421a2ef5`
- internal DATA.PSP SHA-256:
  `3618d2664037bef0523f1ac1b07139477bada8ada0c96e6aef43c42a1ec0692b`
- profile token: `DENSE`、accepted hardware run RID: `00437654`
- flags: RD=1 / UQ=1 / ONEPASS=1、WARM_QUEUE=0、STATIC_PROXY=0、
  ENEMY_P5_WARM_QUEUE=0、QUIESCENT_ANM=0、HOT_PREFETCH=0

RIDはrunごとに新規記録し、baseline/candidateを混同しない。M0 candidateは当然DATA.PSPが変わるため、
上記hashは「差替え前baselineの同定」とfeature-off control buildの照合に使う。

### M-ME0A: 起動microbench（ゲーム描画へ未接続）

目的: 作ってから1周で棄却する前に、ME計算自体が成立するか判定する。

- scalar FPU/FCR31のSC↔ME bit一致。実弾から採った値と境界値で全vertex word比較
- 0/128/512/768/1024 records、32/48/64B input、axis/rotated混在
- submit固定費、SC writeback、ME invalidate/kernel/writeback、SC invalidate/copyを別計測
- warm/cold cacheの双方
- objdumpとstack-usageでhot loopの外部callなし、VFPU命令なし、stack 8KiB guard内を確認
- custom-core initの`kcall.prx` write/load時間・bytes・失敗時のcold-reboot状態を記録
- 既存BGM/SFXを鳴らした状態でUND/FATAL確認

GO条件:

- bit mismatch 0
- guard/timeout/fault 0
- 1024 recordsがoutput上限内
- W15相当jobのME kernel p99が、後述M-ME0Bの実deadline内に入る見込み

FPU不一致、kernelだけで10ms超、cache込みで利益が残らない場合はここでMERWを棄却。

### M-ME0B: shadow deadline測定

canonical DENSEで実際に描画しながら、post-chainでjobをkickしpriority 10で採用条件まで観測する。
ME結果は画面へ使わない。

計測:

- submit→priority 10の実slack p50/p95/p99/min
- ME done時刻、eligible/published/ready/consumed、not-ready、signatureDrop、late、quarantine
- snapshot + cacheのSC費
- ME同時動作によるSC calc/render、GE tail、audio mixの回帰
- title/menuのjob 0 A/AでSC/GE/audio回帰。可能なら同条件の消費電力/電池減少も参考記録

GO条件:

- target W12〜W15で`eligible == published == consumed == target frames`、
  `notReady == signatureDrop == coverageDrop == 0`
- snapshot + SC cache費 W15平均0.8ms以下
- MEを起動しただけのSC/GE回帰0.2ms以下
- UND/FATAL 0

### I-ME1: CORRECTNESS shadow

SC canonical頂点とME頂点を同じframeで生成し、描画はSCだけを使用。

- ordered vertex/state stream hashを毎frame比較
- logical state hashはtexture pointer/handleを直接含めず、source indexと正規化したstate値を使う
- mismatch時にslot/state/word indexをRAM logへ保存
- simulation hash、RNG、score、graze、hit、bullet/enemy/player stateを基準と比較
- 混在primitive frameのframebuffer readback
- pause、time stop、固定30切替、stage transition、HOME終了

合格条件は全て0 mismatch。近似一致は不合格。

### I-ME2: copy版（最初の性能判定）

- SCが現行順でVM副作用をcommit
- ME outputを既存Anm vertex arenaへ連続range単位でcopy
- 現行renderer state/Flush/UQ/GE submit境界を維持
- frame全体のheader/epochが不一致なら全体DENSE fallback

promotion gate:

- W15 critical平均 **1.0ms以上改善**、p99とmissも改善
- W12〜W14と代表ボス窓でcritical/p99/missの有意な回帰なし
- target frameの`consumed/submitted = 100%`、signature/coverage drop 0
- busy wait 0、timeout/late/epoch drop 0（通常同一replay）
- correctness全green

1ms未満ならdirect-submitへ飛ばず、MERWを棄却する。

### I-ME3: direct-submit版

I-ME2が合格した場合だけ行う別増分。

- priority 10で前batchを正規にFlush
- SCがMEのordered run tableに従い、既存低レベルGU関数へexternal Main RAM vertex rangeを渡す
- GE command発行はSC、MEは一切sceGuを呼ばない
- slotはGE fence/list completionまで`GE_IN_FLIGHT`

promotion gate:

- I-ME2比でさらに0.4ms以上、DENSE比でW15 **2.0ms以上改善**
- W12〜W15と代表ボス窓でcritical/p99/miss非回帰
- target frameの`consumed/submitted = 100%`
- draw call/flush/state countが設計値と一致
- mixed frame framebuffer readback一致

### I-ME4: 純粋update補助（必要時のみ再設計）

I-ME3後もW14/W15が16.7msを超える場合だけ検討する。

移せる候補はimmutable position snapshotからのbroadphase maskやcull候補まで。SCが元slot順でexact
collisionと全副作用をcommitする。RunCommands、ANM script、RNG、sound、item spawn、timer、authority
stateは移さない。

現状は排他phase計測も、SCと並走できるdependency scheduleも確定していないため、改善値を提示しない。
I-ME3の新しいprofiling結果なしに実装せず、現時点の60FPS予算にも加算しない。

## 8. 改善見積

### 8.1 window別

| 窓 | base critical | copy版削減(H) | copy後(H) | direct削減(H) | direct後(H) |
|---|---:|---:|---:|---:|---:|
| W12 | 15.510 | 0.6〜1.2 | 14.3〜14.9 | 0.9〜1.7 | 13.8〜14.6 |
| W13 | 16.228 | 0.8〜1.5 | 14.7〜15.4 | 1.1〜1.9 | 14.3〜15.1 |
| W14 | 18.932 | 0.9〜1.7 | 17.2〜18.0 | 1.4〜2.4 | 16.5〜17.5 |
| W15 | 19.962 | 1.3〜2.4 | 17.6〜18.7 | 2.0〜3.3 | 16.7〜18.0 |

レンジの根拠:

- 上限: 現弾draw 2.919〜5.074ms
- 参考となるdraw-side bundle移動量: WARM_QUEUE 1.472〜2.752ms
  （geometry限定ではなくVM/cache/sincos/副作用移動を含む）
- 差引き: compact snapshot、cache handoff、VM副作用commit、copyまたはdirect run submit
- 追加不確定: ME/SC/GEのMain RAM bus競合、deadline未完、FPU演算速度

FableのW15 15.8〜16.2msはCPU欄、この表はcriticalであり同じ指標ではないため直接比較しない。
いずれにせよM0前に中央見積としては採用できない。ME描画だけで60固定を約束せず、direct後の実測で
不足が判明した場合にだけ、別設計のupdate補助またはSC側最適化を再予算化する。

### 8.2 60FPS判定

- 表示deadline: 16.667ms
- 工学目標: 15.7ms（約1ms jitter reserve）
- 最終条件: 診断OFF相当、同一Lunatic replay、対象全域0 miss、複数run再現

W15は16.667msへ2.695ms、15.7msへ4.262msの削減が必要。direct版の上振れなら16.667msへ届くが、
15.7msにはME描画単独では不足する可能性が高い。

## 9. 正しさ・安全・回帰gate

必須:

- gameplay simulation/replay hash毎frame一致
- ordered vertex/state stream bit一致
- selected framebuffer readback一致
- 弾数、当たり判定、速度、描画品質、primitive、draw順、blend、zwrite不変
- audio PCM/UND/FATAL非回帰
- text cache、BGM gate、SFX Main RAM、SHIKIGAMI、Music Room、GE portrait cache非回帰
- ME eDRAM extent **0/0、常時UNUSED**
- PSP-1000、通常PSP-2000+、Go各feature-off profileのbuild/test非回帰

旧accepted invariantの単一`ME JOBS=0`は、この実験profileでは意図的に意味が変わる。音声と描画を
混同しないようtelemetryを `ME_AUDIO_JOBS=0` と `MERW_SUBMITTED/COMPLETED/...` に分離する。
音声ME job 0とME eDRAM UNUSEDは維持し、MERW compute jobだけが増える。default releaseのfeature-off
profileでは従来どおりMERW jobも0であることを確認する。

release counter:

- `eligible`, `submitted`, `ready`, `consumed`, `notReady`, `signatureDrop`,
  `lateIgnored`, `timeout`, `quarantined`
- `epochDrop`, `generationDrop`, `globalDrop`, `boundsDrop`, `fallbackFrames`
- input/output bytes、kernelUs、snapshotUs、cacheUs、commitUs、copyUs、submitUs

診断counterはRAMに蓄積し、hot pathからMemory Stickへ同期書込みしない。

## 10. lifecycle上の出荷ブロッカー

custom coreの `meLibDefaultInit()` はSony T2/MeRpc handlerを置換する一方向takeoverで、現実装は
power lock、suspend latch、clean STOP、終了後cold rebootを要求する。これは現accepted releaseにはない
機能制約である。

さらに現`meLibLoadPrx()`はembedded payloadを起動のたびにMemory Stickの`./kcall.prx`へ書いてから
loadする。M0ではこのstartup I/O時間と失敗経路を計測し、gameplay開始後の追加MS writeは0を要求する。
default release候補では、配布時にhash固定したPRXをread-only loadする方式、または別の実証済みloaderへ
直し、毎起動の無条件writeを残さない。

従って:

1. M-ME0〜I-ME3は `TH07_PSP_ME_RENDER_WORKER` の実験profileに限定。
2. 実機投入前の3段階表では、変更される機能にMERW jobと起動時PRX I/O、失われる機能に
   少なくとも通常suspendと終了後の他ME application利用を明記。
3. 田中さんの明示承認なしにこのprofileをメモステへ投入しない。
4. default release昇格には、Sony ME runtimeの安全なrestoreを実証するか、非破壊的な別backendを設計し直す。
5. restore未解決のまま性能だけ合格しても「研究版合格」であり「出荷GO」ではない。

## 11. 不採用事項

- ME eDRAMの上位/下位/refresh/scratch利用
- ME VFPU
- synchronous `th07_psp_me_vertex_pack()`をframe hot pathで呼ぶこと
- 1 bullet 1 command、128 bulletごとのmailbox再dispatch
- SCのbusy wait、6ms timeout待ち
- live `Bullet` / `AnmVm` / renderer pointerをMEが読む・書くこと
- authoritative bullet calc/collision/RNG/score/sound/item処理の初手移管
- sort、texture groupingによる描画順変更
- MEからのsceGu/GE submit
- 実際に前フレームの頂点を表示すること
- PSP-1000/2000/Goへの同時展開

## 12. 実装開始判定

設計判定は次のとおり。

- **M-ME0A/B: GO**。最小コストで見積を実測へ昇格できる。
- **I-ME1: M0合格後GO**。
- **I-ME2: correctness合格後GO**。
- **I-ME3: copy版が1ms以上改善した場合だけGO**。
- **I-ME4: 現時点NO-GO。I-ME3後の再計測・再設計待ち**。
- **default release採用: NO-GO。custom-core lifecycle/restore未解決**。

最初に書くべきコードはゲーム配線ではなく、M-ME0AのFPU bit一致＋batched throughput harness。
その結果が悪ければ、実機リプレイを何周も使わずME案全体を棄却できる。
