param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("linux", "windows")]
    [string]$Target,
    [Parameter(Mandatory = $true)]
    [string]$RepositoryRoot,
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [Parameter(Mandatory = $true)]
    [string]$ProgressFile,
    [switch]$PackagingStep,
    [ValidateSet("folder", "single")][string]$Format = "folder"
)

$ErrorActionPreference = "Stop"
# Windows targets compile on Windows; WSL is used only for Linux targets.
if ($Target -eq "windows") {
    function Write-ProgressFile([int]$Percent, [string]$Status) {
        $temporary = "$ProgressFile.tmp"
        [System.IO.File]::WriteAllText($temporary, "$Percent|$Status`n")
        Move-Item -Force $temporary $ProgressFile
    }
    function Invoke-Checked([string]$Program, [string[]]$Arguments) {
        & $Program @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$Program failed with exit code $LASTEXITCODE."
        }
    }
    New-Item -ItemType Directory -Force $BuildDirectory | Out-Null
    $vcpkgRoot = $env:VCPKG_ROOT
    if (-not $vcpkgRoot) {
        $vcpkgRoot = Join-Path $env:LOCALAPPDATA "cpp-bengine/toolchains/vcpkg-2026.10.16"
        if (-not (Test-Path (Join-Path $vcpkgRoot "vcpkg.exe"))) {
            Write-ProgressFile 5 "Preparing vcpkg for native Windows..."
            if (-not (Test-Path (Join-Path $vcpkgRoot ".git"))) {
                Invoke-Checked "git" @("clone", "--depth", "1", "--branch", "2026.10.16",
                    "https://github.com/microsoft/vcpkg.git", $vcpkgRoot)
            }
            Invoke-Checked (Join-Path $vcpkgRoot "bootstrap-vcpkg.bat") @("-disableMetrics")
        }
    }
    $vcpkg = Join-Path $vcpkgRoot "vcpkg.exe"
    if (-not (Test-Path $vcpkg)) {
        throw "VCPKG_ROOT must contain vcpkg.exe: $vcpkgRoot"
    }
    $triplet = if ($Format -eq "single") { "x64-windows-static" } else { "x64-windows" }
    Write-ProgressFile 15 "Preparing native Windows dependencies..."
    Invoke-Checked $vcpkg @("install", "--overlay-ports=$RepositoryRoot/cmake/vcpkg-ports", "assimp:$triplet", "bullet3:$triplet",
        "freetype:$triplet", "glfw3:$triplet", "glm:$triplet", "lua:$triplet")
    $targetBuild = Join-Path $BuildDirectory "platform-builds/windows"
    Write-ProgressFile 60 "Configuring native Windows runtime..."
    Invoke-Checked "cmake" @("-S", $RepositoryRoot, "-B", $targetBuild,
        "-G", "Visual Studio 17 2022", "-A", "x64",
        "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot/scripts/buildsystems/vcpkg.cmake",
        "-DVCPKG_TARGET_TRIPLET=$triplet", "-DCOMMONS_USE_RELATIVE_ASSET_PATHS=ON",
        "-DBUILD_TESTING=OFF", "-DBUILD_RUNTIME=ON", "-DBUILD_EDITOR=OFF")
    Write-ProgressFile 80 "Compiling native Windows runtime..."
    Invoke-Checked "cmake" @("--build", $targetBuild, "--target", "runtime_app",
        "--config", "Release", "--parallel", "2")
    # vcpkg's app-local deployment copies DLLs beside the executable.
    # Keep the same public output folder as Linux/cross builds.
    $apps = Join-Path $targetBuild "apps"
    Copy-Item -Force (Join-Path $apps "Release/*") $apps
    $assets = Join-Path $apps "assets"
    New-Item -ItemType Directory -Force $assets | Out-Null
    Copy-Item -Recurse -Force (Join-Path $RepositoryRoot "commons/assets/*") $assets
    Write-ProgressFile $(if ($PackagingStep) { 95 } else { 100 }) "Runtime compiled: $apps/runtime_app.exe"
    return
}

$wsl = Get-Command "wsl.exe" -ErrorAction SilentlyContinue
if (-not $wsl) {
    throw "WSL2 is required to build Linux targets from Windows. Install WSL2 and a Linux distribution first."
}
$kernelVersion = & $wsl.Source --exec uname -r 2>&1
if ($LASTEXITCODE -ne 0 -or $kernelVersion -notmatch "microsoft-standard-WSL2") {
    throw "The default WSL distribution must run on WSL2. Install or upgrade it with 'wsl --set-version <Distro> 2'."
}

function Convert-ToWslPath([string]$Path) {
    $resolved = & $wsl.Source --exec wslpath -a $Path 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Could not resolve '$Path' in the default WSL2 distribution: $resolved"
    }
    return ($resolved | Select-Object -Last 1).Trim()
}

$repoPath = Convert-ToWslPath $RepositoryRoot
$buildPath = Convert-ToWslPath $BuildDirectory
$progressPath = Convert-ToWslPath $ProgressFile
$scriptPath = "$repoPath/scripts/compile_runtime.sh"

& $wsl.Source --exec bash $scriptPath $Target $repoPath $buildPath $progressPath
exit $LASTEXITCODE
