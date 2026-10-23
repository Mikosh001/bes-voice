param([string]$Zig,[string]$Source,[string]$Output)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$objects = @()
foreach ($unit in 'preprocess','filterbank','fftwrap','smallft','mdf') {
    $object = Join-Path $Output "$unit.o"
    & $Zig cc -target x86_64-windows-gnu -O2 -DFLOATING_POINT -DUSE_SMALLFT '-DEXPORT=' -I "$Source/include" -I "$Source/libspeexdsp" -c "$Source/libspeexdsp/$unit.c" -o $object
    if ($LASTEXITCODE) { throw "SpeexDSP compilation failed: $unit" }
    $objects += $object
}
& $Zig ar rcs "$Output/libspeexdsp.a" @objects
if ($LASTEXITCODE) { throw 'SpeexDSP archive failed' }
