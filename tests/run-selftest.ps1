param(
    [string]$Exe = "build\67t.exe",
    [string]$Log = "build\selftest.log"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    if (-not (Test-Path $Exe)) {
        throw "Missing $Exe. Run .\build.bat first."
    }
    $logPath = Join-Path $root $Log
    if (Test-Path $logPath) { Remove-Item -Force $logPath }
    $process = Start-Process -FilePath (Join-Path $root $Exe) `
        -ArgumentList @("--selftest", "--log", $Log) -PassThru -Wait
    if (Test-Path $logPath) {
        Get-Content $logPath
    } else {
        Write-Host "No self-test log was produced."
    }
    if ($process.ExitCode -ne 0) {
        throw "Self-test failed with exit code $($process.ExitCode)."
    }
    $summary = Select-String -Path $logPath -Pattern "summary passed=(\d+) failed=(\d+)"
    if (-not $summary) {
        throw "Self-test log has no summary line."
    }
    if ($summary.Matches[0].Groups[2].Value -ne "0") {
        throw "Self-test reported failures."
    }
    Write-Host "Self-test OK."
}
finally {
    Pop-Location
}
