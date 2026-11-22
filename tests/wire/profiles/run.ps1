param([string]$RepoRoot, [string]$Compiler = $env:CC, [string]$IncludeDir,
      [int]$DurationSeconds = 300, [string]$OutputDirectory)
$ErrorActionPreference='Stop'
if(!$RepoRoot){$RepoRoot=(Resolve-Path "$PSScriptRoot/../../..").Path}
if(!$IncludeDir){$IncludeDir="$RepoRoot/build/claw/staging/source"}
if(!$Compiler){$Compiler='clang'}
if($DurationSeconds -lt 40 -or $DurationSeconds -gt 3600){throw 'Duration must be 40 to 3600 seconds.'}
if(!$OutputDirectory){$OutputDirectory="$RepoRoot/build/tests/wire-profiles/run-$((Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmssZ'))"}
if(Test-Path -LiteralPath $OutputDirectory){throw 'Choose a new output directory.'}
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$OutputDirectory=(Resolve-Path $OutputDirectory).Path
$net="$RepoRoot/source/network"
$flags=@('-std=gnu11','-O2','-Wno-unused-function','-Wno-unused-parameter','-Wno-ignored-attributes','-DLAN_TEST_IO',
    '-Dptime_t=uint64_t',"-I$IncludeDir","-I$IncludeDir/cglm","-I$net")
$exe="$OutputDirectory/lan_profile.exe"
& $Compiler @flags "$PSScriptRoot/profile.c" "$PSScriptRoot/profile_transport.c" "$RepoRoot/tests/wire/fixture_registry.c" "$net/lan_frame.c" "$net/lan_rpc.c" -lws2_32 -o $exe
if($LASTEXITCODE){throw 'Profile compile failed'}
$jobs=@()
$ports=@{}
try {
    foreach($name in 'nominal','constant50','constant100','jitter','stall') {
        $p=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
        $p.Start()
        try {$port=$p.LocalEndpoint.Port} finally {$p.Stop()}
        if($ports.ContainsValue($port)){throw 'Port allocation collision: retry the runner.'}
        $ports[$name]=$port
        foreach($role in 'host','peer'){
            $stem="$OutputDirectory/$name-$role"
            $child=Start-Process -FilePath $exe -ArgumentList @($role,$port,$name,($DurationSeconds*1000)) -PassThru -WindowStyle Hidden -RedirectStandardOutput "$stem.log" -RedirectStandardError "$stem.err"
            $child.PriorityClass='BelowNormal'
            $jobs+=@{Role=$role;Profile=$name;Process=$child}
        }
    }
    $manifest=@{utc=(Get-Date).ToUniversalTime().ToString('o');hostname=$env:COMPUTERNAME;seconds=$DurationSeconds;ports=$ports;binding='127.0.0.1';compiler=(& $Compiler --version | Select-Object -First 1);executable_sha256=(Get-FileHash $exe).Hash;profile_source_sha256=(Get-FileHash "$PSScriptRoot/profile.c").Hash;units=@{}}
    foreach($unit in 'lan_transport.c','lan_socket.inc','lan_frame.c','lan_rpc.c'){ $manifest.units[$unit]=(Get-FileHash "$net/$unit").Hash }
    $manifest | ConvertTo-Json -Depth 4 | Set-Content "$OutputDirectory/manifest.json" -Encoding utf8
    Write-Output "Profile run $OutputDirectory. Local ports $($ports.Values -join ', ')"
    $failed=$false
    foreach($job in $jobs){$p=$job.Process; if(!$p.WaitForExit(($DurationSeconds+22)*1000) -or $p.ExitCode -ne 0) { $failed=$true }}
    foreach($name in $ports.Keys) {
        $h=Get-Content "$OutputDirectory/$name-host.log" -Tail 2
        $p=Get-Content "$OutputDirectory/$name-peer.log" -Tail 1
        Write-Output $h; Write-Output $p
        if($h[-1] -notmatch 'result,PASS,.+tx_fnv=(\w+),rx_fnv=(\w+)' ){ $failed=$true; continue }
        $outH=$Matches[1]; $inH=$Matches[2]
        if($p -notmatch 'result,PASS,.+tx_fnv=(\w+),rx_fnv=(\w+)' -or $outH -ne $Matches[2] -or $inH -ne $Matches[1]){ $failed=$true }
    }
    if($failed){throw 'A long profile failed or the endpoint digests differed.'}
    & "$PSScriptRoot/verify.ps1" -RunDirectory $OutputDirectory
    'PASS all five loopback profiles, both endpoint stream digests matched' | Tee-Object -FilePath "$OutputDirectory/PASS.txt"
} finally {
    foreach($job in $jobs){if(!$job.Process.HasExited){$job.Process.Kill(); $job.Process.WaitForExit()}}
}
