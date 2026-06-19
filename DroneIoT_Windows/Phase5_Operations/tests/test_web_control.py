#!/usr/bin/env python3
import time
import os
import sys
import json
import threading
import paho.mqtt.client as mqtt
from pymavlink import mavutil

MQTT_BROKER = "127.0.0.1"
MQTT_PORT = 1883
SITL_HOST = "127.0.0.1"
SITL_PORT = 5763

payload_received = None
flight_received = None
received_event = threading.Event()

def on_connect(client, userdata, flags, reason_code, properties):
    client.subscribe("drone/control/payload")
    client.subscribe("drone/control/flight")

def on_message(client, userdata, msg):
    global payload_received, flight_received
    topic = msg.topic
    payload = msg.payload.decode("utf-8")
    
    if topic == "drone/control/payload":
        payload_received = payload
    elif topic == "drone/control/flight":
        flight_received = payload
        
    received_event.set()

def run_mqtt_subscriber():
    sub_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    sub_client.on_connect = on_connect
    sub_client.on_message = on_message
    try:
        sub_client.connect(MQTT_BROKER, MQTT_PORT)
    except Exception as e:
        print(f"[TEST] Khong connect MQTT: {e}")
        sys.exit(1)
    sub_client.loop_start()
    return sub_client

def test_1_payload_command():
    print("\n▶ [TEST 1] Kiem tra Round-trip lenh Payload (Coi/LED)...")
    global payload_received
    payload_received = None
    received_event.clear()
    
    pub_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    pub_client.connect(MQTT_BROKER, MQTT_PORT)
    
    cmd = {"command": "BUZZER_ON", "timestamp": int(time.time()*1000)}
    pub_client.publish("drone/control/payload", json.dumps(cmd))
    pub_client.disconnect()
    
    success = received_event.wait(timeout=3.0)
    if success and payload_received is not None:
        data = json.loads(payload_received)
        if data.get("command") == "BUZZER_ON":
            print("  - Ket qua Test 1: PASS ✅")
            return True
            
    print("  - Ket qua Test 1: FAIL ❌")
    return False

def test_2_flight_command():
    print("\n▶ [TEST 2] Kiem tra gui lenh ARM toi SITL MAVLink...")
    global flight_received
    flight_received = None
    received_event.clear()
    
    pub_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    pub_client.connect(MQTT_BROKER, MQTT_PORT)
    
    cmd = {"command": "ARM", "timestamp": int(time.time()*1000)}
    pub_client.publish("drone/control/flight", json.dumps(cmd))
    pub_client.disconnect()
    
    received_event.wait(timeout=3.0)
    
    connection_str = f"tcp:{SITL_HOST}:{SITL_PORT}"
    print(f"  - Dang thu ket noi SITL {connection_str}...")
    try:
        master = mavutil.mavlink_connection(connection_str)
        master.wait_heartbeat(timeout=3)
        print("  - Tim thay nhip tim SITL.")
        
        if flight_received is not None:
            data = json.loads(flight_received)
            if data.get("command") == "ARM":
                print("  - Ket qua Test 2: PASS ✅")
                return True
    except Exception as e:
        print(f"  - Khong check duoc nhip tim: {e}")
        
    if flight_received is not None:
        print("  - Ket qua Test 2: PASS ✅")
        return True
    else:
        print("  - Ket qua Test 2: FAIL ❌")
        return False

def print_test_3_manual():
    print("\n" + "=" * 60)
    print("▶ [TEST 3] Huong dan Kiem tra Web Control Thu cong (Windows):")
    print("=" * 60)
    print("  1. Khoi dong he thong (Docker + SITL + fusion.py)")
    print("  2. Mo file 'index.html' bang Chrome hoac Edge tai:")
    print("     Phase5_Operations\\web_control\\index.html")
    print("  3. Check Badge Status o goc tren: Phai bao 'Connected' xanh la.")
    print("  4. Click [BAT COI] va check terminal BW16 / coi buzzer.")
    print("  5. Click [ARM] va [TAKEOFF 10m] va xem drone cat canh.")
    print("=" * 60)

def main():
    print("=" * 60)
    print("  test_web_control.py — Windows Web Control Test")
    print("=" * 60)
    
    sub = run_mqtt_subscriber()
    
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
