param(
    [string]$OutDir = "",
    [string]$ObjDir = "",
    [string]$JavaHome = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $OutDir) { $OutDir = Join-Path $Root "build\native" }
if (-not $ObjDir) { $ObjDir = Join-Path $Root "build\obj" }

$MsysRoot = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { "C:\msys64" }
$MsysBin = Join-Path $MsysRoot "ucrt64\bin"
$Gcc = Join-Path $MsysBin "gcc.exe"
$Gxx = Join-Path $MsysBin "g++.exe"

if (-not (Test-Path $Gcc) -or -not (Test-Path $Gxx)) {
    throw "MSYS2 gcc/g++ not found under $MsysBin"
}

if (-not $JavaHome) { $JavaHome = $env:JAVA_HOME }
if (-not $JavaHome -or -not (Test-Path (Join-Path $JavaHome "include\jni.h"))) {
    $fallback = "C:\Program Files\Java\jdk-17"
    if (Test-Path (Join-Path $fallback "include\jni.h")) {
        $JavaHome = $fallback
    } else {
        throw "jni.h not found. Set JAVA_HOME to a JDK."
    }
}

New-Item -ItemType Directory -Force -Path $OutDir, $ObjDir | Out-Null
$env:PATH = "$MsysBin;$(Join-Path $MsysRoot 'usr\bin');$env:PATH"

$Includes = @(
    "-I$Root",
    "-I$(Join-Path $Root 'cubiomes')",
    "-I$(Join-Path $Root 'jni')",
    "-I$(Join-Path $JavaHome 'include')",
    "-I$(Join-Path $JavaHome 'include\win32')"
)

$CSources = @(
    "cubiomes\finders.c",
    "cubiomes\generator.c",
    "cubiomes\layers.c",
    "cubiomes\biomenoise.c",
    "cubiomes\biomes.c",
    "cubiomes\noise.c",
    "cubiomes\util.c",
    "cubiomes\quadbase.c"
)

# river_finder.cpp is #included by the JNI bridge translation unit
$CppSources = @(
    "jni\sunnyslopes_riverfinder_RiverFinderBridge.cpp"
)

$Objs = @()
foreach ($src in $CSources) {
    $name = [IO.Path]::GetFileNameWithoutExtension($src)
    $obj = Join-Path $ObjDir "$name.o"
    Write-Host "CC  $src"
    & $Gcc -O2 -DNDEBUG -D_WIN32 -std=c11 @Includes -c (Join-Path $Root $src) -o $obj
    if ($LASTEXITCODE -ne 0) { throw "gcc failed on $src" }
    $Objs += $obj
}

foreach ($src in $CppSources) {
    $name = [IO.Path]::GetFileNameWithoutExtension($src)
    $obj = Join-Path $ObjDir "$name.o"
    Write-Host "CXX $src"
    & $Gxx -O2 -DNDEBUG -D_WIN32 -std=c++20 @Includes -c (Join-Path $Root $src) -o $obj
    if ($LASTEXITCODE -ne 0) { throw "g++ failed on $src" }
    $Objs += $obj
}

$Dll = Join-Path $OutDir "libriverFinderLibJ.dll"
Write-Host "LINK $Dll"
& $Gxx -shared -o $Dll @Objs -static-libgcc -static-libstdc++ "-Wl,--kill-at"
if ($LASTEXITCODE -ne 0) { throw "link failed" }
Write-Host "Built $Dll"
Get-Item $Dll | Format-List FullName, Length, LastWriteTime
