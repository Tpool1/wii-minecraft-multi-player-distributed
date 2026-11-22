[CmdletBinding()]
param([string]$Compiler = 'clang', [string]$DependencySource = '')
$ErrorActionPreference='Stop'
$repo = (Resolve-Path "$PSScriptRoot/../..").Path
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('fcavex-interest-' + [guid]::NewGuid())
[void][IO.Directory]::CreateDirectory($scratch)
$src = Get-Content -LiteralPath "$repo/source/network/server_world.c" -Raw
$fn = [regex]::Match($src,'(?ms)^bool server_world_furthest_chunk\(.*?^}')
if (!$fn.Success) { throw 'Production eviction function was not found' }
[IO.File]::WriteAllText("$scratch/eviction-body.inc",$fn.Value)
& $Compiler -std=c99 -O1 -Wall -Wextra -Werror "-I$scratch" "$PSScriptRoot/interest.c" -o "$scratch/interest.exe"
if ($LASTEXITCODE -ne 0) { throw "Compile failed $LASTEXITCODE" }
& "$scratch/interest.exe"
$code = $LASTEXITCODE
Write-Output "Authority interest behavioral result = $code (0 pass; 21 peer-owned column evicted; 22 needed column evicted; 23 offline regression)"
if ($code -ne 0) { exit $code }
& $Compiler -std=c99 -O1 -Wall -Wextra -Werror "$PSScriptRoot/peer-record.c" -o "$scratch/peer-record.exe"
if ($LASTEXITCODE -ne 0) { throw "Checkpoint test compile failed" }
Push-Location $scratch
try {
    & "$scratch/peer-record.exe"
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}
Write-Output "Peer persistence behavioral result = $code (0 pass; 31-38 failed save/recovery assertion)"
if ($code -ne 0) { exit $code }
if (!$DependencySource) {
    $DependencySource = (Join-Path $repo 'build/claw/staging/source')
    if (!(Test-Path -LiteralPath "$DependencySource/m-lib/m-string.h")) { $DependencySource=Join-Path $repo 'source' }
}
if (!(Test-Path -LiteralPath "$DependencySource/m-lib/m-string.h")) {
    throw 'Specify -DependencySource with prepared m-lib, cglm and cNBT headers or run tools/windows/Build-Wii.ps1 once.'
}
$level=Get-Content -LiteralPath "$repo/source/network/level_archive.c" -Raw
$writer=[regex]::Match($level,'(?ms)^bool level_archive_write_inventory\(.*?^}')
if(!$writer.Success) { throw 'Inventory writer function was not found' }
[IO.File]::WriteAllText("$scratch/inventory-write.inc",$writer.Value)
& $Compiler -std=gnu99 -O1 -DPLATFORM_PC -DCGLM_ALL_UNALIGNED "-I$DependencySource" "-I$DependencySource/network" "-I$scratch" "$PSScriptRoot/inventory-save.c" -o "$scratch/inventory-save.exe"
if($LASTEXITCODE) { throw 'Inventory allocation test did not compile' }
& "$scratch/inventory-save.exe"
if($LASTEXITCODE) {exit $LASTEXITCODE}
& $Compiler -std=gnu99 -O2 -DPLATFORM_PC -DCGLM_ALL_UNALIGNED "-I$DependencySource" "-I$DependencySource/network" `
    "$PSScriptRoot/gameplay.c" "$repo/source/network/world_txn.c" "$repo/source/item/inventory.c" -o "$scratch/gameplay.exe"
if ($LASTEXITCODE -ne 0) { throw 'Gameplay authority test compile failed' }
& "$scratch/gameplay.exe"
$code=$LASTEXITCODE
Write-Output "Two-player gameplay authority behavioral result = $code"
if ($code -ne 0) { exit $code }
& $Compiler -std=c99 -Wall -Wextra -Werror "$PSScriptRoot/world-txn.c" "$repo/source/network/world_txn.c" -o "$scratch/world-txn.exe"
if ($LASTEXITCODE -ne 0) { throw 'World journal test compile failed' }
Push-Location $scratch
try {
    & "$scratch/world-txn.exe"
    $code = $LASTEXITCODE
} finally { Pop-Location }
Write-Output "Interrupted world checkpoint recovery behavioral result = $code"
if ($code -ne 0) { exit $code }
