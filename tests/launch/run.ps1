param([string]$Compiler = 'cc')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$binary = Join-Path ([IO.Path]::GetTempPath()) ("fcavex-launch-" + [guid]::NewGuid() + ".exe")
try {
    & $Compiler -std=c99 -Wall -Wextra -Werror -pedantic (Join-Path $repo 'tests/launch/launch_test.c') (Join-Path $repo 'source/game/session_launch.c') -o $binary
    if ($LASTEXITCODE) { throw "Launch tests did not compile ($LASTEXITCODE)" }
    & $binary
    if ($LASTEXITCODE) { throw "Launch behavior tests failed ($LASTEXITCODE)" }
} finally {
    if (Test-Path -LiteralPath $binary) { Remove-Item -LiteralPath $binary }
}
