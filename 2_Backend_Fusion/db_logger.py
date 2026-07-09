"""
db_logger.py — Xử lý ghi dữ liệu vào InfluxDB
"""
import os
import sys
from influxdb_client import InfluxDBClient
from influxdb_client.client.write_api import SYNCHRONOUS
import config

def load_token() -> str:
    token = config.INFLUX_TOKEN
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

def init_db():
    token = load_token()
    print("[INFLUX] Khởi tạo Client...")
    influx = InfluxDBClient(url=config.INFLUX_URL, token=token, org=config.INFLUX_ORG)
    write_api = influx.write_api(write_options=SYNCHRONOUS)
    return influx, write_api
