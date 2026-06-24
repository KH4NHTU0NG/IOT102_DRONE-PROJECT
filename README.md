# Drone IoT — Hệ Thống Giám Sát & Điều Khiển UAV Thông Minh

> **Phiên bản v3.3** — Simplify + Remove SRF05 | 24/06/2026

Dự án IoT tích hợp Board **Ameba BW16 (RTL8720DN)** với cụm cảm biến môi trường và Servo thả hàng, kết nối qua **MQTT** về máy chủ Python Fusion Gateway trên macOS. Dữ liệu được lưu vào **InfluxDB** (Docker), hiển thị trực quan trên **Grafana** và điều khiển qua **Web Control Dashboard**.

---

## Kiến Trúc Hệ Thống

```
┌──────────────────────────────────────────────────────────┐
│  LỚP THIẾT BỊ                                            │
│  ┌─────────────────────────────────────────────────────┐ │
│  │  BW16 RTL8720DN Payload Board                       │ │
│  │  Inputs:  DHT22 ─ PA30 │ MQ-135 ─ PB3              │ │
│  │  Outputs: OLED (I2C) │ Buzzer ─ PA14 │ LED ─ PA15  │ │
│  │           Servo (PWM) ─ PA13                        │ │
│  └──────────────┬──────────────────────────────────────┘ │
│                 │ WiFi / MQTT (broker.hivemq.com)         │
│  ┌──────────────▼──────────────┐                          │
│  │  ArduPilot SITL             │ ◄── QGroundControl       │
│  └──────────────┬──────────────┘                          │
│                 │ MAVLink TCP:5763                         │
└─────────────────┼────────────────────────────────────────┘
                  │
┌─────────────────▼────────────────────────────────────────┐
│  LỚP GATEWAY (Python fusion.py)                           │
│  ┌─────────────────────────────────────────────────────┐ │
│  │  MQTT Subscribe sensors → parse → InfluxDB Write   │ │
│  │  MQTT Subscribe flight  → dispatch thread → MAVLink │ │
│  └─────────────────────────────────────────────────────┘ │
└───────────┬──────────────────────┬───────────────────────┘
            │ HTTP API             │ MQTT WebSockets :8000
┌───────────▼──────────┐  ┌───────▼──────────────────────┐
│  InfluxDB + Grafana  │  │  Web Control Dashboard        │
│  (Docker)            │  │  (index.html)                 │
└──────────────────────┘  └──────────────────────────────┘
```

---

## Thành Phần Phần Cứng

| Linh kiện | Chân BW16 | Điện áp | Giao thức |
|:----------|:----------|:--------|:----------|
| DHT22 (Nhiệt/Ẩm) | DATA → PA30 | 3.3V | One-Wire |
| MQ-135 (Khí gas) | AOUT → PB3 | 5V | Analog ADC |
| OLED SSD1306 | SDA→PA26, SCL→PA25 | 3.3V | I2C |
| Servo SG90 | SIG → PA13 | 5V | PWM |
| Buzzer | I/O → PA14 | 3.3V | GPIO |
| LED Đỏ (Cảnh báo) | Anode → PA15 | 3.3V | GPIO |

---

## Cài Đặt Lần Đầu

### Yêu cầu

- macOS M1/M2/M3/M4
- Docker Desktop
- Arduino IDE 2.x + Board Package **Ameba RTL8720DN 3.1.9**
- Python 3.10+
- ArduPilot SITL (`sim_vehicle.py`)

### Thư viện Arduino cần cài

1. `PubSubClient` (Nick O'Leary)
2. `DHT sensor library` (Adafruit)
3. `Adafruit GFX Library`
4. `Adafruit SSD1306`

### Bước 1: Cấu hình WiFi

Mở `Phase3_BW16/bw16_sensor/secrets.h` và điền thông tin mạng:

```cpp
#define SECRET_SSID "Ten_WiFi_Cua_Ban"
#define SECRET_PASS "Mat_Khau_WiFi"
```

> ⚠️ File `secrets.h` đã được thêm vào `.gitignore`. **Không commit** file này lên GitHub.

### Bước 2: Khởi động Docker

```bash
cd Phase1_Docker
bash setup.sh
```

Script tự động:
- Khởi động Mosquitto, InfluxDB, Grafana qua Docker Compose v2
- Tạo bucket `drone_data` trong InfluxDB
- Lưu token vào `Phase4_Fusion/.influx_token`

### Bước 3: Tạo Python venv

```bash
cd Phase4_Fusion
bash setup_venv.sh
```

### Bước 4: Nạp Firmware BW16

1. Mở `Phase3_BW16/bw16_sensor/bw16_sensor.ino` bằng Arduino IDE
2. Chọn **Board** → `AI-Thinker BW16`
3. Chọn **Port** (cổng USB của BW16)
4. Nhấn **Upload**
5. Mở **Serial Monitor** ở 115200 baud — xác nhận thấy `[SYSTEM] Setup hoan tat!`

---

## Khởi Động Hàng Ngày

> ⚠️ **Quan trọng:** Tất cả lệnh bên dưới phải chạy từ thư mục `DroneIoT_macOS/`.

```bash
# Vào đúng thư mục gốc trước
cd <đường dẫn đến DroneIoT_macOS>

# Một lệnh khởi động toàn bộ hệ thống
bash Phase5_Operations/start_all.sh
```

Hoặc thủ công từng bước (cũng chạy từ `DroneIoT_macOS/`):

```bash
cd <đường dẫn đến DroneIoT_macOS>

# 1. Docker
cd Phase1_Docker && docker compose up -d && cd ..

# 2. SITL (mở Terminal mới)
cd Phase2_SITL && bash run_sitl.sh

# 3. Fusion Gateway (Terminal khác)
cd Phase4_Fusion
source drone_env/bin/activate
python3 fusion.py
```

---

## Web Control Dashboard

Mở file `Phase5_Operations/web_control/index.html` bằng Chrome/Firefox (Ctrl+O / Cmd+O).

**Tính năng:**
- Hiển thị telemetry thời gian thực: Nhiệt độ, Độ ẩm, CO2, RSSI, Khoảng cách va chạm
- Điều khiển Drone: ARM / TAKEOFF 10m / LAND / RTL (có confirm dialog)
- Điều khiển Payload: Bật/tắt Còi, Đèn LED, Servo thả hàng
- Auto-reconnect MQTT khi mất kết nối (exponential backoff, tối đa 30s)
- Disable tất cả nút khi chưa kết nối (tránh lệnh nhầm)



## Dừng Hệ Thống

```bash
# Từ thư mục gốc IOT102_DRONE-PROJECT/
cd <đường dẫn đến IOT102_DRONE-PROJECT>
bash Phase5_Operations/stop_all.sh

# Hoặc thủ công
pkill -f fusion.py
cd Phase1_Docker && docker compose down
```

---

## Changelog

### v3.1 — 24/06/2026 (UI Redesign + Script Fix)

**Web Dashboard**
- 🎨 Thiết kế lại giao diện: Engineering UI thuần túy, không có dấu hiệu AI vibe design
- Font: Inter (UI) + JetBrains Mono (data/log)
- Bảng màu neutral: `#f0f2f5` nền, white card, `#d9dde4` border
- Layout 2 cột (Telemetry | Flight+Payload) + hàng dưới (Servo + MQTT config)
- Button system: primary/danger/success/warning/secondary/ghost

**Scripts**
- 🐛 **FIX S-01**: Thêm guard kiểm tra thư mục vào `start_all.sh` — in hướng dẫn rõ ràng nếu chạy sai vị trí
- 🐛 **FIX S-02**: Tương tự cho `stop_all.sh`
- 📝 Thêm comment đầu file hướng dẫn cách chạy đúng

**README**
- 📝 Bổ sung warning "phải chạy từ DroneIoT_macOS/" ở mọi section có lệnh script
- 📝 Thêm lệnh `cd` đầy đủ đường dẫn vào tất cả code block

---

### v3.0 — 24/06/2026 (Full Audit & Optimization)

**Firmware (bw16_sensor.ino)**
- 🐛 **FIX F-01**: `is_alert` → biến toàn cục `env_alert` — OLED nay hiển thị đúng cảnh báo khí gas
- ⚡ **OPT F-05**: Thay String `+=` JSON bằng `snprintf()` — giảm heap fragmentation trên MCU
- ⚡ **OPT F-06**: `callback()` dùng `String::reserve()` — loại bỏ O(n²) memory allocation
- 🐛 **FIX F-02**: Sửa debug messages sai số chân (PA14/PA15)
- 🐛 **FIX F-03**: `updateOLED()` dùng `mq_raw_val` cached — CO2 hiển thị khớp MQTT
- 🐛 **FIX F-04**: Clamp góc Servo `constrain(angle, 0, 180)` — tránh hỏng motor
- 🧹 **OPT F-07**: Extract magic numbers thành hằng số có tên (`WIFI_MAX_RETRIES`, `OLED_I2C_ADDR`, v.v.)

**Gateway (fusion.py)**
- 🔴 **FIX G-01**: Tách TAKEOFF/LAND/RTL thành daemon threads — `master_lock` không còn bị giữ ~2.5s, `mavlink_loop` không bị starve
- 🔴 **FIX G-02+G-03**: Thêm `finally` block clean-up MQTT (`loop_stop + disconnect`) và MAVLink (`master.close()`)
- 🧹 **FIX G-04**: Xóa dead code `sensor_received` Event
- 🧹 **FIX G-05**: Xóa redundant `command_long_send` cho LAND/RTL
- 🐛 **FIX G-06**: Validate altitude `max(1.0, min(alt, 100.0))`
- 🐛 **FIX G-07**: Token placeholder check bổ sung `"YOUR_INFLUXDB_TOKEN_HERE"`

**Web Dashboard (index.html)**
- 🔴 **FIX W-01**: Sửa CDN filename `pj-paho-mqtt.min.js` → `paho-mqtt.min.js`
- 🔒 **FIX W-02**: Thay `innerHTML` bằng `textContent` — vá lỗ hổng XSS
- ⚡ **FIX W-03**: Auto-reconnect với exponential backoff (1s → 2s → 4s → ... → 30s)
- 🛡️ **FIX W-04**: Confirm dialog cho lệnh nguy hiểm (ARM/TAKEOFF/LAND/RTL)
- ⚡ **FIX W-05**: Disable tất cả control buttons khi MQTT disconnected
- ⚡ **OPT W-06**: Cache DOM references — tránh `getElementById()` lặp trong hot path
- ⚡ **OPT W-07**: `addLog()` chỉ split khi cần — O(1) thay vì O(n) mỗi lần log

**Infrastructure**
- 🔧 **I-01**: Đồng bộ MQTT topics → `iot102_drone/*`
- 🐛 **I-02**: Thêm timeout 15s cho InfluxDB khởi động
- 🔧 **I-03**: Pin Docker versions: `mosquitto:2.0`, `grafana:10.4.0`
- 🐛 **I-04**: Fix float comparison `==` → `abs() < 1.0` trong test_latency.py
- 🔧 **I-05**: `docker-compose` v1 → `docker compose` v2 trong tất cả shell scripts

### v2.6 — 23/06/2026
- Fix OLED Double Wire.begin hang (Incorrect pin: 26)
- Fix BW16 auto-reconnect WiFi

### v2.5 — 22/06/2026
- Tích hợp SITL ArduPilot với MAVLink TCP
- Thread-safe `master_lock` cho MAVLink operations
- Fusion gateway auto-drain MAVLink socket buffer

---

## Xử Lý Sự Cố

| Triệu chứng | Nguyên nhân | Giải pháp |
|:------------|:------------|:----------|
| `No such file or directory` khi chạy script | Đang ở sai thư mục | `cd DroneIoT_macOS` trước, rồi `bash Phase5_Operations/start_all.sh` |
| Serial Monitor in `Error amb_ard_pin_check_fun` | `Wire.begin()` bị gọi 2 lần | Đã sửa trong v2.6 |
| OLED không hiển thị cảnh báo gas | `is_alert` scoping sai | Đã sửa trong v3.0 |
| Web không kết nối được MQTT | CDN Paho sai filename | Đã sửa trong v3.0 |
| Gateway bị treo khi gửi TAKEOFF | `master_lock` contention | Đã sửa trong v3.0 |
| InfluxDB không nhận data | Token sai/rỗng | Chạy lại `setup.sh`, kiểm tra `.influx_token` |
| BW16 không kết nối WiFi | Sai SSID/pass | Kiểm tra `secrets.h` |

---

## Cấu Trúc Thư Mục

```
DroneIoT_macOS/
├── Phase1_Docker/
│   ├── docker-compose.yml      # Mosquitto 2.0, InfluxDB 2.0, Grafana 10.4
│   ├── mosquitto/mosquitto.conf
│   └── setup.sh                # Chạy 1 lần đầu
├── Phase2_SITL/
│   ├── install_sitl.sh
│   └── run_sitl.sh
├── Phase3_BW16/
│   └── bw16_sensor/
│       ├── bw16_sensor.ino     # Firmware v3.0
│       └── secrets.h           # ← KHÔNG commit file này
├── Phase4_Fusion/
│   ├── fusion.py               # Gateway v3.0
│   ├── requirements.txt
│   └── setup_venv.sh
└── Phase5_Operations/
    ├── start_all.sh
    ├── stop_all.sh
    ├── web_control/
    │   └── index.html          # Dashboard v3.0
    └── tests/
        ├── test_web_control.py
        ├── test_latency.py
        ├── test_continuity.py
        └── test_fault_tolerance.py
```

---

> **Tác giả:** IOT102 Drone Project — Team  
> **Môi trường:** macOS Apple Silicon + ArduPilot SITL + HiveMQ Public Broker
