# Start the C2 backend in serial mode (gateway on USB). Open http://127.0.0.1:8000 afterwards.
# Usage: powershell -ExecutionPolicy Bypass -File run_backend_serial.ps1 -Port COM10
param([Parameter(Mandatory)][string]$Port, [int]$Baud = 115200)
$backend = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) "backend"
$env:C2_SOURCE = "serial"; $env:C2_SERIAL_PORT = $Port; $env:C2_SERIAL_BAUD = "$Baud"
Set-Location $backend
Write-Host "C2 backend (serial $Port @ $Baud) -> http://127.0.0.1:8000  (Ctrl+C to stop)" -ForegroundColor Cyan
& ".\.venv\Scripts\python.exe" -m uvicorn app.main:app --host 127.0.0.1 --port 8000
