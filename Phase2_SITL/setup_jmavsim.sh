#!/usr/bin/env bash
# ============================================================
# setup_jmavsim.sh — Tải và biên dịch jMAVSim 3D Simulator
# ============================================================
set -euo pipefail

echo "========================================"
echo "  Cài đặt & Biên dịch jMAVSim 3D"
echo "========================================"

if [ ! -d "$HOME/jMAVSim" ]; then
    echo "▶ Đang clone jMAVSim repository kèm submodules..."
    git clone --recursive https://github.com/PX4/jMAVSim.git "$HOME/jMAVSim"
else
    echo "▶ Cập nhật submodules cho jMAVSim..."
    cd "$HOME/jMAVSim"
    git submodule update --init --recursive
fi

echo "▶ Biên dịch jMAVSim bằng Ant..."
cd "$HOME/jMAVSim"
ant

echo ""
echo "✅ jMAVSim đã được biên dịch thành công!"
echo "   File JAR: ~/jMAVSim/out/production/jmavsim.jar"
