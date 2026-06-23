#!/usr/bin/env python3
import time
import os
import sys
import json
import threading
import paho.mqtt.client as mqtt
from influxdb_client import InfluxDBClient

INFLUX_URL = "http://localhost:8086"
INFLUX_ORG = "drone_org"
INFLUX_BUCKET = "drone_data"

MQTT_BROKER = "127.0.0.1"
MQTT_PORT = 1883
MQTT_TOPIC = "tuonghuy_drone/payload/sensors"

def get_token():
    paths = [
        "../Phase4_Fusion/.influx_token",
        "../../Phase4_Fusion/.influx_token",
        "./.influx_token",
        "Phase4_Fusion/.influx_token"
    ]
    for p in paths:
        abs_p = os.path.abspath(os.path.join(os.path.dirname(__file__), p))
        if os.path.exists(abs_p):
            with open(abs_p) as f:
                return f.read().strip()
    token = os.environ.get("INFLUX_TOKEN", "")
    if not token:
        print("[ERROR] Không tìm thấy INFLUX_TOKEN. Chạy setup.sh hoặc export INFLUX_TOKEN=...")
        sys.exit(1)
    return token

latencies = []
test_finished = threading.Event()
lock = threading.Lock()

def query_latest_co2(client):
    query_api = client.query_api()
    query = f'''
    from(bucket: "{INFLUX_BUCKET}")
      |> range(start: -1m)
      |> filter(fn: (r) => r._measurement == "drone_telemetry")
      |> filter(fn: (r) => r._field == "co2")
      |> last()
    '''
    try:
        result = query_api.query(org=INFLUX_ORG, query=query)
        for table in result:
            for record in table.records:
                return record.get_value(), record.get_time()
    except Exception:
        pass
    return None, None

def on_connect(client, userdata, flags, reason_code, properties):
    client.subscribe(MQTT_TOPIC)

def on_message(client, userdata, msg):
    global latencies
    t_mqtt = time.time()
    try:
        data = json.loads(msg.payload.decode("utf-8"))
        co2_val = data.get("co2")
        if co2_val is None:
            return
        
        threading.Thread(target=poll_db, args=(co2_val, t_mqtt), daemon=True).start()
    except Exception as e:
        print(f"[TEST] Loi MQTT callback: {e}")

def poll_db(co2_val, t_mqtt):
    global latencies
    token = get_token()
    influx = InfluxDBClient(url=INFLUX_URL, token=token, org=INFLUX_ORG)
    
    start_poll = time.time()
    timeout = 10.0
    
    while time.time() - start_poll < timeout:
        val, t_db = query_latest_co2(influx)
        if val is not None and abs(float(val) - float(co2_val)) < 1.0:
            t_detected = time.time()
            latency = (t_detected - t_mqtt) * 1000.0
            
            with lock:
                latencies.append(latency)
                count = len(latencies)
                print(f"[TEST] Mau #{count:02d}: CO2={co2_val} -> Nhan trong DB sau {latency:.1f} ms")
                if count >= 20:
                    test_finished.set()
            break
        time.sleep(0.2)
        
    influx.close()

def main():
    print("=" * 60)
    print("  test_latency.py — Windows Platform Latency Test")
    print("  Dang lang nghe MQTT va do dac 20 mau...")
    print("=" * 60)
    
    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT)
    except ConnectionRefusedError:
        print("[TEST] ❌ Khong ket noi duoc MQTT Broker. Hay start Docker.")
        sys.exit(1)
        
    threading.Thread(target=mqtt_client.loop_forever, daemon=True).start()
    
    finished = test_finished.wait(timeout=60.0)
    mqtt_client.disconnect()
    
    with lock:
        if len(latencies) < 5:
            print(f"\n[TEST] ❌ Khong du mau de do dac (chi co {len(latencies)} mau).")
            sys.exit(1)
            
        mean_lat = sum(latencies) / len(latencies)
        max_lat = max(latencies)
        
        print("\n" + "=" * 60)
        print("KET QUA DO DO TRE:")
        print(f"  - So mau kiem tra: {len(latencies)}")
        print(f"  - Do tre trung binh: {mean_lat:.1f} ms")
        print(f"  - Do tre lon nhat: {max_lat:.1f} ms")
        print("=" * 60)
        
        if max_lat < 2000.0:
            print("KET LUAN: PASS ✅ (Max latency < 2000ms)")
            sys.exit(0)
        else:
            print("KET LUAN: FAIL ❌ (Max latency >= 2000ms)")
            sys.exit(1)

if __name__ == "__main__":
    main()
