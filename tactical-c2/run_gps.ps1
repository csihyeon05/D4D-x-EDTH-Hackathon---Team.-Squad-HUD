# Squad C2 - phone GPS mode (Windows). Double-click RUN_GPS.bat (simulator) or RUN_SUIT.bat (real suit).
# Serves HTTPS on :8443 so a phone on the same hotspot/Wi-Fi can open https://<laptop-ip>:8443/gps
# (browsers block the Geolocation API on plain http except localhost).
#   -Source serial   read the NU40 gateway on USB (sensor packets) in the SAME server as the phone GPS,
#                    so the COP joins each soldier's sensors and phone position.
#   -SerialPort COMx gateway port (default: auto-detect the NU40, USB VID 239A)
#   -Soldiers 1,2    print one QR per soldier; the link pre-fills the phone's Soldier ID
param(
  [ValidateSet("sim", "serial")][string]$Source = "sim",
  [string]$SerialPort = "",
  [int[]]$Soldiers = @()
)
$ErrorActionPreference = "Continue"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$be = Join-Path $root "backend"
Set-Location $be
$port = 8443
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$env:PYTHONIOENCODING = "utf-8"
function Step($m) { Write-Host "`n=== $m ===" -ForegroundColor Cyan }

# 1) Python virtual environment ------------------------------------------------
Step "Python environment"
$vpy = Join-Path $be ".venv\Scripts\python.exe"
if (-not (Test-Path $vpy)) {
  $sys = $null
  foreach ($c in @("py -3", "python")) {
    $v = & cmd /c "$c --version" 2>&1
    if ($LASTEXITCODE -eq 0 -and "$v" -match "Python 3\.(\d+)" -and [int]$Matches[1] -ge 11) { $sys = $c; break }
  }
  if (-not $sys) { Write-Host "Python 3.11+ not found. Install it from https://www.python.org/downloads/" -ForegroundColor Red; exit 1 }
  & cmd /c "$sys -m venv .venv"
}
& $vpy -c "import fastapi, uvicorn, cryptography, qrcode" 2>$null
if ($LASTEXITCODE -ne 0) {
  Write-Host "Installing missing packages (needs internet - the hotspot is fine)..."
  & $vpy -m pip install -q -r requirements.txt qrcode
  & $vpy -c "import fastapi, uvicorn, cryptography" 2>$null
  if ($LASTEXITCODE -ne 0) { Write-Host "Package install failed. Check the internet connection and run again." -ForegroundColor Red; exit 1 }
}
Write-Host "OK"

# 2) Which IP will the phone reach? (interface that carries the default route) -
Step "Network"
$ip = $null; $alias = $null
try {
  $route = Get-NetRoute -DestinationPrefix "0.0.0.0/0" -ErrorAction Stop |
           Sort-Object { $_.RouteMetric + (Get-NetIPInterface -InterfaceIndex $_.InterfaceIndex -AddressFamily IPv4).InterfaceMetric } |
           Select-Object -First 1
  $alias = $route.InterfaceAlias
  $ip = (Get-NetIPAddress -InterfaceIndex $route.InterfaceIndex -AddressFamily IPv4 |
         Where-Object { $_.IPAddress -notlike "169.254.*" } | Select-Object -First 1).IPAddress
  $prof = Get-NetConnectionProfile -InterfaceIndex $route.InterfaceIndex -ErrorAction SilentlyContinue
} catch {}
if (-not $ip) {
  $ip = & $vpy -c "import socket;s=socket.socket(2,2);s.connect(('10.255.255.255',1));print(s.getsockname()[0])"
}
if (-not $ip) { Write-Host "No network found. Connect this laptop to the phone hotspot first." -ForegroundColor Red; exit 1 }
Write-Host "Laptop IP: $ip  (adapter: $alias)"
if ($prof) { Write-Host "Network profile: $($prof.NetworkCategory)" }

# 3) TLS certificate that matches the current IP ------------------------------
Step "TLS certificate"
$ipFile = Join-Path $be "certs\ip.txt"
$old = if (Test-Path $ipFile) { (Get-Content $ipFile -Raw).Trim() } else { "" }
if (-not (Test-Path (Join-Path $be "certs\cert.pem")) -or $old -ne $ip) {
  & $vpy tools\make_cert.py $ip
  if ($LASTEXITCODE -ne 0) { Write-Host "Certificate creation failed." -ForegroundColor Red; exit 1 }
  Set-Content -Path $ipFile -Value $ip -Encoding ascii
  Write-Host "New certificate: the phone will show a security warning once - choose Advanced > Proceed."
} else { Write-Host "Reusing certificate for $ip" }

# 4) Windows Firewall: allow inbound TCP 8443 from the local subnet -----------
Step "Windows Firewall"
$rule = "Squad C2 GPS $port"
$have = Get-NetFirewallRule -DisplayName $rule -ErrorAction SilentlyContinue
if ($have) { Write-Host "Rule '$rule' already present." }
else {
  Write-Host "Phones cannot connect until TCP $port is allowed in Windows Firewall."
  Write-Host "A rule limited to the local subnet (the hotspot) will be added; Windows asks for admin approval."
  $ans = Read-Host "Add the firewall rule now? [Y/n]"
  if ($ans -eq "" -or $ans -match "^[Yy]") {
    $cmd = "netsh advfirewall firewall add rule name=`"$rule`" dir=in action=allow protocol=TCP localport=$port remoteip=localsubnet profile=any"
    try { Start-Process -FilePath "cmd.exe" -ArgumentList "/c $cmd" -Verb RunAs -Wait -WindowStyle Hidden; Write-Host "Requested." }
    catch { Write-Host "Skipped (admin approval declined)." -ForegroundColor Yellow }
  } else { Write-Host "Skipped. If the phone cannot connect, run this again and answer Y." -ForegroundColor Yellow }
}

# 5) Data source ---------------------------------------------------------------
Step "Data source: $Source"
$env:C2_SOURCE = $Source
if ($Source -eq "serial") {
  if (-not $SerialPort) {
    # NU40 gateway in application mode (bootloader PIDs start with 00).
    $dev = Get-PnpDevice -PresentOnly -Class Ports -ErrorAction SilentlyContinue |
           Where-Object { $_.InstanceId -match "VID_239A" -and $_.InstanceId -notmatch "PID_00" } | Select-Object -First 1
    if ($dev -and $dev.FriendlyName -match "(COM\d+)") { $SerialPort = $Matches[1] }
  }
  if (-not $SerialPort) { Write-Host "NU40 gateway not found. Plug it in, or pass -SerialPort COMx." -ForegroundColor Red; exit 1 }
  $env:C2_SERIAL_PORT = $SerialPort; $env:C2_SERIAL_BAUD = "115200"
  Write-Host "Gateway: $SerialPort @ 115200"
  # Only one program can own the gateway port: stop earlier C2 servers.
  foreach ($p in 8000, $port) {
    Get-NetTCPConnection -LocalPort $p -State Listen -ErrorAction SilentlyContinue |
      Select-Object -ExpandProperty OwningProcess -Unique |
      ForEach-Object { Stop-Process -Id $_ -Force -ErrorAction SilentlyContinue; Write-Host "Stopped earlier server on :$p (pid $_)" }
  }
}

# 6) Start ---------------------------------------------------------------------
$url = "https://${ip}:$port/gps"
$qr = "import sys,qrcode;q=qrcode.QRCode(border=1);q.add_data(sys.argv[1]);q.print_ascii(invert=True)"
if ($Soldiers.Count -gt 0) {
  foreach ($sid in $Soldiers) {
    Step "PHONE on soldier S$sid's outer - scan this"
    Write-Host "  $url`?soldier=$sid" -ForegroundColor Green
    & $vpy -c $qr "$url`?soldier=$sid" 2>$null
  }
  Write-Host "The phone page fills in the Soldier ID from the link. Press Start on the phone."
} else {
  Step "Open this on the PHONE"
  Write-Host "  $url" -ForegroundColor Green
  & $vpy -c $qr $url 2>$null
}
$laptop = if ($Source -eq "serial") { "https://localhost:$port/" } else { "https://localhost:$port/gps/pc" }
Write-Host "LAPTOP: COP https://localhost:$port/   GPS monitor + CSV https://localhost:$port/gps/pc" -ForegroundColor Green
Write-Host "CSV files are also saved automatically in backend\data\"
Start-Job -ScriptBlock { param($u) Start-Sleep 4; Start-Process $u } -ArgumentList $laptop | Out-Null
Write-Host "Stop with Ctrl+C.`n"
& $vpy -m uvicorn app.main:app --host 0.0.0.0 --port $port --ssl-keyfile certs\key.pem --ssl-certfile certs\cert.pem
