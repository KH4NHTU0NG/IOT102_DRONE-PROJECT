"""
mavlink_handler.py — Xử lý kết nối MAVLink và SITL
"""
import time
import json
import threading
from pymavlink import mavutil
import pymavlink.mavwp as mavwp
import config

def _publish_status(status: str, detail: str = ""):
    if config.mqtt_pub is None:
        return
    try:
        config.mqtt_pub.publish(config.TOPIC_STATUS, json.dumps({
            "status": status,
            "detail": detail,
            "ts": time.time()
        }))
    except Exception:
        pass

def _wait_for_ack(command_id: int, timeout: float = 3.0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        with config.master_lock:
            if config.master is None:
                return False
            msg = config.master.recv_match(type='COMMAND_ACK', blocking=False)
        if msg and msg.command == command_id:
            return msg.result == mavutil.mavlink.MAV_RESULT_ACCEPTED
        time.sleep(0.05)
    return False

def _wait_mode_set(target_mode: str, timeout: float = 4.0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        with config.master_lock:
            if config.master is None:
                return False
            hb = config.master.recv_match(type='HEARTBEAT', blocking=False)
        if hb:
            mode_map = mavutil.mode_string_v10(hb)
            if mode_map == target_mode:
                return True
        time.sleep(0.1)
    return False

def _set_mode_with_retry(mode_name: str, retries: int = 3) -> bool:
    for i in range(retries):
        try:
            with config.master_lock:
                if config.master is None:
                    return False
                config.master.set_mode(mode_name)
            time.sleep(0.3)
            with config.master_lock:
                if config.master is None:
                    return False
                hb = config.master.recv_match(type='HEARTBEAT', blocking=True, timeout=2)
            if hb:
                actual = mavutil.mode_string_v10(hb)
                if actual == mode_name:
                    return True
        except Exception as e:
            print(f"[MAVLINK] Mode retry {i+1}: {e}")
        time.sleep(0.5)
    return False

def handle_mode(mode_name):
    try:
        with config.master_lock:
            if config.master is None:
                _publish_status("ERROR", "SITL chưa kết nối")
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

def handle_arm():
    try:
        with config.master_lock:
            if config.master is None:
                _publish_status("ERROR", "SITL chưa kết nối")
                return
        _publish_status("BUSY", "Đang ARM...")
        print("[MAVLINK] ARM: Chuyển GUIDED...")
        _set_mode_with_retry('GUIDED')
        time.sleep(0.3)
        for attempt in range(3):
            with config.master_lock:
                if config.master is None:
                    return
                config.master.mav.command_long_send(
                    config.master.target_system, config.master.target_component,
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

def handle_disarm():
    try:
        with config.master_lock:
            if config.master is None:
                _publish_status("ERROR", "SITL chưa kết nối")
                return
        _publish_status("BUSY", "Đang DISARM...")
        with config.master_lock:
            config.master.mav.command_long_send(
                config.master.target_system, config.master.target_component,
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

def handle_takeoff(alt):
    try:
        alt = max(1.0, min(float(alt), 100.0))
        with config.master_lock:
            if config.master is None:
                _publish_status("ERROR", "SITL chưa kết nối")
                return
        _publish_status("BUSY", "Đang chuẩn bị cất cánh...")
        print("[MAVLINK] TAKEOFF: Chuyển GUIDED...")
        _set_mode_with_retry('GUIDED')
        time.sleep(0.4)
        for attempt in range(3):
            with config.master_lock:
                if config.master is None:
                    return
                config.master.mav.command_long_send(
                    config.master.target_system, config.master.target_component,
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
        time.sleep(0.5)
        _publish_status("BUSY", f"Cất cánh lên {alt}m...")
        with config.master_lock:
            if config.master is None:
                return
            config.master.mav.command_long_send(
                config.master.target_system, config.master.target_component,
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

def handle_recovery():
    try:
        with config.master_lock:
            if config.master is None:
                _publish_status("ERROR", "SITL chưa kết nối")
                return
        _publish_status("BUSY", "Đang phục hồi hệ thống...")
        for param, val in [(b'SIM_ENGINE_FAIL', 0.0), (b'SIM_GPS_DISABLE', 0.0),
                           (b'SIM_WIND_TURB', 0.0), (b'FENCE_ENABLE', 0.0)]:
            with config.master_lock:
                if config.master:
                    config.master.mav.param_set_send(
                        config.master.target_system, config.master.target_component,
                        param, val, mavutil.mavlink.MAV_PARAM_TYPE_REAL32
                    )
            time.sleep(0.2)
        _set_mode_with_retry('GUIDED')
        time.sleep(0.5)
        print("[MAVLINK] ✅ Recovery hoàn tất")
        _publish_status("OK", "Đã phục hồi — Sẵn sàng bay lại")
    except Exception as e:
        print(f"[MAVLINK] Lỗi recovery: {e}")
        _publish_status("ERROR", str(e))

def handle_wind_speed(speed: float):
    with config.master_lock:
        if config.master is None:
            print("[WEATHER] ⚠️  SITL chưa kết nối.")
            return
        try:
            config.master.mav.param_set_send(
                config.master.target_system,
                config.master.target_component,
                b'SIM_WIND_SPD',
                speed,
                mavutil.mavlink.MAV_PARAM_TYPE_REAL32
            )
            print(f"[WEATHER] 🌪️  Đặt SIM_WIND_SPD = {speed} m/s")
        except Exception as e:
            print(f"[WEATHER] ❌ Lỗi: {e}")

def handle_sim_param(param_id: str, value: float):
    with config.master_lock:
        if config.master is None:
            print(f"[SIM_CMD] ⚠️  SITL chưa kết nối. Bỏ qua {param_id}={value}")
            return
        try:
            param_bytes = param_id.encode('utf-8')
            config.master.mav.param_set_send(
                config.master.target_system,
                config.master.target_component,
                param_bytes,
                value,
                mavutil.mavlink.MAV_PARAM_TYPE_REAL32
            )
            print(f"[SIM_CMD] 🔧 Đã set {param_id} = {value}")
        except Exception as e:
            print(f"[SIM_CMD] ❌ Lỗi khi set {param_id}: {e}")

def clear_mission():
    try:
        with config.master_lock:
            if config.master is None:
                _publish_status("ERROR", "SITL chưa kết nối")
                return
        _publish_status("BUSY", "Đang xóa lộ trình cũ...")
        print(f"[MISSION] Bắt đầu xóa mission...")
        
        with config.master_lock:
            if config.master is None:
                return
            config.master.waypoint_clear_all_send()
            ack = config.master.recv_match(type='MISSION_ACK', blocking=True, timeout=3)
            
        if ack and ack.type == 0:
            print("[MISSION] ✅ Clear Mission thành công!")
            _publish_status("OK", "Đã xóa lộ trình bay")
        else:
            print(f"[MISSION] ❌ Lỗi khi xóa mission: {ack}")
            _publish_status("ERROR", "Xóa lộ trình thất bại")
    except Exception as e:
        print(f"[MISSION] ❌ Ngoại lệ khi clear mission: {e}")
        _publish_status("ERROR", str(e))

def upload_mission_thread(points):
    with config.master_lock:
        if config.master is None:
            return
        config.is_uploading_mission = True
        try:
            print(f"[MISSION] Bắt đầu xóa mission cũ...")
            config.master.waypoint_clear_all_send()
            config.master.recv_match(type='MISSION_ACK', blocking=True, timeout=3)
            
            wp = mavwp.MAVWPLoader()
            
            with config.state_lock:
                home_lat = config.gps_data.get("lat", -35.363261)
                home_lon = config.gps_data.get("lon", 149.165230)
                home_alt = config.gps_data.get("alt", 0.0)

            wp.add(mavutil.mavlink.MAVLink_mission_item_int_message(
                config.master.target_system, config.master.target_component,
                0, mavutil.mavlink.MAV_FRAME_GLOBAL,
                mavutil.mavlink.MAV_CMD_NAV_WAYPOINT, 0, 1,
                0, 0, 0, 0,
                int(home_lat * 1e7), int(home_lon * 1e7), home_alt
            ))
            
            wp.add(mavutil.mavlink.MAVLink_mission_item_int_message(
                config.master.target_system, config.master.target_component,
                1, mavutil.mavlink.MAV_FRAME_GLOBAL_RELATIVE_ALT,
                mavutil.mavlink.MAV_CMD_NAV_TAKEOFF, 0, 1,
                0, 0, 0, 0,
                0, 0, 10.0
            ))
            
            seq = 2
            for pt in points:
                wp.add(mavutil.mavlink.MAVLink_mission_item_int_message(
                    config.master.target_system, config.master.target_component,
                    seq, mavutil.mavlink.MAV_FRAME_GLOBAL_RELATIVE_ALT,
                    mavutil.mavlink.MAV_CMD_NAV_WAYPOINT, 0, 1,
                    0, 0, 0, 0,
                    int(pt['lat'] * 1e7), int(pt['lon'] * 1e7), 15.0
                ))
                seq += 1
                
            wp.add(mavutil.mavlink.MAVLink_mission_item_int_message(
                config.master.target_system, config.master.target_component,
                seq, mavutil.mavlink.MAV_FRAME_GLOBAL_RELATIVE_ALT,
                mavutil.mavlink.MAV_CMD_NAV_RETURN_TO_LAUNCH, 0, 1,
                0, 0, 0, 0,
                0, 0, 0
            ))
            
            count = wp.count()
            print(f"[MISSION] Đang đẩy {count} points lên Drone...")
            config.master.waypoint_count_send(count)
            
            for i in range(count):
                msg = config.master.recv_match(type=['MISSION_REQUEST', 'MISSION_REQUEST_INT'], blocking=True, timeout=3)
                if not msg:
                    print(f"[MISSION] ❌ Timeout chờ yêu cầu seq={i}")
                    break
                config.master.mav.send(wp.wp(msg.seq))
                
            ack = config.master.recv_match(type='MISSION_ACK', blocking=True, timeout=3)
            if ack and ack.type == 0:
                print("[MISSION] ✅ Upload Mission thành công!")
            else:
                print(f"[MISSION] ❌ Lỗi upload: {ack}")
                
        except Exception as e:
            print(f"[MISSION] ❌ Ngoại lệ khi upload: {e}")
        finally:
            config.is_uploading_mission = False

def connect_sitl(max_retries: int = 5) -> mavutil.mavfile:
    connection_str = f"tcp:{config.SITL_HOST}:{config.SITL_PORT}"
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
    while True:
        with config.master_lock:
            m = config.master

        if m is None:
            try:
                new_conn = connect_sitl(max_retries=1)
                with config.master_lock:
                    config.master = new_conn
            except Exception:
                time.sleep(5)
                continue
            continue

        if config.is_uploading_mission:
            time.sleep(0.1)
            continue

        try:
            latest_gps = None
            latest_servo = None
            latest_attitude = None
            latest_vfr = None
            latest_sys = None
            
            with config.master_lock:
                if config.master is not None:
                    while True:
                        msg = config.master.recv_msg()
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
            
            with config.state_lock:
                if latest_gps is not None:
                    config.current_alt = latest_gps.relative_alt / 1000.0
                    config.gps_data = {
                        "lat": latest_gps.lat / 1e7,
                        "lon": latest_gps.lon / 1e7,
                        "alt": latest_gps.alt / 1000.0,
                        "relative_alt": config.current_alt,
                        "vx": latest_gps.vx / 100.0,
                        "vy": latest_gps.vy / 100.0,
                        "vz": latest_gps.vz / 100.0,
                        "hdg": latest_gps.hdg,
                    }

                if latest_vfr is not None:
                    config.current_spd = latest_vfr.groundspeed
                    if config.gps_data:
                        config.gps_data["ground_speed"] = round(config.current_spd, 2)

                if latest_sys is not None:
                    config.current_batt = latest_sys.voltage_battery / 1000.0

            latest_hb = None
            with config.master_lock:
                if config.master is not None:
                    latest_hb = config.master.recv_match(type='HEARTBEAT', blocking=False)
            if latest_hb is not None:
                config.current_mode = mavutil.mode_string_v10(latest_hb)
                config.current_armed = bool(latest_hb.base_mode & mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED)
                if config.mqtt_pub is not None:
                    try:
                        config.mqtt_pub.publish(config.TOPIC_STATUS, json.dumps({
                            "mode": config.current_mode,
                            "armed": config.current_armed,
                            "alt": config.current_alt,
                            "ts": time.time()
                        }))
                    except Exception:
                        pass

            if latest_gps is not None and config.mqtt_pub is not None:
                gps_payload = json.dumps(config.gps_data)
                try:
                    config.mqtt_pub.publish(config.TOPIC_GPS, gps_payload)
                except Exception:
                    pass

            if latest_servo is not None and config.mqtt_pub is not None:
                motor_payload = json.dumps({
                    "m1": latest_servo.servo1_raw,
                    "m2": latest_servo.servo2_raw,
                    "m3": latest_servo.servo3_raw,
                    "m4": latest_servo.servo4_raw,
                })
                try:
                    config.mqtt_pub.publish(config.TOPIC_MOTOR_DATA, motor_payload)
                except Exception:
                    pass

            if latest_attitude is not None and config.mqtt_pub is not None:
                attitude_payload = json.dumps({
                    "roll": latest_attitude.roll,
                    "pitch": latest_attitude.pitch,
                    "yaw": latest_attitude.yaw
                })
                try:
                    config.mqtt_pub.publish(config.TOPIC_ATTITUDE, attitude_payload)
                except Exception:
                    pass

            time.sleep(0.1)
        except Exception as e:
            print(f"[MAVLINK] Lỗi đọc GPS: {e}. Reconnecting...")
            with config.master_lock:
                if config.master is not None:
                    try:
                        config.master.close()
                    except Exception:
                        pass
                    config.master = None
            time.sleep(2)
