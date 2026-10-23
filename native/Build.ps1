$ErrorActionPreference = 'Stop'
$sourceDirectory = $PSScriptRoot
$dependencyDirectory = Join-Path $sourceDirectory '.deps'
$buildDirectory = Join-Path $sourceDirectory 'build'
New-Item -ItemType Directory -Force -Path $dependencyDirectory,$buildDirectory | Out-Null
foreach ($command in 'cmake','ninja') {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) {
        throw "Install $command and add it to PATH before building."
    }
}
function Get-VerifiedFile([string]$Url,[string]$Path,[string]$ExpectedHash) {
    if (-not (Test-Path -LiteralPath $Path)) { Invoke-WebRequest -Uri $Url -OutFile $Path }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLower() -ne $ExpectedHash) {
        throw "Checksum mismatch: $Path"
    }
}
$zigDirectory = Join-Path $dependencyDirectory 'zig-x86_64-windows-0.14.1'
$zigArchive = Join-Path $dependencyDirectory 'zig.zip'
if (-not (Test-Path -LiteralPath "$zigDirectory/zig.exe")) {
    Get-VerifiedFile 'https://ziglang.org/download/0.14.1/zig-x86_64-windows-0.14.1.zip' $zigArchive '554f5378228923ffd558eac35e21af020c73789d87afeabf4bfd16f2e6feed2c'
    Expand-Archive -LiteralPath $zigArchive -DestinationPath $dependencyDirectory -Force
}
$opusDirectory = Join-Path $dependencyDirectory 'opus-1.6.1'
if (-not (Test-Path -LiteralPath "$opusDirectory/CMakeLists.txt")) {
    $opusArchive = Join-Path $dependencyDirectory 'opus.tar.gz'
    Get-VerifiedFile 'https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz' $opusArchive '6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1'
    tar -xf $opusArchive -C $dependencyDirectory
    if ($LASTEXITCODE) { throw 'Opus extraction failed' }
}
$speexDirectory = Join-Path $dependencyDirectory 'speexdsp-1.2.1'
if (-not (Test-Path -LiteralPath "$speexDirectory/libspeexdsp/preprocess.c")) {
    $speexArchive = Join-Path $dependencyDirectory 'speexdsp.tar.gz'
    Get-VerifiedFile 'https://downloads.xiph.org/releases/speex/speexdsp-1.2.1.tar.gz' $speexArchive '8c777343e4a6399569c72abc38a95b24db56882c83dbdb6c6424a5f4aeb54d3d'
    tar -xf $speexArchive -C $dependencyDirectory
    if ($LASTEXITCODE) { throw 'SpeexDSP extraction failed' }
}
$zig = (Join-Path $zigDirectory 'zig.exe').Replace('\','/')
$ar = Join-Path $dependencyDirectory 'zig-ar.cmd'
$ranlib = Join-Path $dependencyDirectory 'zig-ranlib.cmd'
@('@echo off',('"'+$zig+'" ar %*')) | Set-Content -LiteralPath $ar -Encoding ascii
@('@echo off',('"'+$zig+'" ranlib %*')) | Set-Content -LiteralPath $ranlib -Encoding ascii
$opusBuild = Join-Path $buildDirectory 'opus'
cmake -S $opusDirectory -B $opusBuild -G Ninja "-DCMAKE_C_COMPILER=$zig" '-DCMAKE_C_COMPILER_ARG1=cc' "-DCMAKE_AR:FILEPATH=$ar" "-DCMAKE_RANLIB:FILEPATH=$ranlib" '-DCMAKE_C_FLAGS=-target x86_64-windows-gnu' '-DCMAKE_BUILD_TYPE=Release' '-DOPUS_DISABLE_INTRINSICS=ON' '-DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF' '-DOPUS_INSTALL_CMAKE_CONFIG_MODULE=OFF'
if ($LASTEXITCODE) { throw 'CMake configuration failed' }
cmake --build $opusBuild -j 4
if ($LASTEXITCODE) { throw 'Opus build failed' }
$speexBuild = Join-Path $buildDirectory 'speex'
& "$sourceDirectory/Build-DSP.ps1" -Zig $zig -Source $speexDirectory -Output $speexBuild
Push-Location $sourceDirectory
try {
    & $zig rc /fo "$buildDirectory/app.res" app.rc
    if ($LASTEXITCODE) { throw 'Resource build failed' }
    & $zig c++ -target x86_64-windows-gnu -std=c++17 -O2 -DNDEBUG -DUNICODE -D_UNICODE -I "$opusDirectory/include" -I "$speexDirectory/include" main.cpp "$buildDirectory/app.res" "$opusBuild/libopus.a" "$speexBuild/libspeexdsp.a" -o "$buildDirectory/BES.exe" '-Wl,--subsystem,windows' -municode -lwinhttp -lbcrypt -lcrypt32 -lws2_32 -lwinmm -lavrt -lshell32 -lgdi32 -luser32 -lcomctl32 -lpsapi -static -s
    if ($LASTEXITCODE) { throw 'BES build failed' }
} finally { Pop-Location }
Copy-Item -LiteralPath "$sourceDirectory/../LICENSE.txt","$sourceDirectory/../THIRD-PARTY.txt" -Destination $buildDirectory -Force
Copy-Item -LiteralPath "$sourceDirectory/../licenses" -Destination $buildDirectory -Recurse -Force
Write-Output "Built: $buildDirectory/BES.exe"
