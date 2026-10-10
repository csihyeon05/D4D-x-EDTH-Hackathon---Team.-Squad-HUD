# Read a serial port for N seconds without writing to it.
# Usage: powershell -ExecutionPolicy Bypass -File read_serial.ps1 -Port COM10 [-Baud 115200] [-Seconds 160]
param([Parameter(Mandatory)][string]$Port, [int]$Baud = 115200, [int]$Seconds = 160)
$p = New-Object System.IO.Ports.SerialPort $Port, $Baud, "None", 8, "One"
$p.DtrEnable = $true; $p.RtsEnable = $true   # TinyUSB CDC only sends when DTR is set
$p.Open()
try {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  while ($sw.ElapsedMilliseconds -lt $Seconds * 1000) {
    $x = $p.ReadExisting(); if ($x) { [Console]::Out.Write($x) }
    Start-Sleep -Milliseconds 100
  }
} finally { $p.Close() }
