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

MQTT_BROKER = "broker.emqx.io"
MQTT_PORT   = 1883

TOPIC_SENSORS     = "iot102_drone/payload/sensors"
TOPIC_FLIGHT_CMD  = "iot102_drone/control/flight"
TOPIC_PAYLOAD_CMD = "iot102_drone/control/payload"
TOPIC_MOTOR_DATA  = "iot102_drone/telemetry/motors"
TOPIC_WEATHER_CMD = "iot102_drone/control/weather"
TOPIC_HEARTBEAT   = "iot102_drone/control/heartbeat"
TOPIC_MISSION_CMD = "iot102_drone/control/mission"
TOPIC_SIM_CMD     = "iot102_drone/control/sim_param"
TOPIC_ATTITUDE    = "iot102_drone/telemetry/attitude"
TOPIC_GPS         = "iot102_drone/telemetry/gps"
TOPIC_STATUS      = "iot102_drone/telemetry/status"  # [FIX] Phản hồi trạng thái bay về Web

SITL_HOST = "127.0.0.1"
SITL_PORT = 5763  # Restore to 5763 for dedicated telemetry stream

FAILSAFE_TIMEOUT = 15  # [NEW] Giây không có heartbeat → RTL

# --- Shared State ---
gps_data   = {}
sensor_data = {}
motor_data  = {"m1": 1000, "m2": 1000, "m3": 1000, "m4": 1000}
state_lock  = threading.Lock()
master_lock = threading.Lock()

last_heartbeat_time = time.time()  # [NEW] Theo dõi thời gian ping cuối cùng

master  = None   # Global MAVLink connection
mqtt_pub = None  # Global MQTT client ref

# --- Downstream SITL Flight State for OLED ---
current_mode = "DISCONN"
current_armed = False
current_alt = 0.0
current_spd = 0.0
current_batt = 12.6
current_wind = 0.0
current_fence_enabled = 0

def get_distance_meters(lat1, lon1, lat2, lon2):
    """Tính khoảng cách Haversine giữa 2 tọa độ GPS (m)."""
    import math
    R = 6371000.0
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = math.sin(dlat/2)**2 + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2)) * math.sin(dlon/2)**2
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1-a))
    return R * c


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

def _publish_status(status: str, detail: str = ""):
    """Phát trạng thái bay về Web Dashboard qua MQTT."""
    if mqtt_pub is None:
        return
    try:
        mqtt_pub.publish(TOPIC_STATUS, json.dumps({
            "status": status,
            "detail": detail,
            "ts": time.time()
        }))
    except Exception:
        pass


def _wait_for_ack(command_id: int, timeout: float = 3.0) -> bool:
    """Chờ COMMAND_ACK từ SITL cho command_id cụ thể."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        with master_lock:
            if master is None:
                return False
            msg = master.recv_match(type='COMMAND_ACK', blocking=False)
        if msg and msg.command == command_id:
            return msg.result == mavutil.mavlink.MAV_RESULT_ACCEPTED
        time.sleep(0.05)
    return False  # Timeout


def _wait_mode_set(target_mode: str, timeout: float = 4.0) -> bool:
    """Chờ HEARTBEAT xác nhận drone đã chuyển sang mode mong muốn."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        with master_lock:
            if master is None:
                return False
            hb = master.recv_match(type='HEARTBEAT', blocking=False)
        if hb:
            mode_map = mavutil.mode_string_v10(hb)
            if mode_map == target_mode:
                return True
        time.sleep(0.1)
    return False


def _set_mode_with_retry(mode_name: str, retries: int = 3) -> bool:
    """Chuyển mode với retry, trả về True nếu thành công."""
    for i in range(retries):
        try:
            with master_lock:
                if master is None:
                    return False
                master.set_mode(mode_name)
            time.sleep(0.3)
            # Verify
            with master_lock:
                if master is None:
                    return False
                hb = master.recv_match(type='HEARTBEAT', blocking=True, timeout=2)
            if hb:
                actual = mavutil.mode_string_v10(hb)
                if actual == mode_name:
                    return True
        except Exception as e:
            print(f"[MAVLINK] Mode retry {i+1}: {e}")
        time.sleep(0.5)
    return False


def _handle_mode(mode_name):
    """Chuyển flight mode với ACK verification."""
    try:
        with master_lock:
            if master is None:
                _publish_status("ERROR", f"SITL chưa kết nối")
                return
        print(f"[MAVLINK] Requesting mode: {mode_name}")
        _publish_status("BUSY", f"Chuyển mode → {mode_name}")
        ok = _set_mode_with_retry(mode_name)
        if ok:
            print(f"[MAVLINK] ✅ Mode confirmed: {mode_name}")
            _publish_status("OK", f"Mode: {mode_name}")
        else:
            print(f"[MAVLINK] ⚠️  Mode {mode_name} không xác nhận được (timeout)")
            _publish_status("WARN", f"Mode {mode_name} chưa xác nhận")
    except Exception as e:
        print(f"[MAVLINK] Lỗi mode {mode_name}: {e}")
        _publish_status("ERROR", str(e))


def _handle_arm():
    """GUIDED → ARM với retry và phản hồi trạng thái."""
    try:
        with master_lock:
            if master is None:
                _publish_status("ERROR", "SITL chưa kết nối")
                return

        _publish_status("BUSY", "Đang ARM...")
        print("[MAVLINK] ARM: Chuyển GUIDED...")
        _set_mode_with_retry('GUIDED')
        time.sleep(0.3)

        for attempt in range(3):
            with master_lock:
                if master is None:
                    return
                master.mav.command_long_send(
                    master.target_system, master.target_component,
                    mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0,
                    1, 21196, 0, 0, 0, 0, 0
                )
            ok = _wait_for_ack(mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, timeout=2.0)
            if ok:
                print("[MAVLINK] ✅ ARM confirmed")
                _publish_status("ARMED", "Drone đã ARM thành công")
                return
            print(f"[MAVLINK] ARM retry {attempt+1}...")
            time.sleep(0.5)

        _publish_status("ERROR", "ARM thất bại sau 3 lần thử")
        print("[MAVLINK] ❌ ARM failed")
    except Exception as e:
        print(f"[MAVLINK] Lỗi ARM: {e}")
        _publish_status("ERROR", str(e))


def _handle_disarm():
    """DISARM với ACK verification."""
    try:
        with master_lock:
            if master is None:
                _publish_status("ERROR", "SITL chưa kết nối")
                return
        _publish_status("BUSY", "Đang DISARM...")
        with master_lock:
            master.mav.command_long_send(
                master.target_system, master.target_component,
                mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0,
                0, 21196, 0, 0, 0, 0, 0
            )
        ok = _wait_for_ack(mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, timeout=3.0)
        if ok:
            print("[MAVLINK] ✅ DISARM confirmed")
            _publish_status("DISARMED", "Drone đã DISARM")
        else:
            print("[MAVLINK] ⚠️  DISARM không có ACK")
            _publish_status("WARN", "DISARM không xác nhận")
    except Exception as e:
        print(f"[MAVLINK] Lỗi DISARM: {e}")
        _publish_status("ERROR", str(e))


def _handle_takeoff(alt):
    """GUIDED → ARM → TAKEOFF với đầy đủ ACK và phản hồi."""
    try:
        alt = max(1.0, min(float(alt), 100.0))
        with master_lock:
            if master is None:
                _publish_status("ERROR", "SITL chưa kết nối")
                return

        _publish_status("BUSY", "Đang chuẩn bị cất cánh...")

        # B1: GUIDED mode
        print("[MAVLINK] TAKEOFF: Chuyển GUIDED...")
        _set_mode_with_retry('GUIDED')
        time.sleep(0.4)

        # B2: ARM
        for attempt in range(3):
            with master_lock:
                if master is None:
                    return
                master.mav.command_long_send(
                    master.target_system, master.target_component,
                    mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, 0,
                    1, 21196, 0, 0, 0, 0, 0
                )
            ok = _wait_for_ack(mavutil.mavlink.MAV_CMD_COMPONENT_ARM_DISARM, timeout=2.0)
            if ok:
                break
            print(f"[MAVLINK] ARM retry {attempt+1}...")
            time.sleep(0.5)
        else:
            _publish_status("ERROR", "Không ARM được, hủy TAKEOFF")
            return

        time.sleep(0.5)  # Chờ ARM ổn định

        # B3: TAKEOFF
        _publish_status("BUSY", f"Cất cánh lên {alt}m...")
        with master_lock:
            if master is None:
                return
            master.mav.command_long_send(
                master.target_system, master.target_component,
                mavutil.mavlink.MAV_CMD_NAV_TAKEOFF, 0,
                0, 0, 0, 0, 0, 0, alt
            )
        ok = _wait_for_ack(mavutil.mavlink.MAV_CMD_NAV_TAKEOFF, timeout=3.0)
        if ok:
            print(f"[MAVLINK] ✅ TAKEOFF confirmed ({alt}m)")
            _publish_status("FLYING", f"Đang cất cánh lên {alt}m")
        else:
            print("[MAVLINK] ⚠️  TAKEOFF ACK timeout")
            _publish_status("WARN", "TAKEOFF gửi nhưng chưa xác nhận")
    except Exception as e:
        print(f"[MAVLINK] Lỗi TAKEOFF: {e}")
        _publish_status("ERROR", str(e))


def _handle_recovery():
    """[FIX] Phục hồi drone sau khi failsafe/motor fail: reset → GUIDED."""
    try:
        with master_lock:
            if master is None:
                _publish_status("ERROR", "SITL chưa kết nối")
                return
        _publish_status("BUSY", "Đang phục hồi hệ thống...")

        # Reset SIM params
        for param, val in [(b'SIM_ENGINE_FAIL', 0.0), (b'SIM_GPS_DISABLE', 0.0),
                           (b'SIM_WIND_TURB', 0.0), (b'FENCE_ENABLE', 0.0)]:
            with master_lock:
                if master:
                    master.mav.param_set_send(
                        master.target_system, master.target_component,
                        param, val, mavutil.mavlink.MAV_PARAM_TYPE_REAL32
                    )
            time.sleep(0.2)

        # Chuyển GUIDED
        _set_mode_with_retry('GUIDED')
        time.sleep(0.5)

        print("[MAVLINK] ✅ Recovery hoàn tất")
        _publish_status("OK", "Đã phục hồi — Sẵn sàng bay lại")
    except Exception as e:
        print(f"[MAVLINK] Lỗi recovery: {e}")
        _publish_status("ERROR", str(e))


# --- [NEW] Wind Speed Handler ---

def _handle_wind_speed(speed: float):
    """[NEW] Đặt tốc độ gió mô phỏng SITL qua MAVLink parameter."""
    with master_lock:
        if master is None:
            print("[WEATHER] ⚠️  SITL chưa kết nối.")
            return
        try:
            master.mav.param_set_send(
                master.target_system,
                master.target_component,
                b'SIM_WIND_SPD',
                speed,
                mavutil.mavlink.MAV_PARAM_TYPE_REAL32
            )
            print(f"[WEATHER] 🌪️  Đặt SIM_WIND_SPD = {speed} m/s")
        except Exception as e:
            print(f"[WEATHER] ❌ Lỗi: {e}")

def _handle_sim_param(param_id: str, value: float):
    """[NEW] Gửi thông số giả lập (Turbulence, Engine Fail, GPS Glitch, v.v.) vào SITL."""
    with master_lock:
        if master is None:
            print(f"[SIM_CMD] ⚠️  SITL chưa kết nối. Bỏ qua {param_id}={value}")
            return
        try:
            param_bytes = param_id.encode('utf-8')
            master.mav.param_set_send(
                master.target_system,
                master.target_component,
                param_bytes,
                value,
                mavutil.mavlink.MAV_PARAM_TYPE_REAL32
            )
            print(f"[SIM_CMD] 🔧 Đã set {param_id} = {value}")
        except Exception as e:
            print(f"[SIM_CMD] ❌ Lỗi khi set {param_id}: {e}")


# --- MQTT Callbacks ---


def _upload_mission_thread(points):
    """Uploads a mission using MAVLink waypoint protocol"""
    with master_lock:
        if master is None:
            return
        
        try:
            print(f"[MISSION] Bắt đầu xóa mission cũ...")
            master.waypoint_clear_all_send()
            master.recv_match(type='MISSION_ACK', blocking=True, timeout=3)
            
            # Create mission items
            import pymavlink.mavwp as mavwp
            wp = mavwp.MAVWPLoader()
            
            # Get current GPS coordinates for HOME position
            with state_lock:
                home_lat = gps_data.get("lat", -35.363261)
                home_lon = gps_data.get("lon", 149.165230)
                home_alt = gps_data.get("alt", 0.0)

            # Seq 0: HOME position (required by MAVLink protocol)
            wp.add(mavutil.mavlink.MAVLink_mission_item_int_message(
                master.target_system, master.target_component,
                0, mavutil.mavlink.MAV_FRAME_GLOBAL,
                mavutil.mavlink.MAV_CMD_NAV_WAYPOINT, 0, 1,
                0, 0, 0, 0,
                int(home_lat * 1e7), int(home_lon * 1e7), home_alt
            ))
            
            # Seq 1: TAKEOFF to 10m
            wp.add(mavutil.mavlink.MAVLink_mission_item_int_message(
                master.target_system, master.target_component,
                1, mavutil.mavlink.MAV_FRAME_GLOBAL_RELATIVE_ALT,
                mavutil.mavlink.MAV_CMD_NAV_TAKEOFF, 0, 1,
                0, 0, 0, 0,
                0, 0, 10.0
            ))
            
            # Seq 2..N+1: WAYPOINTS
            seq = 2
            for pt in points:
                wp.add(mavutil.mavlink.MAVLink_mission_item_int_message(
                    master.target_system, master.target_component,
                    seq, mavutil.mavlink.MAV_FRAME_GLOBAL_RELATIVE_ALT,
                    mavutil.mavlink.MAV_CMD_NAV_WAYPOINT, 0, 1,
                    0, 0, 0, 0,
                    int(pt['lat'] * 1e7), int(pt['lon'] * 1e7), 15.0  # Fly at 15m
                ))
                seq += 1
                
            # Final Seq: RTL
            wp.add(mavutil.mavlink.MAVLink_mission_item_int_message(
                master.target_system, master.target_component,
                seq, mavutil.mavlink.MAV_FRAME_GLOBAL_RELATIVE_ALT,
                mavutil.mavlink.MAV_CMD_NAV_RETURN_TO_LAUNCH, 0, 1,
                0, 0, 0, 0,
                0, 0, 0
            ))
            
            count = wp.count()
            print(f"[MISSION] Đang đẩy {count} points lên Drone...")
            master.waypoint_count_send(count)
            
            for i in range(count):
                msg = master.recv_match(type=['MISSION_REQUEST', 'MISSION_REQUEST_INT'], blocking=True, timeout=3)
                if not msg:
                    print(f"[MISSION] ❌ Timeout chờ yêu cầu seq={i}")
                    break
                master.mav.send(wp.wp(msg.seq))
                
            ack = master.recv_match(type='MISSION_ACK', blocking=True, timeout=3)
            if ack and ack.type == 0:
                print("[MISSION] ✅ Upload Mission thành công!")
            else:
                print(f"[MISSION] ❌ Lỗi upload: {ack}")
                
        except Exception as e:
            print(f"[MISSION] ❌ Ngoại lệ khi upload: {e}")


def on_connect(client, userdata, flags, reason_code, properties):
    global mqtt_pub
    if reason_code == 0:
        mqtt_pub = client
        print(f"[MQTT] ✅ Connected to {MQTT_BROKER}:{MQTT_PORT}")
        client.subscribe(TOPIC_SENSORS)
        client.subscribe(TOPIC_FLIGHT_CMD)
        client.subscribe(TOPIC_PAYLOAD_CMD)
        client.subscribe(TOPIC_WEATHER_CMD)
        client.subscribe(TOPIC_HEARTBEAT)
        client.subscribe(TOPIC_MISSION_CMD)
        client.subscribe(TOPIC_SIM_CMD)  # [NEW]
        print(f"[MQTT] Subscribed: sensors, flight, payload, weather, heartbeat, mission, sim_param")
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
            elif command == "LOITER":
                threading.Thread(target=_handle_mode, args=("LOITER",), daemon=True).start()
            elif command == "ALT_HOLD":
                threading.Thread(target=_handle_mode, args=("ALT_HOLD",), daemon=True).start()
            elif command == "STABILIZE":
                threading.Thread(target=_handle_mode, args=("STABILIZE",), daemon=True).start()
            elif command == "RESET_FLIGHT":
                threading.Thread(target=_handle_mode, args=("GUIDED",), daemon=True).start()
            elif command == "RECOVERY":
                threading.Thread(target=_handle_recovery, daemon=True).start()
                print("[CMD] Recovery sequence started")

        elif topic == TOPIC_PAYLOAD_CMD:
            data = json.loads(payload_str)
            command = data.get("command", "?")
            print(f"[CMD] Payload: {command} (BW16 subscribes directly)")

        elif topic == TOPIC_WEATHER_CMD:
            data = json.loads(payload_str)
            wind_speed = float(data.get("wind_speed", 0.0))
            global current_wind
            current_wind = wind_speed
            threading.Thread(target=_handle_wind_speed, args=(wind_speed,), daemon=True).start()

        # [NEW] Waypoint Mission
        elif topic == TOPIC_MISSION_CMD:
            data = json.loads(payload_str)
            command = data.get("command")
            if command == "START":
                threading.Thread(target=_handle_mode, args=("AUTO",), daemon=True).start()
                print("[MISSION] 🗓️  Chuyển sang mode AUTO → Bắt đầu bay tự động")
            elif command == "PAUSE":
                threading.Thread(target=_handle_mode, args=("LOITER",), daemon=True).start()
                print("[MISSION] ⏸️  LOITER → Tạm dừng tuần tra")
            elif command == "UPLOAD":
                pts = data.get("points", [])
                if pts:
                    threading.Thread(target=_upload_mission_thread, args=(pts,), daemon=True).start()

        # [NEW] Heartbeat từ Web (Failsafe Watchdog)
        elif topic == TOPIC_HEARTBEAT:
            global last_heartbeat_time
            last_heartbeat_time = time.time()
            # print("[WATCHDOG] 📳 Ping nhận được")

        # [NEW] Khảo nghiệm sinh tồn SITL
        elif topic == TOPIC_SIM_CMD:
            data = json.loads(payload_str)
            param_id = data.get("param")
            val = float(data.get("value", 0.0))
            if param_id:
                if param_id == "FENCE_ENABLE":
                    global current_fence_enabled
                    current_fence_enabled = int(val)
                threading.Thread(target=_handle_sim_param, args=(param_id, val), daemon=True).start()

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
            # [FIX] Dùng recv_msg() để lấy tất cả các tin nhắn trong buffer, không bỏ sót
            latest_gps = None
            latest_servo = None
            latest_attitude = None
            latest_vfr = None
            latest_sys = None
            
            with master_lock:
                if master is not None:
                    while True:
                        msg = master.recv_msg()
                        if msg is None:
                            break
                        msg_type = msg.get_type()
                        if msg_type == 'GLOBAL_POSITION_INT':
                            latest_gps = msg
                        elif msg_type == 'SERVO_OUTPUT_RAW':
                            latest_servo = msg
                        elif msg_type == 'ATTITUDE':
                            latest_attitude = msg
                        elif msg_type == 'VFR_HUD':
                            latest_vfr = msg
                        elif msg_type == 'SYS_STATUS':
                            latest_sys = msg

            global current_mode, current_armed, current_alt, current_spd, current_batt
            
            if latest_gps is not None:
                current_alt = latest_gps.relative_alt / 1000.0
                with state_lock:
                    gps_data = {
                        "lat": latest_gps.lat / 1e7,
                        "lon": latest_gps.lon / 1e7,
                        "alt": current_alt
                    }

            if latest_vfr is not None:
                current_spd = latest_vfr.groundspeed

            if latest_sys is not None:
                current_batt = latest_sys.voltage_battery / 1000.0  # mV to V

            # [FIX] Publish flight status (mode + armed) từ HEARTBEAT
            latest_hb = None
            with master_lock:
                if master is not None:
                    latest_hb = master.recv_match(type='HEARTBEAT', blocking=False)
            if latest_hb is not None:
                current_mode = mavutil.mode_string_v10(latest_hb)
                current_armed = bool(latest_hb.base_mode & mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED)
                if mqtt_pub is not None:
                    try:
                        mqtt_pub.publish(TOPIC_STATUS, json.dumps({
                            "mode": current_mode,
                            "armed": current_armed,
                            "alt": current_alt,
                            "ts": time.time()
                        }))
                    except Exception:
                        pass


            if latest_gps is not None and mqtt_pub is not None:
                gps_payload = json.dumps(gps_data)
                try:
                    mqtt_pub.publish(TOPIC_GPS, gps_payload)
                except Exception as e:
                    pass

            if latest_servo is not None and mqtt_pub is not None:
                motor_payload = json.dumps({
                    "m1": latest_servo.servo1_raw,
                    "m2": latest_servo.servo2_raw,
                    "m3": latest_servo.servo3_raw,
                    "m4": latest_servo.servo4_raw,
                })
                try:
                    mqtt_pub.publish(TOPIC_MOTOR_DATA, motor_payload)
                except Exception:
                    pass

            if latest_attitude is not None and mqtt_pub is not None:
                attitude_payload = json.dumps({
                    "roll": latest_attitude.roll,
                    "pitch": latest_attitude.pitch,
                    "yaw": latest_attitude.yaw
                })
                try:
                    mqtt_pub.publish(TOPIC_ATTITUDE, attitude_payload)
                except Exception:
                    pass

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


# --- [NEW] Failsafe Watchdog ---

def watchdog_loop():
    """
    Kiểm tra mỗi 5 giây xem Web có gửi heartbeat không.
    Nếu quá FAILSAFE_TIMEOUT giây không nhận được ping → tự động RTL.
    """
    print("[WATCHDOG] 👁️  Khởi động Failsafe Watchdog (timeout=15s)")
    while True:
        time.sleep(5)  # Kiểm tra mỗi 5 giây
        elapsed = time.time() - last_heartbeat_time
        if elapsed > FAILSAFE_TIMEOUT and master is not None:
            print(f"[WATCHDOG] ⚠️  Mất kết nối Web {elapsed:.0f}s → Gọi RTL!")
            _handle_mode("RTL")


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
    threading.Thread(target=watchdog_loop, daemon=True).start()  # [NEW] Failsafe

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
                )

                # Send downstream flight telemetry to the physical BW16 OLED
                dist_to_home = get_distance_meters(lat, lon, -35.363261, 149.165230)
                fence_status = 0
                if current_fence_enabled == 1:
                    fence_status = 2 if dist_to_home > 50.0 else 1

                telemetry_payload = json.dumps({
                    "mode": current_mode,
                    "armed": 1 if current_armed else 0,
                    "alt": float(current_alt),
                    "spd": float(current_spd),
                    "batt": float(current_batt),
                    "wind": float(current_wind),
                    "fence": int(fence_status)
                })
                try:
                    if mqtt_client and mqtt_client.is_connected():
                        mqtt_client.publish(topic_payload_cmd if 'topic_payload_cmd' in locals() else TOPIC_PAYLOAD_CMD, telemetry_payload)
                except Exception as e:
                    print(f"[MQTT] Gửi Downstream thất bại: {e}")
                
                point.field("wifi_rssi",   float(rssi))

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
