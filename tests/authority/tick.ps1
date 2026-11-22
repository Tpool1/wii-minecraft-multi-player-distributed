param([string]$Compiler = 'clang', [string]$IncludeDir, [string]$OutputDirectory)
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
