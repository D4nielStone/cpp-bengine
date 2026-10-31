param(
    [Parameter(Mandatory = $true)][ValidateSet("linux", "windows")][string]$Target,
    [Parameter(Mandatory = $true)][string]$RepositoryRoot,
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$ProgressFile,
    [Parameter(Mandatory = $true)][string]$ProjectFile,
    [string]$Workspace
)
$ErrorActionPreference = "Stop"
if (-not $Workspace) {
    $key = "$RepositoryRoot|$ProjectFile|$BuildDirectory|$Target"
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $hash = [System.BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($key))).Replace("-", "")
    $Workspace = Join-Path $env:LOCALAPPDATA "cpp-bengine/game-builds/$hash"
}
New-Item -ItemType Directory -Force $Workspace | Out-Null
New-Item -ItemType Directory -Force (Split-Path -Parent $ProgressFile) | Out-Null
if ($Target -eq "linux") {
    # Keep all compilation and packaging in the default Linux distribution.
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
    & $wsl.Source --exec bash "$repo/scripts/platform_build.sh" linux $repo $output $progress $project $work
    exit $LASTEXITCODE
}
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE." }
}
[System.IO.File]::WriteAllText($ProgressFile, "1|Preparing game export...`n")
$hostBuild = Join-Path $Workspace "packager"
Invoke-Checked "cmake" @("-S", "$RepositoryRoot/tools/game_packager", "-B", $hostBuild,
    "-G", "Visual Studio 17 2022", "-A", "x64")
Invoke-Checked "cmake" @("--build", $hostBuild, "--config", "Release", "--parallel", "2")
$packager = Join-Path $hostBuild "Release/game_packager.exe"
$staging = Join-Path $Workspace "package"
$output = Join-Path $BuildDirectory "windows"
Invoke-Checked $packager @("prepare", $RepositoryRoot, $ProjectFile, $staging, $output)
& "$PSScriptRoot/compile_runtime.ps1" -Target windows -RepositoryRoot $RepositoryRoot -BuildDirectory "$Workspace/runtime" -ProgressFile $ProgressFile -PackagingStep
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$binary = Join-Path $Workspace "runtime/platform-builds/windows/apps/runtime_app.exe"
[System.IO.File]::WriteAllText($ProgressFile, "96|Packaging game and dependencies...`n")
Invoke-Checked $packager @("stage", $binary, $staging, "windows")
Invoke-Checked $packager @("publish", $staging, $output)
[System.IO.File]::WriteAllText($ProgressFile, "100|Game exported: $output`n")
