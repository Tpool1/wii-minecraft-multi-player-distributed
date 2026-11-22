param([string]$Compiler = 'clang', [string]$IncludeDir)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/../..").Path
if(!$IncludeDir) { $IncludeDir = "$repo/build/claw/staging/source" }
$IncludeDir = (Resolve-Path -LiteralPath $IncludeDir).Path
$temp = New-Item -ItemType Directory -Path (Join-Path ([IO.Path]::GetTempPath()) ("fcavex-region-" + [guid]::NewGuid()))
Push-Location -LiteralPath $temp
try {
    [void](New-Item -ItemType Directory ./region)
    & $Compiler -std=gnu99 -DPLATFORM_PC -DCGLM_ALL_UNALIGNED "-I$IncludeDir" "-I$IncludeDir/network" "$repo/tests/region/region_test.c" -o ./region_test.exe
    if($LASTEXITCODE) { throw 'Region test did not compile' }
    & ./region_test.exe
    if($LASTEXITCODE) { throw 'Region test failed' }
} finally {
    Pop-Location
    if($temp.Parent.FullName -eq ([IO.Path]::GetTempPath().TrimEnd([IO.Path]::DirectorySeparatorChar))) {
        Remove-Item -LiteralPath $temp.FullName -Recurse
    }
}
