"""
fusion.py — Phase 4: Data Fusion Gateway
MQTT (BW16 sensors + Web control) ↔ MAVLink (SITL) ↔ InfluxDB
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

# --- Config ---
INFLUX_URL    = "http://localhost:8086"
INFLUX_TOKEN  = "YOUR_INFLUXDB_TOKEN_HERE"
INFLUX_ORG    = "drone_org"
INFLUX_BUCKET = "drone_data"

MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT   = 1883

TOPIC_SENSORS     = "iot102_drone/payload/sensors"
TOPIC_FLIGHT_CMD  = "iot102_drone/control/flight"
TOPIC_PAYLOAD_CMD = "iot102_drone/control/payload"

SITL_HOST = "127.0.0.1"
SITL_PORT = 5763

# --- Shared State ---
gps_data = {}
sensor_data = {}
state_lock = threading.Lock()
master_lock = threading.Lock()

master = None  # Global MAVLink connection


def load_token() -> str:
    """Tải token từ file .influx_token hoặc biến môi trường."""
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


# --- MAVLink Command Handlers ---

def _handle_mode(mode_name):
    """Chuyển flight mode (LAND, RTL, GUIDED, ...)."""
    try:
        with master_lock:
            if master is None:
                print(f"[MAVLINK] ⚠️  Chưa kết nối SITL. Bỏ qua {mode_name}.")
                return
            master.set_mode(mode_name)
        print(f"[MAVLINK] ✅ Sent {mode_name} command")
    except Exception as e:
        print(f"[MAVLINK] Lỗi {mode_name}: {e}")


def _handle_arm():
    """Set GUIDED → force ARM."""
    try:
        print("[MAVLINK] ARM: Requesting GUIDED mode first...")
        with master_lock:
            if master is None:
                print("[MAVLINK] ⚠️  Chưa kết nối SITL. Bỏ qua ARM.")
                return
            master.set_mode('GUIDED')
        time.sleep(0.5)

        with master_lock:
            if master is None:
                return
            master.mav.command_long_send(
                master.target_system, master.target_component,
                mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0,
                1, 21196, 0, 0, 0, 0, 0  # force ARM
            )
        print("[MAVLINK] ✅ Sent ARM command (GUIDED + force)")
    except Exception as e:
        print(f"[MAVLINK] Lỗi ARM: {e}")


def _handle_disarm():
    """Gửi lệnh DISARM."""
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
        print(f"[MAVLINK] Lỗi DISARM: {e}")


def _handle_takeoff(alt):
    """GUIDED → ARM → TAKEOFF."""
    try:
        alt = max(1.0, min(float(alt), 100.0))

        # Bước 1: GUIDED mode
        print("[MAVLINK] Requesting GUIDED mode...")
        with master_lock:
            if master is None:
                print("[MAVLINK] ⚠️  Chưa kết nối SITL. Bỏ qua TAKEOFF.")
                return
            master.set_mode('GUIDED')
        time.sleep(0.5)

        # Bước 2: Force ARM
        with master_lock:
            if master is None:
                return
            master.mav.command_long_send(
                master.target_system, master.target_component,
                mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0,
                1, 21196, 0, 0, 0, 0, 0
            )
        time.sleep(0.5)

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
        print(f"[MAVLINK] Lỗi TAKEOFF: {e}")


# --- MQTT Callbacks ---

def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print(f"[MQTT] ✅ Connected to {MQTT_BROKER}:{MQTT_PORT}")
        client.subscribe(TOPIC_SENSORS)
        client.subscribe(TOPIC_FLIGHT_CMD)
        client.subscribe(TOPIC_PAYLOAD_CMD)
        print(f"[MQTT] Subscribed: {TOPIC_SENSORS}, {TOPIC_FLIGHT_CMD}, {TOPIC_PAYLOAD_CMD}")
    else:
        print(f"[MQTT] ❌ Connection failed, code={reason_code}")


def on_message(client, userdata, msg):
    global sensor_data
    topic = msg.topic
    try:
        payload_str = msg.payload.decode("utf-8")

        if topic == TOPIC_SENSORS:  # Sensor data từ BW16
            data = json.loads(payload_str)
            with state_lock:
                sensor_data = data

        elif topic == TOPIC_FLIGHT_CMD:  # Lệnh bay → MAVLink
            data = json.loads(payload_str)
            command = data.get("command")
            alt = data.get("alt", 10.0)
            print(f"[CMD] Nhận lệnh: {command} (alt={alt}m)")

            if command == "ARM":
                threading.Thread(target=_handle_arm, daemon=True).start()
            elif command == "DISARM":
                threading.Thread(target=_handle_disarm, daemon=True).start()
            elif command == "TAKEOFF":
                threading.Thread(target=_handle_takeoff, args=(alt,), daemon=True).start()
            elif command == "LAND":
                threading.Thread(target=_handle_mode, args=("LAND",), daemon=True).start()
            elif command == "RTL":
                threading.Thread(target=_handle_mode, args=("RTL",), daemon=True).start()
            elif command == "RESET_FLIGHT":
                _handle_mode("GUIDED")
                print("[MAVLINK] Mode reset sang GUIDED")

        elif topic == TOPIC_PAYLOAD_CMD:  # Log payload command (LED/Buzzer/Servo)
            data = json.loads(payload_str)
            command = data.get("command", "?")
            print(f"[CMD] Payload: {command} (BW16 subscribes directly)")

    except Exception as e:
        print(f"[MQTT] Lỗi xử lý topic={topic}: {e}")


def on_disconnect(client, userdata, flags, reason_code, properties):
    if reason_code != 0:
        print(f"[MQTT] Disconnected, code={reason_code}. Auto-reconnecting...")


def start_mqtt() -> mqtt.Client:
    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    mqtt_client.on_connect    = on_connect
    mqtt_client.on_message    = on_message
    mqtt_client.on_disconnect = on_disconnect
    mqtt_client.reconnect_delay_set(min_delay=1, max_delay=30)
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
    except Exception as e:
        print(f"[MQTT] Không kết nối được {MQTT_BROKER}:{MQTT_PORT}: {e}")
        sys.exit(1)

    threading.Thread(target=mqtt_client.loop_forever, daemon=True).start()
    return mqtt_client


# --- MAVLink Thread ---

def connect_sitl(max_retries: int = 5) -> mavutil.mavfile:
    connection_str = f"tcp:{SITL_HOST}:{SITL_PORT}"
    for attempt in range(1, max_retries + 1):
        try:
            print(f"[SITL] Connecting {connection_str} ({attempt}/{max_retries})...")
            conn = mavutil.mavlink_connection(connection_str)
            conn.wait_heartbeat(timeout=10)
            print(f"[SITL] ✅ Connected! System ID={conn.target_system}")
            return conn
        except Exception as e:
            print(f"[SITL] Attempt {attempt} failed: {e}")
            if attempt < max_retries:
                wait = min(2 ** attempt, 30)
                print(f"[SITL] Retry in {wait}s...")
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
            # Drain buffer to get latest GPS
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
            time.sleep(0.1)
        except Exception as e:
            print(f"[MAVLINK] Lỗi đọc GPS: {e}. Reconnecting...")
            with master_lock:
                if master is not None:
                    try:
                        master.close()
                    except Exception:
                        pass
                    master = None
            time.sleep(2)


# --- Main ---

def main():
    global master
    print("=" * 60)
    print("  Drone IoT — fusion.py")
    print("=" * 60)

    token = load_token()
    print("[INFLUX] Khởi tạo Client...")
    influx = InfluxDBClient(url=INFLUX_URL, token=token, org=INFLUX_ORG)
    write_api = influx.write_api(write_options=SYNCHRONOUS)

    mqtt_client = start_mqtt()

    try:
        master = connect_sitl()
    except Exception:
        print("[SITL] ⚠️  Không có kết nối SITL. Sẽ thử lại trong background.")

    threading.Thread(target=mavlink_loop, daemon=True).start()

    print(f"\n{'=' * 60}")
    print("  🚀 Fusion Loop — Ctrl+C để dừng")
    print(f"{'=' * 60}\n")

    frames_written = 0

    try:
        while True:
            try:
                time.sleep(1.0)

                with state_lock:
                    gps = dict(gps_data)
                    sensor = dict(sensor_data)

                lat = gps.get("lat", 0.0)
                lon = gps.get("lon", 0.0)
                alt = gps.get("alt", 0.0)

                # Clip giá trị âm (DHT22 báo lỗi gửi -1.0)
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
        influx.close()
        print("[CLEANUP] Đã đóng InfluxDB.")
        if mqtt_client:
            mqtt_client.loop_stop()
            mqtt_client.disconnect()
            print("[MQTT] Disconnected.")
        if master:
            master.close()
            print("[MAVLINK] Disconnected.")

if __name__ == "__main__":
    main()
