# IOT102_DRONE-PROJECT

> **Hệ thống giám sát Drone IoT tích hợp dữ liệu cảm biến thực tế và mô phỏng bay ảo.**
> 
> *A Drone IoT monitoring system integrating real-world sensor telemetry and simulated virtual flight data.*

---

## 📌 Tổng Quan Dự Án / Project Overview

Dự án này cung cấp tài liệu hướng dẫn và mã nguồn hoàn chỉnh nhằm thiết lập một hệ thống **Drone IoT** khép kín, đã được kiểm chứng hoạt động ổn định trên cả hai hệ điều hành **macOS (M-series)** và **Windows**.

Hệ thống hoạt động theo cơ chế dung hợp dữ liệu (Data Fusion):
1. **Dữ liệu thật**: Nhiệt độ (DHT22) và chất lượng không khí (MQ-135) từ board vật lý **BW16 (Realtek)** truyền qua giao thức **MQTT**.
2. **Dữ liệu ảo**: Tọa độ GPS, độ cao, vận tốc từ drone mô phỏng **ArduPilot SITL**.
3. **Gateway Fusion**: Script Python đồng bộ hóa hai luồng dữ liệu theo thời gian thực và đẩy lên cơ sở dữ liệu **InfluxDB**.
4. **Dashboard**: Trực quan hóa dữ liệu sinh động trên **Grafana**.

---

## 📂 Cấu Trúc Repository / Repository Structure

Kho lưu trữ được chia làm 2 thư mục độc lập tối ưu cho từng hệ điều hành:

```
IOT102_DRONE-PROJECT/
├── .gitignore
├── README.md                 <-- [Bạn đang ở đây / You are here]
│
├── DroneIoT_macOS/           <-- Dành cho macOS (Apple Silicon M1/M2/M3/M4)
│   ├── README.md             <-- Hướng dẫn chi tiết cho macOS
│   ├── Phase1_Docker/        <-- docker-compose.yml + mosquitto.conf + setup.sh
│   ├── Phase2_SITL/          <-- Scripts cài đặt & chạy ArduPilot SITL
│   ├── Phase3_BW16/          <-- Sketch Arduino (.ino) & Sơ đồ đấu nối phần cứng
│   ├── Phase4_Fusion/        <-- Python fusion.py + venv setup script
│   └── Phase5_Operations/    <-- Scripts khởi chạy/dừng toàn hệ thống & checklist
│
└── DroneIoT_Windows/         <-- Dành cho Windows 10/11 (Sử dụng WSL2)
    ├── README.md             <-- Hướng dẫn chi tiết cho Windows
    ├── Phase1_Docker/        <-- docker-compose.yml + setup.bat
    ├── Phase2_SITL/          <-- WSL2 setup guide & PowerShell run scripts
    ├── Phase3_BW16/          <-- Sketch Arduino & Sơ đồ đấu nối phần cứng
    ├── Phase4_Fusion/        <-- Python fusion.py + Batch venv setup script
    └── Phase5_Operations/    <-- Batch scripts khởi chạy/dừng toàn hệ thống & checklist
```

---

## 🚀 Các Giai Đoạn Triển Khai / Implementation Phases

Quy trình triển khai bao gồm 5 giai đoạn chính:

| Giai Đoạn (Phase) | Nội Dung / Tasks | Công Cụ & Thành Phần / Tools & Stack |
| :--- | :--- | :--- |
| **Phase 1** | Dựng trạm mặt đất cục bộ (Local Ground Station) | Docker, Eclipse Mosquitto, InfluxDB 2.x, Grafana |
| **Phase 2** | Thiết lập môi trường bay mô phỏng | ArduPilot SITL, QGroundControl, WSL2 (cho Windows) |
| **Phase 3** | Lập trình đọc cảm biến từ phần cứng | Board Realtek RTL8720DN (BW16), DHT22, MQ-135 |
| **Phase 4** | Đồng bộ hóa dữ liệu cảm biến thực và GPS ảo | Python, MAVLink/Pymavlink, Paho-MQTT, InfluxDB Client |
| **Phase 5** | Vận hành toàn bộ hệ thống | Điều khiển drone bay ảo, quan sát telemetry trên Grafana |

---

## 🛠️ Hướng Dẫn Nhanh / Quick Start

### 1. Chọn phiên bản phù hợp với hệ điều hành của bạn:
* Nếu sử dụng **macOS**, chuyển vào thư mục: [`DroneIoT_macOS/README.md`](file:///Users/trankhanhtuong/.gemini/antigravity/scratch/IOT102_DRONE-PROJECT/DroneIoT_macOS/README.md)
* Nếu sử dụng **Windows**, chuyển vào thư mục: [`DroneIoT_Windows/README.md`](file:///Users/trankhanhtuong/.gemini/antigravity/scratch/IOT102_DRONE-PROJECT/DroneIoT_Windows/README.md)

### 2. Các bước chuẩn bị quan trọng:
* **Docker Desktop**: Phải được cài đặt và đang chạy.
* **Arduino IDE**: Cần cài đặt gói board *AmebaD* (cho BW16) và các thư viện `DHT`, `PubSubClient`.
* **Python 3.10+**: Dành cho gateway fusion đồng bộ dữ liệu.

---

## 📝 Bản Quyền / License

Dự án này được xây dựng cho môn học **IOT102** - Trường Đại học FPT.
*Phát triển bởi Khánh Tường.*
