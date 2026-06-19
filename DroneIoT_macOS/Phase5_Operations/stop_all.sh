#!/usr/bin/env bash
# ============================================================
# stop_all.sh — macOS Phase 5: Dừng toàn bộ hệ thống
# ============================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "━━━ Drone IoT — Dừng toàn bộ hệ thống ━━━"
echo ""

# Dừng fusion.py
PID_FILE="$ROOT_DIR/Phase5_Operations/fusion.pid"
if [ -f "$PID_FILE" ]; then
    FUSION_PID=$(cat "$PID_FILE")
    if kill -0 "$FUSION_PID" 2>/dev/null; then
        kill "$FUSION_PID"
        echo "✅ Dừng fusion.py (PID=$FUSION_PID)"
    else
        echo "  fusion.py không còn chạy."
    fi
    rm -f "$PID_FILE"
else
    # Tìm và kill bằng tên
    pkill -f "fusion.py" 2>/dev/null && echo "✅ Dừng fusion.py" || \
        echo "  fusion.py không đang chạy."
fi

# Dừng SITL nếu còn
pkill -f "sim_vehicle.py" 2>/dev/null && echo "✅ Dừng SITL" || \
    echo "  SITL không đang chạy."

# Dừng Docker containers
echo ""
echo "▶ Dừng Docker containers..."
cd "$ROOT_DIR/Phase1_Docker"
docker-compose down
echo "✅ Docker containers đã dừng."

echo ""
echo "━━━ Hệ thống đã dừng hoàn toàn ━━━"
