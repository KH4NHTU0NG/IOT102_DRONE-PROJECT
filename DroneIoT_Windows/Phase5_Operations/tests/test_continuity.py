#!/usr/bin/env python3
import time
import os
import sys
from datetime import datetime
from influxdb_client import InfluxDBClient

INFLUX_URL = "http://localhost:8086"
INFLUX_ORG = "drone_org"
INFLUX_BUCKET = "drone_data"

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

def check_continuity():
    token = get_token()
    client = InfluxDBClient(url=INFLUX_URL, token=token, org=INFLUX_ORG)
    query_api = client.query_api()

    query = f'''
    from(bucket: "{INFLUX_BUCKET}")
      |> range(start: -5m)
      |> filter(fn: (r) => r._measurement == "drone_telemetry")
      |> filter(fn: (r) => r._field == "co2")
      |> keep(columns: ["_time"])
      |> sort(columns: ["_time"])
    '''
    
    try:
        result = query_api.query(org=INFLUX_ORG, query=query)
        times = []
        for table in result:
            for record in table.records:
                times.append(record.get_time())
        
        if len(times) < 2:
            print(f"[TEST] ⚠️ Khong du du lieu de tinh toan (so diem = {len(times)})")
            return False, 1.0
        
        gaps = 0
        total_intervals = len(times) - 1
        
        for i in range(1, len(times)):
            delta = (times[i] - times[i-1]).total_seconds()
            if delta > 3.0:
                gaps += 1
                
        gap_ratio = gaps / total_intervals
        print(f"[TEST] Kiem tra: {len(times)} diem, phat hien {gaps} diem gap > 3s. Ty le loi: {gap_ratio*100:.2f}%")
        
        is_pass = gap_ratio < 0.05
        return is_pass, gap_ratio

    except Exception as e:
        print(f"[TEST] ❌ Loi khi truy van InfluxDB: {e}")
        return False, 1.0
    finally:
        client.close()

def main():
    print("=" * 60)
    print("  test_continuity.py — Windows Platform Continuity Test")
    print("  Chay trong 2 phut, lay mau moi 30 giay...")
    print("=" * 60)
    
    passed_rounds = 0
    total_rounds = 4
    
    for round_num in range(1, total_rounds + 1):
        print(f"\n▶ Round {round_num}/{total_rounds} (Cho 30 giay...)")
        time.sleep(30)
        
        is_pass, ratio = check_continuity()
        if is_pass:
            print(f"Round {round_num}: [PASS] ✅")
            passed_rounds += 1
        else:
            print(f"Round {round_num}: [FAIL] ❌")
            
    print("\n" + "=" * 60)
    success_rate = (passed_rounds / total_rounds) * 100
    print(f"KET QUA: {passed_rounds}/{total_rounds} rounds thanh cong ({success_rate:.1f}%)")
    if passed_rounds == total_rounds:
        print("KET LUAN: PASS ✅")
        sys.exit(0)
    else:
        print("KET LUAN: FAIL ❌")
        sys.exit(1)

if __name__ == "__main__":
    main()
