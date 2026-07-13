"""
mqtt_handler.py — Xử lý kết nối và callbacks MQTT
"""
import time
import json
import threading
import sys
import paho.mqtt.client as mqtt
import config
import mavlink_handler

def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        config.mqtt_pub = client
        print(f"[MQTT] ✅ Connected to {config.MQTT_BROKER}:{config.MQTT_PORT}")
        client.subscribe(config.TOPIC_SENSORS)
        client.subscribe(config.TOPIC_FLIGHT_CMD)
        client.subscribe(config.TOPIC_PAYLOAD_CMD)
        client.subscribe(config.TOPIC_WEATHER_CMD)
        client.subscribe(config.TOPIC_HEARTBEAT)
        client.subscribe(config.TOPIC_MISSION_CMD)
        client.subscribe(config.TOPIC_SIM_CMD)
        print(f"[MQTT] Subscribed: sensors, flight, payload, weather, heartbeat, mission, sim_param")
    else:
        print(f"[MQTT] ❌ Connection failed, code={reason_code}")

def on_message(client, userdata, msg):
    topic = msg.topic
    try:
        payload_str = msg.payload.decode("utf-8")

        if topic == config.TOPIC_SENSORS:
            data = json.loads(payload_str)
            with config.state_lock:
                config.sensor_data = data

        elif topic == config.TOPIC_FLIGHT_CMD:
            data = json.loads(payload_str)
            command = data.get("command")
            alt = data.get("alt", 10.0)
            print(f"[CMD] Nhận lệnh: {command} (alt={alt}m)")

            if command == "ARM":
                threading.Thread(target=mavlink_handler.handle_arm, daemon=True).start()
            elif command == "DISARM":
                threading.Thread(target=mavlink_handler.handle_disarm, daemon=True).start()
            elif command == "TAKEOFF":
                threading.Thread(target=mavlink_handler.handle_takeoff, args=(alt,), daemon=True).start()
            elif command == "LAND":
                threading.Thread(target=mavlink_handler.handle_mode, args=("LAND",), daemon=True).start()
            elif command == "RTL":
                threading.Thread(target=mavlink_handler.handle_mode, args=("RTL",), daemon=True).start()
            elif command == "LOITER":
                threading.Thread(target=mavlink_handler.handle_mode, args=("LOITER",), daemon=True).start()
            elif command == "ALT_HOLD":
                threading.Thread(target=mavlink_handler.handle_mode, args=("ALT_HOLD",), daemon=True).start()
            elif command == "STABILIZE":
                threading.Thread(target=mavlink_handler.handle_mode, args=("STABILIZE",), daemon=True).start()
            elif command == "RESET_FLIGHT":
                threading.Thread(target=mavlink_handler.handle_mode, args=("GUIDED",), daemon=True).start()
            elif command == "RECOVERY":
                threading.Thread(target=mavlink_handler.handle_recovery, daemon=True).start()
                print("[CMD] Recovery sequence started")
            elif command == "SET_ALT":
                # [FIX] Handler SET_ALT bị thiếu — lệnh từ Web bị bỏ qua trước đây
                threading.Thread(target=mavlink_handler.handle_set_alt, args=(alt,), daemon=True).start()
                print(f"[CMD] SET_ALT → {alt}m")

        elif topic == config.TOPIC_PAYLOAD_CMD:
            data = json.loads(payload_str)
            command = data.get("command", "?")
            print(f"[CMD] Payload: {command}")

            if command == "SERVO":
                angle = int(data.get("angle", 0))
                print(f"[CMD] SERVO {angle}° → BW16 xử lý trực tiếp")
            elif command == "BUZZER_ON":
                print(f"[CMD] BUZZER_ON → BW16 xử lý trực tiếp")
            elif command == "LED_ON":
                print(f"[CMD] LED_ON → BW16 xử lý trực tiếp")
            else:
                print(f"[CMD] Payload command `{command}` → BW16 xử lý trực tiếp")

        elif topic == config.TOPIC_WEATHER_CMD:
            data = json.loads(payload_str)
            wind_speed = float(data.get("wind_speed", 0.0))
            config.current_wind = wind_speed
            threading.Thread(target=mavlink_handler.handle_wind_speed, args=(wind_speed,), daemon=True).start()

        elif topic == config.TOPIC_MISSION_CMD:
            data = json.loads(payload_str)
            command = data.get("command")
            if command == "START":
                threading.Thread(target=mavlink_handler.handle_mode, args=("AUTO",), daemon=True).start()
                print("[MISSION] 🗓️  Chuyển sang mode AUTO → Bắt đầu bay tự động")
            elif command == "PAUSE":
                threading.Thread(target=mavlink_handler.handle_mode, args=("LOITER",), daemon=True).start()
                print("[MISSION] ⏸️  LOITER → Tạm dừng tuần tra")
            elif command == "UPLOAD":
                pts = data.get("points", [])
                if pts:
                    threading.Thread(target=mavlink_handler.upload_mission_thread, args=(pts,), daemon=True).start()
            elif command == "CLEAR_MISSION":
                threading.Thread(target=mavlink_handler.clear_mission, daemon=True).start()
                print("[MISSION] 🗑️  Yêu cầu xóa toàn bộ waypoint")

        elif topic == config.TOPIC_HEARTBEAT:
            config.last_heartbeat_time = time.time()

        elif topic == config.TOPIC_SIM_CMD:
            data = json.loads(payload_str)
            param_id = data.get("param")
            val = float(data.get("value", 0.0))
            if param_id:
                if param_id == "FENCE_ENABLE":
                    config.current_fence_enabled = int(val)
                threading.Thread(target=mavlink_handler.handle_sim_param, args=(param_id, val), daemon=True).start()

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
        mqtt_client.connect(config.MQTT_BROKER, config.MQTT_PORT, keepalive=60)
    except Exception as e:
        print(f"[MQTT] Không kết nối được {config.MQTT_BROKER}:{config.MQTT_PORT}: {e}")
        sys.exit(1)

    threading.Thread(target=mqtt_client.loop_forever, daemon=True).start()
    return mqtt_client
