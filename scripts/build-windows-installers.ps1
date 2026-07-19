param(
    [string] $Version = "",
    [string] $Configuration = "Release",
    [string] $BuildDirectory = "",
    [string] $Generator = "Visual Studio 18 2026",
    [switch] $SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$versionFile = Join-Path $repoRoot "cmake\OpalineVersion.cmake"
$versionText = Get-Content $versionFile -Raw

$major = [regex]::Match($versionText, 'OPALINE_VERSION_MAJOR\s+([0-9]+)').Groups[1].Value
$minor = [regex]::Match($versionText, 'OPALINE_VERSION_MINOR\s+([0-9]+)').Groups[1].Value
$patch = [regex]::Match($versionText, 'OPALINE_VERSION_PATCH\s+([0-9]+)').Groups[1].Value
$tweak = [regex]::Match($versionText, 'OPALINE_VERSION_TWEAK\s+([0-9]+)').Groups[1].Value
$sourceVersion = "$major.$minor.$patch"
if (-not [string]::IsNullOrWhiteSpace($tweak) -and $tweak -ne "0") {
    $sourceVersion = "$sourceVersion.$tweak"
}
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = $sourceVersion
}
if ($sourceVersion -ne $Version) {
    throw "Requested installer version $Version does not match source version $sourceVersion."
}
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = "build\release-$Version"
}
$buildRoot = Join-Path $repoRoot $BuildDirectory
$pluginArtifactRoot = Join-Path $buildRoot "OpalineFM_Plugin_artefacts\$Configuration"

if (-not $SkipBuild) {
    $configureArguments = @("-S", $repoRoot, "-B", $buildRoot, "-DOPALINE_AUTO_INCREMENT_VERSION=OFF")
    if (-not (Test-Path (Join-Path $buildRoot "CMakeCache.txt"))) {
        $configureArguments += @("-G", $Generator, "-A", "x64")
    }
    & cmake @configureArguments
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

    & cmake --build $buildRoot --config $Configuration --target OpalineFM_Plugin_Standalone OpalineFM_Plugin_VST3
    if ($LASTEXITCODE -ne 0) { throw "Application/plugin build failed." }
}

$standalone = Join-Path $pluginArtifactRoot "Standalone\Opaline FM.exe"
$vst3 = Join-Path $pluginArtifactRoot "VST3\Opaline FM.vst3\Contents\x86_64-win\Opaline FM.vst3"
if (-not (Test-Path $standalone)) { throw "Standalone artifact was not found: $standalone" }
if (-not (Test-Path $vst3)) { throw "VST3 artifact was not found: $vst3" }

$isccCandidates = @(
    "C:\Program Files\Inno Setup 7\ISCC.exe",
    "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    "C:\Program Files\Inno Setup 6\ISCC.exe"
)
$iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup compiler (ISCC.exe) was not found." }

$distDirectory = Join-Path $repoRoot "dist"
New-Item -ItemType Directory -Force -Path $distDirectory | Out-Null

$definitions = @("/DAppVersion=$Version", "/DBuildRoot=$pluginArtifactRoot", "/O$distDirectory")
& $iscc @definitions (Join-Path $repoRoot "installer\OpalineFM-Standalone.iss")
if ($LASTEXITCODE -ne 0) { throw "Standalone installer compilation failed." }

& $iscc @definitions (Join-Path $repoRoot "installer\OpalineFM-VST3.iss")
if ($LASTEXITCODE -ne 0) { throw "VST3 installer compilation failed." }

Write-Host "Installers created in $distDirectory"
