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
[MAVLink] ✅ Kết nối SITL thành công tcp:127.0.0.1:5763
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

### B7. Test toàn hệ thống với mạch BW16 thực tế

> ✅ Kịch bản này kiểm chứng toàn bộ luồng truyền dữ liệu từ cảm biến phần cứng qua MQTT, tích hợp với GPS của SITL và hiển thị trực quan.

#### B7.1 Chuẩn bị mạch cảm biến BW16
1. Cắm cáp USB kết nối board BW16 (đã nạp firmware) vào máy tính hoặc nguồn điện ngoài.
2. Đảm bảo board đã kết nối thành công vào WiFi và đèn LED xanh trên board sáng nhấp nháy báo hiệu trạng thái hoạt động tốt.
3. Mở Serial Monitor trên Arduino IDE để quan sát log gửi gói tin sensors thành công.

#### B7.2 Khởi động Drone ảo (SITL)
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

#### B7.3 Khởi chạy Data Fusion Gateway
```bash
# Tab Terminal mới
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
source Phase4_Fusion/drone_env/bin/activate
python3 Phase4_Fusion/fusion.py
```

✅ Kết quả thành công trông như sau (các chỉ số nhiệt độ `T` và cảm biến `CO2` hiển thị giá trị thực tế đo từ mạch thay vì số 0):
```
[TOKEN] ✅ Token hợp lệ (length=88)
[INFLUX] ✅ Kết nối OK — version=2.0.9
[MQTT] ✅ Kết nối thành công broker 127.0.0.1:1883
[SITL] ✅ Kết nối thành công! System ID=0
🚀 Bắt đầu Fusion Loop

[FUSION] ✅ #0001  GPS: (-35.3632, 149.1652, 584.0m)  T=28.5°C  CO2=412
[FUSION] ✅ #0002  GPS: (-35.3632, 149.1652, 584.0m)  T=28.4°C  CO2=415
```

#### B7.4 Kiểm tra trên Grafana và kiểm thử cảnh báo
1. Mở **http://localhost:3000 $\rightarrow$ Dashboards $\rightarrow$ Drone IoT Monitor**.
2. Đổi time range sang **"Last 5 minutes"**, bật Auto refresh **5s**.
3. Các biểu đồ Nhiệt độ, Độ ẩm, CO2 sẽ vẽ các đường line dao động thực tế theo các giá trị gửi về từ cảm biến vật lý.
4. **Kiểm tra tính năng cảnh báo (Alert test):**
   - Thổi hơi nóng hoặc đưa khí gas (từ bật lửa) lại gần cảm biến MQ-135 để tăng trị số CO2.
   - Khi CO2 vượt ngưỡng 600, còi buzzer trên mạch sẽ kêu Beep Beep ngắt quãng và đèn LED đỏ sáng.
   - Trên Grafana và Web Control, mức độ cảnh báo sẽ chuyển sang **NGUY HIỂM** màu đỏ nổi bật.

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
| `PORT 5763 chưa mở` | SITL chưa hoàn tất khởi động | Mở tab riêng chạy `run_sitl.sh` trước |
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

## PHẦN D: Web Control & Kiểm thử tích hợp (GĐ 6 - GĐ 10)

### D1. Giao diện Web Control (index.html)
Trang Web Control giao tiếp trực tiếp với Mosquitto Broker qua giao thức MQTT over WebSockets trên cổng `9001` (không cần server backend).
1. Khởi động Docker, SITL và `fusion.py`.
2. Mở file `Phase5_Operations/web_control/index.html` trong trình duyệt (Chrome, Safari, Firefox).
3. Đảm bảo trạng thái badge ở góc trên bên phải báo **"Đã kết nối"** (màu xanh lá).
4. Các chức năng trên giao diện:
   - **Bay (MAVLink SITL)**: Bấm `ARM` để khởi động động cơ, `TAKEOFF 10m` để cất cánh, `LAND` để hạ cánh, `RTL` để quay về điểm xuất phát.
   - **Cảnh báo (BW16)**: Bấm `BẬT CÒI` / `TẮT CÒI` để điều khiển trực tiếp còi báo động trên board BW16. Bấm `Khôi Phục Tự Động Onboard` để trả quyền điều khiển về logic tự động của cảm biến.
   - **Dữ liệu live & Log**: Hiển thị live Nhiệt độ, Độ ẩm, CO2 từ BW16 và RSSI. Hộp log lưu trữ lịch sử 20 lệnh gần nhất.

### D2. Chạy các kịch bản kiểm thử tích hợp (Integration Tests)
Hệ thống đi kèm với 4 kịch bản kiểm thử tự động viết bằng Python nằm trong thư mục `Phase5_Operations/tests/`:

Để chạy kiểm thử, đảm bảo đã kích hoạt môi trường ảo Python:
```bash
cd ~/Desktop/IOT102_DRONE-PROJECT/DroneIoT_macOS
source Phase4_Fusion/drone_env/bin/activate
```

Sau đó chạy từng test:
1. **Kiểm tra tính liên tục dữ liệu**:
   ```bash
   python3 Phase5_Operations/tests/test_continuity.py
   ```
   *Mô tả*: Truy vấn InfluxDB trong 5 phút qua và kiểm tra khoảng cách thời gian giữa các điểm dữ liệu liên tiếp. Pass nếu số gap > 3s chiếm < 5%.
2. **Đo độ trễ đầu cuối (Latency)**:
   ```bash
   python3 Phase5_Operations/tests/test_latency.py
   ```
   *Mô tả*: Đo thời gian thực từ lúc nhận dữ liệu cảm biến qua MQTT đến khi dữ liệu được ghi thành công vào InfluxDB (lấy mẫu 20 lần). Pass nếu độ trễ tối đa < 2 giây.
3. **Kiểm tra khả năng chịu lỗi (Fault Tolerance)**:
   ```bash
   python3 Phase5_Operations/tests/test_fault_tolerance.py
   ```
   *Mô tả*: Mô phỏng kiểm tra trạng thái mất kết nối SITL, stress test MQTT (100 msgs/s) và khôi phục DB. Kết quả ghi vào file `test_report.txt`.
4. **Kiểm tra Web Control**:
   ```bash
   python3 Phase5_Operations/tests/test_web_control.py
   ```
   *Mô tả*: Thực hiện gửi nhận tin nhắn lệnh bay/còi giả lập trên MQTT và hiển thị hướng dẫn xác nhận thủ công.

---

## PHẦN E: Cấu hình Grafana Nâng cao (Bản đồ Geomap & Alert)

### E1. Vẽ bản đồ quỹ đạo (Geomap Panel)
1. Thêm Panel mới trong Grafana Dashboard.
2. Chọn Visualization: **Geomap** (hoặc cài đặt plugin `TrackMap` từ Grafana Store).
3. Dán câu truy vấn Flux sau để gộp tọa độ lat/lon theo cùng timestamp:
   ```flux
   from(bucket: "drone_data")
     |> range(start: -30m)
     |> filter(fn: (r) => r._measurement == "drone_telemetry")
     |> filter(fn: (r) => r._field == "latitude" or r._field == "longitude")
     |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
   ```
4. Tại phần cấu hình Map Layer (bên phải):
   - **Location Mode**: chọn `Coords`
   - **Latitude field**: chọn `latitude`
   - **Longitude field**: chọn `longitude`
5. Nhấn **Apply** → Bản đồ sẽ vẽ trace di chuyển thực tế của drone.

### E2. Cấu hình Alert cảnh báo khí độc CO2
1. Vào **Alerting → Alert rules → Create rule**.
2. Đặt tên rule: `Drone CO2 Warning`.
3. Nhập câu truy vấn Flux lấy CO2:
   ```flux
   from(bucket: "drone_data")
     |> range(start: -1m)
     |> filter(fn: (r) => r._measurement == "drone_telemetry")
     |> filter(fn: (r) => r._field == "co2")
     |> last()
   ```
4. Đặt điều kiện cảnh báo: **Define query and alert condition** -> chọn **Evaluate** -> nếu giá trị cuối cùng `last() > 600`.
5. Đặt tần suất đánh giá (Evaluation interval): `10s`, thời gian chờ kích hoạt (Pending period): `30s`.
6. Tại mục **Contact points**: Cấu hình ghi cảnh báo ra log Grafana hoặc các kênh Email/Slack mong muốn.

---

## PHẦN F: Tài liệu tham chiếu
*   **Báo cáo kỹ thuật học thuật chi tiết**: Xem tại [academic_report.md](Phase5_Operations/academic_report.md)
*   **Sơ đồ nối dây thực tế**: Xem tại [wiring_diagram.md](Phase3_BW16/wiring_diagram.md)

---

## Yêu cầu phần cứng

- Mac chip M1/M2/M3/M4 (Apple Silicon), RAM ≥ 8GB
- Docker Desktop for Mac (Apple Silicon build)
- Board BW16 (RTL8720DN) + cáp USB có dây data
- Cảm biến DHT22 + điện trở pull-up 10kΩ
- Cảm biến MQ-135 + còi cắm qua Transistor NPN 2N2222
