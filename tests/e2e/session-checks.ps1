# Dot-sourced by run.ps1; these checks operate only on the current run's guests and files.
function Get-Count([string]$text, [string]$pattern) { return [regex]::Matches($text,$pattern).Count }

function Get-PairedActivity {
    $a = Read-GuestLog 'A'
    $b = Read-GuestLog 'B'
    if ($a -match 'FIFO: Unknown Opcode' -or $b -match 'FIFO: Unknown Opcode') {
        throw 'Dolphin reported an invalid GPU command during the paired session.'
    }
    return [ordered]@{
        hostLocal = Get-Count $a '\[FCX\] LOCAL slot=0'
        hostSawPeer = Get-Count $a '\[FCX\] REMOTE slot=1'
        peerLocal = Get-Count $b '\[FCX\] LOCAL slot=1'
        peerSawHost = Get-Count $b '\[FCX\] REMOTE slot=0'
        disconnects = Get-Count $a '\[FCX\] LINK role=host connected=0'
    }
}

function Invoke-PairedSoak {
    if (!$SoakMinutes) { return }
    $duration=[TimeSpan]::FromMinutes($SoakMinutes)
    $elapsed=[Diagnostics.Stopwatch]::StartNew()
    $initial=Get-PairedActivity
    $last=$initial
    $activity=@{hostLocal=0;peerLocal=0;hostSawPeer=0;peerSawHost=0}
    $samples=[Collections.Generic.List[object]]::new()
    $nextSample=0
    do {
        foreach ($guest in $guests.Values) { if ($guest.HasExited) { throw "Dolphin $($guest.Id) exited during the soak." } }
        if ($projector.HasExited) { throw 'The shared projector closed during the soak.' }
        $seconds=[int][Math]::Floor($elapsed.Elapsed.TotalSeconds)
        if ($seconds -ge $nextSample -or $elapsed.Elapsed -ge $duration) {
            $now=Get-PairedActivity
            if ($now.disconnects -ne $initial.disconnects) { throw 'Peer link dropped during continuous play.' }
            foreach ($key in @($activity.Keys)) {
                if ($now[$key] -gt $last[$key]) { $activity[$key]=$seconds }
                if ($seconds-$activity[$key] -gt 45) { throw "No new $key gameplay samples for more than 45 seconds." }
            }
            $samples.Add([ordered]@{ seconds=$seconds; utc=[DateTime]::UtcNow.ToString('o'); activity=$now })
            $last=$now
            $nextSample=$seconds+10
            $soak=[ordered]@{ passed=($elapsed.Elapsed -ge $duration); requestedSeconds=$duration.TotalSeconds;
                elapsedSeconds=[Math]::Round($elapsed.Elapsed.TotalSeconds,3); hostPid=$guests.A.Id;
                peerPid=$guests.B.Id; projectorPid=$projector.Id; samples=$samples }
            Set-LocalFile "$run/soak.json" ($soak | ConvertTo-Json -Depth 6)
            if ($soak.passed) { $record.soakSeconds=$soak.elapsedSeconds; return }
        }
        Start-Sleep -Milliseconds 250
    } while ($true)
}

function Invoke-PeerCycles {
    $rounds=[Collections.Generic.List[object]]::new()
    for ($i=1; $i -le $Rejoins; $i++) {
        $watch=[Diagnostics.Stopwatch]::StartNew()
        $previousPid=$guests.B.Id
        $hostSaves=Get-Count (Read-GuestLog 'A') '\[FCX\] PEER_SAVE ok=1'
        $priorConnections=if (Test-Path -LiteralPath "$run/projector-connections.txt") {
            Get-Count (Get-Content -LiteralPath "$run/projector-connections.txt" -Raw) ' disconnected '
        } else { 0 }
        Stop-OwnedProcess $guests.B
        $guests.Remove('B')
        Wait-Until "new host checkpoint for departing peer $i" {
            (Get-Count (Read-GuestLog 'A') '\[FCX\] PEER_SAVE ok=1') -gt $hostSaves
        } 30
        Wait-Until "projector retained A on disconnect $i" {
            (Test-Path -LiteralPath "$run/projector-connections.txt") -and
                (Get-Count (Get-Content -LiteralPath "$run/projector-connections.txt" -Raw) ' disconnected ') -gt $priorConnections
        } 12
        $capture=($i -eq 1 -or $i -eq $Rejoins)
        $suffix=if ($i -eq 1) { '' } else { '-final' }
        if ($capture) {
            $shot='peer-offline'+$suffix
            Set-LocalFile "$run/capture.request" $shot
            Wait-Until "host pane and disconnected notice for round $i" { Test-Path -LiteralPath "$run/$shot-frames.txt" } 20
            $stats=Get-Content -LiteralPath "$run/$shot-frames.txt" -Raw
            if ($stats -notmatch 'A sampledColors=(\d+)' -or [int]$Matches[1] -lt 12) {
                throw 'The host pane lost its game frame while B was offline.'
            }
        }
        $label=if ($i -eq 1) { 'B-first.log' } else { 'B-round-{0:D2}.log' -f $i }
        $previousLog=Join-Path $run $label
        Move-Item -LiteralPath $logs.B -Destination $previousLog
        if ($i -eq 1) { $record.processes.Bfirst=$previousPid }
        Start-Guest 'B'
        Wait-Until "new peer $i received its slot, avatar and local movement" {
            $contents=Read-GuestLog 'B'
            $contents -match '\[FCX\] READY slot=1' -and $contents -match '\[FCX\] REMOTE slot=0' -and
                (Get-Count $contents '\[FCX\] LOCAL slot=1') -ge 3
        } $TimeoutSeconds
        $warmReport=Join-Path $run ('warm-{0:D2}.json' -f $i)
        Wait-Until "restored peer position and a new move accepted by host in round $i" {
            $json=& $node "$PSScriptRoot/verify-cold.cjs" --warm $previousLog $logs.B $logs.A
            $status=$LASTEXITCODE
            Set-LocalFile $warmReport ($json -join [Environment]::NewLine)
            return $status -eq 0
        } $TimeoutSeconds
        $pose=Get-Content -LiteralPath $warmReport -Raw | ConvertFrom-Json
        Rebind-Peer
        if ($capture) {
            $shot='peer-rejoined'+$suffix
            Set-LocalFile "$run/capture.request" $shot
            Wait-Until "rejoined game picture for round $i" { Test-Path -LiteralPath "$run/$shot-frames.txt" } 20
            if ((Get-Content -LiteralPath "$run/$shot-frames.txt" -Raw) -notmatch 'distinctDetailedFrames=True') {
                throw 'The replacement peer pane is blank or duplicates A.'
            }
        }
        $rounds.Add([ordered]@{ round=$i; hostPid=$guests.A.Id; oldPeerPid=$previousPid;
            newPeerPid=$guests.B.Id; projectorPid=$projector.Id; utc=[DateTime]::UtcNow.ToString('o');
            elapsedSeconds=[Math]::Round($watch.Elapsed.TotalSeconds,3); newCheckpoint=$true;
            rejoinedPose=$pose; formerLog=$label })
        Set-LocalFile "$run/rejoins.json" ($rounds | ConvertTo-Json -Depth 7 -AsArray)
        $record.completedRejoins=$i
        Write-Record
    }
    $record.peerRejoined=$true
}
