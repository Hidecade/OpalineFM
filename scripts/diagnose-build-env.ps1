Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Show-Command($name) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        Write-Host "${name}: not found"
        return
    }

    Write-Host "${name}: $($cmd.Source)"
}

Show-Command cmake
Show-Command ninja
Show-Command msbuild
Show-Command cl

$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path -LiteralPath $vswhere) {
    $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    Write-Host "Visual Studio Build Tools: $vsPath"
    if ($vsPath) {
        $msbuild = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
        $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
        Write-Host "MSBuild.exe exists: $(Test-Path -LiteralPath $msbuild)"
        Write-Host "vcvars64.bat exists: $(Test-Path -LiteralPath $vcvars)"
    }
}
else {
    Write-Host "vswhere: not found"
}

Write-Host ""
Write-Host "Active build processes:"
Get-Process | Where-Object { $_.ProcessName -match '^(cmake|ninja|cl|link|mspdbsrv|MSBuild)$' } |
    Select-Object ProcessName, Id, CPU, StartTime |
    Format-Table -AutoSize
