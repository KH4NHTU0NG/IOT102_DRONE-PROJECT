# Drone IoT — macOS Apple Silicon

> Kiểm chứng trên: macOS M-series (Apple Silicon)  
> Phiên bản: Thực tế · Đã fix 12 bugs

## Cấu trúc thư mục

```
DroneIoT_macOS/
├── Phase1_Docker/          ← MQTT + InfluxDB + Grafana
│   ├── docker-compose.yml
│   ├── mosquitto/mosquitto.conf
│   └── setup.sh            ← Chạy đầu tiên
├── Phase2_SITL/            ← Drone ảo ArduPilot
│   ├── install_sitl.sh     ← Cài 1 lần
│   └── run_sitl.sh         ← Chạy mỗi lần
├── Phase3_BW16/            ← Firmware cảm biến
│   ├── bw16_sensor.ino
│   └── wiring_diagram.md
├── Phase4_Fusion/          ← Python Gateway
│   ├── fusion.py           ← Script chính
│   ├── requirements.txt
│   └── setup_venv.sh       ← Cài 1 lần
└── Phase5_Operations/      ← Vận hành
    ├── start_all.sh        ← Khởi động toàn bộ
    ├── stop_all.sh         ← Dừng toàn bộ
    ├── grafana_queries.md
    └── checklist.md
```

---

## Bắt đầu nhanh (Quick Start)

### Lần đầu tiên (setup một lần)

```bash
# 1. Cài Docker Desktop → chờ Engine running
# Tải: https://www.docker.com/products/docker-desktop/

# 2. Dựng server Docker
bash Phase1_Docker/setup.sh
# → Copy token được in ra, sẽ dùng ở bước 4

# 3. Cài ArduPilot SITL
bash Phase2_SITL/install_sitl.sh
# → Sau khi xong, mở Terminal MỚI

# 4. Tạo Python venv
bash Phase4_Fusion/setup_venv.sh

# 5. Điền token vào fusion.py
nano Phase4_Fusion/fusion.py
# Thay TOKEN_CUA_BAN bằng token từ bước 2
```

### Mỗi lần chạy hệ thống

```bash
bash Phase5_Operations/start_all.sh
```

---

## Bugs đã fix so với tài liệu gốc

| # | Bug | File | Fix |
|---|-----|------|-----|
| 1 | `on_connect` 4-arg → DeprecationWarning | `fusion.py` | Dùng `CallbackAPIVersion.VERSION2` + 5-arg |
| 2 | MAVLink crash khi SITL chậm | `fusion.py` | Timeout + exponential backoff retry |
| 3 | TOKEN hardcode gây crash âm thầm | `fusion.py` | Kiểm tra token + load từ file/.env |
| 4 | Container không tự restart sau crash | `docker-compose.yml` | `restart: unless-stopped` |
| 5 | Data mất khi restart Docker | `docker-compose.yml` | Named volumes cho InfluxDB + Grafana |
| 6 | Mosquitto thiếu log config | `mosquitto.conf` | `log_type all` + `persistence false` |
| 7 | BW16 mất WiFi → treo vĩnh viễn | `bw16_sensor.ino` | Reconnect loop với retry counter |
| 8 | MQ-135 ADC chưa có cảnh báo điện áp | `bw16_sensor.ino` | Comment + voltage divider requirement |
| 9 | PATH không reload sau install prereqs | `install_sitl.sh` | `source ~/.zshrc` + `source ~/.zprofile` |
| 10 | SITL không chạy native trên Windows | *(macOS không bị)* | Windows: WSL2 solution |
| 11 | MAVLink disconnect giữa chừng → crash | `fusion.py` | `ConnectionResetError` catch + reconnect |
| 12 | TOKEN sai không có error message rõ | `fusion.py` | `load_token()` với hướng dẫn chi tiết |

---

## Yêu cầu phần cứng

- Mac với chip M1/M2/M3/M4 (Apple Silicon)
- Docker Desktop for Mac (Apple Silicon build)
- Board BW16 (RTL8720DN)
- Cảm biến DHT22
- Cảm biến MQ-135
- Cáp USB Type-C (có dây data, không chỉ sạc)
- Điện trở 10kΩ × 2 (voltage divider cho MQ-135)

## Yêu cầu phần mềm

- macOS Ventura 13+ hoặc Sonoma/Sequoia
- Docker Desktop 4.x (Apple Silicon)
- Python 3.10+
- Arduino IDE 2.x
- QGroundControl (tự tải về)

---

## Tài khoản mặc định

| Dịch vụ | URL | User | Password |
|---------|-----|------|----------|
| Grafana | http://localhost:3000 | admin | admin (đổi lần đầu) |
| InfluxDB | http://localhost:8086 | admin | adminpassword |
