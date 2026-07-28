param(
    [switch]$Kill,
    [switch]$Status,
    [switch]$Log,
    [switch]$WaitReady,
    [int]$BootTimeout = 30
)

$QemuDir = "$PSScriptRoot\..\.qemu"
$QemuBin = "$QemuDir\qemu\bin\qemu-system-xtensa.exe"
$FlashBin = "$QemuDir\flash.bin"
$SerialLog = "$QemuDir\qemu_serial.log"
$QemuPidFile = "$QemuDir\qemu.pid"

function Get-QemuPid {
    if (Test-Path $QemuPidFile) {
        $pid = Get-Content $QemuPidFile -Raw
        $proc = Get-Process -Id $pid -ErrorAction SilentlyContinue
        if ($proc -and $proc.ProcessName -like "*qemu*") { return $pid }
    }
    $proc = Get-Process qemu-system-xtensa -ErrorAction SilentlyContinue
    if ($proc) { return $proc.Id }
    return $null
}

if ($Kill) {
    $pid = Get-QemuPid
    if ($pid) { Stop-Process -Id $pid -Force; Write-Host "QEMU [$pid] killed" }
    else { Write-Host "No QEMU running" }
    return
}

if ($Status) {
    $pid = Get-QemuPid
    if ($pid) {
        $proc = Get-Process -Id $pid
        $mem = [math]::Round($proc.WorkingSet64 / 1MB, 1)
        Write-Host "QEMU running PID=$pid CPU=$($proc.CPU)s MEM=${mem}MB"
        if (Test-Path $SerialLog) {
            $lines = (Get-Content $SerialLog).Count
            $last = Get-Content $SerialLog -Tail 3
            Write-Host "Log: ${lines} lines"
            Write-Host "Tail: $last"
        }
        # Check HTTP
        try {
            $r = Invoke-WebRequest -Uri "http://localhost:8080/" -UseBasicParsing -TimeoutSec 2
            Write-Host "HTTP: $($r.StatusCode) ($($r.Content.Length) bytes)"
        } catch { Write-Host "HTTP: not responding" }
    } else { Write-Host "QEMU not running" }
    return
}

if ($Log) {
    if (Test-Path $SerialLog) { Get-Content $SerialLog }
    else { Write-Host "No log file" }
    return
}

if ($WaitReady) {
    Write-Host "Waiting for firmware to boot (${BootTimeout}s timeout)..."
    $start = Get-Date
    while ($true) {
        if ((Get-Date) - $start -gt [TimeSpan]::FromSeconds($BootTimeout)) {
            Write-Host "TIMEOUT - firmware did not boot within ${BootTimeout}s"
            exit 1
        }
        Start-Sleep -Seconds 1
        if (Test-Path $SerialLog) {
            $log = Get-Content $SerialLog -Raw
            if ($log -match "Ready\. SSID:") {
                Write-Host "FIRMWARE READY"
                return
            }
        }
        # Also check if log file stopped growing (firmware crashed)
    }
    return
}

# ── Start QEMU ────────────────────────────────────────────────────
Write-Host "Starting QEMU ESP32-S3..."
Write-Host "  Flash: $FlashBin"
Write-Host "  Port forward: localhost:8080 -> guest:80"
Write-Host "  Serial log: $SerialLog"

Remove-Item $SerialLog -ErrorAction SilentlyContinue

$proc = Start-Process -NoNewWindow -FilePath $QemuBin -PassThru -ArgumentList @(
    "-nographic",
    "-M", "esp32s3",
    "-m", "2M",
    "-drive", "file=$FlashBin,if=mtd,format=raw",
    "-nic", "user,hostfwd=tcp::8080-:80,model=open_eth",
    "-serial", "file:$SerialLog"
)

$proc.Id | Out-File -FilePath $QemuPidFile -Force
Write-Host "QEMU started PID=$($proc.Id)"
Write-Host "Run '.\scripts\qemu_test.ps1 -WaitReady' to wait for boot"
