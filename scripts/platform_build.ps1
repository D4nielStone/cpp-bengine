param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("linux", "windows")]
    [string]$Target,
    [Parameter(Mandatory = $true)]
    [string]$RepositoryRoot,
    [Parameter(Mandatory = $true)]
    [string]$BuildDirectory,
    [Parameter(Mandatory = $true)]
    [string]$ProgressFile
)

$ErrorActionPreference = "Stop"
$wsl = Get-Command "wsl.exe" -ErrorAction SilentlyContinue
if (-not $wsl) {
    throw "WSL2 is required to build Linux/Windows targets from Windows. Install WSL2 and a Linux distribution first."
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
$scriptPath = "$repoPath/scripts/platform_build.sh"

& $wsl.Source --exec bash $scriptPath $Target $repoPath $buildPath $progressPath
exit $LASTEXITCODE
