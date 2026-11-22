#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SdkRoot,
    [Parameter(Mandatory)][string]$HostCompiler,
    [string]$ProjectRoot = (Join-Path $PSScriptRoot '../..'),
    [string]$DependencyCache
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath $ProjectRoot).Path
$sdk = (Resolve-Path -LiteralPath $SdkRoot).Path
$hostCc = (Get-Command $HostCompiler -ErrorAction Stop).Source
$output = Join-Path $project 'build/claw'
if (!$DependencyCache) { $DependencyCache = Join-Path $output 'downloads' }
$cache = (New-Item -ItemType Directory -Path $DependencyCache -Force).FullName
$stage = Join-Path $output 'staging'
$ppcBin = Join-Path $sdk 'devkitPPC/bin'
$cc = Join-Path $ppcBin 'powerpc-eabi-gcc.exe'
$ogc = Join-Path $sdk 'libogc'
$oldPath = $env:Path

# Materialize publisher archives by commit and check their bytes before extraction.
$deps = @(
    'lvandeve/lodepng ed3c5f14b136c936d615ee3b38aaa7e7e642f12c F9CFC7C35E3B69FF2BC65FCFD71F3FB6ECADAFF4A89C599A622046E5DED4D743',
    'recp/cglm be0defb7ac379432a2864e7e2f668b3d23ab723b 4E93AFE864EFF03D9795A12C5E5365BE2F75B7489510852503E3A2A0B5883CA1',
    'chmod222/cNBT 3ff982eff62b01f7a67079a365b8fbf8a4b47423 91A83C01751D96FBF529538B6FB887910256A7113CAFD964F0A9A8284E9AA88D',
    'kgabis/parson ba29f4eda9ea7703a9f6a9cf2b0532a2605723c3 D311CD3D051965485BFF1C89A3980DF7AD0EE3F9C0F33C18964D8C1CC32EF2CF',
    'P-p-H-d/mlib 43fdd10e772b084f1bb2d0065170c7da12c1da44 7BC7FBD7A6BC7BBE6EF50E50BCE0A44C39DF9CEC33728EACF8CFF3975EAE096F',
    'devkitPro/libfat 2f6465447fa0dc7531747ac44b0f9bbd876c13c4 50AEFD69710D889A01F8B896BB18A04D80FCAE45605775BCEC821ACEC447C760',
    'madler/zlib 51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf D9E270D46252734AA49770FBC544125391617956266F220BD63216C834F3A522',
    'devkitPro/gamecube-tools 609dc7884eae148ee725c93b3d78d69902f69a0c 1D91EAFA8EF866C29B4F6B56FFF172CB317ECE6C2608B8BA58AA64943EDC78BF'
)
$vendor = @{}
try {
    $env:Path = "$ppcBin;$oldPath"
    if (Test-Path -LiteralPath $stage) {
        $existing = (Resolve-Path -LiteralPath $stage).Path
        if ($existing -ne [IO.Path]::GetFullPath((Join-Path $project 'build/claw/staging')) -or
            -not (Test-Path -LiteralPath (Join-Path $existing '.fcavex-build-staging'))) {
            throw "Refusing to replace an unrecognized staging directory: $existing"
        }
        Remove-Item -LiteralPath $existing -Recurse -Force
    }
    New-Item -ItemType Directory -Path $stage -Force | Out-Null
    New-Item -ItemType File -Path (Join-Path $stage '.fcavex-build-staging') | Out-Null
    foreach ($record in $deps) {
        $repo, $commit, $digest = $record.Split(' ')
        $name = $repo.Split('/')[1]
        $archive = Join-Path $cache "$name-$commit.tar.gz"
        if (!(Test-Path -LiteralPath $archive)) {
            Invoke-WebRequest "https://codeload.github.com/$repo/tar.gz/$commit" -OutFile "$archive.download"
            Move-Item -LiteralPath "$archive.download" -Destination $archive
        }
        if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $digest) {
            throw "Digest mismatch for $archive"
        }
        tar.exe -xf $archive -C $stage
        if ($LASTEXITCODE) { throw "Extraction failed: $repo" }
        $vendor[$name] = Join-Path $stage "$name-$commit"
    }
    $src = Join-Path $stage 'source'
    Copy-Item -LiteralPath (Join-Path $project 'source') -Destination $src -Recurse
    Copy-Item -LiteralPath (Join-Path $vendor.cglm 'include/cglm') -Destination $src -Recurse -Force
    foreach ($name in 'cNBT','lodepng','parson','m-lib') {
        New-Item -ItemType Directory -Path (Join-Path $src $name) -Force | Out-Null
    }
    Copy-Item -Path (Join-Path $vendor.mlib '*.h') -Destination "$src/m-lib"
    foreach ($name in 'buffer.c','buffer.h','list.h','nbt_loading.c','nbt_parsing.c','nbt_treeops.c','nbt_util.c','nbt.h') {
        Copy-Item -LiteralPath (Join-Path $vendor.cNBT $name) -Destination "$src/cNBT"
    }
    Copy-Item -LiteralPath (Join-Path $vendor.lodepng 'lodepng.cpp') -Destination "$src/lodepng/lodepng.c"
    Copy-Item -LiteralPath (Join-Path $vendor.lodepng 'lodepng.h') -Destination "$src/lodepng"
    Copy-Item -Path (Join-Path $vendor.parson 'parson.[ch]') -Destination "$src/parson"
    $fatInclude = Join-Path $vendor.libfat 'include'
    [IO.File]::WriteAllText("$fatInclude/libfatversion.h", "#define _LIBFAT_MAJOR_ 1\n#define _LIBFAT_MINOR_ 1\n#define _LIBFAT_PATCH_ 3\n#define _LIBFAT_STRING `"libFAT Release 1.1.3`"\n".Replace('\n',[Environment]::NewLine))
    # The compact newlib in SDK r35 supplies this table as _ctype_.
    [IO.File]::WriteAllText("$stage/newlib-ctype.c", "#include <ctype.h>\n#undef __locale_ctype_ptr\nconst char *__locale_ctype_ptr(void) { return _ctype_; }\n".Replace('\n',[Environment]::NewLine))
    $sources = foreach ($dir in '.', 'block', 'entity', 'graphics', 'network', 'game', 'game/gui', 'platform', 'item', 'item/items', 'cNBT', 'lodepng', 'parson') {
        Get-ChildItem -LiteralPath (Join-Path $src $dir) -Filter '*.c' -File | Sort-Object Name | ForEach-Object FullName
    }
    $sources += Get-ChildItem -LiteralPath (Join-Path $vendor.libfat 'source') -Filter '*.c' -File | Sort-Object Name | ForEach-Object FullName
    foreach ($file in 'adler32','compress','crc32','deflate','infback','inffast','inflate','inftrees','trees','uncompr','zutil') {
        $sources += Join-Path $vendor.zlib "$file.c"
    }
    $sources += "$stage/newlib-ctype.c"
    # GCC 8 records both spellings of the staged path in __FILE__ on Windows.
    $compileFlags = @('-std=gnu99','-O2','-DNDEBUG','-DPLATFORM_WII','-DGEKKO','-mrvl','-mcpu=750','-meabi','-mhard-float',
        '-ffunction-sections','-fdata-sections',"-fmacro-prefix-map=$stage=.","-fmacro-prefix-map=$($stage.ToLowerInvariant())=.","-I$src","-I$ogc/include","-I$fatInclude","-I$($vendor.zlib)")
    $units = for ($i = 0; $i -lt $sources.Count; $i++) {
        [pscustomobject]@{ Source = $sources[$i]; Object = (Join-Path $stage "$i.o") }
    }
    & $cc --version | Select-Object -First 1
    Write-Host "Compiling $($sources.Count) C files"
    $failures = $units | ForEach-Object -Parallel {
        $compiler = $using:cc
        $flags = $using:compileFlags
        $unit = $_
        & $compiler @flags -c $unit.Source -o $unit.Object 2>&1 | ForEach-Object { Write-Host $_ }
        if ($LASTEXITCODE) { $unit.Source }
    } -ThrottleLimit 4
    if ($failures) { throw "Compilation failed: $($failures -join ', ')" }
    $linkArgs = @('-mrvl','-mcpu=750','-meabi','-mhard-float') + @($units.Object) +
        @("-L$ogc/lib/wii",'-lwiiuse','-lbte','-logc','-lm','-Wl,--gc-sections',"-Wl,-Map,$stage/boot.map",'-o',"$stage/boot.elf")
    $response = Join-Path $stage 'wii-link.rsp'
    $linkArgs | ForEach-Object { '"' + $_.Replace('\','/') + '"' } | Set-Content -LiteralPath $response -Encoding utf8NoBOM
    & $cc "@$response"
    if ($LASTEXITCODE) { throw "Wii link failed ($LASTEXITCODE)." }
    $converter = Join-Path $stage 'elf2dol.exe'
    & $hostCc '-DBYTE_ORDER=1234' '-DBIG_ENDIAN=4321' (Join-Path $vendor.'gamecube-tools' 'elftool/elf2dol.c') -O2 -o $converter
    if ($LASTEXITCODE) { throw "Official ELF-to-DOL converter failed to build." }
    & $converter -v "$stage/boot.elf" "$stage/boot.dol"
    if ($LASTEXITCODE) { throw "DOL conversion failed." }
    foreach ($name in 'boot.map','boot.elf','boot.dol') {
        Copy-Item -LiteralPath (Join-Path $stage $name) -Destination (Join-Path $output $name) -Force
    }
    Get-FileHash -LiteralPath "$output/boot.elf","$output/boot.dol" -Algorithm SHA256 | Format-List Hash,Path
} finally {
    $env:Path = $oldPath
}
