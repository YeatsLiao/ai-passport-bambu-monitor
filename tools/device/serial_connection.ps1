# Shared environment/port discovery only. Never opens a serial port or installs anything.
# Ported from ai-passport-sheikah-slate tools/device/serial_connection.ps1;
# build metadata path adapted to this repo's layout (build\ at project root).
function Resolve-DeviceConnection {
    param([string]$Port = '')
    $projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $candidates = [System.Collections.Generic.List[string]]::new()
    $environmentRoots = [System.Collections.Generic.List[string]]::new()
    if ($env:IDF_PYTHON_ENV_PATH) {
        $candidates.Add((Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'))
    }
    if ($env:IDF_TOOLS_PATH) {
        $environmentRoots.Add((Join-Path $env:IDF_TOOLS_PATH 'python_env'))
    }
    $environmentRoots.Add((Join-Path $env:USERPROFILE '.espressif\python_env'))

    # Reuse local build metadata to find a custom Espressif installation.
    # No personal installation path is embedded in the launcher.
    $descriptionPath = Join-Path $projectRoot 'build\project_description.json'
    if (Test-Path -LiteralPath $descriptionPath) {
        try {
            $description = Get-Content -LiteralPath $descriptionPath -Raw | ConvertFrom-Json
            if ($description.idf_path) {
                $installationRoot = Split-Path -Parent (Split-Path -Parent $description.idf_path)
                $environmentRoots.Add((Join-Path $installationRoot 'python_env'))
            }
        } catch {
            Write-Host 'Build metadata unavailable; checking other Python locations.'
        }
    }
    foreach ($root in ($environmentRoots | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $root) {
            foreach ($environment in (Get-ChildItem -LiteralPath $root -Directory | Sort-Object LastWriteTime -Descending)) {
                $candidates.Add((Join-Path $environment.FullName 'Scripts\python.exe'))
            }
        }
    }
    foreach ($command in (Get-Command python.exe -All -ErrorAction SilentlyContinue)) {
        if ($command.Source -and $command.Source -notlike '*\WindowsApps\*') {
            $candidates.Add($command.Source)
        }
    }
    $pythonExecutable = $null
    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }
        & $candidate -B -c "import importlib.util, sys; sys.exit(0 if importlib.util.find_spec('serial') else 1)" 2>$null
        if ($LASTEXITCODE -eq 0) { $pythonExecutable = $candidate; break }
    }
    if (-not $pythonExecutable) {
        throw 'No Python with pyserial found. Run from the ESP-IDF terminal, or set IDF_PYTHON_ENV_PATH to its Python environment directory.'
    }
    if (-not $Port) {
        # Enumerate only; don't send commands to other attached devices.
        $portJson = & $pythonExecutable -B -c "import json; from serial.tools import list_ports; print(json.dumps([p.device for p in list_ports.comports() if p.vid == 0x303a and p.pid == 0x1001]))"
        if ($LASTEXITCODE -ne 0) { throw 'Could not enumerate USB serial devices.' }
        $usbPorts = @($portJson | ConvertFrom-Json)
        if ($usbPorts.Count -eq 1) {
            $Port = $usbPorts[0]
        } elseif ($usbPorts.Count -gt 1) {
            Write-Host ('Multiple ESP USB devices: ' + ($usbPorts -join ', '))
            Write-Host 'Select the SAME port used to flash this device.'
            $preferredPort = if ($usbPorts -contains 'COM4') { 'COM4' } else { $usbPorts[0] }
            $choice = Read-Host "Port [$preferredPort]"
            $Port = if ($choice.Trim()) { $choice.Trim().ToUpperInvariant() } else { $preferredPort }
            if ($usbPorts -notcontains $Port) { throw "Not a detected ESP USB port: $Port" }
        } else {
            throw 'No ESP USB Serial/JTAG device found. Reconnect the device, or specify -Port COMx explicitly.'
        }
    }
    return [pscustomobject]@{ Python = $pythonExecutable; Port = $Port }
}
