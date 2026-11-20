param(
    [Parameter(Mandatory = $true)][ValidateSet("linux", "windows")][string]$Target,
    [Parameter(Mandatory = $true)][string]$RepositoryRoot,
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$ProgressFile,
    [Parameter(Mandatory = $true)][string]$ProjectFile,
    [string]$Workspace,
    [ValidateSet("folder", "single")][string]$Format = "folder",
    [string]$RuntimeTemplate,
    [string]$Packager
)
$ErrorActionPreference = "Stop"
if (-not $Workspace) {
    $key = "$RepositoryRoot|$ProjectFile|$BuildDirectory|$Target"
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $hash = [System.BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($key))).Replace("-", "").Substring(0, 16)
    $Workspace = Join-Path $env:LOCALAPPDATA "cpp-bengine/game-builds/$hash"
}
New-Item -ItemType Directory -Force $Workspace | Out-Null
New-Item -ItemType Directory -Force (Split-Path -Parent $ProgressFile) | Out-Null
function Stop-MissingPrecompiled([string]$Message) {
    [System.IO.File]::WriteAllText($ProgressFile, "0|$Message`n")
    [Console]::Error.WriteLine($Message)
    exit 6
}
if ($Target -eq "linux") {
    if ($env:BENGINE_PORTABLE -eq "1") {
        Stop-MissingPrecompiled "Use the Linux release to export Linux games with its bundled runtime."
    }
    # Linux export requires the Linux packager in the default WSL distribution.
    $wsl = Get-Command "wsl.exe" -ErrorAction Stop
    $kernel = & $wsl.Source --exec uname -r 2>&1
    if ($LASTEXITCODE -ne 0 -or $kernel -notmatch "microsoft-standard-WSL2") { throw "The default distribution must use WSL2." }
    function Convert-ToWslPath([string]$Path) {
        $result = & $wsl.Source --exec wslpath -a $Path 2>&1
        if ($LASTEXITCODE -ne 0) { throw "Could not convert path for WSL: $Path" }
        return ($result | Select-Object -Last 1).Trim()
    }
    $repo = Convert-ToWslPath $RepositoryRoot
    $output = Convert-ToWslPath $BuildDirectory
    $progress = Convert-ToWslPath $ProgressFile
    $project = Convert-ToWslPath $ProjectFile
    $work = Convert-ToWslPath $Workspace
    $template = if ($RuntimeTemplate) { Convert-ToWslPath $RuntimeTemplate } else { "" }
    $linuxEnvironment = @()
    if ($env:BENGINE_WSL_PACKAGER) { $linuxEnvironment += "BENGINE_PACKAGER=$env:BENGINE_WSL_PACKAGER" }
    if ($env:BENGINE_EXPORT_ALLOW_BUILD -eq "1") { $linuxEnvironment += "BENGINE_EXPORT_ALLOW_BUILD=1" }
    if ($linuxEnvironment.Count -gt 0) { $linuxEnvironment = @("env") + $linuxEnvironment }
    & $wsl.Source --exec @linuxEnvironment bash "$repo/scripts/platform_build.sh" linux $repo $output $progress $project $work $Format $template
    exit $LASTEXITCODE
}
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE." }
}
[System.IO.File]::WriteAllText($ProgressFile, "1|Preparing game export...`n")
if (-not $Packager) { $Packager = $env:BENGINE_PACKAGER }
if (-not $Packager) {
    if ($env:BENGINE_EXPORT_ALLOW_BUILD -ne "1" -or $env:BENGINE_PORTABLE -eq "1") {
        Stop-MissingPrecompiled "The precompiled game packager is required. Supply -Packager or set BENGINE_PACKAGER."
    }
    $hostBuild = Join-Path $Workspace "packager"
    $configure = @("-S", "$RepositoryRoot/tools/game_packager", "-B", $hostBuild,
        "-G", "Visual Studio 17 2022", "-A", "x64")
    $vcpkgRoot = $env:VCPKG_ROOT
    if (-not $vcpkgRoot) {
        $vcpkgRoot = Join-Path $env:LOCALAPPDATA "cpp-bengine/toolchains/vcpkg"
        if (-not (Test-Path (Join-Path $vcpkgRoot "vcpkg.exe"))) {
            if (-not (Test-Path (Join-Path $vcpkgRoot ".git"))) {
                Invoke-Checked "git" @("clone", "--depth", "1", "--branch", "2024.11.16",
                    "https://github.com/microsoft/vcpkg.git", $vcpkgRoot)
            }
            Invoke-Checked (Join-Path $vcpkgRoot "bootstrap-vcpkg.bat") @("-disableMetrics")
        }
    }
    Invoke-Checked (Join-Path $vcpkgRoot "vcpkg.exe") @("install", "zlib:x64-windows-static")
    $configure += "-DCMAKE_TOOLCHAIN_FILE=$vcpkgRoot/scripts/buildsystems/vcpkg.cmake"
    $configure += "-DVCPKG_TARGET_TRIPLET=x64-windows-static"
    $configure += '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>'
    Invoke-Checked "cmake" $configure
    Invoke-Checked "cmake" @("--build", $hostBuild, "--config", "Release", "--parallel", "2")
    $Packager = Join-Path $hostBuild "Release/game_packager.exe"
}
$staging = Join-Path $Workspace "package"
$output = Join-Path $BuildDirectory "windows"
$key = (& $Packager template-key $RepositoryRoot windows $Format | Select-Object -Last 1).Trim()
if ($LASTEXITCODE -ne 0) { throw "Could not calculate runtime template compatibility." }
$cache = $env:BENGINE_TEMPLATE_CACHE
if (-not $cache) { $cache = Join-Path $env:LOCALAPPDATA "cpp-bengine/export-templates" }
if ($RuntimeTemplate) {
    $template = $RuntimeTemplate
    Invoke-Checked $Packager @("template-validate", $template, $key, "windows", $Format)
} else {
    $template = $null
    $shippedRoot = if ($env:BENGINE_EXPORT_TEMPLATES) { $env:BENGINE_EXPORT_TEMPLATES } else { "$RepositoryRoot/export_templates" }
    $packagerPath = if (Test-Path -LiteralPath $Packager -PathType Leaf) {
        (Get-Item -LiteralPath $Packager).FullName
    } else {
        (Get-Command -Name $Packager -CommandType Application).Source
    }
    $packagerDirectory = Split-Path -Parent $packagerPath
    foreach ($root in @($shippedRoot, "$packagerDirectory/export_templates", $cache)) {
        $candidate = Join-Path $root "windows/$Format"
        if (Test-Path -LiteralPath "$candidate/$key/template.json") { $candidate = "$candidate/$key" }
        & $Packager template-check $candidate $key windows $Format
        if ($LASTEXITCODE -eq 0) {
            $template = $candidate
            break
        }
    }
    if (-not $template) {
        if ($env:BENGINE_EXPORT_ALLOW_BUILD -ne "1" -or $env:BENGINE_PORTABLE -eq "1") {
            Stop-MissingPrecompiled "No compatible precompiled runtime for windows/$Format. Choose folder export for the host platform or supply a matching template."
        }
        $template = Join-Path $cache "windows/$Format/$key"
        Write-Output "No compatible precompiled template; building it once."
        $runtimeRoot = "$Workspace/runtime-$Format-$key"
        & "$PSScriptRoot/compile_runtime.ps1" -Target windows -RepositoryRoot $RepositoryRoot -BuildDirectory $runtimeRoot -ProgressFile $ProgressFile -PackagingStep -Format $Format
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        $binary = Join-Path $runtimeRoot "platform-builds/windows/apps/runtime_app.exe"
        $currentKey = (& $Packager template-key $RepositoryRoot windows $Format | Select-Object -Last 1).Trim()
        if ($currentKey -ne $key) { throw "Engine sources changed during template build; retry export." }
        Invoke-Checked $Packager @("template-create", $binary, $template, $key, "windows", $Format)
    }
}
Write-Output "Using precompiled runtime template: $template"
Invoke-Checked $Packager @("prepare", $RepositoryRoot, $ProjectFile, $staging, $output)
$binary = Join-Path $template "runtime_app.exe"
[System.IO.File]::WriteAllText($ProgressFile, "96|Packaging game and dependencies...`n")
Invoke-Checked $Packager @("stage", $binary, $staging, "windows")
Invoke-Checked $Packager @("publish", $staging, $output, $Format)
[System.IO.File]::WriteAllText($ProgressFile, "100|Game exported: $output`n")
