param([Parameter(Mandatory)][string]$RunDirectory)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content -LiteralPath "$RunDirectory/manifest.json" -Raw | ConvertFrom-Json
function Stats([uint32[]]$values) {
    $v = @($values | Sort-Object)
    return [ordered]@{min=$v[0];p50=$v[99];p95=$v[189];p99=$v[197];max=$v[199]}
}
$all = [Collections.Generic.List[object]]::new()
$results = foreach($profile in 'nominal','constant50','constant100','jitter','stall'){
    $hostLines = @(Get-Content -LiteralPath "$RunDirectory/$profile-host.log")
    $peerLine = Get-Content -LiteralPath "$RunDirectory/$profile-peer.log" -Tail 1
    $rolePattern = 'result,PASS,role=(host|peer),.+count_out=200,count_in=200,links=1,tx_fnv=(\w+),rx_fnv=(\w+),connected_ms=(\d+),.+envelope_errors=0'
    if($hostLines[-1] -notmatch $rolePattern -or $Matches[1] -ne 'host') {throw "$profile host verdict failed"}
    $hostTx=$Matches[2]; $hostRx=$Matches[3]; $elapsed=[int]$Matches[4]
    if($elapsed -lt $manifest.seconds*1000){throw "$profile shorter than scheduled"}
    if($peerLine -notmatch $rolePattern -or $Matches[1] -ne 'peer' -or $Matches[2] -ne $hostRx -or $Matches[3] -ne $hostTx) {throw "$profile peer verdict or direction hash failed"}
    $samples = @($hostLines | Where-Object { $_ -like 'sample,*' })
    if($samples.Count -ne 200){ throw "$profile missing observations" }
    $rows = for($i=0; $i -lt $samples.Count; ++$i){
        $f=$samples[$i].Split(',')
        $seq=[int]$f[1]; $forward=[uint32]$f[2]; $reverse=[uint32]$f[3]; $rtt=[uint32]$f[4]
        if($seq -ne ($i+1) -or $f.Count -ne 5 -or ($forward+$reverse) -ne $rtt){throw "$profile invalid observation $i"}
        $planned = switch($profile){
            'constant50' { 50 }; 'constant100' { 100 }
            'jitter' { if($seq % 2){30}else{70} }
            'stall' { if($seq -eq 101){2000}else{0} }
            default { 0 }
        }
        if($forward -lt 1000*$planned){throw "$profile observation $seq arrived before its gate"}
        $row=[pscustomobject]@{profile=$profile;sequence=$seq;gate_ms=$planned;forward_us=$forward;reverse_us=$reverse;rtt_us=$rtt}
        $all.Add($row)
        $row
    }
    $gateLine = @($hostLines | Where-Object { $_ -like 'gates,*' })[0]
    if($gateLine -notmatch 'peak_queue_bytes=(\d+)'){throw "$profile lacks queue reading"}
    [ordered]@{profile=$profile;elapsed_ms=$elapsed;each_direction=200;payload_bytes=32;host_to_peer_digest=$hostTx;peer_to_host_digest=$hostRx;host_queue_peak_B=[int]$Matches[1];forward_us=(Stats $rows.forward_us);reverse_us=(Stats $rows.reverse_us);rtt_us=(Stats $rows.rtt_us)}
}
$all | Export-Csv -LiteralPath "$RunDirectory/samples.csv" -NoTypeInformation
[ordered]@{complete=$true;unit='microseconds';clock='QueryPerformanceCounter on one Windows machine';profiles=@($results)} | ConvertTo-Json -Depth 6 | Set-Content "$RunDirectory/summary.json" -Encoding utf8
Write-Output "PASS $($all.Count) ordered samples, five planned gate profiles and ten matching endpoint stream hashes."
