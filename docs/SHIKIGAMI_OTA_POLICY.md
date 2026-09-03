# SHIKIGAMI OTA 運用ポリシー

制定: 2026-08-27

## 結論

今回の初期配備後、TH07 の通常更新で USB から `EBOOT.PBP` を直接差し替えては
ならない。以後は既存の SHIKIGAMI A/B OTA を運搬路とし、置換専用の一回限り
インストーラを未使用スロットへ配信する。

USB を使えるのは、SHIKIGAMI 一式の初回導入、A/B の両方が起動不能になった
災害復旧、またはユーザーが明示的に指示した監査付き復旧だけである。

## 固定する既存経路

```text
XMB launcher  ms0:/PSP/GAME/SHIKIGAMI/EBOOT.PBP
slot A        ms0:/PSP/GAME/SHIKIGAMI/SLOT_A
slot B        ms0:/PSP/GAME/SHIKIGAMI/SLOT_B
TH07 target   ms0:/PSP/GAME/TH07SHIKI/EBOOT.PBP
server        192.168.11.3:9997/TCP
```

監査済みPCサーバーは次である。リリースへ複製する場合も、このファイルとの
SHA-256一致を先に確認する。

```text
C:\Users\kan82\TH07_SHIKIGAMI_PSP_TELEMETRY_20260826\deploy\SHIKIGAMI_PSP_SELF_UPDATE_20260827\PC\shikigami_psp_update_server.py
SHA-256 a949dd90f2219ca88b1c643307f2be24deaa65d3666919dab1f4b18485bae9a4

C:\Users\kan82\TH07_SHIKIGAMI_PSP_TELEMETRY_20260826\deploy\SHIKIGAMI_PSP_SELF_UPDATE_20260827\PC\shikigami_psp_slot.py
SHA-256 f7da7eae0ab0e5e2e04acba6e4cb73aae843cd6d697a30e8915d692d10e84429
```

OTA はルートランチャーや実行中スロットを書き換えない。新しい一回限り
インストーラを未使用スロットへ commit-last で置き、ランチャーの trial と自動
rollback をそのまま使う。生の TH07 `EBOOT.PBP` を A/B スロットへ直接入れない。

## 次回までに必要な置換専用インストーラ

既存の `TH07_SHIKIGAMI_TH07_INSTALLER_20260827` は create-only であり、既存
`TH07SHIKI` を意図的に変更しない。次回は別の replace-only インストーラを作り、
次をすべて満たすこと。

- 実行元を正確な `SLOT_A` または `SLOT_B` に限定する。
- 対象を `ms0:/PSP/GAME/TH07SHIKI/EBOOT.PBP` と、所有する固定名の一時・退避・
  transaction marker に限定する。フォルダー全体、セーブ、設定、他のPRXには触れない。
- 現在の `EBOOT.PBP` を全量読み戻して SHA-256 を計算し、リリース内の
  old-hash allowlist に一致した場合だけ進む。不明なハッシュ、欠損、サイズ変化は
  fail-closed とし、上書きしない。
- 新版を同じディレクトリの `EBOOT.NEW` に書き、close/sync後にサイズとSHA-256を
  全量読み戻し検証する。検証前に現行版を動かさない。
- transaction marker は対象外の固定パスへ置き、old/new のサイズ、SHA-256、
  installer build ID を束縛する。marker が一致しない既存物は削除しない。
- 検証後、現行版を固定名のバックアップへ rename し、`EBOOT.NEW` を
  `EBOOT.PBP` へ rename する。最終ファイルを再度全量検証するまでバックアップを
  消さない。
- 電源断後の再実行は、一致する marker と各ファイルのハッシュから状態を判定する。
  検証済み `EBOOT.NEW` があれば commit を再開し、新版が最終名で正しければ cleanup
  を再開する。新版が欠損・破損なら認可済みバックアップを元名へ戻す。それ以外は
  fail-closed とする。
- HOME取消、書込失敗、rename失敗、最終検証失敗の各点で、旧版が最終名にあるか、
  次回の同一インストーラが安全に resume/rollback できる状態を残す。
- インストーラ自身は trial を confirm しない。実行後の次回 SHIKIGAMI 起動では、
  既存の確認済み安定スロットへ自動で戻る。

診断試験後はobserver EBOOTへ完全rollbackしたため、最初の将来更新で認可すべき
現在の実機 predecessor は次の一件である。更新直前にも実機で一致を再確認し、
一致しなければallowlistへ採用してはならない。

```text
7eb86ff4d608df838e87f28811e0330c47faeb5cf4e31e558c93bac3e5559055
```

以後は、直前の実機配備で凍結したハッシュだけを明示的に追加する。ワイルドカード、
「任意の有効PBP」、空ファイル許可は設けない。

## 2種類のPRXを混同しない

SHIKIGAMI OTA の `ge4wrap_bwv1.prx` は固定 sidecar である。

```text
2150 bytes
SHA-256 103b96431c3ac0713db4104d1e833e4c112a094ce9d273668365f72f118ffc8c
```

TH07 の `kcall.prx` は **2198 bytes** であり、別用途・別バイナリである。2150-byte
sidecar を `kcall.prx` として配備してはならず、2198-byte `kcall.prx` を OTA
sidecar として渡してもならない。通常の EBOOT-only 更新では既存 `kcall.prx` を
変更しない。変更が必要になった場合は、別の allowlist と transaction を備えた
個別監査案件にする。

## Build ID と信頼境界

- OTA `target-build` は過去に配備・配信した最大値より必ず大きくする。既存の
  `0x20260901` を再利用しない。force/downgrade/replay 用の例外を実装しない。
- TH07内部の telemetry build ID と、A/B OTAスロットの build ID を記録上で区別する。
- protocol v1 の SHA-256 は転送破損を検出するが、PCを認証しない。信頼境界は
  固定PC `192.168.11.3`、private LAN、private-profile firewall、ルーター非公開、
  オペレーターによる手動の Square 2回確認である。
- サーバーは候補のハッシュを手元で確認してから手動起動し、Internetへ公開せず、
  対象クライアント完了後に停止する。Square入力を自動化した無人更新は禁止する。

## 将来更新の正確な順序

1. 新TH07を隔離ツリーでビルドし、通常版同一性、ホスト試験、PPSSPP、必要な実機
   gateを完了して、新旧EBOOTのサイズとSHA-256を凍結する。
2. 新EBOOTを内包した replace-only インストーラを作る。old-hash allowlist、固定対象、
   transaction/resume/rollbackをテストし、更新の各書込境界で電源断試験を行う。
3. インストーラを、単調増加させた新しい OTA build ID と固定2150-byte sidecarで
   A/B候補化する。server、slot、候補、manifestの全ハッシュをリリースへ保存する。
4. PPSSPPで inactive-slot commit、trial起動、置換成功、途中断からのresume、旧版への
   rollback、不明old-hash無変更、SHIKIGAMI自動rollbackを再確認する。
5. PCを `192.168.11.3` のprivate LANに接続し、監査済みサーバーを `9997/TCP` で
   手動起動する。引数は新しい `--target-build`、インストーラの `--eboot`、固定
   sidecarの `--sidecar` だけを使う。
6. PSPで確認済み安定版の **SHIKIGAMI PSP Memory POST** を起動し、起動画面で
   Squareを2回押す。完了表示とPCログを確認して通常終了する。
7. SHIKIGAMIを再起動する。ランチャーが新trialを選び、replace-onlyインストーラが
   PASSするまで待って通常終了する。失敗時にUSB上書きへ切り替えない。
8. SHIKIGAMIをもう一度起動し、インストーラをconfirmしなかったため確認済み安定
   スロットへ戻ったことを確認する。
9. XMBからTH07を起動し、タイトル、デモ/ゲーム、BGM、telemetry、正常終了を確認する。
   Memory Stick上の最終EBOOTを読み戻し、新ハッシュとの一致を保存する。
10. server/receiverログ、旧新ハッシュ、OTA build ID、slot、実機結果を凍結する。
    途中断・失敗時は同じ安定SHIKIGAMIから同じOTAを再実行し、installerの
    resume/rollbackに回収させる。

この順序を完了できない候補は「OTA READY」ではない。
