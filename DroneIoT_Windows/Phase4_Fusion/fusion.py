"""
fusion.py — Phase 4: Data Fusion Gateway
Platform: Windows 10/11

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
INFLUX_TOKEN  = "SPSuc2iYUViMysgXOlYD61aYXaiarb7hBPfpHZBAWCknUphbdH4Vqa_C7VLEAp6622vkOXtg1W_yVx5TYG1h9A=="  # Sẽ được ghi đè bằng file .influx_token nếu có
INFLUX_ORG    = "drone_org"
INFLUX_BUCKET = "drone_data"

MQTT_BROKER   = "127.0.0.1"
MQTT_PORT     = 1883

SITL_HOST     = "127.0.0.1"
SITL_PORT     = 5763  # Cổng MAVProxy chuyển tiếp ra TCP

# ══════════════════════════════════════════════════════════
# Khởi tạo Shared State
# ══════════════════════════════════════════════════════════
gps_data = {}
sensor_data = {}
state_lock = threading.Lock()
master_lock = threading.Lock()  # Fix P-003: lock riêng cho master
sensor_received = threading.Event()

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
    
    if not token or token == "TOKEN_CUA_BAN":
        token = os.environ.get("INFLUX_TOKEN", "")
        
    if not token:
        print("[ERROR] INFLUX_TOKEN chưa được cấu hình!")
        sys.exit(1)
    return token

# ══════════════════════════════════════════════════════════
# MQTT Callbacks & Thread
# ══════════════════════════════════════════════════════════
def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print(f"[MQTT] ✅ Kết nối thành công broker {MQTT_BROKER}:{MQTT_PORT}")
        client.subscribe("drone/payload/sensors")
        client.subscribe("drone/control/flight")
        print("[MQTT] Đã subscribe: drone/payload/sensors & drone/control/flight")
    else:
        print(f"[MQTT] ❌ Kết nối thất bại, code={reason_code}")

def on_message(client, userdata, msg):
    global sensor_data, master
    topic = msg.topic
    try:
        payload_str = msg.payload.decode("utf-8")
        if topic == "drone/payload/sensors":
            data = json.loads(payload_str)
            with state_lock:
                sensor_data = data
            sensor_received.set()
        elif topic == "drone/control/flight":
            data = json.loads(payload_str)
            command = data.get("command")
            alt = data.get("alt", 10.0)
            print(f"[CMD] Nhận lệnh bay: {command} (alt={alt}m)")

            # Fix P-003: thread-safe read của master
            with master_lock:
                m = master

            if m is None:
                print("[MAVLINK] ⚠️  Chưa kết nối SITL. Bỏ qua lệnh.")
                return

            if command == "ARM":
                # Fix P-002: ARM cần dùng force arm (param2=21196) trong SITL
                m.mav.command_long_send(
                    m.target_system, m.target_component,
                    mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0,
                    1, 21196, 0, 0, 0, 0, 0
                )
                print("[MAVLINK] Sent ARM command (force)")

            elif command == "TAKEOFF":
                # Fix P-001: Chờ xác nhận mode GUIDED trước khi ARM
                print("[MAVLINK] Requesting GUIDED mode...")
                m.set_mode('GUIDED')

                # Chờ HEARTBEAT xác nhận mode change (tối đa 5s)
                guided_confirmed = False
                deadline = time.time() + 5.0
                while time.time() < deadline:
                    hb = m.recv_match(type='HEARTBEAT', blocking=True, timeout=1.0)
                    if hb and hb.custom_mode == 4:  # GUIDED = mode 4 trong ArduCopter
                        guided_confirmed = True
                        break

                if not guided_confirmed:
                    print("[MAVLINK] ⚠️ GUIDED mode không được xác nhận sau 5s. Vẫn tiếp tục...")
                else:
                    print("[MAVLINK] ✅ GUIDED mode đã xác nhận")

                # ARM (force arm trong SITL)
                m.mav.command_long_send(
                    m.target_system, m.target_component,
                    mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0,
                    1, 21196, 0, 0, 0, 0, 0
                )
                time.sleep(0.5)  # Chờ ARM ổn định

                # TAKEOFF
                m.mav.command_long_send(
                    m.target_system, m.target_component,
                    mavutil.mavlink.MAV_CMD_NAV_TAKEOFF, 0,
                    0, 0, 0, 0, 0, 0, float(alt)
                )
                print(f"[MAVLINK] Sent TAKEOFF command (alt={alt}m)")

            elif command == "LAND":
                m.set_mode('LAND')
                m.mav.command_long_send(
                    m.target_system, m.target_component,
                    mavutil.mavlink.MAV_CMD_NAV_LAND, 0,
                    0, 0, 0, 0, 0, 0, 0
                )
                print("[MAVLINK] Sent LAND command")

            elif command == "RTL":
                m.set_mode('RTL')
                m.mav.command_long_send(
                    m.target_system, m.target_component,
                    mavutil.mavlink.MAV_CMD_NAV_RETURN_TO_LAUNCH, 0,
                    0, 0, 0, 0, 0, 0, 0
                )
                print("[MAVLINK] Sent RTL command")

            elif command == "RESET_FLIGHT":
                # Fix W-003: Handler cho nút Reset Flight trên web
                print("[MAVLINK] Resetting to GUIDED mode...")
                m.set_mode('GUIDED')
                print("[MAVLINK] Mode reset sang GUIDED thành công")

    except Exception as e:
        print(f"[MQTT] Lỗi xử lý tin nhắn: {e}")

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
    except ConnectionRefusedError:
        print(f"[MQTT] Không kết nối được tới {MQTT_BROKER}:{MQTT_PORT}")
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
    # Fix P-003: Dùng master_lock khi đọc/ghi master
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
            msg = m.recv_match(type='GLOBAL_POSITION_INT', blocking=True, timeout=1.0)
            if msg is not None:
                with state_lock:
                    gps_data = {
                        "lat": msg.lat / 1e7,
                        "lon": msg.lon / 1e7,
                        "alt": msg.alt / 1000.0
                    }
        except Exception as e:
            print(f"[MAVLINK] Lỗi đọc gói tin: {e}. Đang reconnect...")
            with master_lock:
                master = None
            time.sleep(2)

# ══════════════════════════════════════════════════════════
# Main Program
# ══════════════════════════════════════════════════════════
def main():
    global master
    print("=" * 60)
    print("  Drone IoT — fusion.py")
    print("  Platform: Windows 10/11")
    print("=" * 60)

    token = load_token()
    print("[INFLUX] Khởi tạo Client...")
    influx = InfluxDBClient(url=INFLUX_URL, token=token, org=INFLUX_ORG)
    write_api = influx.write_api(write_options=SYNCHRONOUS)

    # Khởi động MQTT
    start_mqtt()

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

    influx.close()
    print("[CLEANUP] Đã đóng kết nối InfluxDB.")

if __name__ == "__main__":
    main()
