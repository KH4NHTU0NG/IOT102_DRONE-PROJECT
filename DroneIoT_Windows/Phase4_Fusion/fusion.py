"""
fusion.py — Phase 4: Data Fusion Gateway
Platform: Windows 10/11

Chức năng:
  - Nhận GPS ảo từ ArduPilot SITL (chạy trong WSL2) qua TCP 5760
  - Nhận dữ liệu cảm biến thật từ BW16 qua MQTT (Docker Desktop)
  - Gộp theo timestamp và ghi vào InfluxDB
  - Grafana đọc InfluxDB để hiển thị realtime

Bug fixes:
  #1  : on_connect dùng CallbackAPIVersion.VERSION2 (5-arg) — fix DeprecationWarning
  #2  : MAVLink có timeout + retry loop
  #3  : Kiểm tra TOKEN hợp lệ trước khi kết nối InfluxDB
  #11 : Reconnect MAVLink tự động khi mất kết nối
  #12 : Assertion TOKEN + error message rõ ràng

WINDOWS NOTE:
  - SITL chạy trong WSL2 → connect qua localhost (Windows tự bridge)
  - Docker Desktop chạy trên Windows → MQTT broker tại 127.0.0.1
  - Python chạy trực tiếp trên Windows (không cần WSL2)
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
# CẤU HÌNH — Chỉnh sửa phần này
# ══════════════════════════════════════════════════════════

INFLUX_URL    = "http://localhost:8086"
INFLUX_TOKEN  = "TOKEN_CUA_BAN"       # ← Paste token từ setup.bat
INFLUX_ORG    = "drone_org"
INFLUX_BUCKET = "drone_data"

MQTT_BROKER   = "127.0.0.1"          # Docker Desktop = localhost trên Windows
MQTT_PORT     = 1883
MQTT_TOPIC    = "drone/payload/sensors"

# WINDOWS + WSL2: SITL chạy trong WSL2 nhưng bind 0.0.0.0
# → Windows có thể connect qua localhost
SITL_HOST     = "127.0.0.1"
SITL_PORT     = 5760

# ══════════════════════════════════════════════════════════
# Bug fix #3 & #12: Validate TOKEN
# ══════════════════════════════════════════════════════════
def load_token() -> str:
    token = INFLUX_TOKEN

    # Thử đọc từ file .influx_token
    token_file = os.path.join(os.path.dirname(__file__), ".influx_token")
    if token == "TOKEN_CUA_BAN" and os.path.exists(token_file):
        with open(token_file) as f:
            token = f.read().strip()
        print(f"[TOKEN] Doc tu file: {token_file}")

    # Thử đọc từ environment variable
    if token == "TOKEN_CUA_BAN":
        token = os.environ.get("INFLUX_TOKEN", "TOKEN_CUA_BAN")

    if token == "TOKEN_CUA_BAN" or not token:
        print("=" * 60)
        print("[ERROR] INFLUX_TOKEN chua duoc set!")
        print("")
        print("  Lay token tu CMD:")
        print("  docker exec iot_db influx auth list ^")
        print("    --user admin --hide-headers")
        print("  (Copy cot thu 4 trong output)")
        print("")
        print("  Dan vao INFLUX_TOKEN trong fusion.py")
        print("  HOAC set environment variable truoc khi chay:")
        print("  set INFLUX_TOKEN=your_token_here")
        print("=" * 60)
        sys.exit(1)

    return token


# ══════════════════════════════════════════════════════════
# Thread-safe sensor data store
# ══════════════════════════════════════════════════════════
sensor_data: dict = {}
sensor_lock = threading.Lock()
sensor_received = threading.Event()


# ══════════════════════════════════════════════════════════
# MQTT Callbacks — Bug fix #1
# ══════════════════════════════════════════════════════════
def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print(f"[MQTT] Ket noi thanh cong {MQTT_BROKER}:{MQTT_PORT}")
        client.subscribe(MQTT_TOPIC)
        print(f"[MQTT] Dang lang nghe: {MQTT_TOPIC}")
    else:
        print(f"[MQTT] Ket noi that bai, code={reason_code}")
        print("       Kiem tra Docker container iot_mqtt dang chay.")


def on_message(client, userdata, msg):
    global sensor_data
    try:
        data = json.loads(msg.payload.decode("utf-8"))
        with sensor_lock:
            sensor_data = data
        sensor_received.set()
        print(f"[MQTT] Cam bien: temp={data.get('temp')}C  "
              f"humidity={data.get('humidity')}%  "
              f"co2={data.get('co2')}")
    except json.JSONDecodeError as e:
        print(f"[MQTT] JSON error: {e} - payload: {msg.payload}")
    except Exception as e:
        print(f"[MQTT] Loi: {e}")


def on_disconnect(client, userdata, flags, reason_code, properties):
    if reason_code != 0:
        print(f"[MQTT] Mat ket noi (code={reason_code}). Dang reconnect...")


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
        print("       Kiem tra Docker Desktop dang chay va container iot_mqtt Up.")
        sys.exit(1)

    threading.Thread(target=mqtt_client.loop_forever, daemon=True).start()
    return mqtt_client


# ══════════════════════════════════════════════════════════
# MAVLink — Bug fix #2 & #11
# ══════════════════════════════════════════════════════════
def connect_sitl(max_retries: int = 5) -> mavutil.mavfile:
    connection_str = f"tcp:{SITL_HOST}:{SITL_PORT}"

    for attempt in range(1, max_retries + 1):
        try:
            print(f"[SITL] Dang ket noi {connection_str} (lan {attempt}/{max_retries})...")
            master = mavutil.mavlink_connection(connection_str)
            master.wait_heartbeat(timeout=10)
            print(f"[SITL] Ket noi thanh cong! System ID={master.target_system}")
            return master
        except Exception as e:
            print(f"[SITL] Lan {attempt} that bai: {e}")
            if attempt < max_retries:
                wait = min(2 ** attempt, 30)
                print(f"[SITL] Thu lai sau {wait}s...")
                print(f"       Windows+WSL2: SITL phai bind 0.0.0.0 (xem run_sitl.ps1)")
                time.sleep(wait)
            else:
                print("[SITL] Khong the ket noi sau tat ca lan thu.")
                print(f"       Kiem tra WSL2 va port {SITL_PORT}:")
                print(f"       netstat -an | findstr {SITL_PORT}")
                sys.exit(1)


# ══════════════════════════════════════════════════════════
# Main
# ══════════════════════════════════════════════════════════
def main():
    print("=" * 60)
    print("  Drone IoT - fusion.py")
    print("  Platform: Windows 10/11")
    print("=" * 60)

    token = load_token()
    print(f"[TOKEN] OK (length={len(token)})")

    print(f"\n[INFLUX] Ket noi {INFLUX_URL}...")
    influx    = InfluxDBClient(url=INFLUX_URL, token=token, org=INFLUX_ORG)
    write_api = influx.write_api(write_options=SYNCHRONOUS)

    try:
        health = influx.health()
        if health.status != "pass":
            print(f"[INFLUX] InfluxDB khong healthy: {health.message}")
            sys.exit(1)
        print(f"[INFLUX] OK - version={health.version}")
    except Exception as e:
        print(f"[INFLUX] Khong ket noi duoc: {e}")
        print("         Kiem tra: docker ps | findstr iot_db")
        sys.exit(1)

    print("\n[MQTT] Khoi dong...")
    start_mqtt()

    print("\n[MQTT] Cho du lieu tu BW16...")
    print(f"       (Test: docker exec iot_mqtt mosquitto_pub -t {MQTT_TOPIC} "
          "-m \"{\\\"temp\\\":28.5,\\\"humidity\\\":65,\\\"co2\\\":412}\")")
    sensor_received.wait(timeout=30)

    print("\n[SITL] Khoi dong...")
    master = connect_sitl()

    print("\n" + "=" * 60)
    print("  Bat dau Fusion Loop - Ctrl+C de dung")
    print("=" * 60 + "\n")

    frames_written = 0
    frames_skipped = 0

    while True:
        try:
            msg = master.recv_match(
                type='GLOBAL_POSITION_INT',
                blocking=True,
                timeout=5
            )

            if msg is None:
                print("[SITL] Cho GPS... (SITL co the chua san sang)")
                time.sleep(1)
                continue

            with sensor_lock:
                snap = dict(sensor_data)

            if not snap:
                frames_skipped += 1
                if frames_skipped % 5 == 1:
                    print(f"[FUSION] Cho cam bien tu BW16... (skip={frames_skipped})")
                time.sleep(1)
                continue

            lat = msg.lat / 1e7
            lon = msg.lon / 1e7
            alt = msg.alt / 1000.0

            point = (
                Point("drone_telemetry")
                .field("latitude",    lat)
                .field("longitude",   lon)
                .field("altitude",    alt)
                .field("temperature", snap.get("temp", 0.0))
                .field("humidity",    snap.get("humidity", 0.0))
                .field("co2",         float(snap.get("co2", 0)))
                .field("wifi_rssi",   float(snap.get("rssi", 0)))
            )

            write_api.write(bucket=INFLUX_BUCKET, org=INFLUX_ORG, record=point)
            frames_written += 1

            print(f"[FUSION] #{frames_written:04d}  "
                  f"GPS: ({lat:.4f}, {lon:.4f}, {alt:.1f}m)  "
                  f"T={snap.get('temp', 0)}C  "
                  f"CO2={snap.get('co2', 0)}")

        except KeyboardInterrupt:
            print("\n[FUSION] Dung boi nguoi dung (Ctrl+C)")
            print(f"         Tong frames ghi: {frames_written}")
            break

        except ConnectionResetError:
            print("\n[SITL] Mat ket noi. Dang reconnect...")
            time.sleep(3)
            try:
                master = connect_sitl(max_retries=3)
            except SystemExit:
                print("[SITL] Khong the reconnect.")
                break

        except Exception as e:
            print(f"[LOOP] Loi: {type(e).__name__}: {e}")
            time.sleep(1)

    influx.close()
    print("[CLEANUP] Da dong ket noi InfluxDB.")


if __name__ == "__main__":
    main()
