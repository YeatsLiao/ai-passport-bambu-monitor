param(
    [string]$Port = '',
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
try {
    $projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    if (-not $OutputDirectory) {
        $OutputDirectory = Join-Path $projectRoot 'screenshots'
    }
    . (Join-Path $PSScriptRoot 'serial_connection.ps1')
    $connection = Resolve-DeviceConnection -Port $Port
    $pythonExecutable = $connection.Python
    $Port = $connection.Port
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
    $OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
    $outputPath = Join-Path $OutputDirectory ('bambu-monitor-' + (Get-Date -Format 'yyyyMMdd-HHmmss-ffff') + '.png')
    Write-Host "Capturing from $Port. Close idf.py monitor and other serial tools first."
    & $pythonExecutable -B (Join-Path $PSScriptRoot 'capture_screen.py') --port $Port --output $outputPath
    if ($LASTEXITCODE -ne 0) { throw 'Device capture failed; see the error above.' }
    Write-Host "Saved: $outputPath"
    try { Invoke-Item -LiteralPath $outputPath } catch {
        Write-Host 'PNG saved, but the image viewer could not be opened.'
    }
    exit 0
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
