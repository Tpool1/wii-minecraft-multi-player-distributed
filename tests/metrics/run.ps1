param([string]$Compiler = 'clang')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/../..").Path
$temp = New-Item -ItemType Directory -Path (Join-Path ([IO.Path]::GetTempPath()) ("fcavex-metrics-" + [guid]::NewGuid()))
Push-Location -LiteralPath $temp
try {
    & $Compiler -std=gnu99 -DPLATFORM_PC -Wall -Wextra -Werror "$repo/tests/metrics/metrics_test.c" "$repo/source/game/session_metrics.c" -o ./metrics.exe
    if($LASTEXITCODE) { throw "Metrics suite did not compile" }
    & ./metrics.exe
    if($LASTEXITCODE) { throw "Metrics suite failed" }
} finally {
    Pop-Location
    # Only the unique, newly created test directory is removed.
    if($temp.Parent.FullName -eq ([IO.Path]::GetTempPath().TrimEnd([IO.Path]::DirectorySeparatorChar))) {
        Remove-Item -LiteralPath $temp.FullName -Recurse
    }
}
