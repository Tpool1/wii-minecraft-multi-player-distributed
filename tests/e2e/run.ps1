#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Game,
    [Parameter(Mandatory)][string]$Dolphin,
    [Parameter(Mandatory)][string]$HostCard,
    [string]$PeerCard,
    [Parameter(Mandatory)][string]$OutputRoot,
    [switch]$Play,
    [switch]$Offline,
    [switch]$LegacyControl,
    [ValidateRange(10,600)][int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
if ($LegacyControl) { $Offline=$true }
if (!$Offline -and !$PeerCard) { throw 'Supply a peer SD card for two-player mode.' }
if ($Play -and $Offline) { throw 'Use -Play for a two-player session, or -Offline for the control test.' }
$gamePath = (Resolve-Path -LiteralPath $Game).Path
$dolphinPath = (Resolve-Path -LiteralPath $Dolphin).Path
$cards = @{ A = (Resolve-Path -LiteralPath $HostCard).Path }
if (!$Offline) { $cards.B = (Resolve-Path -LiteralPath $PeerCard).Path }
$csc = Join-Path $env:SystemRoot 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
$node = (Get-Command node -ErrorAction Stop).Path
$root = [IO.Directory]::CreateDirectory([IO.Path]::GetFullPath($OutputRoot)).FullName
$runId = 'run-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0,6)
$run = [IO.Directory]::CreateDirectory((Join-Path $root $runId)).FullName
Write-Host "Run directory $run"
$mode = if ($LegacyControl) { 'legacy-control' } elseif ($Offline) { 'offline' } elseif ($Play) { 'play' } else { 'automated' }
$record = [ordered]@{ utc = [DateTime]::UtcNow.ToString('o'); mode=$mode; game=$gamePath
    gameSHA256=(Get-FileHash -LiteralPath $gamePath).Hash; dolphin=$dolphinPath
    dolphinSHA256=(Get-FileHash -LiteralPath $dolphinPath).Hash; machine=$env:COMPUTERNAME
    cards=@{}; processes=@{}; endpoint='127.0.0.1:45838'; passed=$false }
$projector = $null
$guests = @{}
$logs = @{}
$step = 'initialization'
function Write-Record { $record | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath "$run/run.json" -Encoding utf8NoBOM }
function Set-LocalFile($file, $content) { [IO.File]::WriteAllText($file, $content, [Text.UTF8Encoding]::new($false)) }
function Test-Pulse([string]$lo, [string]$hi) {
    return "(timer(12.0) > $lo) & (timer(12.0) < $hi)"
}
function New-GuestProfile([string]$role) {
    $profile = Join-Path $run $role
    $config = [IO.Directory]::CreateDirectory((Join-Path $profile 'Config')).FullName
    $load = [IO.Directory]::CreateDirectory((Join-Path $profile 'Load')).FullName
    $card = Join-Path $load 'WiiSD.raw'
    [IO.File]::Copy($cards[$role], $card, $false)
    $record.cards[$role] = @{ original=$cards[$role]; sha256=(Get-FileHash -LiteralPath $card).Hash; clone=$card }
    $volume = if ($role -eq 'B') { 0 } else { 75 }
    Set-LocalFile "$config/Dolphin.ini" @"
[General]
WiiSDCardPath = $card
[Core]
WiiSDCard = True
WiiSDCardEnableFolderSync = False
[Input]
BackgroundInput = True
[DSP]
Volume = $volume
[Analytics]
Enabled = False
PermissionAsked = True
[AutoUpdate]
UpdateTrack =
[Interface]
ConfirmStop = False
PauseOnFocusLost = False
"@
    Set-LocalFile "$config/Logger.ini" @"
[Options]
WriteToFile = True
Verbosity = 4
[Logs]
BOOT = True
CORE = True
Video = True
OSREPORT = True
IOS_SD = False
IOS_NET = False
"@
    # D-pad moves the character; fCavEX maps Nunchuk stick to the camera.
    $map = [ordered]@{ 'D-Pad/Up'='W'; 'D-Pad/Down'='S'; 'D-Pad/Left'='A'; 'D-Pad/Right'='D'
        'Nunchuk/Stick/Up'='I'; 'Nunchuk/Stick/Down'='K'; 'Nunchuk/Stick/Left'='J'; 'Nunchuk/Stick/Right'='L'
        'Nunchuk/Buttons/C'='R'; 'Nunchuk/Buttons/Z'='F'; 'Buttons/A'='T'; 'Buttons/B'='SPACE'
        'Buttons/1'='G'; 'Buttons/2'='Y'; 'Buttons/-'='Q'; 'Buttons/+'='E'; 'Buttons/Home'='H' }
    if ($role -eq 'B') {
        $keys = @('UP','DOWN','LEFT','RIGHT','NUMPAD8','NUMPAD5','NUMPAD4','NUMPAD6','NUMPAD3','NUMPAD1','NUMPAD0','RCONTROL','NUMPAD7','NUMPAD9','HOME','END','INSERT')
        $i=0
        foreach ($key in @($map.Keys)) { $map[$key]=$keys[$i]; $i++ }
    }
    if (!$Play) {
        foreach ($key in @($map.Keys)) { $map[$key]='0' }
        if ($role -eq 'A') {
            $map['Nunchuk/Buttons/C'] = Test-Pulse '0.12' '0.18'
            $map['D-Pad/Up'] = Test-Pulse '0.40' '0.43'
            $map['D-Pad/Down'] = Test-Pulse '0.57' '0.60'
        } else {
            $map['Nunchuk/Buttons/C'] = Test-Pulse '0.23' '0.29'
            $map['D-Pad/Down'] = Test-Pulse '0.44' '0.47'
            $map['D-Pad/Up'] = Test-Pulse '0.64' '0.67'
        }
        # A held Z allows digging to complete and prepares a new visible face for later C presses.
        $map['Nunchuk/Buttons/Z'] = '(timer(32.0) > 0.45) & (timer(32.0) < 0.92)'
        if ($Offline) { $map['Buttons/A'] = Test-Pulse '0.04' '0.09' }
    }
    $remote = @('[Wiimote1]','Device = DInput/0/Keyboard Mouse','Source = 1','Extension = Nunchuk',
        'IR/Up = 0','IR/Down = 0','IR/Left = 0','IR/Right = 0') +
        @($map.GetEnumerator() | ForEach-Object { "$($_.Key) = $($_.Value)" })
    Set-LocalFile "$config/WiimoteNew.ini" (($remote -join [Environment]::NewLine) + [Environment]::NewLine)
    if ($Offline) {
        Set-LocalFile "$config/Hotkeys.ini" "[Hotkeys]`r`nDevice = DInput/0/Keyboard Mouse`r`nGeneral/Take Screenshot = timer(6.0) > 0.86`r`n"
    }
    $logs[$role] = Join-Path $profile 'Logs/dolphin.log'
}
function Start-Guest([string]$role) {
    $profile = Join-Path $run $role
    $process = Start-Process -FilePath $dolphinPath -ArgumentList ('-u "{0}" -b -e "{1}"' -f $profile,$gamePath) -WorkingDirectory $profile -PassThru
    $guests[$role] = $process
    $record.processes[$role]=$process.Id
    Write-Record
}
function Stop-OwnedProcess($process) {
    if ($null -eq $process -or $process.HasExited) { return }
    $null = $process.CloseMainWindow()
    if (!$process.WaitForExit(5000)) { $process.Kill(); $null = $process.WaitForExit(5000) }
}
function Read-GuestLog($role) {
    if (!(Test-Path -LiteralPath $logs[$role])) { return '' }
    $stream = [IO.FileStream]::new($logs[$role], [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
    try {
        $reader = [IO.StreamReader]::new($stream)
        try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
    } finally { $stream.Dispose() }
}
function Wait-Until($description, $predicate, [int]$seconds) {
    $deadline = [DateTime]::UtcNow.AddSeconds($seconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($guest in $guests.Values) { if ($guest.HasExited) { throw "$description; Dolphin $($guest.Id) exited $($guest.ExitCode)" } }
        if (& $predicate) { return }
        Start-Sleep -Milliseconds 400
    }
    throw "Timed out after $seconds seconds waiting for $description."
}
function Get-Assessment {
    if (!(Test-Path -LiteralPath $logs.A) -or !(Test-Path -LiteralPath $logs.B)) { return $false }
    $report = & $node "$PSScriptRoot/verify-game.cjs" $logs.A $logs.B
    $code = $LASTEXITCODE
    Set-LocalFile "$run/gameplay.json" ($report -join [Environment]::NewLine)
    if ($code -gt 1) { throw "Verifier failed to run with exit code $code." }
    return $code -eq 0
}
try {
    foreach ($role in $cards.Keys) { New-GuestProfile $role }
    Write-Record
    if (!$Offline) {
        $projectorExe = Join-Path $run 'TwoViews.exe'
        $projectorSource = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'TwoViews.cs')).Path
        & $csc /nologo /target:winexe /optimize+ "/out:$projectorExe" '/r:System.Windows.Forms.dll' '/r:System.Drawing.dll' $projectorSource
        if ($LASTEXITCODE -ne 0) { throw 'Could not compile the Windows desktop compositor.' }
    }
    $step='boot host'
    Start-Guest 'A'
    if ($LegacyControl) {
        $step='unmodified baseline rendering'
        Wait-Until 'three frames from the original single-player binary' {
            $shots = @(Get-ChildItem -LiteralPath (Join-Path $run 'A/ScreenShots') -File -Filter '*.png' -Recurse -ErrorAction SilentlyContinue)
            return $shots.Count -ge 3
        } $TimeoutSeconds
        $record.passed=$true
    } elseif ($Offline) {
        Wait-Until 'offline client READY and movement' {
            if (!(Test-Path -LiteralPath $logs.A)) { return $false }
            $details = & $node "$PSScriptRoot/verify-game.cjs" --offline $logs.A
            $status = $LASTEXITCODE
            Set-LocalFile "$run/offline.json" ($details -join [Environment]::NewLine)
            return $status -eq 0
        } $TimeoutSeconds
        $record.passed=$true
    } else {
        $step='local host listener'
        Wait-Until 'host-owned loopback listener' {
            $rows = @(netstat.exe -ano -p TCP | Where-Object { $_ -match ('\s+LISTENING\s+' + $guests.A.Id + '\s*$') })
            if ($rows -match '\s+(?!127\.0\.0\.1:)\S+:45838\s') { throw 'The game opened the test listener outside 127.0.0.1.' }
            $matched=@($rows -match '\s+127\.0\.0\.1:45838\s')
            if ($matched.Count) { $record.listener = $matched[0].Trim() }
            return $matched.Count -gt 0
        } $TimeoutSeconds
        Start-Guest 'B'
        $step='renderer windows'
        Wait-Until 'two live render windows' {
            $guests.A.Refresh(); $guests.B.Refresh()
            return $guests.A.MainWindowHandle -ne 0 -and $guests.B.MainWindowHandle -ne 0
        } 25
        $handles = @($guests.A.MainWindowHandle.ToInt64(), $guests.B.MainWindowHandle.ToInt64())
        $projector = Start-Process -FilePath "$run/TwoViews.exe" -ArgumentList ('{0} {1} "{2}"' -f $handles[0],$handles[1],$run) -PassThru
        $record.processes.projector=$projector.Id
        $record.sourceWindows=$handles
        Write-Record
        Wait-Until 'registered live thumbnails' { Test-Path -LiteralPath "$run/compositor.txt" } 12
        if ($Play) {
            Write-Host 'The two-player projector is live. F11 toggles fullscreen. Escape closes the projector and its two guests.'
            $projector.WaitForExit()
            $record.passed=$projector.ExitCode -eq 0
        } else {
            $step='in-game state and compositor'
            Wait-Until 'both clients ready to render the world' {
                (Read-GuestLog 'A') -match '\[FCX\] READY slot=0' -and (Read-GuestLog 'B') -match '\[FCX\] READY slot=1'
            } $TimeoutSeconds
            Set-LocalFile "$run/capture.request" 'startup'
            Wait-Until 'independent movement, both accepted replicated edits and both client slots' { Get-Assessment } $TimeoutSeconds
            Set-LocalFile "$run/capture.request" 'in-game'
            Wait-Until 'live composed game image' { Test-Path -LiteralPath "$run/in-game-frames.txt" } 20
            if ((Get-Content -LiteralPath "$run/in-game-frames.txt" -Raw) -notmatch 'distinctDetailedFrames=True') {
                throw 'The projector captured identical or blank panes; inspect in-game-A.png and in-game-B.png.'
            }
            $step='disconnect and peer save'
            Stop-OwnedProcess $guests.B
            $guests.Remove('B')
            Wait-Until 'host flushed departing peer' { (Read-GuestLog 'A') -match '\[FCX\] PEER_SAVE ok=1' } 30
            Copy-Item -LiteralPath $logs.B -Destination "$run/B-first.log"
            $step='peer rejoin'
            $record.processes.Bfirst=$record.processes.B
            $originalPeerLog=Read-GuestLog 'B'
            $logOffset=$originalPeerLog.Length
            Start-Guest 'B'
            Wait-Until 'rejoined peer received slot and local pose' {
                $contents=Read-GuestLog 'B'
                # Dolphin can start a new file or append to the old one.
                if ($contents.Length -ge $logOffset -and $contents.StartsWith($originalPeerLog)) { $contents=$contents.Substring($logOffset) }
                $contents -match '\[FCX\] READY slot=1' -and $contents -match '\[FCX\] LOCAL slot=1'
            } $TimeoutSeconds
            $record.peerRejoined=$true
            $record.passed=$true
        }
    }
} catch {
    $record.error = $_.Exception.Message
    Write-Warning "$step failed. $($record.error)"
} finally {
    Stop-OwnedProcess $projector
    foreach ($role in 'B','A') {
        if ($guests.ContainsKey($role)) { Stop-OwnedProcess $guests[$role] }
    }
    $record.lastStep=$step
    $record.finishedUtc=[DateTime]::UtcNow.ToString('o')
    Write-Record
}
if (!$record.passed) { exit 1 }
