"""
fusion.py — Phase 4: Data Fusion Gateway
Platform: macOS Apple Silicon

Chức năng:
  - Nhận GPS ảo từ ArduPilot SITL qua TCP 5760 (MAVLink)
  - Nhận dữ liệu cảm biến thật từ BW16 qua MQTT
  - Gộp theo timestamp và ghi vào InfluxDB
  - Grafana đọc InfluxDB để hiển thị realtime

Bug fixes:
  #1  : on_connect dùng CallbackAPIVersion.VERSION2 (5-arg) — fix DeprecationWarning
  #2  : MAVLink có timeout + retry loop — không crash khi mất kết nối SITL
  #3  : Kiểm tra TOKEN hợp lệ trước khi kết nối InfluxDB
  #11 : Reconnect MAVLink tự động khi SITL bị disconnect giữa chừng
  #12 : Assertion TOKEN + error message rõ ràng
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
INFLUX_TOKEN  = "TOKEN_CUA_BAN"       # ← Paste token từ setup.sh hoặc .influx_token
INFLUX_ORG    = "drone_org"
INFLUX_BUCKET = "drone_data"

MQTT_BROKER   = "127.0.0.1"
MQTT_PORT     = 1883
MQTT_TOPIC    = "drone/payload/sensors"

SITL_HOST     = "127.0.0.1"
SITL_PORT     = 5760

# ══════════════════════════════════════════════════════════
# Bug fix #3 & #12: Validate TOKEN trước khi bắt đầu
# ══════════════════════════════════════════════════════════
def load_token() -> str:
    """Đọc token từ biến hoặc file .influx_token nếu chưa set."""
    token = INFLUX_TOKEN

    # Thử đọc từ file .influx_token (tự động tạo bởi setup.sh)
    token_file = os.path.join(os.path.dirname(__file__), ".influx_token")
    if token == "TOKEN_CUA_BAN" and os.path.exists(token_file):
        with open(token_file) as f:
            token = f.read().strip()
        print(f"[TOKEN] Đọc từ file: {token_file}")

    # Thử đọc từ environment variable
    if token == "TOKEN_CUA_BAN":
        token = os.environ.get("INFLUX_TOKEN", "TOKEN_CUA_BAN")

    if token == "TOKEN_CUA_BAN" or not token:
        print("=" * 60)
        print("❌ INFLUX_TOKEN chưa được set!")
        print("")
        print("  Cách lấy token:")
        print("  docker exec iot_db influx auth list \\")
        print("    --user admin --hide-headers | awk '{print $4}'")
        print("")
        print("  Sau đó:")
        print("  1. Dán token vào biến INFLUX_TOKEN trong fusion.py")
        print("  2. HOẶC export INFLUX_TOKEN='your_token' trước khi chạy")
        print("=" * 60)
        sys.exit(1)

    return token


# ══════════════════════════════════════════════════════════
# Thread-safe sensor data store
# ══════════════════════════════════════════════════════════
sensor_data: dict = {}
sensor_lock = threading.Lock()
sensor_received = threading.Event()  # Signal khi nhận được data đầu tiên


# ══════════════════════════════════════════════════════════
# MQTT Callbacks — Bug fix #1: dùng CallbackAPIVersion.VERSION2
# ══════════════════════════════════════════════════════════
def on_connect(client, userdata, flags, reason_code, properties):
    """Callback khi kết nối MQTT thành công/thất bại."""
    if reason_code == 0:
        print(f"[MQTT] ✅ Kết nối thành công broker {MQTT_BROKER}:{MQTT_PORT}")
        client.subscribe(MQTT_TOPIC)
        print(f"[MQTT] Đang lắng nghe topic: {MQTT_TOPIC}")
    else:
        print(f"[MQTT] ❌ Kết nối thất bại, code={reason_code}")
        print("       Kiểm tra Docker container iot_mqtt đang chạy.")


def on_message(client, userdata, msg):
    """Callback khi nhận được message từ BW16."""
    global sensor_data
    try:
        data = json.loads(msg.payload.decode("utf-8"))
        with sensor_lock:
            sensor_data = data
        sensor_received.set()  # Báo hiệu đã nhận data đầu tiên
        print(f"[MQTT] 📡 Cảm biến: temp={data.get('temp')}°C  "
              f"humidity={data.get('humidity')}%  "
              f"co2={data.get('co2')}  "
              f"rssi={data.get('rssi', 'N/A')}dBm")
    except json.JSONDecodeError as e:
        print(f"[MQTT] ⚠️  JSON parse error: {e} — payload: {msg.payload}")
    except Exception as e:
        print(f"[MQTT] ❌ Lỗi: {e}")


def on_disconnect(client, userdata, flags, reason_code, properties):
    """Callback khi mất kết nối MQTT — paho tự reconnect."""
    if reason_code != 0:
        print(f"[MQTT] ⚠️  Mất kết nối (code={reason_code}). Đang reconnect...")


def start_mqtt() -> mqtt.Client:
    """Khởi tạo và kết nối MQTT client."""
    # Bug fix #1: dùng CallbackAPIVersion.VERSION2
    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    mqtt_client.on_connect    = on_connect
    mqtt_client.on_message    = on_message
    mqtt_client.on_disconnect = on_disconnect

    # Tự động reconnect
    mqtt_client.reconnect_delay_set(min_delay=1, max_delay=30)

    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
    except ConnectionRefusedError:
        print(f"[MQTT] ❌ Không thể kết nối {MQTT_BROKER}:{MQTT_PORT}")
        print("       Chạy: docker-compose up -d để khởi động broker")
        sys.exit(1)

    # Chạy loop trong thread riêng (non-blocking)
    threading.Thread(target=mqtt_client.loop_forever, daemon=True).start()
    return mqtt_client


# ══════════════════════════════════════════════════════════
# MAVLink connection — Bug fix #2 & #11: reconnect tự động
# ══════════════════════════════════════════════════════════
def connect_sitl(max_retries: int = 5) -> mavutil.mavfile:
    """Kết nối đến SITL qua TCP, tự động retry nếu thất bại."""
    connection_str = f"tcp:{SITL_HOST}:{SITL_PORT}"

    for attempt in range(1, max_retries + 1):
        try:
            print(f"[SITL] Đang kết nối {connection_str} (lần {attempt}/{max_retries})...")
            master = mavutil.mavlink_connection(connection_str)
            master.wait_heartbeat(timeout=10)
            print(f"[SITL] ✅ Kết nối thành công! System ID={master.target_system}")
            return master
        except Exception as e:
            print(f"[SITL] ❌ Lần {attempt} thất bại: {e}")
            if attempt < max_retries:
                wait = min(2 ** attempt, 30)  # Exponential backoff
                print(f"[SITL] Thử lại sau {wait}s...")
                time.sleep(wait)
            else:
                print("[SITL] ❌ Không thể kết nối SITL sau tất cả lần thử.")
                print(f"       Kiểm tra SITL đang chạy và port {SITL_PORT} mở:")
                print(f"       lsof -i :{SITL_PORT}")
                sys.exit(1)


# ══════════════════════════════════════════════════════════
# Main Fusion Loop
# ══════════════════════════════════════════════════════════
def main():
    print("=" * 60)
    print("  Drone IoT — fusion.py")
    print("  Platform: macOS Apple Silicon")
    print("=" * 60)

    # Load & validate token
    token = load_token()
    print(f"[TOKEN] ✅ Token hợp lệ (length={len(token)})")

    # Kết nối InfluxDB
    print(f"\n[INFLUX] Kết nối {INFLUX_URL}...")
    influx    = InfluxDBClient(url=INFLUX_URL, token=token, org=INFLUX_ORG)
    write_api = influx.write_api(write_options=SYNCHRONOUS)

    # Verify InfluxDB connection
    try:
        health = influx.health()
        if health.status != "pass":
            print(f"[INFLUX] ❌ InfluxDB không healthy: {health.message}")
            sys.exit(1)
        print(f"[INFLUX] ✅ Kết nối OK — version={health.version}")
    except Exception as e:
        print(f"[INFLUX] ❌ Không kết nối được: {e}")
        print("         Kiểm tra: docker ps | grep iot_db")
        sys.exit(1)

    # Kết nối MQTT
    print("\n[MQTT] Khởi động...")
    start_mqtt()

    # Chờ nhận data đầu tiên từ BW16 (timeout 30s)
    print("\n[MQTT] Chờ dữ liệu từ BW16...")
    print("       (Nếu không có BW16, chạy test: docker exec iot_mqtt mosquitto_pub "
          f"-t {MQTT_TOPIC} -m '{{\"temp\":28.5,\"humidity\":65,\"co2\":412}}')")
    received = sensor_received.wait(timeout=30)
    if not received:
        print("[MQTT] ⚠️  Chưa nhận data sau 30s — tiếp tục nhưng sẽ skip frame cho đến khi có data")

    # Kết nối SITL
    print("\n[SITL] Khởi động...")
    master = connect_sitl()

    # ── Fusion Loop ─────────────────────────────────────────
    print("\n" + "=" * 60)
    print("  🚀 Bắt đầu Fusion Loop — Ctrl+C để dừng")
    print("=" * 60 + "\n")

    frames_written = 0
    frames_skipped = 0
    reconnect_attempts = 0

    while True:
        try:
            # Nhận GPS từ SITL
            msg = master.recv_match(
                type='GLOBAL_POSITION_INT',
                blocking=True,
                timeout=5
            )

            if msg is None:
                print("[SITL] ⏳ Chờ GPS... (SITL có thể chưa sẵn sàng hoàn toàn)")
                time.sleep(1)
                continue

            # Reset reconnect counter khi nhận được data
            reconnect_attempts = 0

            # Lấy snapshot sensor data (thread-safe)
            with sensor_lock:
                snap = dict(sensor_data)

            if not snap:
                frames_skipped += 1
                if frames_skipped % 10 == 1:
                    print(f"[FUSION] ⏳ BW16 chưa gửi data (skip={frames_skipped}) — ghi GPS với sensor=0")
                # Không skip — vẫn ghi GPS data với sensor mặc định = 0
                snap = {"temp": 0.0, "humidity": 0.0, "co2": 0, "rssi": 0}

            # Build InfluxDB Point
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

            # Ghi vào InfluxDB
            write_api.write(bucket=INFLUX_BUCKET, org=INFLUX_ORG, record=point)
            frames_written += 1

            print(f"[FUSION] ✅ #{frames_written:04d}  "
                  f"GPS: ({lat:.4f}, {lon:.4f}, {alt:.1f}m)  "
                  f"T={snap.get('temp', 0)}°C  "
                  f"CO2={snap.get('co2', 0)}")

        except KeyboardInterrupt:
            print("\n\n[FUSION] Đã dừng bởi người dùng (Ctrl+C)")
            print(f"         Tổng frames ghi: {frames_written}")
            print(f"         Tổng frames skip: {frames_skipped}")
            break

        # Bug fix #11: Reconnect MAVLink khi mất kết nối giữa chừng
        except ConnectionResetError:
            reconnect_attempts += 1
            print(f"\n[SITL] ❌ Mất kết nối MAVLink (lần {reconnect_attempts}). Đang reconnect...")
            time.sleep(3)
            try:
                master = connect_sitl(max_retries=3)
            except SystemExit:
                print("[SITL] Không thể reconnect. Kiểm tra SITL còn chạy không.")
                break

        except Exception as e:
            print(f"[LOOP] ❌ Lỗi không xác định: {type(e).__name__}: {e}")
            time.sleep(1)

    # Cleanup
    influx.close()
    print("[CLEANUP] Đã đóng kết nối InfluxDB.")


if __name__ == "__main__":
    main()
