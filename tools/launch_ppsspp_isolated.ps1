param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z0-9_-]+$')]
    [string]$InstanceName,

    [Parameter(Mandatory = $true)]
    [string]$EbootPath,

    [string]$TemplateApp = "$env:USERPROFILE\.ppsspp-test.cTi2th\app",
    [string]$ProfilesRoot = "$env:USERPROFILE\.ppsspp-codex-instances",
    [switch]$EnableAudio,
    [int]$WindowX = 40,
    [int]$WindowY = 40,
    [switch]$NoStart
)

$ErrorActionPreference = "Stop"

function Set-IniValue {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Section,
        [Parameter(Mandatory = $true)][string]$Key,
        [Parameter(Mandatory = $true)][string]$Value
    )

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.AddRange([System.IO.File]::ReadAllLines($Path))
    $sectionHeader = "[$Section]"
    $sectionStart = -1
    for ($i = 0; $i -lt $lines.Count; ++$i) {
        if ($lines[$i].Trim() -ieq $sectionHeader) {
            $sectionStart = $i
            break
        }
    }

    if ($sectionStart -lt 0) {
        if ($lines.Count -gt 0 -and $lines[$lines.Count - 1] -ne "") {
            $lines.Add("")
        }
        $lines.Add($sectionHeader)
        $lines.Add("$Key = $Value")
    }
    else {
        $sectionEnd = $lines.Count
        for ($i = $sectionStart + 1; $i -lt $lines.Count; ++$i) {
            if ($lines[$i].TrimStart().StartsWith("[")) {
                $sectionEnd = $i
                break
            }
        }

        $keyPattern = '^\s*' + [regex]::Escape($Key) + '\s*='
        $keyIndex = -1
        for ($i = $sectionStart + 1; $i -lt $sectionEnd; ++$i) {
            if ($lines[$i] -match $keyPattern) {
                $keyIndex = $i
                break
            }
        }
        if ($keyIndex -ge 0) {
            $lines[$keyIndex] = "$Key = $Value"
        }
        else {
            $lines.Insert($sectionEnd, "$Key = $Value")
        }
    }

    $utf8Bom = [System.Text.UTF8Encoding]::new($true)
    [System.IO.File]::WriteAllLines($Path, $lines, $utf8Bom)
}

function New-DirectoryJunctionIfMissing {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Target
    )

    if (Test-Path -LiteralPath $Path) {
        if (-not (Get-Item -LiteralPath $Path).PSIsContainer) {
            throw "junction path exists but is not a directory: $Path"
        }
        return
    }
    [void](New-Item -ItemType Junction -Path $Path -Target $Target)
}

$sourceEboot = (Resolve-Path -LiteralPath $EbootPath).Path
$templateRoot = (Resolve-Path -LiteralPath $TemplateApp).Path
$templateExe = Join-Path $templateRoot "PPSSPPWindows64.exe"
$templateAssets = Join-Path $templateRoot "assets"
$templateMemstick = Join-Path $templateRoot "memstick"
$templateSystem = Join-Path $templateMemstick "PSP\SYSTEM"
$templateGame = Join-Path $templateMemstick "PSP\GAME\TH07PSP"

foreach ($required in @($templateExe, $templateAssets, $templateSystem, $templateGame)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "PPSSPP template component missing: $required"
    }
}

$profileRoot = Join-Path ([System.IO.Path]::GetFullPath($ProfilesRoot)) $InstanceName
$appRoot = Join-Path $profileRoot "app"
$memstickRoot = Join-Path $appRoot "memstick"
$systemRoot = Join-Path $memstickRoot "PSP\SYSTEM"
$gameRoot = Join-Path $memstickRoot "PSP\GAME\TH07PSP"
$logRoot = Join-Path $profileRoot "logs"
foreach ($directory in @($appRoot, $systemRoot, $gameRoot, $logRoot)) {
    [void](New-Item -ItemType Directory -Path $directory -Force)
}

$profileExe = Join-Path $appRoot "PPSSPPWindows64.exe"
$alreadyRunning = @(Get-Process PPSSPPWindows64 -ErrorAction SilentlyContinue | Where-Object {
    try { $_.Path -ieq $profileExe } catch { $false }
})
if ($alreadyRunning.Count -gt 0) {
    throw "isolated profile '$InstanceName' is already running as PID $($alreadyRunning[0].Id)"
}

Copy-Item -LiteralPath $templateExe -Destination $profileExe -Force
New-DirectoryJunctionIfMissing -Path (Join-Path $appRoot "assets") -Target $templateAssets
New-DirectoryJunctionIfMissing -Path (Join-Path $gameRoot "th7") -Target (Join-Path $templateGame "th7")

foreach ($name in @(
    "NotoSansJP-Regular.ttf",
    "msgothic.ttc",
    "music_bg.rgb565",
    "title01.psp1000.cache",
    "score.dat",
    "th07.cfg"
)) {
    $source = Join-Path $templateGame $name
    $destination = Join-Path $gameRoot $name
    if ((Test-Path -LiteralPath $source) -and -not (Test-Path -LiteralPath $destination)) {
        Copy-Item -LiteralPath $source -Destination $destination
    }
}

$profileEboot = Join-Path $gameRoot "EBOOT.PBP"
Copy-Item -LiteralPath $sourceEboot -Destination $profileEboot -Force

$configPath = Join-Path $systemRoot "ppsspp.ini"
$controlsPath = Join-Path $systemRoot "controls.ini"
if (-not (Test-Path -LiteralPath $configPath)) {
    Copy-Item -LiteralPath (Join-Path $templateSystem "ppsspp.ini") -Destination $configPath
}
if (-not (Test-Path -LiteralPath $controlsPath)) {
    Copy-Item -LiteralPath (Join-Path $templateSystem "controls.ini") -Destination $controlsPath
}

$nameHash = 2166136261
foreach ($character in $InstanceName.ToCharArray()) {
    $nameHash = (($nameHash -bxor [int][char]$character) * 16777619) -band 0x7fffffff
}
$portOffset = 20000 + ($nameHash % 20000)
$remoteIsoPort = 30000 + ($nameHash % 20000)
$audioValue = if ($EnableAudio) { "True" } else { "False" }

Set-IniValue $configPath "General" "AutoRun" "True"
Set-IniValue $configPath "General" "PauseOnLostFocus" "False"
Set-IniValue $configPath "General" "CheckForNewVersion" "False"
Set-IniValue $configPath "General" "DiscordRichPresence" "False"
Set-IniValue $configPath "General" "RemoteDebuggerOnStartup" "False"
Set-IniValue $configPath "General" "RemoteShareOnStartup" "False"
Set-IniValue $configPath "General" "RemoteISOPort" "$remoteIsoPort"
Set-IniValue $configPath "General" "WindowX" "$WindowX"
Set-IniValue $configPath "General" "WindowY" "$WindowY"
Set-IniValue $configPath "Sound" "Enable" $audioValue
Set-IniValue $configPath "SystemParam" "PSPModel" "0"
Set-IniValue $configPath "SystemParam" "NickName" "Codex-$InstanceName"
Set-IniValue $configPath "Network" "EnableWlan" "False"
Set-IniValue $configPath "Network" "EnableAdhocServer" "False"
Set-IniValue $configPath "Network" "PortOffset" "$portOffset"

$ppssppLog = Join-Path $logRoot "ppsspp.log"
$bootLog = Join-Path $memstickRoot "TH07PSP_BOOT.LOG"
$result = [ordered]@{
    Instance = $InstanceName
    Profile = $profileRoot
    Eboot = $profileEboot
    BootLog = $bootLog
    PpssppLog = $ppssppLog
    Audio = $audioValue
    PortOffset = $portOffset
}

if (-not $NoStart) {
    $quotedEboot = '"' + $profileEboot + '"'
    $arguments = @(
        $quotedEboot,
        '"--config=' + $configPath + '"',
        '"--controlconfig=' + $controlsPath + '"',
        '"--log=' + $ppssppLog + '"'
    )
    $process = Start-Process -FilePath $profileExe -ArgumentList $arguments -PassThru
    $result.ProcessId = $process.Id
}

[pscustomobject]$result
