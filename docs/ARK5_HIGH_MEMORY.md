# ARK-5 / high-memory setup for th07-psp-native

## 日本語

### 必須条件

- 対応CFWはARK-5のみです。
- PSP-2000、PSP-3000、PSP Goでは、対象アプリの`Use Extra Memory`を起動前に必ず`Max`へ
  設定してください。`Default`と`Off`はサポート対象外です。
- PSP-1000には追加Main RAMがないため`Max`は適用されませんが、CFWはARK-5を使用します。
- ARK-5の設定はMemory Stick交換、storage変更、ARKの再導入で失われる場合があります。
  以前同じPSPで起動していても、交換後は設定を再確認してください。

正式統合版は1つの外側EBOOTからmodel別runtimeを起動します。PSP-2000/3000/Go用runtimeは
`PARAM.SFO`の`MEMSIZE=1`を使用し、最大user partitionを前提にしています。2026-08-26の
PSP-3000 / ARK-5実機試験では、`always, highmem, off`があるとgameの`main()`へ到達せず、
`TH07PSP_BOOT.LOG`も更新されませんでした。high memoryを有効にした同じ配布EBOOTはtitleへ到達しました。

### ARK-5 UIでの設定

1. XMBまたはARK-5の設定画面で`東方妖々夢 ～ Perfect Cherry Blossom.`を選びます。
2. `Use Extra Memory`を`Max`へ設定します。`Default`のままにしないでください。
3. gameを終了して起動し直します。設定が再読込されない場合だけVSH restartまたはPSPの電源入れ直しを
   行います。

### SETTINGS.TXTを手動編集する場合

ARK-5 UIで`Max`を選べる場合はUIを使用してください。手動編集が必要な環境では、先にARK-5を
current versionへ更新することを推奨します。

1. `ms0:/PSP/SAVEDATA/ARK_01234/SETTINGS.TXT`をPCへbackupします。PSP GoでARKを内蔵storageへ
   入れている場合は`ef0:/PSP/SAVEDATA/ARK_01234/SETTINGS.TXT`が対象です。
2. `always, highmem, off`を削除し、次の1行へ置き換えます。競合する2行を残さないでください。

```text
homebrew, highmem, on
```

3. gameを起動し直し、ARK-5 UIで対象appの`Use Extra Memory = Max`を確認します。

付属`ARK5_HIGHMEM_SNIPPET.txt`は設定全体ではなくmerge用の1行だけです。既存の
`SETTINGS.TXT`へfileごと上書きしてはいけません。この配布物がARK設定を自動変更することもありません。

### 実機根拠

- PSP: PSP-3000 / model 3
- CFW: ARK-5
- EBOOT SHA-256: `55dc6c2e254d1cad8c63e3965dfdd1b4059021ccecd3bd57a70bd945370bc278`
- `NotoSansJP-Regular.ttf` SHA-256:
  `6ab1664d8adc20b19237ddc451c94e31f493cb851a1917242debf66f9af6da05`
- 結果: high memory有効化後の再起動でtitle到達

この試験は旧PSP-2000+単体EBOOTの起動根拠です。正式統合EBOOTは公開前に同じMax条件で
代表PSP-2000+実機gateを行います。PSP Goではhigh memory有効時にARKがPause機能を無効化します。

一次資料:

- ARK-5の`highmem, off`と`MEMSIZE`処理:
  <https://github.com/PSP-Arkfive/ARK-5/commit/53109a62e28b722d4cb3fd7776341137e6318903>
- ARKのrunlevel別設定構文（構文資料のみ。ARK-4は本移植の対応CFWではありません）:
  <https://github.com/PSP-Archive/ARK-4/wiki/settings>

## English

### Requirements

- ARK-5 is the only supported CFW.
- On PSP-2000, PSP-3000, and PSP Go, set `Use Extra Memory` for the application to `Max` before launch.
  `Default` and `Off` are unsupported.
- PSP-1000 has no extra Main RAM, so `Max` does not apply to it, but ARK-5 is still required.
- ARK-5 settings may be lost after changing a Memory Stick or storage device, or after reinstalling
  ARK. Recheck the setting even if the same PSP launched the game before the change.

The formal unified build launches a model-specific runtime from one outer EBOOT. The
PSP-2000/3000/Go runtime uses `MEMSIZE=1` and assumes the maximum user partition. In a 2026-08-26
PSP-3000 / ARK-5 hardware test, an explicit `always, highmem, off` rule prevented the game from reaching
`main()` and left `TH07PSP_BOOT.LOG` unchanged. The same release EBOOT reached the title after high
memory was enabled.

### ARK-5 UI setup

1. Select `東方妖々夢 ～ Perfect Cherry Blossom.` in XMB or the ARK-5 settings UI.
2. Set `Use Extra Memory` to `Max`. Do not leave it at `Default`.
3. Exit and relaunch the game. Restart VSH or power-cycle the PSP only if ARK did not reload the setting.

### Manual SETTINGS.TXT fallback

Use the ARK-5 UI whenever it offers `Max`; updating to a current ARK-5 version is preferred over manual
editing.

1. Back up `ms0:/PSP/SAVEDATA/ARK_01234/SETTINGS.TXT`. If ARK is installed on PSP Go internal storage,
   use `ef0:/PSP/SAVEDATA/ARK_01234/SETTINGS.TXT` instead.
2. Remove `always, highmem, off` and replace it with the line below. Do not keep conflicting rules.

```text
homebrew, highmem, on
```

3. Relaunch the game and confirm `Use Extra Memory = Max` for the application in the ARK-5 UI.

The packaged `ARK5_HIGHMEM_SNIPPET.txt` is one merge rule, not a complete settings file. Never overwrite
your complete `SETTINGS.TXT` with it. The release does not modify ARK settings automatically.

### Hardware evidence

- PSP: PSP-3000 / model 3
- CFW: ARK-5
- EBOOT SHA-256: `55dc6c2e254d1cad8c63e3965dfdd1b4059021ccecd3bd57a70bd945370bc278`
- `NotoSansJP-Regular.ttf` SHA-256:
  `6ab1664d8adc20b19237ddc451c94e31f493cb851a1917242debf66f9af6da05`
- Result: the title was reached after relaunch with high memory enabled

This is startup evidence for the previous standalone PSP-2000+ EBOOT. The formal unified EBOOT must pass
the same Max-configuration gate on representative PSP-2000+ hardware before publication. ARK disables
the Pause feature on PSP Go while high memory is active.

Primary references:

- ARK-5 `highmem, off` and `MEMSIZE` handling:
  <https://github.com/PSP-Arkfive/ARK-5/commit/53109a62e28b722d4cb3fd7776341137e6318903>
- ARK runlevel-specific setting syntax (syntax reference only; ARK-4 is not a
  supported CFW for this port): <https://github.com/PSP-Archive/ARK-4/wiki/settings>
