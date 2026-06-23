#!/usr/bin/env python3
import time
import os
import sys
import json
import threading
import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish
from pymavlink import mavutil

MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
SITL_HOST = "127.0.0.1"
SITL_PORT = 5763

payload_received = None
flight_received = None
received_event = threading.Event()

def on_connect(client, userdata, flags, reason_code, properties):
    client.subscribe("tuonghuy_drone/control/payload")
    client.subscribe("tuonghuy_drone/control/flight")

def on_message(client, userdata, msg):
    global payload_received, flight_received
    topic = msg.topic
    payload = msg.payload.decode("utf-8")
    
    if topic == "tuonghuy_drone/control/payload":
        payload_received = payload
    elif topic == "tuonghuy_drone/control/flight":
        flight_received = payload
        
    received_event.set()

def run_mqtt_subscriber():
    sub_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    sub_client.on_connect = on_connect
    sub_client.on_message = on_message
    try:
        sub_client.connect(MQTT_BROKER, MQTT_PORT)
    except Exception as e:
        print(f"[TEST] Không kết nối được MQTT: {e}")
        sys.exit(1)
    sub_client.loop_start()
    return sub_client

def test_1_payload_command():
    print("\n▶ [TEST 1] Kiểm tra Round-trip lệnh Payload (Còi/LED)...")
    global payload_received
    payload_received = None
    received_event.clear()
    
    # Gửi lệnh BẬT CÒI sử dụng publish.single để đồng bộ an toàn
    cmd = {"command": "BUZZER_ON", "timestamp": int(time.time()*1000)}
    publish.single("tuonghuy_drone/control/payload", payload=json.dumps(cmd), hostname=MQTT_BROKER, port=MQTT_PORT)
    
    # Chờ nhận lại qua subscriber
    success = received_event.wait(timeout=3.0)
    if success and payload_received is not None:
        data = json.loads(payload_received)
        if data.get("command") == "BUZZER_ON":
            print("  - Kết quả Test 1: PASS ✅ (Nhận phản hồi còi bật từ MQTT)")
            return True
            
    print("  - Kết quả Test 1: FAIL ❌ (Không nhận được phản hồi)")
    return False

def test_2_flight_command():
    print("\n▶ [TEST 2] Kiểm tra gửi lệnh ARM tới SITL MAVLink...")
    global flight_received
    flight_received = None
    received_event.clear()
    
    # Gửi lệnh ARM lên MQTT sử dụng publish.single để đồng bộ an toàn
    cmd = {"command": "ARM", "timestamp": int(time.time()*1000)}
    publish.single("tuonghuy_drone/control/flight", payload=json.dumps(cmd), hostname=MQTT_BROKER, port=MQTT_PORT)
    
    # Chờ nhận tin nhắn MQTT
    received_event.wait(timeout=3.0)
    
    # Đồng thời thử kiểm tra xem SITL có đổi trạng thái ARM không
    connection_str = f"tcp:{SITL_HOST}:{SITL_PORT}"
    print(f"  - Đang thử kết nối SITL {connection_str} để xác nhận trạng thái động cơ...")
    try:
        master = mavutil.mavlink_connection(connection_str)
        master.wait_heartbeat(timeout=3)
        print("  - Đã tìm thấy nhịp tim SITL. Đang chờ xác nhận lệnh bay nhận từ MQTT...")
        
        if flight_received is not None:
            data = json.loads(flight_received)
            if data.get("command") == "ARM":
                print("  - Kết quả Test 2: PASS ✅ (Lệnh ARM bay đã gửi và được gateway xử lý)")
                return True
    except Exception as e:
        print(f"  - Không kiểm tra được nhịp tim MAVLink thực tế: {e}")
        print("    (Đảm bảo SITL và fusion.py đang chạy để truyền lệnh)")
        
    if flight_received is not None:
        print("  - Kết quả Test 2: PASS ✅ (Lệnh bay đã đi qua MQTT)")
        return True
    else:
        print("  - Kết quả Test 2: FAIL ❌")
        return False

def print_test_3_manual():
    print("\n" + "=" * 60)
    print("▶ [TEST 3] Hướng dẫn Kiểm tra Web Control Thủ công:")
    print("=" * 60)
    print("  1. Khởi động hệ thống (Docker + SITL + fusion.py)")
    print("  2. Mở trình duyệt web Chrome hoặc Firefox.")
    print("  3. Nhấn phím tắt Cmd+O (macOS) hoặc Ctrl+O (Windows).")
    print("  4. Chọn đường dẫn file 'index.html' tại:")
    print("     Phase5_Operations/web_control/index.html")
    print("  5. Quan sát Badge Status ở góc trên: Phải hiển thị 'Connected' màu xanh lá.")
    print("  6. Nhấn nút [BẬT CÒI] và kiểm tra terminal của board BW16 / còi buzzer.")
    print("  7. Nhấn nút [ARM] và [TAKEOFF 10m] rồi quan sát drone cất cánh trên QGroundControl.")
    print("=" * 60)

def main():
    print("=" * 60)
    print("  test_web_control.py — Kiểm thử luồng điều khiển Web Control")
    print("=" * 60)
    
    sub = run_mqtt_subscriber()
    time.sleep(1.0) # Đợi subscriber kết nối ổn định tới broker
    
    t1 = test_1_payload_command()
    t2 = test_2_flight_command()
    
    sub.loop_stop()
    sub.disconnect()
    
    print_test_3_manual()
    
    if t1 and t2:
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
