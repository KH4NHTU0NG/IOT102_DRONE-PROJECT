# Drone IoT — Windows 10/11

> Kiểm chứng trên: Windows 10/11 + Docker Desktop (WSL2 backend)  
> Phiên bản: Thực tế · Đã fix 12 bugs

## Cấu trúc thư mục

```
DroneIoT_Windows/
├── Phase1_Docker/          ← MQTT + InfluxDB + Grafana
│   ├── docker-compose.yml
│   ├── mosquitto/mosquitto.conf
│   └── setup.bat           ← Chạy đầu tiên
├── Phase2_SITL/            ← Drone ảo ArduPilot (qua WSL2)
│   ├── wsl2_setup.md       ← Đọc trước khi bắt đầu!
│   ├── install_sitl.ps1    ← Cài 1 lần (PowerShell)
│   └── run_sitl.ps1        ← Chạy mỗi lần (PowerShell)
├── Phase3_BW16/            ← Firmware cảm biến
│   ├── bw16_sensor.ino
│   └── wiring_diagram.md
├── Phase4_Fusion/          ← Python Gateway
│   ├── fusion.py           ← Script chính
│   ├── requirements.txt
│   └── setup_venv.bat      ← Cài 1 lần
└── Phase5_Operations/      ← Vận hành
    ├── start_all.bat       ← Khởi động toàn bộ
    ├── stop_all.bat        ← Dừng toàn bộ
    ├── grafana_queries.md
    └── checklist.md
```

---

## ⚠️ Điểm khác biệt quan trọng so với macOS

| Thành phần | macOS | Windows |
|-----------|-------|---------|
| ArduPilot SITL | Native bash | **Bắt buộc qua WSL2** |
| Script khởi động | `.sh` (bash) | `.bat` / `.ps1` |
| Docker Desktop | Apple Silicon build | WSL2 backend |
| Lấy IP máy tính | `ipconfig getifaddr en0` | `ipconfig` (CMD) |
| Kiểm tra port | `lsof -i :5763` | `netstat -an \| findstr 5763` |

---

## Bắt đầu nhanh (Quick Start)

### Bước 0: Chuẩn bị (làm 1 lần)

```
1. Cài Docker Desktop: https://www.docker.com/products/docker-desktop/
   → Settings → General → "Use WSL 2 based engine" ✅

2. Bật WSL2 + cài Ubuntu 22.04: xem Phase2_SITL\wsl2_setup.md

3. Cho phép PowerShell chạy script:
   (PowerShell as Admin) Set-ExecutionPolicy -Scope CurrentUser RemoteSigned

4. Cài Python 3.10+: https://python.org → tích "Add to PATH"
```

### Lần đầu (setup)

```batch
REM 1. Dựng Docker server
Phase1_Docker\setup.bat

REM 2. Cài SITL trong WSL2 (PowerShell)
Phase2_SITL\install_sitl.ps1

REM 3. Tạo Python venv
Phase4_Fusion\setup_venv.bat

REM 4. Điền token vào fusion.py
notepad Phase4_Fusion\fusion.py
REM Thay TOKEN_CUA_BAN bằng token từ bước 1
```

### Mỗi lần chạy

> [!TIP]
> **Dọn dẹp tiến trình cũ trước khi chạy:**
> Để tránh lỗi xung đột cổng kết nối (như port `5763` hoặc `1883`/`9001` bị chiếm dụng bởi các tiến trình chạy ngầm cũ), bạn nên chạy lệnh dừng hệ thống trước khi khởi động:
> ```batch
> Phase5_Operations\stop_all.bat
> ```

```batch
Phase5_Operations\start_all.bat
```

---

## Bugs đã fix

| # | Bug | Fix |
|---|-----|-----|
| 1 | `on_connect` DeprecationWarning | `CallbackAPIVersion.VERSION2` |
| 2 | MAVLink crash khi SITL chậm | Retry + exponential backoff |
| 3 | TOKEN hardcode | Load từ file / env / interactive |
| 4 | Container không restart | `restart: unless-stopped` |
| 5 | Data mất khi restart | Named Docker volumes |
| 6 | Mosquitto thiếu log | `log_type all` |
| 7 | BW16 WiFi mất → treo | Auto reconnect loop |
| 8 | MQ-135 ADC không cảnh báo | Voltage divider requirement rõ ràng |
| 9 | PATH không reload | `source ~/.zshrc` sau install |
| **10** | **SITL không chạy native Windows** | **WSL2 với port bridging 0.0.0.0** |
| 11 | MAVLink disconnect crash | `ConnectionResetError` + reconnect |
| 12 | TOKEN lỗi không rõ | `load_token()` với hướng dẫn |

> Bug #10 là điểm khác biệt lớn nhất của Windows — ArduPilot SITL không hỗ trợ native Windows, bắt buộc dùng WSL2. `run_sitl.ps1` tự động xử lý việc bind `0.0.0.0` để Windows host có thể connect.

---

## Tài khoản mặc định

| Dịch vụ | URL | User | Password |
|---------|-----|------|----------|
| Grafana | http://localhost:3000 | admin | admin |
| InfluxDB | http://localhost:8086 | admin | adminpassword |

---

## PHẦN D: Web Control & Kiểm thử tích hợp (GĐ 6 - GĐ 10)

### D1. Giao diện Web Control (index.html)
Trang Web Control giao tiếp trực tiếp với Mosquitto Broker qua giao thức MQTT over WebSockets trên cổng `9001` (không cần server backend).
1. Khởi động Docker, SITL (trong WSL2) và `fusion.py` trên Windows.
2. Mở file `Phase5_Operations\web_control\index.html` bằng trình duyệt (Chrome, Edge).
3. Đảm bảo trạng thái badge ở góc trên bên phải báo **"Đã kết nối"** (màu xanh lá).
4. Các nút điều khiển:
   - **Bay (MAVLink SITL)**: Bấm `ARM`, `TAKEOFF 10m`, `LAND`, `RTL` để điều khiển trực tiếp drone ảo.
   - **Cảnh báo (BW16)**: Bấm `BẬT CÒI` / `TẮT CÒI`, `BẬT LED` / `TẮT LED` và `Khôi Phục Tự Động Onboard`.

### D2. Chạy các kịch bản kiểm thử tích hợp (Windows PowerShell/CMD)
Kích hoạt môi trường venv trước khi chạy:
```batch
REM Mở CMD
Phase4_Fusion\drone_env\Scripts\activate
```

Sau đó chạy từng test:
1. **Kiểm tra tính liên tục dữ liệu**:
   ```bash
   python Phase5_Operations/tests/test_continuity.py
   ```
2. **Đo độ trễ đầu cuối (Latency)**:
   ```bash
   python Phase5_Operations/tests/test_latency.py
   ```
3. **Kiểm tra khả năng chịu lỗi (Fault Tolerance)**:
   ```bash
   python Phase5_Operations/tests/test_fault_tolerance.py
   ```
   *Kết quả sẽ ghi vào file `test_report.txt`*.
4. **Kiểm tra Web Control**:
   ```bash
   python Phase5_Operations/tests/test_web_control.py
   ```

---

## PHẦN E: Cấu hình Grafana Nâng cao (Bản đồ Geomap & Alert)

### E1. Vẽ bản đồ quỹ đạo (Geomap Panel)
1. Thêm Panel mới trong Grafana Dashboard.
2. Chọn Visualization: **Geomap**.
3. Dán câu truy vấn Flux sau:
   ```flux
   from(bucket: "drone_data")
     |> range(start: -30m)
     |> filter(fn: (r) => r._measurement == "drone_telemetry")
     |> filter(fn: (r) => r._field == "latitude" or r._field == "longitude")
     |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
   ```
4. Tại phần cấu hình Map Layer (bên phải):
   - **Location Mode**: `Coords`
   - **Latitude field**: `latitude`
   - **Longitude field**: `longitude`
5. Nhấn **Apply**.

### E2. Cấu hình Alert cảnh báo khí độc CO2 > 600
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
4. Đặt điều kiện cảnh báo: chọn **Evaluate** -> nếu giá trị cuối cùng `last() > 600`.
5. Đặt tần suất đánh giá (Evaluation interval): `10s`, thời gian chờ (Pending period): `30s`.

---

## PHẦN F: Tài liệu tham chiếu
*   **Báo cáo kỹ thuật học thuật chi tiết**: Xem tại [academic_report.md](Phase5_Operations/academic_report.md)
*   **Sơ đồ nối dây thực tế**: Xem tại [wiring_diagram.md](Phase3_BW16/wiring_diagram.md)

---

## Yêu cầu hệ thống

- Windows 10 (build 19041+) hoặc Windows 11
- WSL2 với Ubuntu 22.04 LTS
- Docker Desktop 4.x (WSL2 backend)
- Python 3.10+ (cài trên Windows host)
- Arduino IDE 2.x
- PowerShell 5.1+
- RAM tối thiểu: 8GB (khuyến nghị 16GB cho SITL + Docker)
