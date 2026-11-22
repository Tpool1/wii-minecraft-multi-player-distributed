param([string]$Compiler = 'clang', [string]$IncludeDir)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/../..").Path
if(!$IncludeDir) { $IncludeDir = "$repo/build/claw/staging/source" }
$IncludeDir = (Resolve-Path -LiteralPath $IncludeDir).Path
$temp = New-Item -ItemType Directory -Path (Join-Path ([IO.Path]::GetTempPath()) ("fcavex-region-" + [guid]::NewGuid()))
Push-Location -LiteralPath $temp
try {
    [void](New-Item -ItemType Directory ./region)
    $z = (Get-ChildItem (Split-Path $IncludeDir) -Directory -Filter 'zlib-*' | Select-Object -First 1).FullName
    if(!$z) { throw 'Missing staged zlib sources for real-codec regression.' }
    $codec = @(Get-ChildItem "$IncludeDir/cNBT" -File -Filter '*.c' | ForEach-Object FullName)
    $zlib = @('adler32.c','compress.c','crc32.c','deflate.c','inffast.c','inflate.c','inftrees.c','trees.c','zutil.c') | ForEach-Object { Join-Path $z $_ }
    [void](New-Item -ItemType Directory -Path ./metadata/region -Force)
    foreach($test in @('region_test','metadata_test')) {
        & $Compiler -std=gnu99 -DPLATFORM_PC -DCGLM_ALL_UNALIGNED "-I$IncludeDir" "-I$IncludeDir/network" "-I$z" "$repo/tests/region/$test.c" @codec @zlib -o "./$test.exe"
        if($LASTEXITCODE) { throw "$test did not compile" }
        & "./$test.exe"
        if($LASTEXITCODE) { throw "$test failed" }
    }
} finally {
    Pop-Location
    if($temp.Parent.FullName -eq ([IO.Path]::GetTempPath().TrimEnd([IO.Path]::DirectorySeparatorChar))) {
        Remove-Item -LiteralPath $temp.FullName -Recurse
    }
}
