#!/usr/bin/env bash
# ============================================================
# setup.sh — macOS Phase 1: Dựng Docker server trung tâm
# Chạy 1 lần duy nhất để khởi tạo toàn bộ infrastructure
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "========================================"
echo "  Drone IoT — Phase 1 Docker Setup"
echo "  Platform: macOS Apple Silicon"
echo "========================================"

# ── Kiểm tra Docker đang chạy ─────────────────────────────
if ! docker info &>/dev/null; then
    echo "❌ Docker Desktop chưa chạy."
    echo "   → Mở Docker Desktop, chờ biểu tượng trên Menu Bar chuyển XANH."
    exit 1
fi
echo "✅ Docker Engine đang chạy."

# ── Khởi động containers ──────────────────────────────────
echo ""
echo "▶ Khởi động containers (mosquitto, influxdb, grafana)..."
cd "$SCRIPT_DIR"
docker-compose up -d

echo ""
echo "▶ Chờ InfluxDB khởi động hoàn toàn (15 giây)..."
sleep 15

# ── Kiểm tra 3 container đều Up ───────────────────────────
echo ""
echo "▶ Kiểm tra trạng thái containers:"
docker ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"

RUNNING=$(docker ps --filter "name=iot_" --filter "status=running" | grep -c "iot_" || true)
if [ "$RUNNING" -lt 3 ]; then
    echo ""
    echo "❌ Có container chưa Up. Xem log lỗi:"
    docker-compose logs --tail=20
    exit 1
fi
echo ""
echo "✅ Tất cả 3 container đang chạy."

# ── Lấy InfluxDB Token ────────────────────────────────────
echo ""
echo "▶ Lấy InfluxDB API Token..."
sleep 5  # Đợi influxdb init xong hoàn toàn
TOKEN=$(docker exec iot_db influx auth list \
    --user admin \
    --hide-headers 2>/dev/null \
    | awk '{print $4}' \
    | head -1)

if [ -z "$TOKEN" ]; then
    echo "⚠️  Không lấy được token tự động. Thử lấy thủ công:"
    echo "   docker exec iot_db influx auth list --user admin --hide-headers | awk '{print \$4}'"
else
    echo ""
    echo "╔══════════════════════════════════════════════════════╗"
    echo "║  InfluxDB Token (copy ngay, dán vào fusion.py):     ║"
    echo "╠══════════════════════════════════════════════════════╣"
    echo "║  $TOKEN"
    echo "╚══════════════════════════════════════════════════════╝"
    echo ""
    # Tự động ghi token vào file để dùng sau
    echo "$TOKEN" > "$SCRIPT_DIR/../Phase4_Fusion/.influx_token"
    echo "  → Token cũng được lưu tại: Phase4_Fusion/.influx_token"
fi

# ── Test MQTT broker ──────────────────────────────────────
echo ""
echo "▶ Test MQTT broker..."
# Subscribe ngầm trong background 3 giây
docker exec iot_mqtt mosquitto_sub -t "drone/test" -C 1 -W 3 &>/dev/null &
SUB_PID=$!
sleep 1
# Publish test message
docker exec iot_mqtt mosquitto_pub -t "drone/test" -m "hello_drone_iot"
wait $SUB_PID 2>/dev/null && echo "✅ MQTT broker hoạt động bình thường." \
    || echo "⚠️  MQTT test không nhận được message — thử kiểm tra thủ công."

echo ""
echo "========================================"
echo "  ✅ Phase 1 hoàn tất!"
echo "  Grafana: http://localhost:3000"
echo "  InfluxDB: http://localhost:8086"
echo "  MQTT Broker: localhost:1883"
echo "========================================"
