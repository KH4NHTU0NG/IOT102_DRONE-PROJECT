#!/usr/bin/env bash
# ============================================================
# run_jmavsim.sh — Khởi động ArduPilot SITL kèm mô phỏng 3D jMAVSim
# ============================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bash "$SCRIPT_DIR/run_sitl.sh" --jmavsim
