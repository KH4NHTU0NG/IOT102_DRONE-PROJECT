#!/usr/bin/env bash
# ============================================================
# setup_venv.sh — macOS Phase 4: Tạo môi trường Python ảo
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "========================================"
echo "  Drone IoT — Phase 4 Python venv Setup"
echo "  Platform: macOS"
echo "========================================"

VENV_DIR="$SCRIPT_DIR/drone_env"

# Tạo venv nếu chưa có
if [ ! -d "$VENV_DIR" ]; then
    echo "▶ Tạo virtual environment..."
    python3 -m venv "$VENV_DIR"
    echo "✅ Tạo venv tại: $VENV_DIR"
else
    echo "✅ venv đã tồn tại: $VENV_DIR"
fi

# Kích hoạt và cài thư viện
echo ""
echo "▶ Kích hoạt venv và cài dependencies..."
source "$VENV_DIR/bin/activate"
pip install --upgrade pip --quiet
pip install -r "$SCRIPT_DIR/requirements.txt"

echo ""
echo "▶ Verify cài đặt:"
python3 -c "from pymavlink import mavutil; print('  ✅ pymavlink OK')"
python3 -c "import paho.mqtt.client as mqtt; print('  ✅ paho-mqtt OK')"
python3 -c "from influxdb_client import InfluxDBClient; print('  ✅ influxdb-client OK')"

echo ""
echo "========================================"
echo "  ✅ Phase 4 Setup hoàn tất!"
echo ""
echo "  Để chạy fusion.py:"
echo "  source $VENV_DIR/bin/activate"
echo "  python3 $SCRIPT_DIR/fusion.py"
echo "========================================"
