#!/usr/bin/env bash
# ============================================================
# run_mock.sh — Chạy giả lập cảm biến (Không cần mạch thực tế)
#
# Cách chạy:
#   bash Phase5_Operations/run_mock.sh
# ============================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ ! -d "$ROOT_DIR/Phase4_Fusion/drone_env" ]; then
    echo "❌ Không tìm thấy môi trường ảo (drone_env)."
    echo "   Vui lòng chạy 'bash Phase4_Fusion/setup_venv.sh' trước."
    exit 1
fi

echo "🔄 Kích hoạt môi trường ảo Python..."
source "$ROOT_DIR/Phase4_Fusion/drone_env/bin/activate"

echo "🚀 Chạy Mock Sensor..."
python3 "$ROOT_DIR/Phase5_Operations/mock_sensor.py"
