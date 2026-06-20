#!/usr/bin/env python3
"""
auto_takeoff.py — Tự động chuỗi lệnh bay SITL
  1. Kết nối MAVLink qua TCP 5763
  2. Đổi sang GUIDED mode
  3. ARM throttle (force)
  4. TAKEOFF lên độ cao chỉ định

Dùng:
  python3 auto_takeoff.py          # Takeoff mặc định 10m
  python3 auto_takeoff.py --alt 20 # Takeoff lên 20m
"""

import time
import sys
import argparse
from pymavlink import mavutil

# ── Cấu hình ──────────────────────────────────────────────
SITL_HOST    = "127.0.0.1"
SITL_PORT    = 5763          # tcpin port trong run_sitl.sh
TIMEOUT_MODE = 10            # Giây chờ xác nhận GUIDED mode
TIMEOUT_ARM  = 10            # Giây chờ xác nhận ARM
DEFAULT_ALT  = 10.0          # Mét


def log(tag: str, msg: str):
    print(f"[{tag}] {msg}", flush=True)


def connect(host: str, port: int) -> mavutil.mavfile:
    conn_str = f"tcp:{host}:{port}"
    log("CONNECT", f"Đang kết nối SITL tại {conn_str} ...")
    master = mavutil.mavlink_connection(conn_str)
    log("CONNECT", "Chờ heartbeat ...")
    master.wait_heartbeat(timeout=15)
    log("CONNECT", f"OK — System={master.target_system} Component={master.target_component}")
    return master


def set_guided(master: mavutil.mavfile) -> bool:
    """Gửi lệnh chuyển sang GUIDED, chờ xác nhận tối đa TIMEOUT_MODE giây."""
    log("MODE", "Gửi lệnh GUIDED ...")
    master.set_mode("GUIDED")

    deadline = time.time() + TIMEOUT_MODE
    while time.time() < deadline:
        hb = master.recv_match(type="HEARTBEAT", blocking=True, timeout=1.0)
        if hb and hb.custom_mode == 4:          # 4 = GUIDED trong ArduCopter
            log("MODE", "GUIDED mode xác nhận OK")
            return True
    log("MODE", "CAUTION: Không nhận được xác nhận GUIDED sau "
        f"{TIMEOUT_MODE}s — vẫn tiếp tục ARM (SITL thường vẫn OK).")
    return False


def arm_force(master: mavutil.mavfile) -> bool:
    """ARM với param2=21196 (bypass pre-arm checks trong SITL)."""
    log("ARM", "Gửi lệnh ARM (force) ...")
    master.mav.command_long_send(
        master.target_system,
        master.target_component,
        mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM,
        0,          # confirmation
        1,          # param1: 1 = ARM
        21196,      # param2: magic number force-arm
        0, 0, 0, 0, 0,
    )

    deadline = time.time() + TIMEOUT_ARM
    while time.time() < deadline:
        # Đọc COMMAND_ACK hoặc kiểm tra bit ARMED trong HEARTBEAT
        hb = master.recv_match(type="HEARTBEAT", blocking=True, timeout=1.0)
        if hb:
            armed = hb.base_mode & mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED
            if armed:
                log("ARM", "ARM thành công!")
                return True
    log("ARM", "CAUTION: Không xác nhận được ARM sau "
        f"{TIMEOUT_ARM}s — vẫn gửi TAKEOFF.")
    return False


def takeoff(master: mavutil.mavfile, altitude: float):
    """Gửi lệnh TAKEOFF lên độ cao altitude mét."""
    log("TAKEOFF", f"Gửi lệnh TAKEOFF lên {altitude}m ...")
    master.mav.command_long_send(
        master.target_system,
        master.target_component,
        mavutil.mavlink.MAV_CMD_NAV_TAKEOFF,
        0,
        0, 0, 0, 0,     # param 1-4 không dùng
        0, 0,            # lat, lon (0 = giữ vị trí hiện tại)
        float(altitude), # param7 = altitude
    )
    log("TAKEOFF", "Lệnh đã gửi. Drone đang cất cánh ...")


def main():
    parser = argparse.ArgumentParser(description="Auto GUIDED → ARM → TAKEOFF cho ArduPilot SITL")
    parser.add_argument("--alt", type=float, default=DEFAULT_ALT,
                        help=f"Độ cao cất cánh (mét), mặc định {DEFAULT_ALT}m")
    parser.add_argument("--host", type=str, default=SITL_HOST)
    parser.add_argument("--port", type=int, default=SITL_PORT)
    args = parser.parse_args()

    print("=" * 50)
    print("  auto_takeoff.py — Drone IoT SITL")
    print(f"  Mục tiêu: GUIDED → ARM → TAKEOFF {args.alt}m")
    print("=" * 50)

    # 1. Kết nối
    master = connect(args.host, args.port)

    # 2. GUIDED mode
    set_guided(master)
    time.sleep(0.5)

    # 3. ARM (force)
    arm_force(master)
    time.sleep(0.5)

    # 4. TAKEOFF
    takeoff(master, args.alt)

    # 5. Theo dõi độ cao trong 15 giây
    print()
    log("MONITOR", f"Theo dõi độ cao trong 15 giây (mục tiêu: {args.alt}m) ...")
    print("-" * 50)
    deadline = time.time() + 15
    while time.time() < deadline:
        msg = master.recv_match(type="GLOBAL_POSITION_INT", blocking=True, timeout=1.0)
        if msg:
            alt_m = msg.alt / 1000.0
            rel_m = msg.relative_alt / 1000.0
            print(f"  Altitude: {alt_m:.1f}m MSL | {rel_m:.1f}m AGL", flush=True)
    print("-" * 50)
    log("DONE", "Hoàn thành chuỗi cất cánh. Drone đang bay!")


if __name__ == "__main__":
    main()
