#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$Compiler = $env:CC,
    [string]$IncludeDir = $env:FCAVEX_TEST_INCLUDE
)

$ErrorActionPreference = 'Stop'
if (!$Compiler) { $Compiler = 'clang' }
if (!$IncludeDir) { $IncludeDir = Join-Path $PSScriptRoot '../build/claw/staging/source' }
$cc = (Get-Command -Name $Compiler -CommandType Application -ErrorAction Stop).Source
if (!(Test-Path -LiteralPath (Join-Path $IncludeDir 'cglm')) -or
    !(Test-Path -LiteralPath (Join-Path $IncludeDir 'm-lib'))) {
    throw "Missing cglm/ and m-lib/ in $IncludeDir. Run tools/windows/Build-Wii.ps1 or pass -IncludeDir."
}

& "$PSScriptRoot/launch/run.ps1" -Compiler $cc
if ($LASTEXITCODE) { throw "Launch suite failed with exit code $LASTEXITCODE" }
& "$PSScriptRoot/authority/interest.ps1" -Compiler $cc -DependencySource $IncludeDir
if ($LASTEXITCODE) { throw "Authority suite failed with exit code $LASTEXITCODE" }
& "$PSScriptRoot/authority/checkpoint.ps1" -Compiler $cc
if ($LASTEXITCODE) { throw "Checkpoint suite failed with exit code $LASTEXITCODE" }
& "$PSScriptRoot/metrics/run.ps1" -Compiler $cc
if ($LASTEXITCODE) { throw "Metrics suite failed with exit code $LASTEXITCODE" }
& "$PSScriptRoot/wire/run.ps1" -Compiler $cc -IncludeDir $IncludeDir
if ($LASTEXITCODE) { throw "Wire suite failed with exit code $LASTEXITCODE" }

Write-Output 'PASS native launch, authority, checkpoint, metrics and wire suites.'
