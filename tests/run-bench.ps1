param(
    [int]$Lines = 10000,
    [string]$Exe = "build\67t.exe",
    [string]$Log = "build\bench.log"
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
        -ArgumentList @("--bench", "$Lines", "--log", $Log) -PassThru -Wait
    if (Test-Path $logPath) {
        Get-Content $logPath
    }
    if ($process.ExitCode -ne 0) {
        throw "Benchmark failed with exit code $($process.ExitCode)."
    }
}
finally {
    Pop-Location
}
