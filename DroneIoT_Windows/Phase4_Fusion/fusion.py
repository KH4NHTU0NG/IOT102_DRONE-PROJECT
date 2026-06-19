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
INFLUX_TOKEN  = "TOKEN_CUA_BAN"  # Sẽ được ghi đè bằng file .influx_token nếu có
INFLUX_ORG    = "drone_org"
INFLUX_BUCKET = "drone_data"

MQTT_BROKER   = "127.0.0.1"
MQTT_PORT     = 1883

SITL_HOST     = "127.0.0.1"
SITL_PORT     = 5763  # Cổng MAVProxy chuyển tiếp ra TCP từ WSL2

# ══════════════════════════════════════════════════════════
# Khởi tạo Shared State
# ══════════════════════════════════════════════════════════
gps_data = {}
sensor_data = {}
state_lock = threading.Lock()
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
        print(f"[TOKEN] Doc tu file: {token_file}")
    
    if not token or token == "TOKEN_CUA_BAN":
        token = os.environ.get("INFLUX_TOKEN", "")
        
    if not token:
        print("[ERROR] INFLUX_TOKEN chua duoc cau hinh!")
        sys.exit(1)
    return token

# ══════════════════════════════════════════════════════════
# MQTT Callbacks & Thread
# ══════════════════════════════════════════════════════════
def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print(f"[MQTT] ✅ Ket noi thanh cong broker {MQTT_BROKER}:{MQTT_PORT}")
        client.subscribe("drone/payload/sensors")
        client.subscribe("drone/control/flight")
        print("[MQTT] Da subscribe: drone/payload/sensors & drone/control/flight")
    else:
        print(f"[MQTT] ❌ Ket noi that bai, code={reason_code}")

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
            print(f"[CMD] Nhan lenh bay: {command} (alt={alt}m)")
            
            if master is None:
                print("[MAVLINK] ⚠️  Chua ket noi SITL. Bo qua lenh.")
                return
                
            if command == "ARM":
                master.mav.command_long_send(
                    master.target_system, master.target_component,
                    mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0,
                    1, 0, 0, 0, 0, 0, 0
                )
                print("[MAVLINK] Sent ARM command")
            elif command == "TAKEOFF":
                master.set_mode('GUIDED')
                time.sleep(0.2)
                master.mav.command_long_send(
                    master.target_system, master.target_component,
                    mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0,
                    1, 0, 0, 0, 0, 0, 0
                )
                time.sleep(0.2)
                master.mav.command_long_send(
                    master.target_system, master.target_component,
                    mavutil.mavlink.MAV_CMD_NAV_TAKEOFF, 0,
                    0, 0, 0, 0, 0, 0, float(alt)
                )
                print(f"[MAVLINK] Sent TAKEOFF command (alt={alt}m)")
            elif command == "LAND":
                master.set_mode('LAND')
                master.mav.command_long_send(
                    master.target_system, master.target_component,
                    mavutil.mavlink.MAV_CMD_NAV_LAND, 0,
                    0, 0, 0, 0, 0, 0, 0
                )
                print("[MAVLINK] Sent LAND command")
            elif command == "RTL":
                master.set_mode('RTL')
                master.mav.command_long_send(
                    master.target_system, master.target_component,
                    mavutil.mavlink.MAV_CMD_NAV_RETURN_TO_LAUNCH, 0,
                    0, 0, 0, 0, 0, 0, 0
                )
                print("[MAVLINK] Sent RTL command")
    except Exception as e:
        print(f"[MQTT] Loi xu ly tin nhan: {e}")

def on_disconnect(client, userdata, flags, reason_code, properties):
    if reason_code != 0:
        print(f"[MQTT] Mat ket noi broker, code={reason_code}. Dang tu reconnect...")

def start_mqtt() -> mqtt.Client:
    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    mqtt_client.on_connect    = on_connect
    mqtt_client.on_message    = on_message
    mqtt_client.on_disconnect = on_disconnect
    mqtt_client.reconnect_delay_set(min_delay=1, max_delay=30)
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
    except ConnectionRefusedError:
        print(f"[MQTT] Khong the ket noi {MQTT_BROKER}:{MQTT_PORT}")
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
            print(f"[SITL] Dang ket noi {connection_str} (lan {attempt}/{max_retries})...")
            conn = mavutil.mavlink_connection(connection_str)
            conn.wait_heartbeat(timeout=10)
            print(f"[SITL] ✅ Ket noi thanh cong! System ID={conn.target_system}")
            return conn
        except Exception as e:
            print(f"[SITL] Lan {attempt} that bai: {e}")
            if attempt < max_retries:
                wait = min(2 ** attempt, 30)
                print(f"[SITL] Thu lai sau {wait}s...")
                time.sleep(wait)
    raise ConnectionError("Khong the ket noi SITL")

def mavlink_loop():
    global master, gps_data
    while True:
        if master is None:
            try:
                master = connect_sitl(max_retries=1)
            except Exception:
                time.sleep(5)
                continue
        try:
            msg = master.recv_match(type='GLOBAL_POSITION_INT', blocking=True, timeout=1.0)
            if msg is not None:
                with state_lock:
                    gps_data = {
                        "lat": msg.lat / 1e7,
                        "lon": msg.lon / 1e7,
                        "alt": msg.alt / 1000.0
                    }
        except Exception as e:
            print(f"[MAVLINK] Loi doc goi tin: {e}. Dang reconnect...")
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
    print("[INFLUX] Khoi tao Client...")
    influx = InfluxDBClient(url=INFLUX_URL, token=token, org=INFLUX_ORG)
    write_api = influx.write_api(write_options=SYNCHRONOUS)

    # Khởi động MQTT
    start_mqtt()

    # Khởi động kết nối SITL
    try:
        master = connect_sitl()
    except Exception:
        print("[SITL] ⚠️  Khong co ket noi SITL luc khoi chay. Se thu lai trong background thread.")

    # Khởi động luồng MAVLink
    threading.Thread(target=mavlink_loop, daemon=True).start()

    print("\n" + "=" * 60)
    print("  🚀 Bat dau Fusion Loop — Ctrl+C de dung")
    print("=" * 60 + "\n")

    frames_written = 0

    while True:
        try:
            time.sleep(1.0)

            with state_lock:
                gps = dict(gps_data)
                sensor = dict(sensor_data)

            lat = gps.get("lat", 0.0)
            lon = gps.get("lon", 0.0)
            alt = gps.get("alt", 0.0)

            temp = sensor.get("temp", 0.0)
            hum = sensor.get("humidity", 0.0)
            co2 = sensor.get("co2", 0)
            alert = sensor.get("alert", 0)
            rssi = sensor.get("rssi", 0)

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
                      f"GPS: ({lat:.6f}, {lon:.6f}, {alt:.1f}m) "
                      f"T={temp}C, H={hum}%, CO2={co2}, Alert={alert}")
            except Exception as db_err:
                print(f"[INFLUX] ❌ Ghi DB that bai: {db_err}")

        except KeyboardInterrupt:
            print("\n\n[FUSION] Dung boi nguoi dung (Ctrl+C)")
            break
        except Exception as e:
            print(f"[LOOP] Loi: {e}")

    influx.close()
    print("[CLEANUP] Da dong ket noi InfluxDB.")

if __name__ == "__main__":
    main()
