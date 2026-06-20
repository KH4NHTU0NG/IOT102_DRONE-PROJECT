# Drone IoT — macOS (Apple Silicon)

Hướng dẫn dành riêng cho macOS M1/M2/M3/M4. Đọc README gốc để hiểu tổng quan kiến trúc hệ thống.

---

## Mục lục

1. [Yêu cầu hệ thống](#yêu-cầu-hệ-thống)
2. [Cấu trúc thư mục](#cấu-trúc-thư-mục)
3. [Cài đặt lần đầu](#cài-đặt-lần-đầu)
4. [Nạp firmware BW16](#nạp-firmware-bw16)
5. [Khởi động hàng ngày](#khởi-động-hàng-ngày)
6. [Cấu hình Grafana](#cấu-hình-grafana)
7. [Web Control](#web-control)
8. [Kiểm thử tự động](#kiểm-thử-tự-động)
9. [Dừng hệ thống](#dừng-hệ-thống)
10. [Xử lý sự cố](#xử-lý-sự-cố)

---

## Yêu cầu hệ thống

- Mac chip Apple Silicon (M1/M2/M3/M4), RAM tối thiểu 8GB
- Docker Desktop for Mac (bản Apple Silicon)
- Arduino IDE 2.x
- QGroundControl
- Python 3.11+
- Board BW16 (RTL8720DN) + cáp USB data

---

## Cấu trúc thư mục

```
DroneIoT_macOS/
├── Phase1_Docker/
│   ├── docker-compose.yml
│   ├── mosquitto/mosquitto.conf
│   └── setup.sh              -- Chạy 1 lần đầu
├── Phase2_SITL/
│   ├── install_sitl.sh       -- Cài 1 lần
│   └── run_sitl.sh           -- Chạy mỗi phiên
├── Phase3_BW16/
│   ├── bw16_sensor/
│   │   └── bw16_sensor.ino   -- Firmware chinh
│   └── wiring_diagram.md
├── Phase4_Fusion/
│   ├── fusion.py
│   ├── requirements.txt
│   └── setup_venv.sh         -- Tạo venv 1 lần
└── Phase5_Operations/
    ├── start_all.sh
    ├── stop_all.sh
    ├── web_control/index.html
    └── tests/
```

---

## Cài đặt lần đầu

> Chỉ thực hiện 1 lần duy nhất.

### Bước 1 — Cài phần mềm nền

```bash
# Homebrew (nếu chưa có)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Python 3.11
brew install python@3.11
```

Tải thêm:
- Docker Desktop: https://www.docker.com/products/docker-desktop (chọn bản Apple Silicon)
- Arduino IDE 2.x: https://www.arduino.cc/en/software
- QGroundControl: https://qgroundcontrol.com/downloads

### Bước 2 — Dựng Docker server

Mở Docker Desktop, chờ icon ở thanh menu ổn định, rồi chạy:

```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
bash Phase1_Docker/setup.sh
```

Terminal sẽ in ra một chuỗi InfluxDB Token dài. Token cũng được tự động lưu vào `Phase4_Fusion/.influx_token` — không cần làm thêm gì.

Kiểm tra 3 container đang chạy:

```bash
docker ps --format "table {{.Names}}\t{{.Status}}"
```

Phải thấy `iot_mqtt`, `iot_db`, `iot_grafana` đều ở trạng thái `Up`.

### Bước 3 — Cài ArduPilot SITL

> Bước này mất 15–30 phút, đừng đóng Terminal.

```bash
bash Phase2_SITL/install_sitl.sh
```

Sau khi hoàn tất, đóng Terminal hiện tại và mở Terminal mới để PATH được nạp lại đúng.

### Bước 4 — Tạo Python virtual environment

```bash
bash Phase4_Fusion/setup_venv.sh
```

---

## Nạp firmware BW16

### Đấu nối phần cứng

| Cảm biến | Chân cảm biến | Chân BW16 | Ghi chú |
|---|---|---|---|
| DHT22 | VCC | 3.3V | |
| DHT22 | GND | GND | |
| DHT22 | DATA | PA_26 | Thêm điện trở pull-up 10k ohm giữa VCC và DATA |
| MQ-135 | VCC | 5V | |
| MQ-135 | GND | GND | |
| MQ-135 | AOUT | PB_1 | Bắt buộc qua cầu phân áp 2 x 10k ohm (chip chỉ chịu 3.3V) |
| LED Đỏ | Anode | PB_3 | Qua điện trở 220 ohm |
| LED Xanh | Anode | PA_27 | Qua điện trở 220 ohm |
| Buzzer | + | PA_15 | Active High |

> Không dùng PA_12 (trùng TX Log Console) và PA_30 (chân JTAG gây treo board).

### Cài board package và thư viện

Trong Arduino IDE:
1. **Boards Manager**: tìm `AmebaD`, cài bản mới nhất.
2. **Library Manager**: tìm và cài `DHT sensor library` (Adafruit) và `PubSubClient` (Nick O'Leary).

### Cấu hình và upload

1. Mở `Phase3_BW16/bw16_sensor/bw16_sensor.ino`.
2. Sửa 3 dòng đầu:
   ```cpp
   const char* ssid        = "TEN_WIFI_CUA_BAN";
   const char* password    = "MAT_KHAU_WIFI";
   const char* mqtt_server = "192.168.x.x"; // IP máy Mac chạy Docker
   ```
   Lấy IP máy Mac: `ipconfig getifaddr en0`
3. Chọn Board: `AmebaD (RTL8720DN) > BW16`, chọn đúng cổng.
4. Nhấn **Upload**. Khi IDE bắt đầu kết nối board: nhấn giữ **BURN**, nhấn thả **RESET** một lần, rồi thả **BURN**.
5. Sau khi IDE báo `Upload done`, nhấn **RESET** để board chạy bình thường.
6. Mở Serial Monitor (115200 baud) — board sẽ in log kết nối WiFi, MQTT, rồi dữ liệu cảm biến mỗi 2 giây.

---

## Khởi động hàng ngày

Dọn tiến trình cũ trước để tránh xung đột cổng:

```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
bash Phase5_Operations/stop_all.sh
```

### Cách 1 — Tự động (khuyên dùng)

```bash
bash Phase5_Operations/start_all.sh
```

Script tự khởi động Docker, chờ SITL sẵn sàng, rồi chạy fusion.py ngầm và in log ra `Phase5_Operations/fusion.log`.

### Cách 2 — Thủ công (từng bước, dùng khi debug)

**Terminal 1 — Docker** (nếu chưa chạy):
```bash
cd Phase1_Docker && docker-compose up -d
```

**Terminal 2 — SITL** (mở tab mới Cmd+T):
```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
bash Phase2_SITL/run_sitl.sh
```
Chờ đến khi xuất hiện `MAV>` và `EKF3 IMU0 origin set`. Không đóng tab này.

**Terminal 3 — fusion.py** (mở tab mới):
```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
source Phase4_Fusion/drone_env/bin/activate
python3 Phase4_Fusion/fusion.py
```

Log bình thường:
```
[MQTT] Ket noi thanh cong broker 127.0.0.1:1883
[SITL] Ket noi thanh cong! System ID=1
[FUSION] #0001 GPS: (-35.36326, 149.16523, 584.0m) T=28.5C, H=65.0%, CO2=412, Alert=0
```

Không đóng tab này trong suốt phiên làm việc.

**QGroundControl**: Mở lên, sẽ tự kết nối qua UDP 14550 và hiện "Ready to Fly".

---

## Cấu hình Grafana

Truy cập `http://localhost:3000` — đăng nhập `admin / admin`.

### Kết nối Data Source InfluxDB (làm 1 lần)

Vào **Connections > Data Sources > Add data source > InfluxDB**:

| Trường | Giá trị |
|---|---|
| Query Language | `Flux` |
| URL | `http://influxdb:8086` |
| Organization | `drone_org` |
| Bucket | `drone_data` |
| Token | Chạy: `cat Phase4_Fusion/.influx_token` |

Nhấn **Save & Test** — phải thấy `datasource is working`.

### Tạo Dashboard

Tạo 6 panel, mỗi panel dùng query template sau (chỉ thay `"temperature"` thành field tương ứng):

```flux
from(bucket: "drone_data")
  |> range(start: -10m)
  |> filter(fn: (r) => r._measurement == "drone_telemetry")
  |> filter(fn: (r) => r._field == "temperature")
```

| Panel | Field |
|---|---|
| Nhiet do (C) | `temperature` |
| Do am (%) | `humidity` |
| Chat luong khi CO2 | `co2` |
| Do cao bay (m) | `altitude` |
| Vi do GPS | `latitude` |
| WiFi RSSI | `wifi_rssi` |

### Panel bản đồ quỹ đạo (Geomap)

```flux
from(bucket: "drone_data")
  |> range(start: -30m)
  |> filter(fn: (r) => r._measurement == "drone_telemetry")
  |> filter(fn: (r) => r._field == "latitude" or r._field == "longitude")
  |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
```

Chọn Visualization: **Geomap** > Map Layer > Location Mode: `Coords` > Latitude field: `latitude` > Longitude field: `longitude`.

### Alert CO2

**Alerting > Alert rules > Create rule** — đặt điều kiện `last() > 600` trên field `co2`, Evaluation interval `10s`, Pending period `30s`.

---

## Web Control

Web Control kết nối trực tiếp với Mosquitto qua WebSocket cổng 9001, không cần backend.

**Mở bằng Firefox** (khuyên dùng — Chrome block WebSocket từ file://):
```
File > Open File > Phase5_Operations/web_control/index.html
```

Hoặc dùng Chrome qua HTTP server:
```bash
cd Phase5_Operations/web_control
python3 -m http.server 8080
# Mở: http://localhost:8080
```

Badge góc trên phải hiện **"Da ket noi"** xanh = OK.

Chức năng:
- **ARM / TAKEOFF / LAND / RTL**: Gửi lệnh bay tới SITL qua fusion.py
- **BAT COI / TAT COI**: Điều khiển buzzer trực tiếp trên BW16
- **BAT LED / TAT LED**: Điều khiển LED trên BW16
- **Khoi phuc tu dong**: Trả quyền điều khiển còi/LED về logic cảm biến tự động

---

## Kiểm thử tự động

```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
source Phase4_Fusion/drone_env/bin/activate

# 1. Tinh lien tuc du lieu (gap > 3s phai duoi 5%)
python3 Phase5_Operations/tests/test_continuity.py

# 2. Do tre MQTT den InfluxDB (yeu cau < 2000ms)
python3 Phase5_Operations/tests/test_latency.py

# 3. Stress test MQTT + kha nang chiu loi
python3 Phase5_Operations/tests/test_fault_tolerance.py

# 4. Luong dieu khien Web Control
python3 Phase5_Operations/tests/test_web_control.py
```

Kết quả ghi vào `Phase5_Operations/test_report.txt`.

---

## Dừng hệ thống

```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
bash Phase5_Operations/stop_all.sh
```

Hoặc thủ công: Ctrl+C ở tab fusion.py > Ctrl+C ở tab SITL > `docker-compose -f Phase1_Docker/docker-compose.yml down`.

---

## Xử lý sự cố

| Triệu chứng | Nguyên nhân | Cách xử lý |
|---|---|---|
| Board treo sau khi in banner, không in thêm gì | Chân GPIO xung đột | Không dùng PA_12, PA_30 |
| MQTT thất bại `rc=-2` | Sai IP broker | Chạy `ipconfig getifaddr en0`, cập nhật `mqtt_server` trong code |
| fusion.py in GPS (0.00, 0.00) | SITL chưa chạy hoặc port 5763 bị chiếm | Chạy stop_all.sh rồi khởi động lại SITL |
| Nhiet do / do am hien thi 0 | DHT22 chua cam hoac sai chan | Cam DHT22 vao PA_26, xem Serial Monitor |
| Web badge "Ket noi loi" | Mosquitto chua chay hoac dung Chrome voi file:// | Kiem tra `docker ps`, doi sang Firefox |
| TAKEOFF that bai | Pre-arm check chua pass | Cho QGroundControl hien "Ready to Fly" truoc |
| `iot_db already in use` | Container cu con ton tai | `docker rm -f iot_db iot_mqtt iot_grafana` |
| `No such file or directory` | Dang dung sai thu muc | `cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS` truoc khi chay |
