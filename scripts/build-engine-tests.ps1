param(
    [int] $IdleTimeoutSeconds = 30
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
$logDir = Join-Path $repo ".tmp"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

function Invoke-IdleChecked {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Name,

        [Parameter(Mandatory = $true)]
        [string] $FilePath,

        [Parameter(Mandatory = $true)]
        [string[]] $Args
    )

    $stdout = Join-Path $logDir "$Name.stdout.log"
    $stderr = Join-Path $logDir "$Name.stderr.log"
    Remove-Item -LiteralPath $stdout, $stderr -ErrorAction SilentlyContinue

    $quotedArgs = foreach ($arg in $Args) {
        if ($arg -match '\s') { '"' + ($arg -replace '"', '\"') + '"' } else { $arg }
    }

    $process = Start-Process -FilePath $FilePath `
                             -ArgumentList ($quotedArgs -join ' ') `
                             -WorkingDirectory $repo `
                             -WindowStyle Hidden `
                             -PassThru `
                             -RedirectStandardOutput $stdout `
                             -RedirectStandardError $stderr

    $lastActivity = Get-Date
    $lastOutLength = 0
    $lastErrLength = 0
    $lastCpuById = @{}
    Write-Host "$Name pid=$($process.Id)"

    while (-not $process.HasExited) {
        Start-Sleep -Seconds 5
        $activity = $false

        foreach ($path in @($stdout, $stderr)) {
            if (-not (Test-Path -LiteralPath $path)) { continue }
            $item = Get-Item -LiteralPath $path
            $previous = if ($path -eq $stdout) { $lastOutLength } else { $lastErrLength }
            if ($item.Length -gt $previous) {
                $activity = $true
                $text = Get-Content -LiteralPath $path -Raw -ErrorAction SilentlyContinue
                if ($null -ne $text -and $text.Length -gt $previous) {
                    Write-Host $text.Substring($previous)
                }
                if ($path -eq $stdout) { $lastOutLength = $item.Length } else { $lastErrLength = $item.Length }
            }
        }

        $tracked = Get-Process | Where-Object { $_.ProcessName -match '^(cmake|ninja|cl|link|mspdbsrv|MSBuild)$' }
        foreach ($child in $tracked) {
            $oldCpu = $lastCpuById[$child.Id]
            if ($null -eq $oldCpu -or $child.CPU -gt $oldCpu) { $activity = $true }
            $lastCpuById[$child.Id] = $child.CPU
        }

        if ($activity) { $lastActivity = Get-Date }
        $idle = [int]((Get-Date) - $lastActivity).TotalSeconds
        Write-Host ("--- {0} idle={1}s ---" -f (Get-Date -Format "HH:mm:ss"), $idle)

        if ($idle -ge $IdleTimeoutSeconds) {
            Write-Host "$Name idle for $idle seconds; stopping build processes."
            $tracked | Stop-Process -Force -ErrorAction SilentlyContinue
            if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
            exit 124
        }
    }

    $process.WaitForExit()
    foreach ($path in @($stdout, $stderr)) {
        if (Test-Path -LiteralPath $path) {
            $text = Get-Content -LiteralPath $path -Raw -ErrorAction SilentlyContinue
            if ($text) { Write-Host $text }
        }
    }

    if ($process.ExitCode -ne 0) { exit $process.ExitCode }
}

Invoke-IdleChecked -Name "configure-engine-vs-debug" -FilePath "cmake" -Args @("--preset", "engine-vs-debug")
Invoke-IdleChecked -Name "build-engine-vs-debug" -FilePath "cmake" -Args @("--build", "--preset", "engine-vs-debug")
Invoke-IdleChecked -Name "test-engine-vs-debug" -FilePath "ctest" -Args @("--preset", "engine-vs-debug")
