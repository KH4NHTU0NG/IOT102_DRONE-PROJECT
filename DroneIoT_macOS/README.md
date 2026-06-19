# Drone IoT — macOS Apple Silicon

> ✅ Kiểm chứng thực tế trên: macOS M-series (Apple Silicon)
> 🔧 Đã fix 12 bugs so với tài liệu gốc

## Cấu trúc thư mục

```
DroneIoT_macOS/
├── Phase1_Docker/          ← MQTT + InfluxDB + Grafana
│   ├── docker-compose.yml
│   ├── mosquitto/mosquitto.conf
│   └── setup.sh            ← Chạy đầu tiên (1 lần duy nhất)
├── Phase2_SITL/            ← Drone ảo ArduPilot
│   ├── install_sitl.sh     ← Cài 1 lần
│   └── run_sitl.sh         ← Chạy MỖI LẦN dùng hệ thống
├── Phase3_BW16/            ← Firmware cảm biến vật lý
│   ├── bw16_sensor.ino
│   └── wiring_diagram.md
├── Phase4_Fusion/          ← Python Gateway
│   ├── fusion.py           ← Script dung hợp dữ liệu
│   ├── requirements.txt
│   └── setup_venv.sh       ← Tạo môi trường (1 lần)
└── Phase5_Operations/      ← Vận hành hàng ngày
    ├── start_all.sh        ← Khởi động tổng thể
    ├── stop_all.sh         ← Dừng toàn bộ
    ├── grafana_queries.md
    └── checklist.md
```

---

## 🚀 HƯỚNG DẪN KHỞI ĐỘNG ĐẦY ĐỦ (ĐÃ KIỂM CHỨNG)

> ⚠️ **Đọc kỹ phần này trước khi bắt đầu!**
> Các bước phải thực hiện **đúng thứ tự**. Sai thứ tự → hệ thống không kết nối được.

---

## PHẦN A: Cài đặt lần đầu (Chỉ làm 1 lần duy nhất)

### A1. Cài đặt phần mềm nền tảng

```bash
# Cài Homebrew (nếu chưa có)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Cài Python 3 và pip
brew install python@3.11
```

- **Docker Desktop**: Tải tại https://www.docker.com/products/docker-desktop/ → Chọn bản **Apple Silicon (M1/M2/M3/M4)**
- **Arduino IDE 2.x**: Tải tại https://www.arduino.cc/en/software
- **QGroundControl**: Tải tại https://qgroundcontrol.com/downloads/

### A2. Dựng server Docker (MQTT + InfluxDB + Grafana)

> Mở Docker Desktop trước, chờ icon ở thanh menu chuyển sang **màu trắng/xanh** (Engine running).

```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
bash Phase1_Docker/setup.sh
```

✅ Kết quả mong đợi:
```
✅ Phase 1 hoàn tất!
  Grafana:  http://localhost:3000
  InfluxDB: http://localhost:8086
  MQTT:     localhost:1883

→ Lấy InfluxDB API Token...
InfluxDB Token (copy ngay, dán vào fusion.py):
  <TOKEN DÀI XUẤT HIỆN Ở ĐÂY>
→ Token cũng được lưu tại: Phase4_Fusion/.influx_token
```

> 📋 **Copy toàn bộ chuỗi Token** được in ra — dùng ở bước A4.

### A3. Cài đặt ArduPilot SITL

> ⚠️ Bước này mất **15–30 phút** để clone và build lần đầu. Đừng đóng Terminal!

```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
bash Phase2_SITL/install_sitl.sh
```

✅ Kết quả mong đợi ở cuối:
```
✅ Cài đặt ArduPilot SITL hoàn tất!
   PATH đã được thêm vào ~/.zshrc
```

**Sau khi xong → Đóng Terminal hiện tại và mở Terminal MỚI** (để PATH reload đúng).

### A4. Dán InfluxDB Token vào fusion.py

```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
nano Phase4_Fusion/fusion.py
```

Dùng phím mũi tên di chuyển đến **dòng 34**, sửa:
```python
# TRƯỚC (sai):
INFLUX_TOKEN  = "TOKEN_CUA_BAN"

# SAU (đúng — dán token từ bước A2):
INFLUX_TOKEN  = "SPSuc2iy...9Aw=="
```

Lưu và thoát: nhấn **`Ctrl + O`** → **Enter** → **`Ctrl + X`**

### A5. Tạo môi trường Python (venv)

```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
bash Phase4_Fusion/setup_venv.sh
```

✅ Kết quả mong đợi:
```
✅ Môi trường Python đã sẵn sàng tại Phase4_Fusion/drone_env
```

### A6. Nạp firmware lên board BW16

Xem chi tiết tại [`Phase3_BW16/wiring_diagram.md`](Phase3_BW16/wiring_diagram.md).

Tóm tắt nhanh:
1. Đấu nối DHT22 và MQ-135 theo sơ đồ (lưu ý **voltage divider** cho MQ-135).
2. Mở **Arduino IDE** → mở file `Phase3_BW16/bw16_sensor.ino`.
3. Điền WiFi và IP máy tính vào code:
   ```cpp
   const char* ssid = "TEN_WIFI";
   const char* password = "MAT_KHAU";
   const char* mqtt_server = "192.168.x.x"; // IP máy tính chạy Docker
   ```
4. Chọn Board: **`AmebaD (RTL8720DN)` → `BW16`**.
5. Nạp code: Nhấn **Upload** → Khi IDE đếm ngược → Giữ **BURN** + nhấn **RESET** → Thả **BURN**.

---

## PHẦN B: Chạy hệ thống mỗi ngày (Đúng thứ tự!)

> 🔴 **QUAN TRỌNG**: Thứ tự khởi động phải là:
> **Docker → SITL → (chờ MAV>) → Fusion → QGroundControl**

### B1. Đảm bảo Docker Desktop đang chạy

Kiểm tra icon Docker ở thanh menu trên cùng phải đang chạy (màu trắng). Nếu chưa → mở Docker Desktop và chờ.

```bash
# Kiểm tra 3 container đang chạy
docker ps --format "table {{.Names}}\t{{.Status}}"
```

✅ Phải thấy 3 dòng: `iot_mqtt`, `iot_db`, `iot_grafana` đều là `Up ...`

Nếu container chưa chạy:
```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS/Phase1_Docker
docker-compose up -d
```

### B2. [TAB 1] Chạy SITL trong Terminal riêng

> ⚠️ **Mở tab Terminal MỚI** (`Cmd + T`) — KHÔNG dùng chung tab với bước khác!

```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
bash Phase2_SITL/run_sitl.sh
```

**Chờ đến khi thấy dòng `MAV>` xuất hiện** (lần đầu mất 1–3 phút build):
```
AP: ArduPilot Ready
AP: EKF3 IMU0 origin set
...
MAV>          ← Đây! SITL đã sẵn sàng
```

> ✅ Sau khi thấy `MAV>` → QGroundControl sẽ **tự động kết nối** qua UDP 14550 và hiện **"Ready to Fly"**.
>
> ⚠️ **KHÔNG đóng Tab này** trong suốt quá trình dùng hệ thống!

### B3. [TAB 2] Chạy Data Fusion Gateway

Mở **tab Terminal MỚI thứ 2** (`Cmd + T`):

```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
source Phase4_Fusion/drone_env/bin/activate
python3 Phase4_Fusion/fusion.py
```

✅ Kết quả mong đợi:
```
[TOKEN] ✅ Token hợp lệ
[MQTT] ✅ Kết nối thành công broker 127.0.0.1:1883
[MAVLink] ✅ Kết nối SITL thành công tcp:127.0.0.1:5760
[Fusion] ✅ Bắt đầu ghi dữ liệu vào InfluxDB...
[Fusion] GPS: lat=... lon=... alt=...  Temp=28.5°C  AQ=125
```

> ⚠️ **KHÔNG đóng Tab này** — đây là cầu nối dữ liệu chính!

### B4. Kết nối QGroundControl với SITL

1. Mở **QGroundControl** (nếu chưa mở).
2. Nếu **không tự kết nối** sau 10 giây → nhấn vào chữ **"Disconnected - Click to manually connect"**.
3. Cài đặt kết nối thủ công:
   - **Type**: UDP
   - **Port**: `14550`
4. Nhấn **Connect** → QGC sẽ hiện bản đồ với drone và trạng thái **"Ready to Fly"** ✈️

### B5. Xem Dashboard Grafana

Mở trình duyệt → truy cập: **http://localhost:3000**

| Dịch vụ | URL | User | Password |
|---------|-----|------|----------|
| Grafana | http://localhost:3000 | `admin` | `admin` |
| InfluxDB | http://localhost:8086 | `admin` | `adminpassword` |

---

### B6. Setup Grafana Dashboard

> Chỉ làm **1 lần duy nhất** sau khi đã có Data Source InfluxDB.

#### B6.1 Kết nối Grafana với InfluxDB
1. Mở **http://localhost:3000** → Đăng nhập `admin / admin`
2. **Connections → Data Sources → Add data source → InfluxDB**
3. Điền thông tin:

| Trường | Giá trị |
|--------|---------|
| **Query Language** | `Flux` |
| **URL** | `http://influxdb:8086` |
| **Organization** | `drone_org` |
| **Token** | *(lấy bằng lệnh bên dưới)* |
| **Default Bucket** | `drone_data` |

```bash
# Lấy token từ file (cách nhanh nhất):
cat ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS/Phase4_Fusion/.influx_token
```

4. Nhấn **Save & Test** → Phải thấy ✅ `datasource is working`

#### B6.2 Tạo Dashboard 6 Panels
**Dashboards → New → New Dashboard → nhấn ô Panel (biểu tượng +)**

Với mỗi panel, làm theo thứ tự:
1. Phần dưới: chọn Data source = **influxdb** → dán Flux query
2. Phần trên phải: chọn **All visualizations → Time series**
3. Đổi tên panel → nhấn **Apply**

| Panel | Tiêu đề | Flux query (`_field == ...`) |
|-------|---------|------------------------------|
| 1 | `🌡️ Nhiệt độ (°C)` | `"temperature"` |
| 2 | `💧 Độ ẩm (%)` | `"humidity"` |
| 3 | `🌫️ Chất lượng khí CO2` | `"co2"` |
| 4 | `🛫 Độ cao bay (m)` | `"altitude"` |
| 5 | `📍 Vĩ độ GPS` | `"latitude"` |
| 6 | `📶 WiFi RSSI` | `"wifi_rssi"` |

**Template query** (chỉ thay phần `"temperature"` thành field tương ứng):
```flux
from(bucket: "drone_data")
  |> range(start: -10m)
  |> filter(fn: (r) => r._measurement == "drone_telemetry")
  |> filter(fn: (r) => r._field == "temperature")
```

Sau khi tạo xong 6 panels → **Save dashboard** → đặt tên `Drone IoT Monitor`

---

### B7. Test toàn hệ thống (không cần mạch BW16)

> ✅ Có thể test được với chỉ SITL. GPS và Altitude sẽ có data thật. Sensor (nhiệt độ, CO2...) sẽ hiện `0` cho đến khi cắm mạch BW16.

#### B7.1 Restart SITL với GPS fix (nếu đang chạy, Ctrl+C rồi chạy lại)
```bash
# Tab Terminal SITL (Cmd+T để mở tab mới)
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
bash Phase2_SITL/run_sitl.sh
```
Chờ đến khi thấy:
```
AP: EKF3 IMU0 origin set   ← GPS đã fix (khoảng 30-60 giây)
MAV>                        ← Sẵn sàng hoàn toàn
```

#### B7.2 Chạy fusion.py (Tab Terminal mới)
```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
source Phase4_Fusion/drone_env/bin/activate
python3 Phase4_Fusion/fusion.py
```

✅ Kết quả thành công trông như sau:
```
[TOKEN] ✅ Token hợp lệ (length=88)
[INFLUX] ✅ Kết nối OK — version=2.0.9
[MQTT] ✅ Kết nối thành công broker 127.0.0.1:1883
[SITL] ✅ Kết nối thành công! System ID=0
🚀 Bắt đầu Fusion Loop

[FUSION] ✅ #0001  GPS: (-35.3632, 149.1652, 584.0m)  T=0°C  CO2=0
[FUSION] ✅ #0002  GPS: (-35.3632, 149.1652, 584.0m)  T=0°C  CO2=0
```

#### B7.3 Xem data trên Grafana
1. Mở **http://localhost:3000 → Dashboards → Drone IoT Monitor**
2. Đổi time range sang **"Last 5 minutes"** (góc trên giữa)
3. Nhấn **Refresh** (hoặc bật Auto refresh 5s)
4. Panel **Độ cao bay** và **Vĩ độ GPS** sẽ hiện **đường line thật** từ SITL ✅

#### B7.4 Xử lý nếu SITL vẫn báo `Chờ GPS...`
Chuyển sang tab SITL, tại dấu nhắc `MAV>` gõ:
```
mode guided
```
→ fusion.py sẽ nhận GPS ngay lập tức.

---

## PHẦN C: Dừng hệ thống


```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
bash Phase5_Operations/stop_all.sh
```

Hoặc thủ công:
1. Nhấn `Ctrl + C` ở Tab fusion.py
2. Nhấn `Ctrl + C` ở Tab SITL
3. `docker-compose -f Phase1_Docker/docker-compose.yml down`

---

## 🐛 Xử lý lỗi thường gặp

| Lỗi | Nguyên nhân | Fix |
|-----|-------------|-----|
| QGroundControl hiện "Disconnected" | SITL chưa chạy hoặc chưa tới `MAV>` | Chờ thêm hoặc kết nối thủ công UDP 14550 |
| `PORT 5760 chưa mở` | SITL chưa hoàn tất khởi động | Mở tab riêng chạy `run_sitl.sh` trước |
| `iot_db already in use` | Container cũ còn tồn tại | `docker rm -f iot_db iot_mqtt iot_grafana` |
| `TOKEN_CUA_BAN chưa set` | Chưa dán token vào fusion.py | Xem lại bước A4 |
| `No such file or directory` (setup.sh) | Đang đứng sai thư mục | `cd DroneIoT_macOS` trước khi chạy |
| fusion.py không nhận data BW16 | BW16 chưa kết nối WiFi | Kiểm tra Serial Monitor (115200 baud) |

---

## Bugs đã fix so với tài liệu gốc

| # | Bug | File | Fix |
|---|-----|------|-----|
| 1 | `on_connect` 4-arg → DeprecationWarning | `fusion.py` | `CallbackAPIVersion.VERSION2` + 5-arg |
| 2 | MAVLink crash khi SITL chậm | `fusion.py` | Timeout + exponential backoff retry |
| 3 | TOKEN hardcode gây crash âm thầm | `fusion.py` | Kiểm tra token + load từ file/.env |
| 4 | Container không tự restart | `docker-compose.yml` | `restart: unless-stopped` |
| 5 | Data mất khi restart Docker | `docker-compose.yml` | Named volumes |
| 6 | Mosquitto thiếu log config | `mosquitto.conf` | `log_type all` + `persistence false` |
| 7 | BW16 mất WiFi → treo vĩnh viễn | `bw16_sensor.ino` | Reconnect loop |
| 8 | MQ-135 ADC không cảnh báo điện áp | `bw16_sensor.ino` | Voltage divider requirement |
| 9 | PATH không reload sau install | `install_sitl.sh` | `source ~/.zshrc` + `source ~/.zprofile` |
| 11 | MAVLink disconnect giữa chừng | `fusion.py` | `ConnectionResetError` catch + reconnect |
| 12 | TOKEN sai không có error rõ | `fusion.py` | `load_token()` với hướng dẫn chi tiết |

---

## Yêu cầu phần cứng

- Mac chip M1/M2/M3/M4 (Apple Silicon), RAM ≥ 8GB
- Docker Desktop for Mac (Apple Silicon build)
- Board BW16 (RTL8720DN) + cáp USB **có dây data**
- Cảm biến DHT22 + điện trở pull-up 10kΩ
- Cảm biến MQ-135 + **2 điện trở 10kΩ** (voltage divider bắt buộc)


