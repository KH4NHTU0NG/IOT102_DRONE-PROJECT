#!/usr/bin/env bash
# ============================================================
# start_all.sh — macOS Phase 5: Khởi động toàn bộ hệ thống
# Thứ tự: Docker → BW16 → SITL → Fusion → QGC
#
# Cách chạy đúng:
#   cd <đường dẫn đến DroneIoT_macOS>
#   bash Phase5_Operations/start_all.sh
# ============================================================
set -euo pipefail

# Tự động detect ROOT_DIR từ vị trí script (luôn đúng dù chạy từ đâu)
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Guard: kiểm tra cấu trúc thư mục hợp lệ
if [ ! -d "$ROOT_DIR/Phase1_Docker" ] || [ ! -d "$ROOT_DIR/Phase4_Fusion" ]; then
    echo "❌ Không tìm thấy cấu trúc dự án tại: $ROOT_DIR"
    echo ""
    echo "   Hãy chạy script từ thư mục DroneIoT_macOS:"
    echo "   cd <đường dẫn đến DroneIoT_macOS>"
    echo "   bash Phase5_Operations/start_all.sh"
    exit 1
fi

echo "╔══════════════════════════════════════════════════╗"
echo "║       Drone IoT — Start All (macOS)              ║"
echo "║  Thứ tự: Docker → BW16 → SITL → Fusion → QGC   ║"
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
echo "━━━ [1/4] Khởi động Docker containers ━━━"
if ! docker info &>/dev/null; then
    echo "❌ Docker Desktop chưa chạy!"
    echo "   → Mở Docker Desktop và chờ biểu tượng xanh."
    exit 1
fi
cd "$ROOT_DIR/Phase1_Docker"
docker compose up -d
echo "✅ Docker: iot_mqtt, iot_db, iot_grafana"
sleep 10  # Chờ InfluxDB init

# ── Bước 2: Kiểm tra BW16 ─────────────────────────────────
echo ""
echo "━━━ [2/4] Kiểm tra BW16 Board ━━━"
echo "  → Cắm nguồn board BW16 (cáp USB hoặc nguồn ngoài)"
echo "  → Quan sát LED: nháy đều = WiFi OK"
echo "  → Hệ thống sẽ tự động bắt dữ liệu khi BW16 kết nối xong."

# ── Bước 3: SITL (chạy nền) ───────────────────────────────
echo ""
echo "━━━ [3/4] Khởi động SITL (Chạy nền) ━━━"
nohup bash "$ROOT_DIR/Phase2_SITL/run_sitl.sh" > "$ROOT_DIR/Phase5_Operations/sitl.log" 2>&1 &
echo "  → Đang chờ SITL khởi động (khoảng 10-30s)..."

# Verify port 5763
for i in {1..30}; do
    if lsof -i :5763 &>/dev/null; then
        echo "✅ Port 5763 đang mở. SITL đã sẵn sàng."
        break
    fi
    sleep 1
done
if ! lsof -i :5763 &>/dev/null; then
    echo "⚠️  Port 5763 chưa mở sau 30s — SITL có thể bị lỗi, kiểm tra Phase5_Operations/sitl.log."
fi

# ── Bước 4: Fusion gateway ────────────────────────────────
echo ""
echo "━━━ [4/4] Khởi động Data Fusion Gateway ━━━"
VENV="$ROOT_DIR/Phase4_Fusion/drone_env"
if [ ! -d "$VENV" ]; then
    echo "  → Chưa có venv. Đang tạo..."
    bash "$ROOT_DIR/Phase4_Fusion/setup_venv.sh"
fi
source "$VENV/bin/activate"
echo "  → Khởi động fusion.py trong nền..."
nohup python3 "$ROOT_DIR/Phase4_Fusion/fusion.py" \
    > "$ROOT_DIR/Phase5_Operations/fusion.log" 2>&1 &
FUSION_PID=$!
echo "$FUSION_PID" > "$ROOT_DIR/Phase5_Operations/fusion.pid"
echo "✅ fusion.py PID=$FUSION_PID"
echo "   Log: $ROOT_DIR/Phase5_Operations/fusion.log"
echo "   Tail log: tail -f $ROOT_DIR/Phase5_Operations/fusion.log"

# ── Tóm tắt ──────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════╗"
echo "║  ✅ Toàn bộ hệ thống đã khởi động!              ║"
echo "║                                                  ║"
echo "║  Grafana:    http://localhost:3000               ║"
echo "║  InfluxDB:   http://localhost:8086               ║"
echo "║  MQTT:       localhost:1883                      ║"
echo "║                                                  ║"
echo "║  BƯỚC TIẾP THEO:                                ║"
echo "║  1. Mở QGroundControl → tự kết nối UDP 14550    ║"
echo "║  2. Mở http://localhost:3000 → Grafana           ║"
echo "║  3. Xem log fusion: tail -f Phase5_Operations/fusion.log ║"
echo "║  4. Xem log SITL:   tail -f Phase5_Operations/sitl.log   ║"
echo "╚══════════════════════════════════════════════════╝"
