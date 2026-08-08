param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Up", "Down", "Z", "X", "Select")]
    [string]$Key,
    [int]$HoldMs = 160,
    [int]$Count = 1,
    [int]$GapMs = 50
)

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class PpssppKeyInput
{
    [DllImport("user32.dll")]
    public static extern void keybd_event(byte virtualKey, byte scanCode, uint flags,
                                          UIntPtr extraInfo);
}
'@

$process = Get-Process PPSSPPWindows64 -ErrorAction Stop
$shell = New-Object -ComObject WScript.Shell
[void]$shell.AppActivate($process.MainWindowTitle)
Start-Sleep -Milliseconds 100

$virtualKey = switch ($Key) {
    "Up" { 0x26 }
    "Down" { 0x28 }
    "Z" { 0x5A }
    "X" { 0x58 }
    "Select" { 0x56 }
}

for ($i = 0; $i -lt $Count; ++$i) {
    [PpssppKeyInput]::keybd_event($virtualKey, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $HoldMs
    [PpssppKeyInput]::keybd_event($virtualKey, 0, 2, [UIntPtr]::Zero)
    if ($i + 1 -lt $Count) {
        Start-Sleep -Milliseconds $GapMs
    }
}
