#!/usr/bin/env bash
# ============================================================
# install_sitl.sh — macOS Phase 2: Cài ArduPilot SITL native
# Kiểm chứng: macOS Apple Silicon (M-series)
#
# BUG FIX #9: thêm source ~/.zshrc sau install để reload PATH
# CẢNH BÁO: Bắt buộc chạy SITL native — KHÔNG dùng Docker
#           (Image Docker không hỗ trợ Mac ARM / thiếu X11)
# ============================================================
set -euo pipefail

echo "========================================"
echo "  Drone IoT — Phase 2 SITL Install"
echo "  Platform: macOS Apple Silicon"
echo "========================================"

# ── Kiểm tra prerequisites ────────────────────────────────
echo "▶ Kiểm tra Xcode Command Line Tools..."
if ! xcode-select -p &>/dev/null; then
    echo "▶ Cài Xcode CLT..."
    xcode-select --install
    echo "  → Chờ cửa sổ cài đặt hoàn tất, rồi chạy lại script này."
    exit 0
fi
echo "✅ Xcode CLT đã có."

echo "▶ Kiểm tra Homebrew..."
if ! command -v brew &>/dev/null; then
    echo "▶ Cài Homebrew..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    # Thêm brew vào PATH cho Apple Silicon
    echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
    eval "$(/opt/homebrew/bin/brew shellenv)"
fi
echo "✅ Homebrew đã có."

# ── Clone ArduPilot ───────────────────────────────────────
if [ -d ~/ardupilot ]; then
    echo "✅ ~/ardupilot đã tồn tại. Bỏ qua bước clone."
else
    echo ""
    echo "▶ Clone ArduPilot (5–15 phút)..."
    git clone https://github.com/ArduPilot/ardupilot.git ~/ardupilot
fi

echo ""
echo "▶ Cập nhật submodules..."
cd ~/ardupilot
git submodule update --init --recursive

# ── Cài dependencies hệ thống ────────────────────────────
echo ""
echo "▶ Cài dependencies ArduPilot (Tools/environment_install)..."
echo "  Cảnh báo pyenv có thể xuất hiện — bỏ qua là bình thường."
cd ~/ardupilot
Tools/environment_install/install-prereqs-mac.sh -y || true

# Bug fix #9: reload PATH sau khi cài prereqs
echo ""
echo "▶ Reload PATH..."
if [ -f ~/.zshrc ]; then
    source ~/.zshrc 2>/dev/null || true
fi
if [ -f ~/.zprofile ]; then
    source ~/.zprofile 2>/dev/null || true
fi

# ── Cài Python packages bắt buộc ─────────────────────────
echo ""
echo "▶ Cài Python packages cho SITL..."
echo "  QUAN TRỌNG: empy phải đúng phiên bản 3.3.4"

python3 -m pip install "empy==3.3.4" --quiet
echo "  ✅ empy==3.3.4"

python3 -m pip install mavproxy --quiet
echo "  ✅ mavproxy"

python3 -m pip install gnureadline --quiet
echo "  ✅ gnureadline"

python3 -m pip install pexpect --quiet
echo "  ✅ pexpect"

# ── Verify cài đặt ───────────────────────────────────────
echo ""
echo "▶ Verify empy version:"
python3 -c "import em; print('  empy version:', em.__version__)"

echo ""
echo "▶ Verify mavproxy:"
which mavproxy.py 2>/dev/null && echo "  ✅ mavproxy OK" || \
    python3 -m mavproxy --version 2>/dev/null | head -1 && echo "  ✅ mavproxy OK" || \
    echo "  ⚠️  mavproxy không tìm thấy trong PATH — thử: python3 -m pip install mavproxy"

echo ""
echo "========================================"
echo "  ✅ Phase 2 Install hoàn tất!"
echo ""
echo "  BƯỚC TIẾP THEO:"
echo "  Mở Terminal MỚI, rồi chạy: ./run_sitl.sh"
echo "  (Terminal mới cần thiết để reload PATH)"
echo "========================================"
