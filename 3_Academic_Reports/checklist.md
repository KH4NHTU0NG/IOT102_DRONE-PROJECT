# Checklist Kiểm Thử & Vận Hành Thực Địa (IRL Field Operations Checklist)
*Tài liệu kiểm định quy chuẩn cho nhánh bay thật `IRL_test` — Đồ án IOT102 Drone Environmental Payload*

---

## 🛑 PHẦN 1: KIỂM TRA TẠI NHÀ TRƯỚC NGÀY RA SÂN (`PRE-FLIGHT HOME CHECK`)

Trước khi mang máy bay ra sân thực địa, nhóm kỹ sư BẮT BUỘC phải kiểm tra đạt 100% các tiêu chí dưới đây:

### 1. Nguồn Điện & Pin
- [ ] **Pin LiPo 4S 1800mAh:** Dùng sạc cân bằng kiểm tra điện áp đạt **`16.8V`** (mỗi cell đạt từ `4.18V - 4.20V`). Tuyệt đối không bay nếu có cell dưới `3.8V` trước chuyến bay.
- [ ] **Pin cho tay cầm MC6C:** Kiểm tra 4 viên pin AA hoặc pin sạc trong tay cầm hiển thị đèn xanh lá (đủ điện áp `> 5.5V`).
- [ ] **Nguồn cho Payload BW16:** Nếu dùng Sạc dự phòng mini (`Power Bank 5V`), kiểm tra sạc đầy 100% dung lượng. Nếu dùng cáp lấy nguồn từ giắc cân bằng pin LiPo qua `UBEC 5V/3A`, đo lại đầu ra đạt chuẩn `4.9V - 5.1V`.

### 2. Phần Cứng & Cơ Khí Máy Bay
- [ ] **Ốc siết động cơ (`MT2204`):** Kiểm tra 16 con ốc dưới tay khung carbon đã được siết chặt với keo khóa ren (`Loctite Blue 242`).
- [ ] **Cánh quạt (`Propellers`):** Kiểm tra cánh không bị sứt mẻ hay nứt. Lắp đúng chiều Thuận/Nghịch theo quy tắc chuẩn Quadcopter X:
  - `Motor 1 (Sau Phải)` & `Motor 4 (Trước Trái):` Cánh quay thuận (`CW`).
  - `Motor 2 (Trước Phải)` & `Motor 3 (Sau Trái):` Cánh quay nghịch (`CCW`).
- [ ] **Hộp Payload IoT:** Kiểm tra mạch `Realtek BW16`, cảm biến `DHT22`, `MQ-135`, `OLED`, và `Servo SG90` đã được cố định chắc chắn dưới bụng máy bay bằng băng dính gai (`Velcro`) và dây rút (`Zip-ties`).
- [ ] **Chống rung & tụt dây (`Breadboard Security`):** Các đầu dây Dupont trên Breadboard đã được chấm keo nến (`Hot glue`) để chống bung khi motor rung tốc độ cao. Cảm biến siêu âm (`HC-SR04`) hướng thẳng xuống đất và không bị cánh quạt che khuất.

### 3. Phần Mềm & Sóng Vô Tuyến
- [ ] **Nạp cấu hình WiFi 4G Hotspot:** Mở tệp `1_BW16_IoT_Payload/bw16_sensor/secrets.h` trong Arduino IDE, xác nhận `SECRET_SSID` và `SECRET_PASS` khớp chính xác với điểm phát WiFi trên điện thoại di động của bạn. Nạp (`Upload`) thành công vào mạch BW16.
- [ ] **Đồng bộ sóng (`Bind RX/TX`):** Bật tay cầm `MC6C` và cắm nguồn máy bay, xác nhận đèn LED trên bộ thu `MC7REV2` sáng đứng không chớp nháy (kết nối `S.BUS` 2.4GHz ổn định).

---

## ⚡ PHẦN 2: QUY TRÌNH KHỞI ĐỘNG TẠI SÂN BAY (`AIRFIELD STARTUP SEQUENCE`)

Tuân thủ đúng thứ tự 6 bước dưới đây tại bãi bay thực địa để đảm bảo an toàn tối đa:

| Bước | Hành Động Kỹ Thuật | Người Thực Hiện | Tiêu Chí Xác Nhận (`OK Criteria`) |
| :---: | :--- | :--- | :--- |
| **1** | Bật Phát WiFi Di Động (`4G Personal Hotspot`) trên điện thoại di động. | Kỹ sư IoT | Điện thoại báo chế độ Hotspot sẵn sàng, không chặn địa chỉ MAC. |
| **2** | Bật tay cầm điều khiển vô tuyến **`Microzone MC6C`** trước tiên. | Phi công (Pilot) | Cần ga (`Throttle`) đã hạ về `0%`, công tắc `ARM` đang ở vị trí `DISARMED`. Tay cầm kêu `Tít` và đèn xanh sáng. |
| **3** | Cắm pin LiPo 4S vào giắc `XT60` của máy bay (hoặc bật nguồn cho mạch BW16). | Phi công & Kỹ sư | 4 Motor phát tiếng chuông khởi động `Tít tít tít - Tít tít`. Đèn trên bộ thu `MC7REV2` sáng cố định. |
| **4** | Kiểm tra kết nối WiFi của Hộp Payload. | Kỹ sư IoT | Màn hình OLED SSD1306 trên bụng máy bay hiển thị `[WIFI] Connected OK!` và địa chỉ IP được cấp. |
| **5** | Mở Giao diện Trạm mặt đất (`2_Web_GCS_Dashboard/index.html`) trên Laptop / Smartphone. Nhấn nút **`Connect`** vào Cloud MQTT (`broker.hivemq.com : 8000`). | Kỹ sư GCS | Đèn trạng thái góc phải Web Dashboard chuyển sang màu Xanh Lá **`Online`**. |
| **6** | Kiểm tra dữ liệu Telemetry & Kiểm thử cơ cấu thả hàng trước khi bay. | Kỹ sư GCS | • Các chỉ số Nhiệt độ, Độ ẩm, Khí Gas/CO2, Khoảng cách Sonar cập nhật đều đặn 1s/lần.<br>• Nhấn thử nút `Open 90°` và `Close 0°` trên Web ➔ Chốt Servo mở/đóng chính xác, còi `Buzzer` kêu đúng lệnh. |

---

## 🚁 PHẦN 3: QUY TRÌNH BAY & THẢ HÀNG THỰC TẾ (`IRL FLIGHT & DROP PROCEDURE`)

1. **Chuẩn bị khu vực bay:** Đảm bảo bán kính `15 mét` xung quanh điểm cất cánh không có người qua lại hay chướng ngại vật.
2. **Cất cánh (`Takeoff`):** Phi công gạt công tắc `ARM` trên tay cầm MC6C (máy bay quay cánh ở tốc độ idle), từ từ đẩy cần ga `Throttle` để máy bay cất cánh đạt độ cao ổn định (`3 - 5 mét`).
3. **Di chuyển đến mục tiêu:** Phi công điều khiển máy bay bay lơ lửng (`Loiter / Hover`) ngay phía trên khu vực thả hàng cứu trợ.
4. **Giám sát độ cao Sonar:** Kỹ sư quan sát thông số `Sonar Dist` trên Web Dashboard. Khi độ cao cách mặt đất `< 100 cm` (hoặc đạt độ cao an toàn cho phép), báo cáo: *"Độ cao tối ưu, chuẩn bị thả chốt!"*
5. **Thực thi lệnh thả hàng (`Payload Drop`):** Kỹ sư nhấn nút **`Open 90°`** (hoặc kéo thanh trượt Servo) trên Web Dashboard. Chốt Servo mở ra thả gói hàng xuống đất.
6. **Đóng chốt & Hạ cánh (`Land`):** Kỹ sư nhấn **`Close 0°`** đóng chốt Servo lại. Phi công hạ cần ga từ từ cho máy bay tiếp đất nhẹ nhàng, sau đó gạt công tắc `DISARM` để tắt hoàn toàn động cơ.

---

## 🔧 PHẦN 4: BẢNG XỬ LÝ SỰ CỐ THỰC ĐỊA (`FIELD TROUBLESHOOTING GUIDE`)

| Triệu Chứng | Nguyên Nhân Khả Dĩ | Cách Khắc Phục Nhanh Tại Sân |
| :--- | :--- | :--- |
| **Màn hình OLED báo `WiFi Connecting...` mãi không xong** | • Tên `SSID` hoặc Mật khẩu trong `secrets.h` không khớp.<br>• Điện thoại chưa bật Hotspot hoặc đang để băng tần `5GHz` (BW16 cần `2.4GHz` hoặc Dual-band). | • Kiểm tra lại điện thoại chuyển Hotspot sang chế độ **`Maximize Compatibility` (2.4GHz)**.<br>• Khởi động lại nguồn mạch BW16. |
| **Web Dashboard báo `MQTT Offline` / Không bấm Connect được** | • Mất kết nối Internet từ Laptop/Điện thoại ra ngoài Cloud.<br>• Broker public `broker.hivemq.com` bị nghẽn ngẫu nhiên. | • Kiểm tra Laptop/Điện thoại đã kết nối đúng mạng có Internet.<br>• Nhấn `F5` tải lại trang Web Dashboard rồi bấm lại `Connect`. |
| **Cảm biến khí Gas (`MQ-135`) báo `DANGER` liên tục ngay khi mới bật** | • Màng nung bên trong MQ-135 chưa được làm nóng đủ (`Pre-heat`).<br>• Lỗ cắm Dải nguồn 5V bị sụt áp làm điện áp tham chiếu ADC bị lệch. | • Để mạch chạy liên tục trong `2 - 3 phút` để cảm biến MQ-135 ổn định nhiệt độ.<br>• Nhấn nút **`Reset Onboard`** trên Web Dashboard. |
| **Nhấn nút mở Servo trên Web nhưng chốt thả hàng không xoay** | • Dây tín hiệu Servo `PA13` (`PA_13`) bị lỏng khỏi Breadboard.<br>• Nguồn cấp `VCC (5V)` của Servo bị yếu hoặc chạm dây mass. | • Kiểm tra chắc chắn 3 dây Servo: Đỏ (`+5V`), Đen (`GND`), Vàng/Trắng (`PA13`).<br>• Nhấn thử nút `LED ON` hoặc `Buzzer ON` để xác nhận lệnh MQTT vẫn tới mạch. |
| **Số đo Sonar nhảy loạn xạ (`0 cm` hoặc `999 cm`)** | • Cảm biến siêu âm bị nghiêng hoặc cánh quạt che khuất đường truyền âm.<br>• Mặt cỏ đất ghồ ghề hấp thụ sóng siêu âm. | • Kiểm tra góc gắn HC-SR04 vuông góc 90° với mặt đất.<br>• Cảm biến siêu âm chỉ chính xác nhất trong phạm vi `< 250 cm`. |

---
> **Xác nhận hoàn tất kiểm định:** Mọi quy trình trong tài liệu này tuân thủ đúng chuẩn kiểm thử khắt khe (`Meticulous Debug and Test Workflow`), đảm bảo an toàn tuyệt đối cho người và thiết bị trong quá trình bay thực địa.
