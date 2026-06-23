#!/usr/bin/env bash
# ============================================================
# start_all.sh — macOS Phase 5: Khởi động toàn bộ hệ thống
# Tuân theo đúng thứ tự khởi động đã kiểm chứng
# ============================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "╔══════════════════════════════════════════════════╗"
echo "║       Drone IoT — Start All (macOS)              ║"
echo "║  Thứ tự: Docker → BW16 → SITL → Fusion → QGC   ║"
echo "╚══════════════════════════════════════════════════╝"
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
echo ""
read -r -p "  BW16 đã kết nối WiFi chưa? [y/N]: " bw16_ready
if [[ "$bw16_ready" != "y" && "$bw16_ready" != "Y" ]]; then
    echo "  ⚠️  Tiếp tục — fusion.py sẽ chờ data từ BW16."
fi

# ── Bước 3: SITL (mở Terminal mới) ────────────────────────
echo ""
echo "━━━ [3/4] Khởi động SITL ━━━"
echo "  → Mở Terminal MỚI và chạy:"
echo "     cd ../Phase2_SITL && bash run_sitl.sh"
echo ""
read -r -p "  Chờ SITL hiện 'MAV>' rồi nhấn Enter để tiếp tục..."

# Verify port 5763
if ! lsof -i :5763 &>/dev/null; then
    echo "⚠️  Port 5763 chưa mở — SITL có thể chưa sẵn sàng."
    echo "   Đợi thêm 30 giây cho SITL build xong."
    sleep 30
fi
echo "✅ Port 5763 đang mở."

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
echo "║  3. Xem log: tail -f Phase5_Operations/fusion.log║"
echo "╚══════════════════════════════════════════════════╝"
