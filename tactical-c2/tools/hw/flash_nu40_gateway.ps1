# Build and flash the NU40 commander gateway (firmware branch, firmware/nu40-commander).
# Usage:  powershell -ExecutionPolicy Bypass -File flash_nu40_gateway.ps1 [-Root D:\D4D_EDTH_HACKATHON\firmware-branch]
# If the board does not enter the bootloader automatically, double-press RESET when asked.
param(
  [string]$Root = "D:\D4D_EDTH_HACKATHON\firmware-branch",
  [string]$Branch = "firmware"
)
$ErrorActionPreference = "Stop"
$repo = "https://github.com/csihyeon05/D4D-x-EDTH-Hackathon---Team.-Squad-HUD.git"
if (!(Test-Path $Root)) { git clone --depth 1 -b $Branch $repo $Root } else { git -C $Root pull --ff-only }
git -C $Root log -1 --format="firmware commit: %h %ad %s"

$venv = Join-Path $env:USERPROFILE ".venvs\pio"
if (!(Test-Path "$venv\Scripts\pio.exe")) {
  py -3.11 -m venv $venv
  & "$venv\Scripts\python.exe" -m pip install -q --upgrade pip platformio
}
$proj = Join-Path $Root "firmware\nu40-commander"
Push-Location $proj
& "$venv\Scripts\pio.exe" run -e nu40_commander
if ($LASTEXITCODE -ne 0) { throw "build failed" }
Pop-Location
$zip = Join-Path $proj ".pio\build\nu40_commander\firmware.zip"

function Get-NuPorts { Get-PnpDevice -PresentOnly | Where-Object { $_.Class -eq "Ports" -and $_.InstanceId -match "VID_239A" } }
Get-NuPorts | ForEach-Object { "found: " + $_.FriendlyName + "  " + $_.InstanceId }

Write-Host "Double-press RESET on the NU40 now (bootloader = new COM port / USB drive). Waiting 60 s..." -ForegroundColor Yellow
$boot = $null
for ($i = 0; $i -lt 60 -and -not $boot; $i++) {
  # Application PIDs seen: 0x80AC (old fw), 0x8029 (gateway fw). Bootloader seen: 0x00DA.
  $boot = Get-NuPorts | Where-Object { $_.InstanceId -match "PID_00" } | Select-Object -First 1
  Start-Sleep 1
}
if (-not $boot) { throw "bootloader port not found" }
$port = [regex]::Match($boot.FriendlyName, "COM\d+").Value
"bootloader on $port"
$nrfutil = Join-Path $env:USERPROFILE ".platformio\packages\tool-adafruit-nrfutil\adafruit-nrfutil.py"
& "$venv\Scripts\python.exe" $nrfutil --verbose dfu serial -pkg $zip -p $port -b 115200 --singlebank
Start-Sleep 3
Get-NuPorts | ForEach-Object { "after flash: " + $_.FriendlyName }
"Expected output at 115200 baud: 'GW,uptime_s=...' every 10 s and 'PKT:<36 hex>,RSSI:<dBm>' per soldier packet."
