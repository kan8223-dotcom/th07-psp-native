# 助言・増分チェックリスト（生きた台帳）

最終更新: 2026-09-01 Codex（C1 UV16 M0は起動前不合格、ログ回収後D:をRID30へ復旧）。
**このファイルを唯一の消化管理台帳とし、Fable/Codexとも状態変更時に直接更新する。**

状態: ✅=その段階まで完了 / 📋=PC実装・ビルドのみ / ⬜=未着手 / ❌=棄却 / 🔒=SC権威として維持 / 🔍=計測待ち。

## 0. 忘却・誤報防止ルール

- [x] **スティック操作は `tools/psp_stick.py` 経由のみ（Fable/Codex共通）**。
      生のpowershell直叩き・ドライブ文字決め打ち・ログ名決め打ちを禁止。
      `find`=署名走査同定 / `status`=SHA+台帳同定+ログ一覧 / `pull-log`=読取回収 /
      `deploy --expect`=guard付き投入 / `restore`=台帳復旧。
      「見えない」報告は本ツールの全走査表出力を添えたときのみ有効。
      実機合格版は `tools/psp_known_builds.json` へ必ず登録。
- [x] デバッグ目的の実機起動を田中さんに依頼しない（診断は全条件1周方式に集約）。
      田中さんの起動はACCEPT本判定のみ。
- [x] 作業開始時にこの台帳を読む。
- [x] 報告は必ず **PC実装 / D:メモステ投入 / 実機確認** の3段階に分ける。
- [x] PCでコードが完成しただけの項目を「済み」と呼ばない。
- [x] 実機結果後、RID・EBOOT SHA・ログ場所・合否理由をこの台帳へ追記する。
- [x] 実機投入は田中さんの明示的な「いれて」の後だけ。H:（PSP Go）は明示依頼なしに触らない。
- [x] PSP-1000を毎回ビルドしない。対象プロファイルに関係する時だけ行う。
- [x] 1回の実機runで判定する性能増分は1つ。比較元をRIDで固定する。
- [x] SHIKIGAMI受信機はWindows側Pythonで`192.168.11.3:9996`を待受する。WSL2内だけのUDP bindではPSPのLAN打電を受けられない。WLAN LED点灯とPC受信成功を混同しない。

### 現在のD:と次の一手

- D: は **RID30 A1-MOVE** へ復旧済み（readback SHA-256 `743C14CC...EDACBC`）。C1 UV16単独M0 `AA55040A...22A4159` は実機でタイトル前cold rebootとなり不合格。ログ`artifacts/TH07PSP_BOOT.C1-UV16-XMBRETURN.20260901-012526.LOG`（SHA `6BC067F4...A5854DAA9`）ではN=0 command-10比較が`OK1`まで成功し、N=128へ進む前のREADY解放またはguard再確認で失敗。UV数値不一致は未検出だがcleanup gateを通っていない。失敗版は`artifacts/stick_backups/EBOOT.PRE-20260901-012527.AA55040A.PBP`へ退避済み。RID30 A1-MOVE自体は実機で機能合格だが、本命の幽々子撃破PERF窓はoverflow欠落のため性能未判定のまま。
- RID25 eDRAM full-seed試験は実機GO0。ゲーム経路を速くする機能ではない。
- **RID24 Item描画適応prefix版**はPC実装・D:投入まで済んだが、実機で`ME1A SELFTEST NG`となり不合格。旧Item版にも実機合格実績はない。
- [x] 田中さんの「いれて」後にRID24だけを投入する。
- [x] RID24失敗ログを回収し、RID26一回起動診断版をD:へ投入する。
- [x] RID26を一回起動しログ回収。I0通常ItemとIA権威不一致は合格、IR破損next試験だけMEが旧値を再読して不合格。ログ`artifacts/TH07PSP_BOOT.RID26.XMBRETURN.RUN1.LOG`、SHA `2FD6FB4D...B9119BA6`。
- [x] RID27で2本目を`index+8192`へ変更したが、実機IRは修正前と完全同値の旧値を返し、仮説を棄却。ログ`artifacts/TH07PSP_BOOT.RID27-XMBRETURN-RUN1.20260831-184449.LOG`、SHA `D097C58D...EDDCE4`。D:はRID22へ復旧済み。以後はPCだけで原因を確定し、optional Item失敗はSC fallbackで起動継続させるまで実機投入しない。
- [x] RID28をPC実装・D:投入・実機確認。Sony T2 native WBInv-allでもIR staleは直らず、Item自己検査はNG。ただしItemだけ閉じ、Bullet再検証に合格してタイトル/デモを継続。ログ`artifacts/TH07PSP_BOOT.RID28-PSHOME-FREEZE-RUN1.20260831-193229.LOG`、SHA `B2E4B93B...58C07D`。成果物`build/final60/ime7_adaptive_item_safe_rid28_20260831/`。
- [x] RID29をPC実装・PSP-3000ビルド・D:投入。可変Item/VM/sidecar/authorityだけをME uncached volatile読みに変更し、immutable sprite/repsはcached維持。SHIKIGAMI独立type 12で`ITEM_ME`、`BULLET_ME`、fallback理由、I0/IA/IR失敗段階、retry結果を初回/1Hz/終了時に打電する。全579テスト合格。成果物`build/final60/ime7_adaptive_item_uncached_rid29_20260831/`、readback SHA `A52E45FF...5ADAA5`。実機Stage 6中に`ENABLED / BULLET ON / SELFTEST PASS / retry 0`を反復受信し、起動契約は合格。Item多発場面の受入のみ残る。
- [x] 幽々子撃破までのStage 6 ACCEPT runを完走し、対象窓をRID29ログで回収する。
- [ ] アイテムの数・順序・吸引・回収・得点/PIV・見た目に変化がないことを目視確定する（音声は`UND=0`）。
- [x] Item描画だけでは60固定に足りないと判定。撃破直後W254は旧I-ME4のAVG 16.862ms / MISS85（約35fps）からRID29の15.542ms / MISS32（約47fps）へ改善したが、W253は16.484ms / MISS55。次はA1-MOVEへ進む。
- [ ] RID29の正式PERF profileは終端`OVERFLOW 11 LINES`でINVALID。対象窓は揃って比較可能だが、次版では診断量を減らすかRAM容量を調整し、性能受入ログをdropなしにする。

## A. メーター観察由来の増分候補（田中さん発掘・優先度順）

| # | 項目 | PC | D: | 実機 | 現在の事実 / 次のgate |
|---|---|---:|---:|---:|---|
| A1-OBS | **幽々子撃破: 全弾がアイテム化し自機へ吸引、SC100%・約30FPS、ME遊休** | ✅ | ✅ | 🔍 | RID30ボス戦再走は完走ログを回収したが、`OVERFLOW 33 LINES`で幽々子撃破を含む末尾約34秒のPERF窓が欠落。旧SHIKIGAMI `FPS=`はreplay保存値なので無効。比較可能な末尾前6窓はRID29比平均+0.0045msで実質同一、改善根拠なし |
| A1-DRAW | Item描画をME prefix / SC suffixへ分割 | ✅ | ✅ | ✅ | RID29で起動・完走・runtime採用を確認。撃破直後W254は旧I-ME4比AVG−1.320ms、MISS85→32。ただし約47fpsで60固定には不足。対象窓は有効だがrun全体の正式profileはoverflowのため次版で是正する |
| A1-SPAWN | 全弾走査→アイテム生成 | ⬜ | ⬜ | ⬜ | 現在SC。`RemoveAllBullets` / `DespawnBullets` / radius消去が弾・レーザーを走査してItemを生成。allocationと順序はSC権威のまま、bulk/SoA化またはME候補生成を検討 |
| A1-MOVE | Item吸引・移動更新 | ✅ | ✅ | 🔍 | RID30 PC完成。全599テスト、PSP-3000 build、実バイナリ監査、XMB同一性が合格。実機は反魂蝶終了まで完走し、type 13で `A1_MOVE=ENABLED / ITEM_MOVE=ON / SELFTEST-PASS / BULLET=OK / ITEM=OK`、降格・Fatal・UNDなし。ボス戦再走ログも回収済みだが、診断overflowで本命の幽々子撃破PERF窓とA1採用内訳が欠落。比較可能6窓の実フレーム時間はRID29比平均+0.0045msで実質同一のため、性能改善は証明できていない。command10実capture+command12全6route bit一致を起動時検査し、clean Item不一致はmotionだけOFF→ME17再検査→RID29 Item draw/Bullet MEで起動継続。生成/消滅/回収/得点/PIV/SFX commitはSC固定、遅延・不一致は待たずcanonical fallback。成果物`build/final60/a1_item_motion_rid30_20260831/`、XMB/readback SHA `743C14CC...EDACBC` |
| A1-COLLECT | 回収判定・得点/PIV・SFX | 🔒 | 🔒 | ✅ | 現在SC権威。丸投げ禁止。将来はMEによる候補絞り込みのみ可、最終判定とcommitはSC |
| A1-POPFX | 得点popup・弾消しEffect | 📋(設計) | ✅(popupのみ) | 一部✅ | popupはSCバッチ版が実機合格。Effect layer 0/3は現行コード監査と専用M0復帰設計まで完了、コア未修正・未build・未投入。可変Effect/VM/next/generation/sin-cos/serial-countにRID29式uncached volatile修正が必要。synthetic→real capture→command→consume、0/1/408、連続job、full cache fence、0→2 canonical→3、失敗時全Effect SC降格を`docs/EFFECT_ME_REENABLE_M0_PLAN.md`に固定。E1〜E4 targetは`psp3000-effect-m0-e1-synthetic-build`〜`e4-consume-build`、研究RIDは`0x260901e1u`〜`0x260901e4u`とし、現時点では全て投入非承認。旧I-ME8 ALL-INはEffect単独事故を証明しておらず、lean=1を含むため再利用禁止 |
| A1-SAME | **同型イベントの横展開** | ⬜ | ⬜ | ⬜ | スペル終了、ボス/中ボス撃破、ボム・半径弾消し、全敵消去を対象化。`RemoveAllBullets`、`DespawnBullets`、`RemoveBulletsInRadius`、`RemoveAllEnemies`を共通計測する |
| A2 | 道中大量弾幕でSC/MEとも100% | ✅ | ✅ | ✅ | RID22メーターで確認。新しい積み荷は予測busy<80%時だけ。飽和時は追加せず、record痩身・16bit頂点・SC suffixで均衡させる |
| A3 | 背景スプライト・3DがME未使用 | ⬜ | ⬜ | ⬜ | 現在は完全未実装。走査・行列・run表構築の無副作用部分を監査してから予算器へ追加。実描画はGE |
| A4-DRAW | 自機ショットの描画準備 | ✅(計測のみ) | ⬜ | ⬜ | default-off `PSP_PERF_PLAYER_SHOT` observerをPC実装。`Player::DrawBullets()` frontendを2打刻し、state1 shot数をlocal集計して1回だけwindowへpublish。同一`PERF ACCEPT`行の`PSD`=window µs、`PSN`=active-shot visits、`PSF`=frontend callsで測る。P06/Rの部分集合で再加算なし、deferred force-flushなし。C1/C2全OFF固定RID30比較のPSP-3000 build・全646テスト・バイナリ監査合格。比較target/aliasはfeature=0を固定し、PL専用targetだけ=1。成果物`build/final60/player_shot_perf_pc_20260901/`、EBOOT SHA `C401A448...F1E4`。D:未投入・実機未確認 |
| A4-AUTH | 自機ショット誘導・命中・ダメージ | 🔒 | 🔒 | ✅ | 現在SC。誘導はbit一致が証明できる場合だけ候補、命中とダメージcommitはSC固定 |
| A5-MEASURE | SE連動負荷の窓別分離 | ⬜ | ⬜ | ⬜ | 現ログは終了時累積のみ。mixUs/calls/active voices/hit・effect数を同じ窓で記録する |
| A5-SCALAR | SC mixerのbit一致scalar fast path | ⬜ | ⬜ | ⬜ | SC mixer実額は全周平均約0.52ms/frame相当、最大約1.0ms/block。`mixDivisor=1`固定なのにsample loopに除算が残るため最初の候補 |
| A5-VFPU | SC mixer VFPU化 | ⬜ | ⬜ | ⬜ | 会話上の案だけでコードなし。Q16積・負値shift・PCM CRC完全一致が条件。scalar後 |
| A5-MEAUDIO | runtime ME音声mix | ❌ | ❌ | ❌ | MEコア/selftestは存在しRID25 boot合格だが、runtimeはSC固定・ME audio jobs=0。単一workerへ同期再配線すると11.6ms DAC締切を破るため棄却。再開には非同期FIFO/音声優先設計が必要 |

## B. アーキテクチャ決定（合意済み）

| # | 項目 | PC | D: | 実機 | 備考 |
|---|---|---:|---:|---:|---|
| B1-ITEM | Item専用の決定論的80%予算器 | ✅ | ✅ | ✅ | RID29でItem自己検査PASS、runtime gate OPEN、密集時fail-closed、幽々子撃破時の採用を実機確認 |
| B1-GENERAL | 汎用予算器（Item/Effect/背景/自機shot） | ⬜ | ⬜ | ⬜ | まだ存在しない。RID24合格後に一般化する |
| B2-ITEM | ME prefix / SC suffix助け合い分割 | ✅ | ✅ | ✅ | RID29実機合格。Item自己検査/予算/deadlineが外れた場合はSC suffixへ安全に戻り、Bullet MEを維持 |
| B3 | 権威処理のSC固定契約 | ✅ | ✅ | ✅ | ECL・最終当たり判定・回収・スコア・生成/消滅commitを維持 |

## C. MEダイエット隊列（道中密集=両CPU飽和の主戦場）

| # | 項目 | 状態 | 備考 |
|---|---|---|---|
| C1 | **16bit頂点出力**（首位） | ❌(M0-1) | PC実装・4構成PSP-3000 build・全646テスト・独立バイナリ/XMB監査は合格したが、UV16単独XMB `AA55040A...22A4159` は実機タイトル前cold reboot。N=0比較は`OK1`、N=128前のREADY解放/guard再確認cleanup gateで不合格。D:はRID30へ復旧済み。OFF ELF `130DB4A8...AFD620`、UV16=`71CE0385...98D65`。成果物`build/final60/c1_uv16_m0_deploy_20260901/`、失敗ログ`artifacts/TH07PSP_BOOT.C1-UV16-XMBRETURN.20260901-012526.LOG`。次はcleanup条件を個別打電/boot-note化してPCで直すまで再投入禁止。XYZ16/両方もブロック |
| C2 | **record痩身** | 📋 | PC実装・PSP-3000単独3構成+累積+固定RID30 OFF build・全633テスト・独立ABI/capacity/binary arena監査合格。C2a=Bullet output 16→4B/slot、C2b=Bullet seed 64→56B/slot（candidate/inBounds bitplane）、C2c=Item seed 64→48B/slot（candidate/state0/state1/auto bitplane）。generation再読不一致時はcandidate/inBounds/slotを同時clear、不正plane/stateはsegment-local fail-closed。OFF ELF `130DB4A8...AFD620` は固定RID30と完全一致。成果物`build/final60/c2_record_slim_pc_20260831/`、検証契約`docs/C2_RECORD_SLIM_VALIDATION.md`。**D:/H:未操作・実機未確認** |
| C3 | eDRAM seed staging | ❌ | RID25実測で棄却: kernel−0.243msに対し搬入65,728Bが+1.314ms。再開条件=転送方式の劇的高速化のみ |
| C4 | eDRAM常駐コホート | ❌ | 現構造ではSC権威と分岐し契約違反。zero-copy/SoAで権威を壊さない新設計が出た場合だけ別増分として再評価 |

## D. SC側の残り大物

| # | 項目 | 状態 | 備考 |
|---|---|---|---|
| D1 | **弾正本SoA大改造**（20〜32時間級・最後の侵襲的手段） | ⬜ | 全計測が指す「散在構造の歩行税」への直接攻撃。全部盛りでも温存中 |
| D2 | I4行列submitキャッシュ | 📋(計測完成) | 既存post-cache `mMatrixSubmissions`をPL/M専用`TH07_PSP_PERF_PLAYER_SHOT`時だけ同一`PERF ACCEPT`行へ`Mxx.x`（平均submit/frame）として露出。追加ログ行・再加算・追加カウンタなし。feature=0のRID30/C1/C2は旧ACCEPT形式と256B bufferを維持し、C1 UV16再build ELFは保存済みSHA `71CE0385...98D65`とbyte一致、`M`文字列なし。全646テスト合格、D:未投入・実機未確認。実機M/frame×2-6µsで上限算出し、15回/frame未満ならI4本体は作らず棄却 |
| D3 | スクラッチパッド16K | ⬜ | 端数枠（〜0.2ms級） |
| D4 | ミキサー高速化 | 🔍 | A5-MEASURE→divisor==1 scalar fast path→必要ならVFPUの順。PCM完全一致 |

## E. プロセス提案

| # | 項目 | 状態 | 備考 |
|---|---|---|---|
| E1 | **実機合格時にgit commit+タグ** | ⬜ | ツリー汚染事件（I-ME8三連敗+メーター版失敗の真因）の再発防止柵。**未採用のまま——強く推奨継続** |
| E2 | RID台帳（ログ行にRID埋込） | ✅ | 運用中、効果実証済み |
| E3 | 事前上限ゲート（キャッシュ/プロキシ系増分は節約上限算出が先） | ✅ | QANM/SPRXD教訓の制度化。RID25でも機能 |
| E4 | Fableビルドは隔離箱（th07_fable_buildbox）、共有root禁止 | ✅ | 並行ビルド衝突事件より |
| E5 | この台帳を作業開始・終了時に更新 | ✅ | 2026-08-31開始。PC/D:/実機を混同しない |
| E6 | **Item/Bullet/A1-MOVE起動判断を打電だけで判定** | ✅ | RID29 type 12に加え、RID30実機でtype 13を1Hz反復受信。`A1_MOVE=ENABLED / ITEM_MOVE=ON / BULLET_ME=ON / SELFTEST-PASS / TEST=1/0 / RETRY=0/0 / BULLET=OK / ITEM=OK`をUSBログなしで確定。Windows受信機もRID30対応版へ再起動済み |

## F. 現在地（60固定の残り算数）

- W15道中密集: RID29は18.324ms。Itemは予算器でほぼSC fallbackし、ここはA1ではなくMEダイエットC1/C2の主戦場
- 幽々子撃破窓: Item描画ME化（RID29）で旧約35fps→約47fpsまで回復したが、W253/W254とも60固定未達。A1-MOVE（RID30）は機能ON・bit一致・安全降格まで実機合格した一方、本命窓がログoverflowで欠落したため性能効果は未判定
- 反魂蝶: 16.3〜16.9ms（崖すれすれ、A1/A5系の回収余地あり）
- 経路: **C1 cleanup失敗をPC修正（再投入禁止）→ C2単独階段（a→b→c→abc）またはPL/M計測1run → 合格した増分だけ性能版へ昇格 → 必要ならD1**。現在D:はRID30。追加投入は未承認。
