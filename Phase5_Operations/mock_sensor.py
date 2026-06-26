#!/usr/bin/env python3
"""
MOCK SENSOR (VIRTUAL HARDWARE)
Script này giả lập Board Ameba BW16 và cụm cảm biến/servo.
Dành cho mục đích Demo hoặc Test hệ thống (Fusion Gateway + Web Control + Grafana) 
mà không cần cắm mạch thật.

Cách chạy:
    python3 Phase5_Operations/mock_sensor.py
"""

import time
import json
import random
import paho.mqtt.client as mqtt

MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
TOPIC_SENSORS = "iot102_drone/payload/sensors"
TOPIC_PAYLOAD = "iot102_drone/control/payload"

# State
current_temp = 25.0
current_hum = 60.0
current_co2 = 400

def on_connect(client, userdata, flags, reason_code, properties):
    if reason_code == 0:
        print(f"✅ [MOCK SENSOR] Đã kết nối tới MQTT Broker: {MQTT_BROKER}")
        client.subscribe(TOPIC_PAYLOAD)
        print(f"🎧 [MOCK SENSOR] Đang lắng nghe lệnh điều khiển tại: {TOPIC_PAYLOAD}")
    else:
        print(f"❌ [MOCK SENSOR] Kết nối thất bại, mã lỗi: {reason_code}")

def on_message(client, userdata, msg, properties=None):
    try:
        command = json.loads(msg.payload.decode('utf-8'))
        print(f"\n⚡ [MOCK SENSOR - NHẬN LỆNH TỪ WEB]: {command}")
        
        # Mô phỏng phản hồi phần cứng
        if 'servo_angle' in command:
            print(f"   ➔ Quay Servo tới góc {command['servo_angle']} độ")
        if 'buzzer' in command:
            state = "BẬT" if command['buzzer'] == 1 else "TẮT"
            print(f"   ➔ Còi Buzzer đang {state}")
        if 'led_red' in command:
            state = "BẬT" if command['led_red'] == 1 else "TẮT"
            print(f"   ➔ Đèn LED Đỏ đang {state}")
    except json.JSONDecodeError:
        print(f"⚠️ [MOCK SENSOR] Payload lỗi: {msg.payload}")

def generate_telemetry():
    global current_temp, current_hum, current_co2
    
    # Dao động ngẫu nhiên nhỏ gọn
    current_temp += random.uniform(-0.5, 0.5)
    current_hum += random.uniform(-1.0, 1.0)
    current_co2 += random.randint(-10, 10)
    
    # Giới hạn an toàn
    current_temp = max(15.0, min(current_temp, 45.0))
    current_hum = max(30.0, min(current_hum, 90.0))
    current_co2 = max(300, min(current_co2, 2000))
    
    alert = 1 if current_co2 > 1000 else 0
    rssi = random.randint(-70, -40)
    
    return {
        "temp": round(current_temp, 1),
        "humidity": round(current_hum, 1),
        "co2": int(current_co2),
        "alert": alert,
        "rssi": rssi,
        "dht_ok": 1
    }

def main():
    print("🚀 Khởi động MOCK SENSOR (Virtual Hardware)...")
    
    client = mqtt.Client(client_id=f"DroneIoT_Mock_{random.randint(1000, 9999)}", callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = on_connect
    client.on_message = on_message
    
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        client.loop_start()
        
        while True:
            # Sinh dữ liệu giả và publish mỗi 2 giây (giống hệt mạch BW16)
            payload = generate_telemetry()
            json_payload = json.dumps(payload)
            
            client.publish(TOPIC_SENSORS, json_payload)
            print(f"📡 [MOCK SENSOR - GỬI DATA]: {json_payload}")
            
            time.sleep(2)
            
    except KeyboardInterrupt:
        print("\n🛑 Đã dừng MOCK SENSOR.")
    except Exception as e:
        print(f"❌ Lỗi: {e}")
    finally:
        client.loop_stop()
        client.disconnect()

if __name__ == "__main__":
    main()
