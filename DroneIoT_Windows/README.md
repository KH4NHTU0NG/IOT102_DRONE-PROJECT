# Drone IoT — Windows 10/11

Hướng dẫn dành riêng cho Windows 10/11. Đọc README gốc để hiểu tổng quan kiến trúc hệ thống.

---

## Mục lục

1. [Yêu cầu hệ thống](#yêu-cầu-hệ-thống)
2. [Khác biệt so với macOS](#khác-biệt-so-với-macos)
3. [Cấu trúc thư mục](#cấu-trúc-thư-mục)
4. [Cài đặt lần đầu](#cài-đặt-lần-đầu)
5. [Nạp firmware BW16](#nạp-firmware-bw16)
6. [Khởi động hàng ngày](#khởi-động-hàng-ngày)
7. [Cấu hình Grafana](#cấu-hình-grafana)
8. [Web Control](#web-control)
9. [Kiểm thử tự động](#kiểm-thử-tự-động)
10. [Dừng hệ thống](#dừng-hệ-thống)
11. [Xử lý sự cố](#xử-lý-sự-cố)

---

## Yêu cầu hệ thống

- Windows 10 (build 19041+) hoặc Windows 11
- WSL2 với Ubuntu 22.04 LTS (bắt buộc cho SITL)
- Docker Desktop 4.x, bật WSL2 backend
- Python 3.10+ cài trên Windows host (không phải trong WSL2)
- Arduino IDE 2.x
- PowerShell 5.1+
- RAM tối thiểu 8GB (khuyến nghị 16GB)

---

## Khác biệt so với macOS

| Thành phần | macOS | Windows |
|---|---|---|
| ArduPilot SITL | Chạy native bash | Bắt buộc qua WSL2 |
| Script khởi động | `.sh` | `.bat` / `.ps1` |
| Lấy IP máy tính | `ipconfig getifaddr en0` | `ipconfig` (tìm dòng IPv4) |
| Kiểm tra port | `lsof -i :5763` | `netstat -an \| findstr 5763` |
| Mở Python venv | `source drone_env/bin/activate` | `drone_env\Scripts\activate` |

---

## Cấu trúc thư mục

```
DroneIoT_Windows/
├── Phase1_Docker/
│   ├── docker-compose.yml
│   ├── mosquitto/mosquitto.conf
│   └── setup.bat             -- Chạy 1 lần đầu
├── Phase2_SITL/
│   ├── wsl2_setup.md         -- Đọc trước khi bắt đầu
│   ├── install_sitl.ps1      -- Cài 1 lần (PowerShell)
│   └── run_sitl.ps1          -- Chạy mỗi phiên (PowerShell)
├── Phase3_BW16/
│   ├── bw16_sensor/
│   │   └── bw16_sensor.ino   -- Firmware chinh
│   └── wiring_diagram.md
├── Phase4_Fusion/
│   ├── fusion.py
│   ├── requirements.txt
│   └── setup_venv.bat        -- Tạo venv 1 lần
└── Phase5_Operations/
    ├── start_all.bat
    ├── stop_all.bat
    ├── web_control/index.html
    └── tests/
```

---

## Cài đặt lần đầu

> Chỉ thực hiện 1 lần duy nhất.

### Bước 1 — Cài WSL2 và Ubuntu

Mở PowerShell với quyền Administrator:

```powershell
# Bật WSL2
wsl --install -d Ubuntu-22.04

# Cho phep chay script PowerShell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

Khởi động lại máy nếu được yêu cầu. Xem thêm chi tiết trong `Phase2_SITL\wsl2_setup.md`.

### Bước 2 — Cài Docker Desktop

Tải tại https://www.docker.com/products/docker-desktop — chọn bản Windows.

Sau khi cài: vào **Settings > General**, bật **"Use WSL 2 based engine"**.

### Bước 3 — Cài Python trên Windows host

Tải tại https://python.org — bắt buộc tích chọn **"Add Python to PATH"** khi cài.

### Bước 4 — Dựng Docker server

Mở Docker Desktop, chờ icon ở taskbar ổn định, rồi mở CMD:

```cmd
cd C:\Users\ten_user\Desktop\IOT102_DRONE-PROJECT\DroneIoT_Windows
Phase1_Docker\setup.bat
```

Terminal sẽ in ra InfluxDB Token. Token cũng được lưu tự động vào `Phase4_Fusion\.influx_token`.

Kiểm tra container:

```cmd
docker ps
```

Phải thấy `iot_mqtt`, `iot_db`, `iot_grafana` đều đang chạy.

### Bước 5 — Cài ArduPilot SITL trong WSL2

Mở PowerShell:

```powershell
cd C:\Users\ten_user\Desktop\IOT102_DRONE-PROJECT\DroneIoT_Windows
.\Phase2_SITL\install_sitl.ps1
```

> Bước này mất 15–30 phút, đừng đóng cửa sổ.

### Bước 6 — Tạo Python virtual environment

```cmd
Phase4_Fusion\setup_venv.bat
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
| LED Do | Anode | PB_3 | Qua dien tro 220 ohm |
| LED Xanh | Anode | PA_27 | Qua dien tro 220 ohm |
| Buzzer | + | PA_15 | Active High |

> Không dùng PA_12 (trùng TX Log Console) và PA_30 (chân JTAG gây treo board).

### Upload code

1. Mở `Phase3_BW16\bw16_sensor\bw16_sensor.ino` bằng Arduino IDE.
2. Sửa 3 dòng cấu hình:
   ```cpp
   const char* ssid        = "TEN_WIFI_CUA_BAN";
   const char* password    = "MAT_KHAU_WIFI";
   const char* mqtt_server = "192.168.x.x"; // IP máy Windows
   ```
   Lấy IP: mở CMD > gõ `ipconfig` > tìm dòng `IPv4 Address`.
3. Chọn Board: `AmebaD (RTL8720DN) > BW16`, chọn đúng cổng COM.
4. Nhấn **Upload**. Khi IDE bắt đầu kết nối board: nhấn giữ **BURN**, nhấn thả **RESET** một lần, rồi thả **BURN**.
5. Sau khi IDE báo `Upload done`, nhấn **RESET** để board chạy bình thường.
6. Mở Serial Monitor (115200 baud) để xem log kết nối và dữ liệu cảm biến.

---

## Khởi động hàng ngày

Dọn tiến trình cũ trước:

```cmd
Phase5_Operations\stop_all.bat
```

### Cách 1 — Tự động (khuyên dùng)

```cmd
cd C:\Users\ten_user\Desktop\IOT102_DRONE-PROJECT\DroneIoT_Windows
Phase5_Operations\start_all.bat
```

### Cách 2 — Thủ công (từng bước, dùng khi debug)

**Cửa sổ CMD 1 — Docker** (nếu chưa chạy):
```cmd
cd Phase1_Docker && docker-compose up -d
```

**Cửa sổ PowerShell 2 — SITL**:
```powershell
cd C:\Users\ten_user\Desktop\IOT102_DRONE-PROJECT\DroneIoT_Windows
.\Phase2_SITL\run_sitl.ps1
```
Chờ đến khi xuất hiện `MAV>` và `EKF3 IMU0 origin set`. Không đóng cửa sổ này.

**Cửa sổ CMD 3 — fusion.py**:
```cmd
cd C:\Users\ten_user\Desktop\IOT102_DRONE-PROJECT\DroneIoT_Windows
Phase4_Fusion\drone_env\Scripts\activate
python Phase4_Fusion\fusion.py
```

Log bình thường:
```
[MQTT] Ket noi thanh cong broker 127.0.0.1:1883
[SITL] Ket noi thanh cong! System ID=1
[FUSION] #0001 GPS: (-35.36326, 149.16523, 584.0m) T=28.5C, H=65.0%, CO2=412, Alert=0
```

**QGroundControl**: Mở lên, tự kết nối qua UDP 14550 và hiện "Ready to Fly".

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
| Token | Chạy: `type Phase4_Fusion\.influx_token` trong CMD |

Nhấn **Save & Test** — phải thấy `datasource is working`.

### Tạo Dashboard

6 panel, mỗi panel dùng query template (chỉ thay field):

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

Visualization: **Geomap** > Location Mode: `Coords` > Latitude: `latitude` > Longitude: `longitude`.

---

## Web Control

Web Control kết nối Mosquitto qua WebSocket cổng 9001.

Mở bằng Edge hoặc Firefox:
```
Phase5_Operations\web_control\index.html
```

Nếu dùng Chrome cần chạy HTTP server trước:
```cmd
cd Phase5_Operations\web_control
python -m http.server 8080
REM Mo trinh duyet: http://localhost:8080
```

Badge góc trên phải hiện **"Da ket noi"** xanh = OK.

Chức năng:
- **ARM / TAKEOFF / LAND / RTL**: Gửi lệnh bay tới SITL qua fusion.py
- **BAT COI / TAT COI**: Điều khiển buzzer trực tiếp trên BW16
- **BAT LED / TAT LED**: Điều khiển LED trên BW16
- **Khoi phuc tu dong**: Trả quyền điều khiển về logic cảm biến

---

## Kiểm thử tự động

```cmd
cd C:\Users\ten_user\Desktop\IOT102_DRONE-PROJECT\DroneIoT_Windows
Phase4_Fusion\drone_env\Scripts\activate

REM 1. Tinh lien tuc du lieu
python Phase5_Operations\tests\test_continuity.py

REM 2. Do tre MQTT den InfluxDB
python Phase5_Operations\tests\test_latency.py

REM 3. Stress test MQTT + kha nang chiu loi
python Phase5_Operations\tests\test_fault_tolerance.py

REM 4. Luong dieu khien Web Control
python Phase5_Operations\tests\test_web_control.py
```

Kết quả ghi vào `Phase5_Operations\test_report.txt`.

---

## Dừng hệ thống

```cmd
Phase5_Operations\stop_all.bat
```

Hoặc thủ công: Ctrl+C ở cửa sổ fusion.py > Ctrl+C ở cửa sổ SITL > `docker-compose -f Phase1_Docker\docker-compose.yml down`.

---

## Xử lý sự cố

| Triệu chứng | Nguyên nhân | Cách xử lý |
|---|---|---|
| Board treo sau khi in banner | Chan GPIO xung dot | Khong dung PA_12, PA_30 |
| MQTT that bai `rc=-2` | Sai IP broker | Chay `ipconfig`, cap nhat `mqtt_server` trong code |
| fusion.py in GPS (0.00, 0.00) | SITL chua chay hoac port 5763 bi chiem | Chay stop_all.bat roi khoi dong lai SITL |
| Nhiet do / do am hien 0 | DHT22 chua cam hoac sai chan | Cam DHT22 vao PA_26, xem Serial Monitor |
| Web badge "Ket noi loi" | Mosquitto chua chay | Kiem tra `docker ps` |
| TAKEOFF that bai | Pre-arm check chua pass | Cho QGroundControl hien "Ready to Fly" truoc |
| SITL khong ket noi duoc tu Windows host | WSL2 port binding | Kiem tra run_sitl.ps1 bind `0.0.0.0` khong phai `127.0.0.1` |
| `iot_db already in use` | Container cu con ton tai | `docker rm -f iot_db iot_mqtt iot_grafana` |
| PowerShell bao loi execution policy | Chua cap quyen chay script | `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned` |
