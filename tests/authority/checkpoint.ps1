param([string]$Compiler = 'cc')
$ErrorActionPreference='Stop'
$repo=(Resolve-Path "$PSScriptRoot/../..").Path
$dir=[IO.Directory]::CreateDirectory((Join-Path ([IO.Path]::GetTempPath()) ("fcavex-checkpoint-" + [guid]::NewGuid()))).FullName
Push-Location -LiteralPath $dir
try {
    & $Compiler -std=c99 -Wall -Wextra -Werror "$repo/source/network/checkpoint_file.c" "$repo/tests/authority/checkpoint.c" -o "$dir/test.exe"
    if ($LASTEXITCODE) { throw "Checkpoint tests did not compile" }
    & "$dir/test.exe"
    if ($LASTEXITCODE) { throw "Checkpoint tests failed" }
} finally {
    Pop-Location
    foreach ($name in 'level.dat','level.dat.fcx-prev','level.dat.fcx-new','test.exe') {
        $file=Join-Path $dir $name
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
    }
    [IO.Directory]::Delete($dir)
}
