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
| Kiểm tra port | `lsof -i :5760` | `netstat -an \| findstr 5760` |

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

## Yêu cầu hệ thống

- Windows 10 (build 19041+) hoặc Windows 11
- WSL2 với Ubuntu 22.04 LTS
- Docker Desktop 4.x (WSL2 backend)
- Python 3.10+ (cài trên Windows host)
- Arduino IDE 2.x
- PowerShell 5.1+
- RAM tối thiểu: 8GB (khuyến nghị 16GB cho SITL + Docker)
