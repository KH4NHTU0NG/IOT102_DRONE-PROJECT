# Đề xuất Tích hợp Phần cứng (Hardware Integration Proposal)
**Dự án:** Hệ thống Trạm Điều khiển Mặt đất & IoT Payload cho Drone (Web GCS)
**Module Cốt lõi:** Tách biệt môi trường Software-In-The-Loop (SITL) và Hardware Bench Test (Mamba F405)

---

## 1. Mở đầu: Vấn đề của Kiến trúc Hỗn hợp (Hybrid Architecture)
Trong quá trình phát triển hệ thống Drone chuyên nghiệp, việc sử dụng dữ liệu mô phỏng (Simulated Data) trộn lẫn với dữ liệu vật lý thời gian thực (Real-time Physical Sensor) là vi phạm nguyên tắc an toàn của **Thuật toán Lọc Kalman Mở rộng (Extended Kalman Filter - EKF)**.

Cụ thể, EKF liên tục so sánh dữ liệu từ Cảm biến chuyển động (IMU/Gyro/Accel) và Dữ liệu định vị (GPS). Nếu hệ thống bơm tọa độ GPS giả lập (cho biết Drone đang di chuyển) nhưng mạch điều khiển bay (Flight Controller) Mamba F405 lại đứng yên trên bàn thử nghiệm (IMU báo vận tốc 0m/s), EKF sẽ lập tức kích hoạt lỗi **"EKF Variance Error"** (Sai lệch dữ liệu định vị) và khóa toàn bộ hệ thống (Failsafe/Disarm).

Do đó, để đảm bảo tính hàn lâm và chuyên nghiệp của đồ án, hệ thống đã được tái cấu trúc thành **2 môi trường thử nghiệm độc lập** hoàn toàn, phục vụ cho 2 mục đích trình diễn khác nhau.

---

## 2. Môi trường 1: Bay Mô phỏng Toàn phần (SITL Mode)
**Script khởi động:** `start_all.sh`

Môi trường SITL (Software-In-The-Loop) là tiêu chuẩn công nghiệp để phát triển và xác thực thuật toán điều khiển (Flight Logic) trước khi áp dụng vào phần cứng thực. Toàn bộ mã nguồn ArduCopter được biên dịch và chạy thẳng trên nhân hệ điều hành của máy trạm (Mac/Linux).

**Vai trò trong Đồ án:**
- Cung cấp môi trường bay hoàn hảo không phụ thuộc vào rủi ro thời tiết hay lỗi phần cứng.
- Cung cấp dữ liệu GPS giả lập liên tục để biểu diễn hệ thống Bản đồ (Map) trên Web GCS.
- Cho phép trình diễn việc thiết lập Lộ trình bay tự động (Waypoint Mission) qua phần mềm QGroundControl.
- Kích hoạt và thử nghiệm các kịch bản khẩn cấp (RTL/Land) khi kết nối mạng bị gián đoạn (Failsafe Watchdog).

---

## 3. Môi trường 2: Bàn Thử nghiệm Phần cứng (Mamba Hardware Test)
**Script khởi động:** `start_mamba_hw.sh`

Thay vì bay mô phỏng, mạch **Mamba F405 (ArduCopter)** được cắm trực tiếp vào hệ thống thông qua cáp USB MAVLink để đóng vai trò là "Bộ não trung tâm" xử lý vật lý. Lúc này, mô hình sẽ hoạt động như một Trạm thử nghiệm Phần cứng (Hardware Bench Test).

**Vai trò trong Đồ án:**
- **Xác thực Phản hồi Vật lý:** Web GCS sẽ hiển thị La bàn 3D (Attitude Indicator) chuyển động thời gian thực với độ trễ cực thấp (< 50ms) dựa trên chính các thao tác nghiêng, lật bo mạch của người dùng, chứng minh băng thông truyền tải của Gateway MAVLink -> MQTT.
- **Tương tác Payload thực (BW16):** Đánh giá khả năng cấp nguồn (Power Distribution) từ Mamba cho bo mạch IoT BW16 và các cảm biến.
- **Kiểm định I/O:** Chuyển hóa các lệnh điều khiển (Mở Servo thả hàng, bật Còi báo động) từ Web GCS qua WiFi đến phần cứng thực một cách trực quan, làm cơ sở vững chắc cho việc ráp khung máy bay (Airframe) thật trong tương lai.

---

## 4. Tổng kết
Việc tách biệt hệ thống thành 2 module `start_all.sh` (Cho trình diễn bay trên Map) và `start_mamba_hw.sh` (Cho trình diễn vật lý) giúp đồ án đạt được tính bao quát cao. Hệ thống vừa có khả năng mô phỏng vận hành diện rộng như một hệ thống UAV thương mại (Software), vừa chứng minh được khả năng chế tạo, điều khiển và tích hợp linh kiện điện tử chuyên dụng (Hardware).
