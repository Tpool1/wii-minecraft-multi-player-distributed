[CmdletBinding()]
param(
    [string]$Compiler = $env:CC,
    [string]$IncludeDir = $env:FCAVEX_TEST_INCLUDE,
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$Compiler) { $Compiler = 'clang' }
if (!$IncludeDir) { $IncludeDir = Join-Path $repo 'build/claw/staging/source' }
if (!(Test-Path -LiteralPath (Join-Path $IncludeDir 'm-lib/m-string.h'))) {
    throw 'Pass -IncludeDir containing the pinned m-lib/ and cglm/ headers from Build-Wii.ps1 staging.'
}
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repo 'build/tests/wire' }
[void](New-Item -ItemType Directory -Force -Path $OutputDirectory)
$OutputDirectory = (Resolve-Path $OutputDirectory).Path
$IncludeDir = (Resolve-Path $IncludeDir).Path
$net = Join-Path $repo 'source/network'
$flags = @('-std=gnu11','-Wall','-Wextra','-Wno-unused-function','-Wno-unused-parameter','-Wno-ignored-attributes',
           '-Dptime_t=uint64_t',"-I$IncludeDir","-I$IncludeDir/cglm","-I$net")
function Build-Test([string]$Name,[string[]]$Units,[string[]]$Libraries) {
    $files = @((Join-Path $PSScriptRoot ($Name+'_test.c')))
    $files += $Units | ForEach-Object { Join-Path $net ($_+'.c') }
    $exe = Join-Path $OutputDirectory ($Name+'.exe')
    & $Compiler @flags @files @Libraries -o $exe
    if ($LASTEXITCODE) { throw "$Name compile failed" }
}
Build-Test lan_frame @('lan_frame') @()
Build-Test lan_rpc @('lan_rpc') @()
Build-Test lan_transport @('lan_transport','lan_frame','lan_rpc') @('-lws2_32')
foreach ($name in 'lan_frame','lan_rpc') {
    & (Join-Path $OutputDirectory ($name+'.exe')) 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory ($name+'.log'))
    if ($LASTEXITCODE) { throw "$name failed" }
}
# Reserve a loopback-only ephemeral port, then hand it to the host fixture.
$probe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
$probe.Start()
try { $port = $probe.LocalEndpoint.Port } finally { $probe.Stop() }
$processes = foreach ($role in 'host','peer') {
    Start-Process -FilePath (Join-Path $OutputDirectory 'lan_transport.exe') -ArgumentList @($role,$port) -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory ($role+'.log')) -RedirectStandardError (Join-Path $OutputDirectory ($role+'-errors.log'))
}
$failed = $false
foreach ($p in $processes) {
    if (!$p.WaitForExit(16000) -or $p.ExitCode -ne 0) { $failed = $true }
}
Get-Content -LiteralPath (Join-Path $OutputDirectory 'host.log'),(Join-Path $OutputDirectory 'peer.log')
if ($failed) {
    Get-Content -LiteralPath (Join-Path $OutputDirectory 'host-errors.log'),(Join-Path $OutputDirectory 'peer-errors.log')
    throw 'Loopback transport failed.'
}
