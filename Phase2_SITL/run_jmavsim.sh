#!/usr/bin/env bash
# ============================================================
# run_jmavsim.sh — Khởi động SITL ArduCopter + Cửa sổ 3D jMAVSim
# ============================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "========================================"
echo "  Khởi động Mô phỏng 3D jMAVSim + SITL"
echo "========================================"

# Tìm đường dẫn Java
JAVA_CMD=""
if [ -x "/opt/homebrew/Cellar/openjdk/26.0.1/bin/java" ]; then
    JAVA_CMD="/opt/homebrew/Cellar/openjdk/26.0.1/bin/java"
elif [ -x "/opt/homebrew/opt/openjdk@17/bin/java" ]; then
    JAVA_CMD="/opt/homebrew/opt/openjdk@17/bin/java"
elif java -version >/dev/null 2>&1; then
    JAVA_CMD="java"
fi

JMAVSIM_JAR="$HOME/jMAVSim/out/production/jmavsim.jar"

if [ -n "$JAVA_CMD" ] && [ -f "$JMAVSIM_JAR" ]; then
    echo "🎮 Đang mở cửa sổ 3D jMAVSim..."
    # Khởi chạy cửa sổ 3D jMAVSim chạy ngầm kết nối cổng telemetry 14550
    (cd "$HOME/jMAVSim" && "$JAVA_CMD" --add-exports java.desktop/sun.awt=ALL-UNNAMED -cp "$JMAVSIM_JAR:$HOME/jMAVSim/lib/*" me.drton.jmavsim.Simulator -udp 14560 > /tmp/jmavsim.log 2>&1 &)
    JMAVSIM_PID=$!
    echo "✅ Cửa sổ 3D jMAVSim đã mở (PID: $JMAVSIM_PID)"
    sleep 2
else
    echo "⚠️ Không tìm thấy jmavsim.jar hoặc Java. Đang chạy SITL chế độ tiêu chuẩn..."
fi

# Khởi chạy ArduCopter SITL
bash "$SCRIPT_DIR/run_sitl.sh"
