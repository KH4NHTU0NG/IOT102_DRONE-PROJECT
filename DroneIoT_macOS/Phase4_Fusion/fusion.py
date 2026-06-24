"""
fusion.py — Phase 4: Data Fusion Gateway
Platform: macOS Apple Silicon

Chức năng:
  - Chạy luồng MQTT: Nhận sensor data từ BW16 và lệnh bay từ Web Control
  - Chạy luồng MAVLink: Nhận GPS từ SITL và gửi lệnh điều khiển bay
  - Luồng chính: Đồng bộ dữ liệu mỗi 1 giây và ghi vào InfluxDB
"""

from pymavlink import mavutil
import paho.mqtt.client as mqtt
from influxdb_client import InfluxDBClient, Point
from influxdb_client.client.write_api import SYNCHRONOUS
import json
import threading
import time
import sys
import os

# ══════════════════════════════════════════════════════════
# CẤU HÌNH
# ══════════════════════════════════════════════════════════
INFLUX_URL    = "http://localhost:8086"
INFLUX_TOKEN  = "YOUR_INFLUXDB_TOKEN_HERE"  # Sẽ được ghi đè bằng file .influx_token nếu có
INFLUX_ORG    = "drone_org"
INFLUX_BUCKET = "drone_data"

# ── Cấu hình MQTT ──
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT   = 1883

TOPIC_SENSORS     = "tuonghuy_drone/payload/sensors"
TOPIC_FLIGHT_CMD  = "tuonghuy_drone/control/flight"
TOPIC_PAYLOAD_CMD = "tuonghuy_drone/control/payload"  # Thêm: lắng nghe để log/forward
TOPIC_STATUS      = "tuonghuy_drone/status/gateway"   # Thêm: gửi ACK về web

SITL_HOST     = "127.0.0.1"
SITL_PORT     = 5763  # Cổng MAVProxy chuyển tiếp ra TCP

# ══════════════════════════════════════════════════════════
# Khởi tạo Shared State
# ══════════════════════════════════════════════════════════
gps_data = {}
sensor_data = {}
state_lock = threading.Lock()
master_lock = threading.Lock()  # Fix P-003: lock riêng cho master

master = None  # Global MAVLink connection

# ══════════════════════════════════════════════════════════
# Bug fix #3: Tải token động từ file .influx_token
# ══════════════════════════════════════════════════════════
def load_token() -> str:
    token = INFLUX_TOKEN
    token_file = os.path.join(os.path.dirname(__file__), ".influx_token")
    if os.path.exists(token_file):
        with open(token_file) as f:
            token = f.read().strip()
        print(f"[TOKEN] Đọc từ file: {token_file}")
    
    if not token or token in ("TOKEN_CUA_BAN", "YOUR_INFLUXDB_TOKEN_HERE"):
        token = os.environ.get("INFLUX_TOKEN", "")
        
    if not token:
        print("[ERROR] INFLUX_TOKEN chưa được cấu hình!")
        sys.exit(1)
    return token

# ══════════════════════════════════════════════════════════
# MQTT Callbacks & Thread
# ══════════════════════════════════════════════════════════
# Subscribe callback
def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print(f"[MQTT] ✅ Kết nối thành công broker {MQTT_BROKER}:{MQTT_PORT}")
        client.subscribe(TOPIC_SENSORS)
        client.subscribe(TOPIC_FLIGHT_CMD)
        client.subscribe(TOPIC_PAYLOAD_CMD)  # F-FIX: Lắng nghe payload để log
        print(f"[MQTT] Đã subscribe: {TOPIC_SENSORS}, {TOPIC_FLIGHT_CMD}, {TOPIC_PAYLOAD_CMD}")
    else:
        print(f"[MQTT] ❌ Kết nối thất bại, code={reason_code}")

# ── G-01: Các handler chạy trên thread riêng để tránh giữ master_lock quá lâu ──

def _handle_arm():
    """F-05 FIX: ARM cần set GUIDED mode trước, rồi mới ARM."""
    try:
        print("[MAVLINK] ARM: Requesting GUIDED mode first...")
        with master_lock:
            if master is None:
                print("[MAVLINK] ⚠️  Chưa kết nối SITL. Bỏ qua ARM.")
                return
            master.set_mode('GUIDED')
        time.sleep(0.5)  # Chờ mode change

        # Force ARM (param2=21196) trong SITL
        with master_lock:
            if master is None:
                return
            master.mav.command_long_send(
                master.target_system, master.target_component,
                mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0,
                1, 21196, 0, 0, 0, 0, 0
            )
        print("[MAVLINK] ✅ Sent ARM command (GUIDED + force)")
    except Exception as e:
        print(f"[MAVLINK] Lỗi xử lý ARM: {e}")


def _handle_disarm():
    """DISARM drone."""
    try:
        with master_lock:
            if master is None:
                print("[MAVLINK] ⚠️  Chưa kết nối SITL. Bỏ qua DISARM.")
                return
            master.mav.command_long_send(
                master.target_system, master.target_component,
                mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0,
                0, 0, 0, 0, 0, 0, 0  # param1=0 = DISARM
            )
        print("[MAVLINK] ✅ Sent DISARM command")
    except Exception as e:
        print(f"[MAVLINK] Lỗi xử lý DISARM: {e}")


def _handle_takeoff(alt):
    """G-01: Tách TAKEOFF ra thread riêng để không chặn mavlink_loop (~2.5s)."""
    try:
        # G-06: Giới hạn độ cao hợp lệ [1.0, 100.0]
        alt = max(1.0, min(float(alt), 100.0))

        # Bước 1: Chuyển sang GUIDED mode
        print("[MAVLINK] Requesting GUIDED mode...")
        with master_lock:
            if master is None:
                print("[MAVLINK] ⚠️  Chưa kết nối SITL. Bỏ qua lệnh TAKEOFF.")
                return
            master.set_mode('GUIDED')

        # Chờ HEARTBEAT xác nhận mode change (tối đa 2.0s)
        guided_confirmed = False
        deadline = time.time() + 2.0
        while time.time() < deadline:
            with master_lock:
                if master is None:
                    return
                hb = master.recv_match(type='HEARTBEAT', blocking=True, timeout=0.1)
            if hb and hb.custom_mode == 4:  # GUIDED = mode 4 trong ArduCopter
                guided_confirmed = True
                break

        if not guided_confirmed:
            print("[MAVLINK] ⚠️ GUIDED mode không được xác nhận sau 2s. Vẫn tiếp tục...")
        else:
            print("[MAVLINK] ✅ GUIDED mode đã xác nhận")

        # Bước 2: ARM (force arm trong SITL)
        with master_lock:
            if master is None:
                return
            master.mav.command_long_send(
                master.target_system, master.target_component,
                mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0,
                1, 21196, 0, 0, 0, 0, 0
            )
        time.sleep(0.5)  # Chờ ARM ổn định — không giữ lock

        # Bước 3: TAKEOFF
        with master_lock:
            if master is None:
                return
            master.mav.command_long_send(
                master.target_system, master.target_component,
                mavutil.mavlink.MAV_CMD_NAV_TAKEOFF, 0,
                0, 0, 0, 0, 0, 0, alt
            )
        print(f"[MAVLINK] Sent TAKEOFF command (alt={alt}m)")
    except Exception as e:
        print(f"[MAVLINK] Lỗi xử lý TAKEOFF: {e}")


def _handle_land():
    """G-01/G-05: Tách LAND ra thread riêng. set_mode('LAND') là đủ."""
    try:
        with master_lock:
            if master is None:
                print("[MAVLINK] ⚠️  Chưa kết nối SITL. Bỏ qua lệnh LAND.")
                return
            master.set_mode('LAND')
        print("[MAVLINK] Sent LAND command")
    except Exception as e:
        print(f"[MAVLINK] Lỗi xử lý LAND: {e}")


def _handle_rtl():
    """G-01/G-05: Tách RTL ra thread riêng. set_mode('RTL') là đủ."""
    try:
        with master_lock:
            if master is None:
                print("[MAVLINK] ⚠️  Chưa kết nối SITL. Bỏ qua lệnh RTL.")
                return
            master.set_mode('RTL')
        print("[MAVLINK] Sent RTL command")
    except Exception as e:
        print(f"[MAVLINK] Lỗi xử lý RTL: {e}")


def on_message(client, userdata, msg):
    global sensor_data
    topic = msg.topic
    try:
        payload_str = msg.payload.decode("utf-8")

        # ── Xử lý dữ liệu cảm biến từ BW16 ──
        if topic == TOPIC_SENSORS:
            data = json.loads(payload_str)
            with state_lock:
                sensor_data = data

        # ── Xử lý lệnh điều khiển bay → MAVLink ──
        elif topic == TOPIC_FLIGHT_CMD:
            data = json.loads(payload_str)
            command = data.get("command")
            alt = data.get("alt", 10.0)
            print(f"[CMD] Nhận lệnh bay: {command} (alt={alt}m)")

            if command == "ARM":
                # F-05 FIX: Tách ARM ra thread để set GUIDED trước
                threading.Thread(target=_handle_arm, daemon=True).start()

            elif command == "DISARM":
                threading.Thread(target=_handle_disarm, daemon=True).start()

            elif command == "TAKEOFF":
                threading.Thread(target=_handle_takeoff, args=(alt,), daemon=True).start()

            elif command == "LAND":
                threading.Thread(target=_handle_land, daemon=True).start()

            elif command == "RTL":
                threading.Thread(target=_handle_rtl, daemon=True).start()

            elif command == "RESET_FLIGHT":
                with master_lock:
                    if master is None:
                        print("[MAVLINK] ⚠️  Chưa kết nối SITL. Bỏ qua lệnh.")
                        return
                    print("[MAVLINK] Resetting to GUIDED mode...")
                    master.set_mode('GUIDED')
                print("[MAVLINK] Mode reset sang GUIDED thành công")

        # ── Log lệnh payload (LED/Buzzer/Servo) — forward tới BW16 qua broker ──
        elif topic == TOPIC_PAYLOAD_CMD:
            data = json.loads(payload_str)
            command = data.get("command", "?")
            print(f"[CMD] Payload command received: {command} (relay to BW16 via broker)")
            # Không cần forward — BW16 đã subscribe trực tiếp topic này
            # Gateway chỉ log để debug

    except Exception as e:
        print(f"[MQTT] Lỗi xử lý tin nhắn topic={topic}: {e}")

def on_disconnect(client, userdata, flags, reason_code, properties):
    if reason_code != 0:
        print(f"[MQTT] Mất kết nối broker, code={reason_code}. Đang tự kết nối lại...")

def start_mqtt() -> mqtt.Client:
    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    mqtt_client.on_connect    = on_connect
    mqtt_client.on_message    = on_message
    mqtt_client.on_disconnect = on_disconnect
    mqtt_client.reconnect_delay_set(min_delay=1, max_delay=30)
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
    except Exception as e:
        print(f"[MQTT] Không kết nối được tới {MQTT_BROKER}:{MQTT_PORT}. Chi tiết: {e}")
        sys.exit(1)
    
    threading.Thread(target=mqtt_client.loop_forever, daemon=True).start()
    return mqtt_client

# ══════════════════════════════════════════════════════════
# MAVLink Thread
# ══════════════════════════════════════════════════════════
def connect_sitl(max_retries: int = 5) -> mavutil.mavfile:
    connection_str = f"tcp:{SITL_HOST}:{SITL_PORT}"
    for attempt in range(1, max_retries + 1):
        try:
            print(f"[SITL] Đang kết nối {connection_str} (lần {attempt}/{max_retries})...")
            conn = mavutil.mavlink_connection(connection_str)
            conn.wait_heartbeat(timeout=10)
            print(f"[SITL] ✅ Kết nối thành công! System ID={conn.target_system}")
            return conn
        except Exception as e:
            print(f"[SITL] Lần {attempt} thất bại: {e}")
            if attempt < max_retries:
                wait = min(2 ** attempt, 30)
                print(f"[SITL] Thử lại sau {wait}s...")
                time.sleep(wait)
    raise ConnectionError("Không kết nối được SITL")

def mavlink_loop():
    global master, gps_data
    while True:
        with master_lock:
            m = master

        if m is None:
            try:
                new_conn = connect_sitl(max_retries=1)
                with master_lock:
                    master = new_conn
            except Exception:
                time.sleep(5)
                continue
            continue

        try:
            # Drain socket buffer to prevent latency buildup
            latest_msg = None
            with master_lock:
                if master is not None:
                    while True:
                        msg = master.recv_match(type='GLOBAL_POSITION_INT', blocking=False)
                        if msg is None:
                            break
                        latest_msg = msg

            if latest_msg is not None:
                with state_lock:
                    gps_data = {
                        "lat": latest_msg.lat / 1e7,
                        "lon": latest_msg.lon / 1e7,
                        "alt": latest_msg.relative_alt / 1000.0
                    }
            time.sleep(0.1)  # Release CPU and lock
        except Exception as e:
            print(f"[MAVLINK] Lỗi đọc gói tin: {e}. Đang reconnect...")
            with master_lock:
                if master is not None:
                    try:
                        master.close()
                    except Exception:
                        pass
                    master = None
            time.sleep(2)

# ══════════════════════════════════════════════════════════
# Main Program
# ══════════════════════════════════════════════════════════
def main():
    global master
    print("=" * 60)
    print("  Drone IoT — fusion.py")
    print("  Platform: macOS Apple Silicon")
    print("=" * 60)

    token = load_token()
    print("[INFLUX] Khởi tạo Client...")
    influx = InfluxDBClient(url=INFLUX_URL, token=token, org=INFLUX_ORG)
    write_api = influx.write_api(write_options=SYNCHRONOUS)

    # G-02: Lưu mqtt_client để cleanup khi thoát
    mqtt_client = start_mqtt()

    # Khởi động kết nối SITL lần đầu
    try:
        master = connect_sitl()
    except Exception:
        print("[SITL] ⚠️  Khởi động không có kết nối SITL. Sẽ thử lại trong background thread.")

    # Khởi động luồng MAVLink background
    threading.Thread(target=mavlink_loop, daemon=True).start()

    print("\n" + "=" * 60)
    print("  🚀 Bắt đầu Fusion Loop — Ctrl+C để dừng")
    print("=" * 60 + "\n")

    frames_written = 0

    try:
        while True:
            try:
                time.sleep(1.0)

                # Lấy snapshot thread-safe
                with state_lock:
                    gps = dict(gps_data)
                    sensor = dict(sensor_data)

                # Xử lý Graceful fallbacks
                lat = gps.get("lat", 0.0)
                lon = gps.get("lon", 0.0)
                alt = gps.get("alt", 0.0)

                # Fix P-005: Clip giá trị âm (DHT22 báo lỗi gửi -1.0) về 0 để UI hiển thị đúng
                temp = max(0.0, float(sensor.get("temp", 0.0)))
                hum  = max(0.0, float(sensor.get("humidity", 0.0)))
                co2  = max(0, int(sensor.get("co2", 0)))
                alert = sensor.get("alert", 0)
                distance = float(sensor.get("distance", -1.0))
                rssi  = sensor.get("rssi", 0)

                point = (
                    Point("drone_telemetry")
                    .field("latitude",    float(lat))
                    .field("longitude",   float(lon))
                    .field("altitude",    float(alt))
                    .field("temperature", float(temp))
                    .field("humidity",    float(hum))
                    .field("co2",         float(co2))
                    .field("alert",       float(alert))
                    .field("distance",    float(distance))
                    .field("wifi_rssi",   float(rssi))
                )

                try:
                    write_api.write(bucket=INFLUX_BUCKET, org=INFLUX_ORG, record=point)
                    frames_written += 1
                    print(f"[FUSION] ✅ #{frames_written:04d} "
                          f"GPS: ({lat:.5f}, {lon:.5f}, {alt:.1f}m) "
                          f"T={temp}°C, H={hum}%, CO2={co2}, Alert={alert}")
                except Exception as db_err:
                    print(f"[INFLUX] ❌ Ghi DB thất bại: {db_err}")

            except KeyboardInterrupt:
                print("\n\n[FUSION] Đã dừng bởi người dùng (Ctrl+C)")
                break
            except Exception as e:
                print(f"[LOOP] Lỗi: {e}")
    finally:
        # G-03: Cleanup tất cả tài nguyên khi thoát
        influx.close()
        print("[CLEANUP] Đã đóng kết nối InfluxDB.")
        if mqtt_client:
            mqtt_client.loop_stop()
            mqtt_client.disconnect()
            print('[MQTT] Da ngat ket noi.')
        if master:
            master.close()
            print('[MAVLink] Da dong ket noi.')

if __name__ == "__main__":
    main()
