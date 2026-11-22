param([string]$Compiler = 'clang', [string]$IncludeDir, [string]$OutputDirectory, [switch]$InertOnly)
$ErrorActionPreference='Stop'
$repo = (Resolve-Path "$PSScriptRoot/../..").Path
if(!$IncludeDir) { $IncludeDir = "$repo/build/claw/staging/source" }
if(!$OutputDirectory) { $OutputDirectory = "$repo/build/tests/tick" }
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$OutputDirectory = (Resolve-Path $OutputDirectory).Path
function Body([string]$unit, [string]$start) {
    $raw = Get-Content -LiteralPath "$repo/source/$unit" -Raw
    $m = [regex]::Match($raw,"(?ms)^$start.*?^}")
    if(!$m.Success) { throw "Missing production definition $start in $unit" }
    return $m.Value
}
$world = 'network/server_world.c'
$idx = [regex]::Match((Get-Content "$repo/source/$world" -Raw), '(?m)^#define S_CHUNK_IDX[^\n]*\n[^\n]*').Value
if(!$idx) { throw 'Missing production chunk layout macro' }

# Use the real loader, setter and regular/random passes. Copy the ID gate into
# the fixture with a counter; keep this instrument out of the timing fixture
# and the shipped code. Fail loudly if the storage expression changes.
$scan = Body $world 'void server_world_tick\('
$gate = 'sc->ids[column + y]'
if(!$scan.Contains($gate)) { throw 'Update the raw-ID read counter for the current world scan' }
$scan = $scan.Replace($gate, '(++id_reads, sc->ids[column + y])')
$inertBody = $idx, (Body $world 'static bool server_chunk_get_block\('),
    (Body $world 'bool server_world_get_block\('), (Body 'block/blocks.c' 'void blocks_side_offset\('),
    (Body $world 'static bool server_world_light_get_block\('), (Body $world 'static void server_world_light_set_light\('),
    (Body $world 'bool server_world_set_block\('), (Body $world 'bool server_world_is_chunk_loaded\('),
    (Body $world 'bool server_world_load_chunk\('), $scan, (Body $world 'void server_world_random_tick\('),
    (Body 'util.c' 'inline uint8_t nibble_read\(').Replace('inline ',''),
    (Body 'util.c' 'inline void nibble_write\(').Replace('inline ','')
[IO.File]::WriteAllLines("$OutputDirectory/inert-body.inc",[string[]]$inertBody)
$inertExe = "$OutputDirectory/inert-tick.exe"
& $Compiler -std=gnu99 -O2 -DPLATFORM_PC -DCGLM_ALL_UNALIGNED "-I$repo/source" "-I$IncludeDir" "-I$IncludeDir/cglm" "-I$OutputDirectory" "$PSScriptRoot/inert-tick.c" -o $inertExe
if($LASTEXITCODE) { throw 'inert tick fixture compile failed' }
& $inertExe 2>&1 | Tee-Object -FilePath "$OutputDirectory/inert-tick.log"
if($LASTEXITCODE) { throw "inert tick fixture failed (exit $LASTEXITCODE)" }
if($InertOnly) { return }

$body = $idx, (Body $world 'static bool server_chunk_get_block\('), (Body $world 'bool server_world_get_block\('),
    (Body 'block/blocks.c' 'void blocks_side_offset\('), (Body $world 'void server_world_tick\(')
[IO.File]::WriteAllLines("$OutputDirectory/tick-body.inc",[string[]]$body)
# Count all metadata/lighting reads in a separately compiled translation unit.
$read = (Body 'util.c' 'inline uint8_t nibble_read\(').Replace('inline ','').Replace('{',"{`n    ++nibble_reads;")
[IO.File]::WriteAllText("$OutputDirectory/nibble.c",("#include <stdint.h>`n#include <stddef.h>`nuint64_t nibble_reads;`n" + $read))
$exe = "$OutputDirectory/tick.exe"
& $Compiler -std=gnu99 -O2 -DPLATFORM_PC -DCGLM_ALL_UNALIGNED "-I$IncludeDir" "-I$IncludeDir/cglm" "-I$OutputDirectory" "$PSScriptRoot/tick.c" "$OutputDirectory/nibble.c" -o $exe
if($LASTEXITCODE) { throw 'tick fixture compile failed' }
& $exe 2>&1 | Tee-Object -FilePath "$OutputDirectory/tick.log"
if($LASTEXITCODE) { throw 'tick fixture failed' }
