#!/usr/bin/env bash
# ============================================================
# start_all.sh — macOS Phase 5: Khởi động hệ thống HITL
# Thứ tự: Docker → Kiểm tra Mamba → Khởi động Fusion
# ============================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ ! -d "$ROOT_DIR/Phase1_Docker" ] || [ ! -d "$ROOT_DIR/Phase4_Fusion" ]; then
    echo "❌ Không tìm thấy cấu trúc dự án tại: $ROOT_DIR"
    exit 1
fi

echo "╔══════════════════════════════════════════════════╗"
echo "║       Drone IoT — Start All (HITL)               ║"
echo "║  Thứ tự: Docker → Mamba F405 → Fusion Gateway  ║"
echo "╚══════════════════════════════════════════════════╝"
echo "  ROOT: $ROOT_DIR"
echo ""

# ── Dọn dẹp tiến trình cũ ─────────────────────────────────
echo "━━━ Dọn dẹp hệ thống ━━━"
if pgrep -f "fusion.py" > /dev/null; then
    echo "  → Đang tắt các tiến trình fusion.py cũ bị kẹt..."
    pkill -f "fusion.py" || true
    sleep 1
fi

# ── Bước 1: Docker server ─────────────────────────────────
echo ""
echo "━━━ [1/3] Khởi động Docker containers ━━━"
if ! docker info &>/dev/null; then
    echo "❌ Docker Desktop chưa chạy!"
    echo "   → Mở Docker Desktop và chờ biểu tượng xanh."
    exit 1
fi
cd "$ROOT_DIR/Phase1_Docker"
docker compose up -d
echo "✅ Docker: iot_mqtt, iot_db, iot_grafana"
sleep 5

# ── Bước 2: Dò tìm Mamba F405 (HITL) ──────────────────────
echo ""
echo "━━━ [2/3] Dò tìm cổng USB của mạch Mamba F405... ━━━"
USB_PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -n 1 || echo "")

if [ -z "$USB_PORT" ]; then
    echo "❌ Không tìm thấy mạch Mamba F405!"
    echo "   Vui lòng cắm cáp USB nối mạch với máy Mac và thử lại."
    exit 1
fi
echo "✅ Đã tìm thấy mạch Mamba tại cổng: $USB_PORT"

# ── Bước 3: Fusion gateway ────────────────────────────────
echo ""
echo "━━━ [3/3] Khởi động Data Fusion Gateway ━━━"
VENV="$ROOT_DIR/Phase4_Fusion/drone_env"
if [ ! -d "$VENV" ]; then
    echo "  → Chưa có venv. Đang tạo..."
    bash "$ROOT_DIR/Phase4_Fusion/setup_venv.sh"
fi
source "$VENV/bin/activate"

echo "  → Khởi động fusion.py trong nền..."
nohup python3 "$ROOT_DIR/Phase4_Fusion/fusion.py" --device "$USB_PORT" \
    > "$ROOT_DIR/Phase5_Operations/fusion.log" 2>&1 &
FUSION_PID=$!
echo "$FUSION_PID" > "$ROOT_DIR/Phase5_Operations/fusion.pid"
echo "✅ fusion.py PID=$FUSION_PID"

# ── Tóm tắt ──────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════╗"
echo "║  ✅ Toàn bộ hệ thống HITL đã khởi động!          ║"
echo "║                                                  ║"
echo "║  Grafana:    http://localhost:3000               ║"
echo "║  Xem log:    tail -f Phase5_Operations/fusion.log║"
echo "╚══════════════════════════════════════════════════╝"
