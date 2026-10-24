param(
    [string]$BuildDirectory = "build",
    [switch]$RunTests
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if ([System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $buildDirectory = $BuildDirectory
} else {
    $buildDirectory = Join-Path $repoRoot $BuildDirectory
}

$configureArgs = @(
    "-S", $repoRoot,
    "-B", $buildDirectory,
    "-DBUILD_TESTING=$($RunTests.IsPresent.ToString().ToUpperInvariant())",
    "-DBUILD_RUNTIME=ON",
    "-DBUILD_EDITOR=ON"
)

$vcpkgRoot = $env:VCPKG_ROOT
if (-not $vcpkgRoot -and (Test-Path "C:\vcpkg\scripts\buildsystems\vcpkg.cmake")) {
    $vcpkgRoot = "C:\vcpkg"
}
if ($vcpkgRoot) {
    $toolchain = Join-Path $vcpkgRoot "scripts/buildsystems/vcpkg.cmake"
    if (-not (Test-Path $toolchain)) {
        throw "VCPKG_ROOT does not contain a CMake toolchain: $toolchain"
    }
    $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
    $configureArgs += "-DVCPKG_TARGET_TRIPLET=x64-windows"
}

& cmake @configureArgs
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& cmake --build $buildDirectory --config Release --parallel 2
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if ($RunTests) {
    & ctest --test-dir $buildDirectory --output-on-failure -C Release
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
