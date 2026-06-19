# ============================================================
# install_sitl.ps1 — Windows Phase 2: Cài ArduPilot SITL qua WSL2
# Yêu cầu: WSL2 với Ubuntu 22.04 đã cài (xem wsl2_setup.md)
# Chạy bằng PowerShell (không cần Admin)
# ============================================================

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Drone IoT - Phase 2 SITL Install"     -ForegroundColor Cyan
Write-Host "  Platform: Windows (via WSL2 Ubuntu)"  -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Kiem tra WSL2 co san khong
$wslCheck = wsl --list --verbose 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] WSL2 chua cai. Xem wsl2_setup.md de huong dan." -ForegroundColor Red
    exit 1
}

if (-not ($wslCheck -match "Ubuntu")) {
    Write-Host "[ERROR] Khong tim thay Ubuntu trong WSL2." -ForegroundColor Red
    Write-Host "        Cai Ubuntu 22.04 tu Microsoft Store, xem wsl2_setup.md" -ForegroundColor Yellow
    exit 1
}
Write-Host "[OK] WSL2 Ubuntu da san sang." -ForegroundColor Green

# Script bash cai dat trong WSL2
$installScript = @'
#!/bin/bash
set -euo pipefail

echo "==> Cap nhat package list..."
sudo apt-get update -qq

echo "==> Cai prerequisites..."
sudo apt-get install -y -qq \
    git python3 python3-pip python3-dev \
    build-essential libssl-dev pkg-config \
    wget curl lsb-release 2>/dev/null

# Clone ArduPilot neu chua co
if [ ! -d ~/ardupilot ]; then
    echo "==> Clone ArduPilot (5-15 phut)..."
    git clone https://github.com/ArduPilot/ardupilot.git ~/ardupilot
fi

echo "==> Cap nhat submodules..."
cd ~/ardupilot
git submodule update --init --recursive

echo "==> Cai environment prerequisites..."
Tools/environment_install/install-prereqs-ubuntu.sh -y || true
source ~/.profile 2>/dev/null || true

echo "==> Cai Python packages..."
python3 -m pip install "empy==3.3.4" --quiet
echo "  empy==3.3.4 OK"
python3 -m pip install mavproxy --quiet
echo "  mavproxy OK"
python3 -m pip install pexpect --quiet
echo "  pexpect OK"

echo ""
echo "==> Verify empy:"
python3 -c "import em; print('  empy', em.__version__)"

echo ""
echo "SITL Install hoan tat trong WSL2!"
'@

Write-Host ""
Write-Host "[*] Chay script cai dat trong WSL2 Ubuntu..." -ForegroundColor Yellow
Write-Host "    (Co the mat 5-15 phut tuy toc do mang)" -ForegroundColor Yellow
Write-Host ""

# Chay script trong WSL2
$installScript | wsl bash -s

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Green
    Write-Host "  [OK] Phase 2 Install hoan tat!"       -ForegroundColor Green
    Write-Host "  Buoc tiep theo: Chay run_sitl.ps1"    -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Green
} else {
    Write-Host ""
    Write-Host "[ERROR] Cai dat that bai. Kiem tra log tren." -ForegroundColor Red
    exit 1
}
