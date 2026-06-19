# ============================================================
# run_sitl.ps1 — Windows Phase 2: Khởi động ArduPilot SITL
# Chạy SITL trong WSL2, bridge port ra Windows host
#
# BUG FIX #10: SITL không chạy native trên Windows
#              → Bắt buộc dùng WSL2, script tự xử lý bridging
# ============================================================

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Drone IoT - Phase 2 SITL Launcher"    -ForegroundColor Cyan
Write-Host "  Platform: Windows (WSL2 Bridge)"       -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Kiem tra WSL2
$wslCheck = wsl --list --verbose 2>&1
if ($LASTEXITCODE -ne 0 -or -not ($wslCheck -match "Ubuntu")) {
    Write-Host "[ERROR] WSL2 Ubuntu chua san sang. Chay install_sitl.ps1 truoc." -ForegroundColor Red
    exit 1
}

# Lay IP cua Windows host (ma WSL2 thay)
$hostIP = (Get-NetIPAddress -AddressFamily IPv4 |
    Where-Object { $_.InterfaceAlias -notmatch "Loopback" -and
                   $_.InterfaceAlias -notmatch "WSL" -and
                   $_.IPAddress -notmatch "^169" } |
    Select-Object -First 1).IPAddress

if (-not $hostIP) {
    Write-Host "[WARN] Khong doc duoc IP host, dung 0.0.0.0 (listen all)" -ForegroundColor Yellow
    $hostIP = "0.0.0.0"
}

Write-Host ""
Write-Host "[*] Windows Host IP: $hostIP" -ForegroundColor Yellow
Write-Host "[*] QGroundControl (Windows) se connect UDP :14550" -ForegroundColor Yellow
Write-Host "[*] fusion.py (Windows) se connect TCP :5763" -ForegroundColor Yellow
Write-Host ""
Write-Host "Cho cac dong sau xuat hien truoc khi dung:" -ForegroundColor Cyan
Write-Host "  AP: ArduPilot Ready" -ForegroundColor Green
Write-Host "  AP: EKF3 IMU0 origin set" -ForegroundColor Green
Write-Host "  MAV>" -ForegroundColor Green
Write-Host ""
Write-Host "!!! KHONG DONG terminal nay trong khi lam viec !!!" -ForegroundColor Red
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Script bash de chay trong WSL2
$sitlCmd = @"
source ~/.profile 2>/dev/null || true
source ~/.bashrc 2>/dev/null || true

# Kiem tra SITL co san
if [ ! -f ~/ardupilot/Tools/autotest/sim_vehicle.py ]; then
    echo "ERROR: SITL chua cai. Chay install_sitl.ps1 truoc."
    exit 1
fi

# Giai phong port neu bi chiem
fuser -k 5760/tcp 2>/dev/null || true
fuser -k 5763/tcp 2>/dev/null || true
fuser -k 14550/udp 2>/dev/null || true
sleep 1

# Chay SITL
# --out bind vao 0.0.0.0 de Windows host co the connect
python3 ~/ardupilot/Tools/autotest/sim_vehicle.py \
    -v ArduCopter \
    --out=udp:0.0.0.0:14550 \
    --out=tcpin:0.0.0.0:5763 \
    --custom-location=-35.363261,149.165230,584,0 \
    --speedup 1 \
    --slave 0 \
    --sim-address=127.0.0.1 \
    -I0
"@

# Chay trong WSL2
wsl bash -c $sitlCmd
