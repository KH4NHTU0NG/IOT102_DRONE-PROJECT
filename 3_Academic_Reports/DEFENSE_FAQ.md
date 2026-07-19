# BÍ KÍP BẢO VỆ ĐỒ ÁN DRONE IOT (DEFENSE FAQ — BAY THẬT THỰC ĐỊA `IRL_test`)
*Bộ câu hỏi phản biện từ Hội đồng Giám khảo & Lời giải chuẩn kỹ thuật chuyên sâu*

---

## PHẦN 1: TỔNG QUAN KIẾN TRÚC HAI TẦNG ĐỘC LẬP (`DUAL-LAYER ARCHITECTURE`)

Khi bảo vệ đồ án thực địa (`IRL_test`), điểm sáng giá nhất của nhóm là đã tư duy và thiết kế hệ thống tách biệt thành **2 tầng vận hành độc lập hoàn toàn**:

1. **Tầng Động Lực & An Toàn Bay (`Flight Control Layer - 2.4GHz Radio`):**
   - Phi công điều khiển máy bay cất/hạ cánh bằng tay cầm vô tuyến **Microzone MC6C** kết nối bộ thu `MC7REV2` qua giao thức **S.BUS 2.4GHz**.
   - Độ trễ cực thấp (`< 10ms`), không phụ thuộc vào mạng Internet hay Cloud. Đảm bảo an toàn tuyệt đối khi bay ngoài trời.

2. **Tầng Giám Sát & Tải Trọng IoT (`Payload Layer - WiFi 4G Cloud MQTT`):**
   - Hộp tải trọng nhúng vi điều khiển băng tần kép **Realtek Ameba BW16 (RTL8720DN)** gắn dưới bụng máy bay.
   - Thu thập chỉ số môi trường (`DHT22, MQ-135, Sonar HC-SR04`) và điều khiển góc Servo SG90 thả chốt hàng.
   - Kết nối tự động vào mạng WiFi 4G Hotspot, gửi/nhận lệnh trực tiếp từ **Public Cloud MQTT Broker (`broker.hivemq.com : 1800 / WebSockets 8000`)** về Web Dashboard tại mặt đất.

---

## PHẦN 2: TOP CÂU HỎI PHẢN BIỆN (FAQ) TỪ HỘI ĐỒNG VÀ ĐÁP ÁN CHUẨN

### ❓ Câu 1: Tại sao nhóm không cắm thẳng cảm biến vào mạch Flight Controller của máy bay mà phải tách riêng ra hộp mạch Realtek BW16?
**Đáp:** "Dạ thưa thầy/cô, việc tách riêng Tầng Lái Máy Bay (`Flight Controller`) và Tầng Giám Sát Tải Trọng (`IoT Payload BW16`) là **Kiến trúc Phân tán (`Decentralized Architecture`)** tiêu chuẩn trong thiết kế UAV thực chiến hiện đại:
- **Thứ nhất (An toàn bay tối thượng):** Mạch Flight Controller cần tập trung 100% tài nguyên CPU để tính toán vòng lặp PID giữ thăng bằng cho 4 động cơ (`1000 - 8000 lần/giây`). Nếu bắt mạch bay gánh thêm việc đọc cảm biến Gas analog, quản lý kết nối WiFi 4G và xử lý giao thức MQTT, ngắt mạng (`Network Interrupt`) hoặc treo chip IoT sẽ làm máy bay mất kiểm soát và rơi ngay lập tức.
- **Thứ hai (Dự phòng thảm họa — Redundancy):** Nhờ hộp BW16 có kết nối WiFi 4G và nguồn cấp độc lập, nếu máy bay gặp sự cố hạ cánh khẩn cấp hoặc mất sóng vô tuyến RC, hộp tải trọng vẫn tiếp tục gửi chỉ số môi trường và còi báo động (`Buzzer`) về trạm mặt đất, giúp cứu hộ và thu hồi thiết bị an toàn."

### ❓ Câu 2: Tại sao hệ thống lại sử dụng giao thức MQTT kết hợp WebSockets thay vì dùng HTTP RESTful API thông thường?
**Đáp:** "Dạ thưa thầy/cô, vì đặc thù của hệ thống giám sát trên máy bay là **Dữ liệu tuôn chảy liên tục (`Real-time Telemetry Streaming`)**:
- Nếu sử dụng HTTP RESTful, mỗi lần muốn cập nhật thông số, Web Dashboard sẽ phải liên tục gửi các request 'Hỏi - Đáp' (`Polling`) lên máy chủ. Việc này tạo ra hàng nghìn kết nối TCP mở/đóng liên tục, gây nghẽn băng thông 4G và làm tiêu hao pin của mạch nhúng rất nhanh.
- Với **MQTT qua WebSockets (Port 8000)**, hệ thống áp dụng cơ chế `Publish/Subscribe` (Xuất bản/Đăng ký). Web Dashboard và mạch BW16 chỉ cần mở **đúng 1 đường hầm kết nối duy nhất (`Single Persistent Connection`)**. Khi cảm biến trên máy bay có số liệu mới, Public Cloud Broker sẽ chủ động 'đẩy' (`Push`) dữ liệu về trình duyệt ngay lập tức với độ trễ chỉ vài chục mili-giây, cực kỳ nhẹ và tiết kiệm tài nguyên."

### ❓ Câu 3: Mạch BW16 vừa phải duy trì kết nối WiFi 4G vừa phải điều khiển Servo thả chốt. Làm sao để Servo không bị co giật (`Servo Jitter`) do sóng WiFi gây nhiễu?
**Đáp:** "Dạ, đây chính là điểm cải tiến kỹ thuật nổi bật trong phần firmware C++ (`bw16_sensor.ino`) của nhóm em.
- Trên các vi điều khiển IoT thông thường (như ESP8266 hay Arduino), nếu dùng thư viện `Servo.h` (dựa trên Software PWM), mỗi lần bộ phát WiFi truyền bản tin MQTT, ngắt hệ thống (`Interrupt`) sẽ làm sai lệch độ rộng xung khiến Servo bị co giật loạn xạ, có thể tự động rơi chốt thả hàng giữa trời.
- Để khắc phục triệt để, nhóm em đã lập trình sử dụng **Hardware PWM API (`pwmout_api`)** trực tiếp từ bộ đếm phần cứng của Realtek Ameba SDK (`chân PA13 / PA_13`). Bộ đếm phần cứng tự động tạo xung chuẩn `50Hz (20ms)` độc lập hoàn toàn với lõi xử lý WiFi, giúp chốt Servo thả hàng giữ vững góc `0°` tuyệt đối và chỉ xoay `90°` khi nhận đúng lệnh từ trạm mặt đất!"

### ❓ Câu 4: Làm thế nào để đảm bảo khi phi công cất cánh ngoài trời, mạch BW16 nhận đủ nguồn điện ổn định mà không làm cháy nổ vi điều khiển?
**Đáp:** "Dạ thưa thầy/cô, nhóm em tuân thủ nghiêm ngặt **Quy tắc cách ly và chuyển đổi điện áp (`True Voltage Regulation & Isolation`)**:
- Pin bay của máy bay là pin LiPo 4S có điện áp cao (`16.8V`). Trong khi đó mạch Realtek BW16 chỉ chịu điện áp tối đa `5V` tại chân `VIN` và hoạt động logic ở `3.3V`.
- Nhóm em không bao giờ lấy nguồn trực tiếp từ pin LiPo vào mạch mà sử dụng mạch ổn áp nội bộ **UBEC 5V/3A** từ Flight Controller (hoặc mạch hạ áp mini gắn giắc cân bằng pin LiPo). Mạch UBEC hạ điện áp từ 16.8V xuống đúng 5V chuẩn, cung cấp dòng điện dồi dào (`3A`) nuôi đủ cả mạch BW16, cảm biến khí Gas MQ-135 (cần dòng nung lớn) và động cơ Servo SG90 mà không gây sụt áp hay nóng mạch chính."

### ❓ Câu 5: Cảm biến siêu âm (`HC-SR04`) trên bụng máy bay đóng vai trò gì trong quy trình thả hàng thực địa?
**Đáp:** "Dạ, cảm biến siêu âm `HC-SR04` hướng thẳng xuống mặt đất đóng vai trò là **Thước đo độ cao cận cảnh (`Precision Altitude Altimeter`)** cho cơ cấu thả chốt:
- Khi máy bay bay trên cao (`> 3 mét`), phi công điều khiển hạ dần độ cao xuống khu vực mục tiêu.
- Khi máy bay tiến vào phạm vi `< 250 cm`, cảm biến siêu âm liên tục phát xung `10us` và gửi khoảng cách thực tế (`Sonar Dist`) về Web Dashboard. Kỹ sư quan sát thông số này, khi thấy máy bay cách mặt đất `< 100 cm` — mức độ cao an toàn tối ưu để thả gói hàng cứu trợ mà không bị vỡ — kỹ sư mới nhấn nút `Open 90°` để mở chốt thả hàng."

---

## PHẦN 3: ĐIỂM NHẤN TRONG MÃ NGUỒN C++ (`bw16_sensor.ino`)
Khi thầy cô hỏi về code, hãy chỉ ra 3 điểm lập trình chuyên nghiệp sau:
1. **Quản lý thời gian không chặn (`Non-blocking millis() loop`):** Toàn bộ chu kỳ đọc cảm biến và gửi MQTT được điều phối bằng hàm `millis()`, tuyệt đối không dùng `delay()` để mạch luôn phản hồi tức thì với lệnh mở Servo từ Web.
2. **Lọc dữ liệu cảm biến (`Data Validation & Filtering`):** Kiểm tra giá trị `isnan()` từ DHT22 và giới hạn khoảng cách Sonar (`constrain`) trước khi đóng gói JSON, tránh gửi dữ liệu rác lên Cloud.
3. **Cơ chế Watchdog / Failsafe:** Nếu mất kết nối WiFi 4G, đèn LED trạng thái chuyển sang nháy chậm và còi Buzzer phát tín hiệu cảnh báo để kỹ sư mặt đất biết tình trạng mạch.
