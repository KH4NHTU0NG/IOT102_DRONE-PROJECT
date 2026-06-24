#!/usr/bin/env bash
# ============================================================
# run_sitl.sh — macOS Phase 2: Khởi động ArduPilot SITL
# Kiểm chứng: macOS Apple Silicon (M-series)
#
# Mở đồng thời:
#   - UDP 14550 cho QGroundControl (tự auto-detect)
#   - TCP 5763 cho fusion.py
# ============================================================
set -euo pipefail

SITL_BIN=~/ardupilot/Tools/autotest/sim_vehicle.py

echo "========================================"
echo "  Drone IoT — Phase 2 SITL Launcher"
echo "  Platform: macOS Apple Silicon"
echo "========================================"

# ── Kiểm tra SITL tồn tại ────────────────────────────────
if [ ! -f "$SITL_BIN" ]; then
    echo "❌ Không tìm thấy sim_vehicle.py."
    echo "   → Hãy chạy install_sitl.sh trước."
    exit 1
fi
echo "✅ Tìm thấy SITL: $SITL_BIN"

# ── Kiểm tra port 5760 không bị chiếm ────────────────────
if lsof -ti :5760 &>/dev/null; then
    echo "⚠️  Port 5760 đang bị chiếm. Giải phóng..."
    lsof -ti :5760 | xargs kill -9 2>/dev/null || true
    sleep 1
fi

# ── Kiểm tra port 14550 không bị chiếm ───────────────────
if lsof -ti :14550 &>/dev/null; then
    echo "⚠️  Port 14550 đang bị chiếm. Giải phóng..."
    lsof -ti :14550 | xargs kill -9 2>/dev/null || true
    sleep 1
fi

echo ""
echo "▶ Khởi động SITL ArduCopter..."
echo "  Lần đầu sẽ BUILD trong 1–3 phút, sau đó nhanh hơn."
echo "  Chờ các dòng sau xuất hiện:"
echo "    AP: ArduPilot Ready"
echo "    AP: EKF3 IMU0 origin set"
echo "    MAV>"
echo ""
echo "  ⚠️  KHÔNG đóng Terminal này trong khi làm việc!"
echo "========================================"
echo ""

python3 "$SITL_BIN" \
    -v ArduCopter \
    --out=udp:127.0.0.1:14550 \
    --out=tcpin:127.0.0.1:5763 \
    --custom-location=-35.363261,149.165230,584,0 \
    --speedup 1 \
    --slave 0 \
    --sim-address=127.0.0.1 \
    -I0
