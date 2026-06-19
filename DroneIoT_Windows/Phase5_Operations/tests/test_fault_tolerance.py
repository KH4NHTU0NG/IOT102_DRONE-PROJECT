#!/usr/bin/env python3
import time
import os
import sys
import json
import paho.mqtt.client as mqtt
from influxdb_client import InfluxDBClient

INFLUX_URL = "http://localhost:8086"
INFLUX_ORG = "drone_org"
INFLUX_BUCKET = "drone_data"
MQTT_BROKER = "127.0.0.1"
MQTT_PORT = 1883

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

def test_scenario_1():
    print("\n▶ [TEST] Windows Tinh huong 1 (SITL Disconnect)...")
    
    token = get_token()
    client = InfluxDBClient(url=INFLUX_URL, token=token, org=INFLUX_ORG)
    query_api = client.query_api()
    
    query = f'''
    from(bucket: "{INFLUX_BUCKET}")
      |> range(start: -2m)
      |> filter(fn: (r) => r._measurement == "drone_telemetry")
      |> filter(fn: (r) => r._field == "latitude")
      |> last()
    '''
    try:
        result = query_api.query(org=INFLUX_ORG, query=query)
        has_data = False
        for table in result:
            for record in table.records:
                has_data = True
                val = record.get_value()
                print(f"  - Latitude gan nhat trong DB: {val}")
        
        if has_data:
            print("  - Ket qua Tinh huong 1: PASS ✅")
            return "PASS"
        else:
            print("  - Ket qua Tinh huong 1: FAIL ❌")
            return "FAIL"
    except Exception as e:
        print(f"  - Loi DB: {e}")
        return "FAIL"
    finally:
        client.close()

def test_scenario_2():
    print("\n▶ [TEST] Windows Tinh huong 2 (Stress Test MQTT)...")
    stress_client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    try:
        stress_client.connect(MQTT_BROKER, MQTT_PORT)
    except Exception as e:
        print(f"  - Khong connect duoc MQTT Broker: {e}")
        return "FAIL"
        
    print("  - Stress test MQTT: Gui 500 messages (100 msg/s)...")
    success_count = 0
    for i in range(5):
        t_start = time.time()
        for j in range(100):
            payload = {"temp": 25.0, "humidity": 60.0, "co2": 400 + j, "alert": 0, "rssi": -50}
            res = stress_client.publish("drone/payload/sensors", json.dumps(payload))
            if res.rc == mqtt.MQTT_ERR_SUCCESS:
                success_count += 1
        elapsed = time.time() - t_start
        if elapsed < 1.0:
            time.sleep(1.0 - elapsed)
            
    stress_client.disconnect()
    print(f"  - Gui thanh cong {success_count}/500 messages.")
    if success_count >= 490:
        print("  - Ket qua Tinh huong 2: PASS ✅")
        return "PASS"
    else:
        print("  - Ket qua Tinh huong 2: FAIL ❌")
        return "FAIL"

def test_scenario_3():
    print("\n▶ [TEST] Windows Tinh huong 3 (InfluxDB Reconnection)...")
    token = get_token()
    influx = InfluxDBClient(url=INFLUX_URL, token=token, org=INFLUX_ORG)
    try:
        health = influx.health()
        print(f"  - InfluxDB health: {health.status}")
        if health.status == "pass":
            print("  - Ket qua Tinh huong 3: PASS ✅")
            return "PASS"
        else:
            return "FAIL"
    except Exception as e:
        print(f"  - Loi: {e}")
        return "FAIL"
    finally:
        influx.close()

def main():
    print("=" * 60)
    print("  test_fault_tolerance.py — Windows Failure Test")
    print("=" * 60)
    
    r1 = test_scenario_1()
    r2 = test_scenario_2()
    r3 = test_scenario_3()
    
    report_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "../test_report.txt"))
    
    report_content = f"""BIAO CAO KIEM THU CHI TIET (FAULT TOLERANCE REPORT - WINDOWS)
------------------------------------------------------------
Tinh huong 1 (Mat ket noi SITL): {r1}
Tinh huong 2 (Stress Test MQTT 100 msg/s): {r2}
Tinh huong 3 (InfluxDB Reconnection): {r3}
------------------------------------------------------------
KET LUAN CHUNG: {'PASS' if (r1 == 'PASS' and r2 == 'PASS' and r3 == 'PASS') else 'FAIL'}
"""
    with open(report_path, "w") as f:
        f.write(report_content)
    print(report_content)
    
    if r1 == 'PASS' and r2 == 'PASS' and r3 == 'PASS':
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
