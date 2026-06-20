# IOT102 Drone IoT Project

Hệ thống giám sát môi trường trên drone, kết hợp dữ liệu cảm biến thực tế từ board BW16 và dữ liệu bay từ mô phỏng ArduPilot SITL.

---

## Mục lục

1. [Tổng quan](#tổng-quan)
2. [Cấu trúc thư mục](#cấu-trúc-thư-mục)
3. [Kiến trúc hệ thống](#kiến-trúc-hệ-thống)
4. [Phần cứng — Đấu nối BW16](#phần-cứng--đấu-nối-bw16)
5. [Cài đặt lần đầu](#cài-đặt-lần-đầu)
6. [Khởi động hàng ngày](#khởi-động-hàng-ngày)
7. [Dừng hệ thống](#dừng-hệ-thống)
8. [Kiểm thử tự động](#kiểm-thử-tự-động)
9. [Xử lý sự cố](#xử-lý-sự-cố)

---

## Tổng quan

Hệ thống gồm 3 luồng dữ liệu chính:

- **Cảm biến thực (BW16)**: DHT22 đo nhiệt độ/độ ẩm, MQ-135 đo chất lượng không khí. Dữ liệu gửi qua WiFi theo giao thức MQTT.
- **Drone ảo (SITL)**: ArduPilot SITL mô phỏng GPS, độ cao, vận tốc. Kết nối qua giao thức MAVLink/TCP.
- **Gateway tổng hợp (fusion.py)**: Script Python nhận cả hai luồng, đồng bộ theo thời gian thực và ghi vào InfluxDB.

Giao diện gồm Grafana (biểu đồ) và Web Control (điều khiển còi/LED và lệnh bay).

---

## Cấu trúc thư mục

```
IOT102_DRONE-PROJECT/
├── README.md
├── DroneIoT_macOS/          -- Dành cho macOS (Apple Silicon)
│   ├── README.md
│   ├── Phase1_Docker/       -- Mosquitto, InfluxDB, Grafana (docker-compose)
│   ├── Phase2_SITL/         -- Cài đặt và chạy ArduPilot SITL
│   ├── Phase3_BW16/         -- Firmware Arduino cho board BW16
│   ├── Phase4_Fusion/       -- fusion.py + Python venv
│   └── Phase5_Operations/   -- Scripts khởi động/dừng + tests
└── DroneIoT_Windows/        -- Dành cho Windows 10/11 (WSL2)
    ├── README.md
    ├── Phase1_Docker/
    ├── Phase2_SITL/
    ├── Phase3_BW16/
    ├── Phase4_Fusion/
    └── Phase5_Operations/
```

---

## Kiến trúc hệ thống

```
[DHT22 + MQ-135]
       |
    [BW16]  --WiFi/MQTT-->  [Mosquitto Broker :1883]
                                     |
[ArduPilot SITL] --TCP/MAVLink-->  [fusion.py]  -->  [InfluxDB :8086]
                                                           |
                                                     [Grafana :3000]

[Web Control :9001 WebSocket] --> [fusion.py] --> SITL / BW16
```

| Thành phần | Công nghệ | Cổng |
|---|---|---|
| MQTT Broker | Mosquitto (Docker) | 1883 (TCP), 9001 (WebSocket) |
| Database | InfluxDB 2.x (Docker) | 8086 |
| Dashboard | Grafana (Docker) | 3000 |
| Drone ảo | ArduPilot SITL + MAVProxy | 5763 (TCP), 14550 (UDP) |
| Gateway | Python fusion.py | - |
| Web Control | HTML tĩnh + Paho MQTT | - |

---

## Phần cứng — Đấu nối BW16

### Sơ đồ chân

| Cảm biến | Chân cảm biến | Chân BW16 | Ghi chú |
|---|---|---|---|
| DHT22 | VCC | 3.3V | |
| DHT22 | GND | GND | |
| DHT22 | DATA | PA_26 | Thêm điện trở pull-up 10k ohm giữa VCC và DATA |
| MQ-135 | VCC | 5V | |
| MQ-135 | GND | GND | |
| MQ-135 | AOUT | PB_1 | Bắt buộc qua cầu phân áp (2 x 10k ohm) vì BW16 chỉ chịu 3.3V |
| LED Đỏ | + | PB_3 | Qua điện trở 220 ohm |
| LED Xanh | + | PA_27 | Qua điện trở 220 ohm |
| Buzzer | + | PA_15 | Active High |

> Lưu ý: Không dùng PA_12 (trùng TX Log Console) và PA_30 (chân JTAG, sẽ gây treo board).

### Nạp firmware lên BW16

1. Mở Arduino IDE, cài Board Package **AmebaD** và thư viện `DHT sensor library` + `PubSubClient`.
2. Mở file `Phase3_BW16/bw16_sensor/bw16_sensor.ino`.
3. Sửa 3 dòng cấu hình:
   ```cpp
   const char* ssid        = "TEN_WIFI_CUA_BAN";
   const char* password    = "MAT_KHAU_WIFI";
   const char* mqtt_server = "IP_MAY_TINH_CHAY_DOCKER"; // vd: 192.168.1.15
   ```
4. Chọn Board: `AmebaD (RTL8720DN) > BW16` và chọn đúng cổng COM.
5. Nhấn **Upload**. Khi IDE bắt đầu kết nối, nhấn giữ **BURN** trên board rồi nhấn thả **RESET** một lần, sau đó thả **BURN**.
6. Sau khi IDE báo `Upload done`, nhấn **RESET** một lần nữa để board chạy bình thường.
7. Mở Serial Monitor (115200 baud) — bạn sẽ thấy log kết nối WiFi và MQTT, rồi dữ liệu cảm biến gửi đi mỗi 2 giây.

---

## Cài đặt lần đầu

> Chỉ làm 1 lần duy nhất.

### Bước 1 — Khởi động Docker và lấy InfluxDB Token

**macOS:**
```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
bash Phase1_Docker/setup.sh
```

**Windows:**
```cmd
cd C:\Users\ten_user\Desktop\IOT102_DRONE-PROJECT\DroneIoT_Windows
Phase1_Docker\setup.bat
```

Sau khi chạy xong, terminal sẽ in ra một chuỗi token dài. Sao chép token đó.

### Bước 2 — Dán token vào fusion.py

Mở file `Phase4_Fusion/.influx_token` (tạo mới nếu chưa có) và dán token vào:

```
SPSuc2iYUViMysgXOlYD61aYXaiarb7hBPfpHZBAWCknUphbdH4Vqa_C7VLEAp6622vkOXtg1W_yVx5TYG1h9A==
```

(Token trên là ví dụ — dùng token thực từ output của setup.sh)

### Bước 3 — Tạo Python virtual environment

**macOS:**
```bash
bash Phase4_Fusion/setup_venv.sh
```

**Windows:**
```cmd
Phase4_Fusion\setup_venv.bat
```

### Bước 4 — Cài đặt ArduPilot SITL

Xem hướng dẫn chi tiết trong `Phase2_SITL/README.md` của từng platform.

---

## Khởi động hàng ngày

> Dọn tiến trình cũ trước để tránh lỗi xung đột cổng:

**macOS:** `bash Phase5_Operations/stop_all.sh`  
**Windows:** `Phase5_Operations\stop_all.bat`

### Cách 1 — Tự động (khuyên dùng)

**macOS:**
```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
bash Phase5_Operations/start_all.sh
```

**Windows:**
```cmd
cd C:\Users\ten_user\Desktop\IOT102_DRONE-PROJECT\DroneIoT_Windows
Phase5_Operations\start_all.bat
```

Script sẽ tự khởi động Docker, chờ SITL, rồi chạy fusion.py ngầm.

---

### Cách 2 — Thủ công (từng bước, dùng khi debug)

**Bước 1: Docker**
```bash
# macOS
cd DroneIoT_macOS/Phase1_Docker && docker-compose up -d

# Windows
cd DroneIoT_Windows\Phase1_Docker && docker-compose up -d
```

**Bước 2: SITL** — Mở terminal mới
```bash
# macOS
bash Phase2_SITL/run_sitl.sh

# Windows (PowerShell)
powershell Phase2_SITL/run_sitl.ps1
```
Chờ đến khi xuất hiện dòng `MAV>` và `EKF3 IMU0 origin set`.

**Bước 3: fusion.py** — Mở terminal mới
```bash
# macOS
source Phase4_Fusion/drone_env/bin/activate
python3 Phase4_Fusion/fusion.py

# Windows
Phase4_Fusion\drone_env\Scripts\activate
python Phase4_Fusion\fusion.py
```
Kết quả bình thường trên terminal:
```
[FUSION] #0001 GPS: (-35.36326, 149.16523, 584.0m) T=28.5C, H=65.0%, CO2=412, Alert=0
```

**Bước 4: Web Control**

Mở bằng Firefox (khuyên dùng) hoặc chạy HTTP server nhỏ rồi mở Chrome:
```bash
# Chạy HTTP server (nếu dùng Chrome)
cd Phase5_Operations/web_control
python3 -m http.server 8080
# Mở trình duyệt: http://localhost:8080
```
Badge góc trên phải hiển thị **"Da ket noi"** — hệ thống hoạt động.

**Bước 5: Grafana**

Truy cập `http://localhost:3000` — đăng nhập `admin / admin`.

Kết nối Data Source InfluxDB:
- Query Language: `Flux`
- URL: `http://influxdb:8086`
- Organization: `drone_org`
- Bucket: `drone_data`
- Token: token lấy từ Bước 1 cài đặt

---

## Dừng hệ thống

```bash
# macOS
bash Phase5_Operations/stop_all.sh

# Windows
Phase5_Operations\stop_all.bat
```

---

## Kiểm thử tự động

Kích hoạt virtual environment trước, rồi chạy từng file test:

```bash
# Kích hoạt venv (macOS)
source Phase4_Fusion/drone_env/bin/activate

# 1. Tính liên tục dữ liệu (gap > 3s phải dưới 5%)
python Phase5_Operations/tests/test_continuity.py

# 2. Độ trễ từ MQTT đến InfluxDB (yêu cầu < 2000ms)
python Phase5_Operations/tests/test_latency.py

# 3. Stress test MQTT + khả năng chịu lỗi
python Phase5_Operations/tests/test_fault_tolerance.py

# 4. Luồng điều khiển Web Control
python Phase5_Operations/tests/test_web_control.py
```

Kết quả chi tiết được ghi vào `Phase5_Operations/test_report.txt`.

---

## Xử lý sự cố

| Triệu chứng | Nguyên nhân | Cách xử lý |
|---|---|---|
| Board BW16 treo ngay sau khi khởi động, không in gì sau banner | Chân GPIO bị xung đột | Kiểm tra không dùng PA_12, PA_30 trong code |
| Serial Monitor thấy WiFi OK nhưng MQTT thất bại `rc=-2` | Sai IP broker | Chạy `ipconfig getifaddr en0` (macOS) để lấy IP đúng, cập nhật `mqtt_server` trong code |
| fusion.py in GPS (0.00, 0.00) | SITL chưa khởi động hoặc port 5763 bị chiếm | Chạy stop_all.sh rồi khởi động lại SITL trước |
| Web Control badge "Ket noi loi" | Mosquitto chưa chạy hoặc mở file:/// bằng Chrome | Kiểm tra `docker ps`, dùng Firefox hoặc chạy HTTP server |
| Nhiệt độ/độ ẩm hiển thị 0 | DHT22 chưa cắm hoặc sai chân | Cắm DHT22 vào PA_26, kiểm tra Serial Monitor xem board báo lỗi không |
| TAKEOFF không hoạt động | Pre-arm check thất bại trong SITL | Chờ QGroundControl hiển thị "Ready to Fly" trước khi nhấn TAKEOFF |

---

## Thông tin

Dự án môn **IOT102** — Trường Đại học FPT.  
Phát triển bởi Khánh Tường.
