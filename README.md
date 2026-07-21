# DANH MỤC MÃ NGUỒN CHÍNH THỨC — ĐỒ ÁN CUỐI KÌ DRONE IOT102

Bộ thư mục này chứa **đúng các tệp mã nguồn cốt lõi (Core Source Code)** của hệ thống, đã được loại bỏ toàn bộ file log, file cấu hình trung gian, script chạy thử và dữ liệu rác để nộp Hội đồng đánh giá.

---

## BẢNG TỔNG HỢP CẤU TRÚC MÃ NGUỒN (CODEBASE TABLE)

| Lớp Kiến Trúc | Thư mục / Đường dẫn | Tên File Mã Nguon | Ngôn ngữ | Chức năng chính |
| :--- | :--- | :--- | :--- | :--- |
| **Lớp 1: Thiết Bị Vật Lý (IoT Payload)** | `1_Firmware_BW16/` | `bw16_sensor.ino` | C++ / Arduino | Đọc cảm biến khí độc MQ-135, DHT22, HC-SR04; điều khiển Servo thả hàng (Non-blocking PWM), OLED và Buzzer qua MQTT WiFi. |
| **Lớp 2: Cổng Tích Hợp (Fusion Gateway)** | `2_Backend_Fusion/` | `main.py` | Python 3 | Khởi chạy đa luồng (Multi-threading), quản lý vòng đời Gateway kết nối MAVLink và MQTT. |
|  |  | `mavlink_handler.py` | Python 3 | Giao tiếp UDP 14550 với Flight Controller (SITL/PX4); đọc Telemetry 60 FPS, nạp Waypoints và gửi lệnh bay MAVLink. |
|  |  | `mqtt_handler.py` | Python 3 | Đăng ký & Xuất bản gói tin MQTT JSON; điều khiển ngoại vi Payload và chuyển tiếp trạng thái Drone lên Web Dashboard. |
|  |  | `db_logger.py` | Python 3 | Ghi nhận tự động toàn bộ lịch sử bay và thông số môi trường vào cơ sở dữ liệu SQLite (`flight_data.db`). |
| **Lớp 3: Giám Sát Mặt Đất (Web GCS)** | `3_Web_GCS_Dashboard/` | `index.html` | HTML5 | Khung giao diện Ground Control Station với bản đồ bay, bảng điều khiển HUD và cụm phím thao tác. |
|  |  | `assets/css/styles.css` | CSS3 | Hệ thống giao diện hiện đại (Glassmorphism, Dark Theme, bố cục chuẩn chuyên nghiệp). |
|  |  | `assets/js/app.js` | JavaScript | Xử lý MQTT WebSockets, dựng mô hình 3D Attitude mượt mà 60 FPS (SLERP) và bản đồ tương tác Leaflet Waypoints. |

---

## THỐNG KÊ TỔNG QUAN
- **Tổng số tệp mã nguồn chính:** 8 file
- **Kiến trúc phần mềm:** Hướng sự kiện (Event-driven) & Đa luồng phi đồng bộ (Asynchronous Multi-threading)
- **Giao thức liên thông:** MAVLink v2 (Hàng không) & MQTT v3.1.1 over WebSockets (IoT)
