# ARK-5 high-memory setup for the PSP-2000+ build

## 日本語

### 対象

この手順は、PSP-2000/3000/Goで64MB版`Touhou 7 PSP-2000+ Beta`をARK-5から起動する場合だけが対象です。
PSP-1000には追加Main RAMがないため適用しないでください。

64MB版EBOOTは`PARAM.SFO`の`MEMSIZE=1`を使用します。2026-08-26のPSP-3000 / ARK-5実機試験では、
ARKの`SETTINGS.TXT`に`always, highmem, off`がある状態だと、ゲームの`main()`より前に起動できず、
`TH07PSP_BOOT.LOG`も更新されませんでした。homebrew runlevelだけでhigh memoryを有効にし、ゲームを起動し直すと
同じGitHub配布EBOOTでタイトル画面まで正常に到達しました。

現行ARK-5一次実装では、`highmem, off`は`HIGHMEM_FORCE_OFF`として扱われ、EBOOTの`MEMSIZE`も
走査しません。`homebrew, highmem, on`は、このEBOOTの起動時にARK-5 UIの`Default`と同じ判定を行い、
`MEMSIZE=1`を持つ64MB EBOOTを最大user partitionへ昇格させます。retail gameへMAXを強制する設定ではありません。

### 設定

現行ARK-5の設定UIを使用できる場合は、`Use Extra Memory`を`Off`のままにせず、`Default`を選びます。
`Default`が表示されない旧版では、下記の手動設定を使用するかARK-5を更新してください。

`SETTINGS.TXT`を手動編集する場合：

1. `ms0:/PSP/SAVEDATA/ARK_01234/SETTINGS.TXT`をPCへバックアップします。PSP GoでARKを内蔵
   ストレージへ入れている場合は`ef0:/PSP/SAVEDATA/ARK_01234/SETTINGS.TXT`が対象です。
2. `always, highmem, off`を削除し、次の1行へ置き換えます。両方の行を残さないでください。

```text
homebrew, highmem, on
```

3. XMBから`Touhou 7 PSP-2000+ Beta`を起動し直します。反映されない場合だけVSH再起動または
   PSPの電源入れ直しを行います。

同梱の`ARK5_HIGHMEM_SNIPPET.txt`は設定全体ではなく、マージ用の1行だけを含むsnippetです。
既存の`SETTINGS.TXT`へファイルごと上書きしないでください。

一次資料：

- ARK-5の`highmem, off`と`MEMSIZE`処理：
  <https://github.com/PSP-Arkfive/ARK-5/commit/53109a62e28b722d4cb3fd7776341137e6318903>
- ARKのrunlevel別設定：<https://github.com/PSP-Archive/ARK-4/wiki/settings>

### 実機確認済みの正本

- PSP: PSP-3000 / model 3
- CFW: ARK-5
- EBOOT SHA-256: `55dc6c2e254d1cad8c63e3965dfdd1b4059021ccecd3bd57a70bd945370bc278`
- `NotoSansJP-Regular.ttf` SHA-256: `6ab1664d8adc20b19237ddc451c94e31f493cb851a1917242debf66f9af6da05`
- 結果: 設定変更後の再起動試験でタイトル画面到達

この配布物はARK設定を自動変更しません。元へ戻す場合はバックアップした`SETTINGS.TXT`を復元してください。
PSP Goではhigh memory有効時にARKがPause機能を無効化します。

## English

### Scope

This applies only when launching the 64 MiB `Touhou 7 PSP-2000+ Beta` build through ARK-5 on a
PSP-2000, PSP-3000, or PSP Go. Do not apply it to PSP-1000; that model has no extra Main RAM.

The 64 MiB EBOOT uses `MEMSIZE=1` in `PARAM.SFO`. In a 2026-08-26 PSP-3000 / ARK-5 hardware test,
an explicit `always, highmem, off` rule prevented the EBOOT from reaching game `main()` and left
`TH07PSP_BOOT.LOG` unchanged. Enabling high memory only for the homebrew runlevel and relaunching
the game allowed the same GitHub release EBOOT to reach the title screen.

In current ARK-5 source, `highmem, off` selects `HIGHMEM_FORCE_OFF` and suppresses the EBOOT
`MEMSIZE` scan. For this EBOOT, `homebrew, highmem, on` produces the same selection as ARK-5's
`Default` UI setting; its detected `MEMSIZE=1` then promotes the 64 MiB EBOOT to the maximum user
partition. It does not force MAX memory on retail games.

### Configuration

When using the current ARK-5 settings UI, do not leave `Use Extra Memory` set to `Off`; select
`Default`. If an older ARK-5 build does not show `Default`, use the manual rule below or update ARK-5.

When editing `SETTINGS.TXT` manually:

1. Back up `ms0:/PSP/SAVEDATA/ARK_01234/SETTINGS.TXT` to your computer. If ARK is installed on PSP Go
   internal storage, edit `ef0:/PSP/SAVEDATA/ARK_01234/SETTINGS.TXT` instead.
2. Remove `always, highmem, off` and replace it with the line below. Do not keep both rules.

```text
homebrew, highmem, on
```

3. Relaunch `Touhou 7 PSP-2000+ Beta` from XMB. If ARK has not reloaded the setting, restart VSH or
   power-cycle the PSP once.

The packaged `ARK5_HIGHMEM_SNIPPET.txt` is a merge snippet, not a complete settings file. Never
overwrite your full `SETTINGS.TXT` with it.

Primary references:

- ARK-5 `highmem, off` and `MEMSIZE` handling:
  <https://github.com/PSP-Arkfive/ARK-5/commit/53109a62e28b722d4cb3fd7776341137e6318903>
- ARK runlevel-specific settings: <https://github.com/PSP-Archive/ARK-4/wiki/settings>

### Hardware-validated artifact

- PSP: PSP-3000 / model 3
- CFW: ARK-5
- EBOOT SHA-256: `55dc6c2e254d1cad8c63e3965dfdd1b4059021ccecd3bd57a70bd945370bc278`
- `NotoSansJP-Regular.ttf` SHA-256: `6ab1664d8adc20b19237ddc451c94e31f493cb851a1917242debf66f9af6da05`
- Result: title screen reached in the relaunch test after the settings change

The package does not modify ARK settings automatically. Restore your backed-up `SETTINGS.TXT` to
roll back the change.
On PSP Go, ARK disables the Pause feature while high memory is active.
