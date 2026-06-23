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

MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT = 1883
MQTT_TOPIC = "tuonghuy_drone/payload/sensors"

executor = None
influx_client = None

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
    return os.environ.get("INFLUX_TOKEN", "SPSuc2iYUViMysgXOlYD61aYXaiarb7hBPfpHZBAWCknUphbdH4Vqa_C7VLEAp6622vkOXtg1W_yVx5TYG1h9A==")

# Shared test variables
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
    global latencies, executor, influx_client
    t_mqtt = time.time()
    try:
        data = json.loads(msg.payload.decode("utf-8"))
        co2_val = data.get("co2")
        if co2_val is None:
            return
        
        if executor is not None:
            executor.submit(poll_db, co2_val, t_mqtt, influx_client)
    except Exception as e:
        print(f"[TEST] Lỗi MQTT callback: {e}")

def poll_db(co2_val, t_mqtt, influx):
    global latencies
    start_poll = time.time()
    timeout = 10.0 # 10 seconds timeout
    
    while time.time() - start_poll < timeout:
        val, t_db = query_latest_co2(influx)
        if val is not None and abs(float(val) - float(co2_val)) < 1.0:  # Fix T-001: tolerance thay == float
            # Detected! Calculate latency
            t_detected = time.time()
            latency = (t_detected - t_mqtt) * 1000.0 # in ms
            
            with lock:
                latencies.append(latency)
                count = len(latencies)
                print(f"[TEST] Mẫu #{count:02d}: CO2={co2_val} -> Nhận trong DB sau {latency:.1f} ms")
                if count >= 20:
                    test_finished.set()
            break
        time.sleep(0.2) # Poll every 200ms

def main():
    print("=" * 60)
    print("  test_latency.py — Khởi động đo độ trễ từ MQTT đến InfluxDB")
    print("  Đang lắng nghe MQTT và đo đạc 20 mẫu...")
    print("=" * 60)
    
    # Khởi động MQTT
    mqtt_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT)
    except ConnectionRefusedError:
        print("[TEST] ❌ Không thể kết nối MQTT Broker. Hãy khởi động Docker.")
        sys.exit(1)
        
    threading.Thread(target=mqtt_client.loop_forever, daemon=True).start()
    
    # Khởi động thread pool executor và shared InfluxDB client
    import concurrent.futures
    global executor, influx_client
    token = get_token()
    influx_client = InfluxDBClient(url=INFLUX_URL, token=token, org=INFLUX_ORG)
    executor = concurrent.futures.ThreadPoolExecutor(max_workers=5)

    # Wait for 20 samples or timeout
    finished = test_finished.wait(timeout=60.0)
    mqtt_client.disconnect()

    if executor is not None:
        executor.shutdown(wait=False)
    if influx_client is not None:
        influx_client.close()
    
    with lock:
        if len(latencies) < 5:
            print(f"\n[TEST] ❌ Không thu thập đủ mẫu để đo đạc (chỉ có {len(latencies)} mẫu).")
            print("       Đảm bảo fusion.py đang chạy và có cảm biến BW16/stress test gửi data.")
            sys.exit(1)
            
        mean_lat = sum(latencies) / len(latencies)
        max_lat = max(latencies)
        
        print("\n" + "=" * 60)
        print("KẾT QUẢ ĐO ĐỘ TRỄ:")
        print(f"  - Số mẫu kiểm tra: {len(latencies)}")
        print(f"  - Độ trễ trung bình: {mean_lat:.1f} ms")
        print(f"  - Độ trễ lớn nhất: {max_lat:.1f} ms")
        print("=" * 60)
        
        if max_lat < 2000.0:
            print("KẾT LUẬN: PASS ✅ (Max latency < 2000ms)")
            sys.exit(0)
        else:
            print("KẾT LUẬN: FAIL ❌ (Max latency >= 2000ms)")
            sys.exit(1)

if __name__ == "__main__":
    main()
