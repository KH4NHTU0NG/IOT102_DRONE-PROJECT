# BÍ KÍP BẢO VỆ ĐỒ ÁN DRONE IOT (DEFENSE FAQ)

Tài liệu này tổng hợp toàn bộ kiến trúc lõi của dự án và cách trả lời các câu hỏi phản biện từ Hội đồng Giám khảo. Hãy đọc kỹ để nắm vững "linh hồn" của hệ thống.

---

## PHẦN 1: TỔNG QUAN KIẾN TRÚC HỆ THỐNG (DATA FLOW)

**Hệ thống được chia làm 3 mảng độc lập nhưng giao tiếp thời gian thực với nhau:**

1. **Mảng IoT Payload (Mạch BW16 - C++):**
   - Đóng vai trò là "Thiết bị ngoại vi" thu thập môi trường.
   - Nó đọc cảm biến (Nhiệt độ DHT11, Khí Gas MQ2), nhận diện nguy hiểm và hú còi.
   - Dữ liệu được đẩy thẳng lên mạng Internet qua giao thức **MQTT** (Mosquitto/HiveMQ). Nhờ WiFi, nó không phụ thuộc vào bộ não điều khiển bay.

2. **Mảng Điều khiển Bay (Mamba F405 hoặc SITL):**
   - Đóng vai trò là "Bộ não Không gian" (Flight Controller). 
   - Nó xuất ra tín hiệu vị trí GPS, la bàn (Attitude), tốc độ động cơ thông qua giao thức đặc thù của hàng không là **MAVLink**.

3. **Mảng Trạm Mặt đất (Web GCS - JS/HTML & Gateway fusion.py):**
   - Giao diện Web (HTML/JS) không thể tự động hiểu được ngôn ngữ MAVLink của máy bay.
   - Do đó, script `fusion.py` đóng vai trò là "Phiên dịch viên" (Gateway). Nó cắm vào mạch Mamba (hoặc SITL), đọc MAVLink, giải mã thành định dạng JSON dễ hiểu, rồi lại đẩy lên mạng qua MQTT.
   - Web GCS chỉ việc đăng ký (Subscribe) các topic MQTT để hứng toàn bộ dữ liệu từ cả BW16 và Mamba để vẽ biểu đồ và Bản đồ.

---

## PHẦN 2: TOP CÂU HỎI PHẢN BIỆN (FAQ) VÀ CÁCH TRẢ LỜI

### ❓ Câu 1: Tại sao em không cắm thẳng cảm biến vào mạch Mamba F405 mà phải dùng thêm mạch BW16 cho tốn kém?
**Đáp:** "Dạ thưa thầy/cô, việc tách riêng Payload (BW16) và Flight Controller (Mamba) là **Kiến trúc Phân tán (Decentralized Architecture)** tiêu chuẩn trong thiết kế UAV hiện đại. 
- Thứ nhất, mạch điều khiển bay (Mamba) cần tập trung 100% tài nguyên CPU để tính toán cân bằng PID (1000 lần/giây). Nếu bắt nó xử lý thêm cảm biến môi trường và kết nối mạng, nó có thể bị quá tải dẫn đến rơi máy bay.
- Thứ hai, việc dùng BW16 giúp Payload có thể tự kết nối WiFi/4G độc lập. Nếu mạch bay bị hỏng hoặc mất kết nối vô tuyến (Radio), Payload vẫn tiếp tục gửi tọa độ và báo động khí gas về trạm mặt đất, tăng tính an toàn và dự phòng (Redundancy)."

### ❓ Câu 2: Script `fusion.py` hoạt động thế nào? Nếu xử lý nhiều thứ cùng lúc thì có bị đơ không?
**Đáp:** "Dạ thưa thầy/cô, `fusion.py` không hề bị đơ vì em đã áp dụng kỹ thuật **Đa luồng (Multi-threading)**. 
- Em tách chương trình thành các Thread (luồng) chạy song song: Một luồng chuyên đọc MAVLink từ Mamba, một luồng chuyên nghe lệnh từ MQTT, và một luồng chuyên gửi dữ liệu lên Web. 
- Việc áp dụng Threading (với thư viện `threading` trong Python) giúp các tác vụ I/O (chờ mạng, chờ cổng USB) không bị chặn (block) lẫn nhau, giúp dữ liệu hiển thị trên Web gần như không có độ trễ (Real-time)."

### ❓ Câu 3: Tại sao lại dùng MQTT để giao tiếp với Web thay vì dùng HTTP API (RESTful)?
**Đáp:** "Dạ, vì đặc thù của Drone là **dữ liệu Streaming** (dữ liệu tuôn chảy liên tục). Tọa độ GPS và độ nghiêng thay đổi từng mili-giây.
- Nếu dùng HTTP, Web GCS sẽ phải liên tục gửi request "Hỏi - Đáp" (Polling), gây nghẽn mạng và tốn pin máy bay.
- Với MQTT, em dùng cơ chế Publish/Subscribe (Xuất bản/Đăng ký) và giao thức WebSockets. Web GCS chỉ cần mở 1 kết nối duy nhất, dữ liệu từ Drone sẽ tự động 'bắn' thẳng về trình duyệt một cách thụ động, giúp tiết kiệm băng thông tối đa và cực kỳ nhẹ."

### ❓ Câu 4: Đồ án này có mô phỏng (SITL) và có mạch thật (Mamba). Tại sao em phải làm cả hai?
**Đáp:** "Dạ, đây là mô hình **Digital Twin & Khảo nghiệm Lai (Hardware-in-the-loop / SITL)**.
- Khi làm việc trong phòng Lab, Drone không bắt được sóng vệ tinh GPS, nên nếu dùng mạch thật thì bản đồ (Map) trên Web GCS không thể hoạt động.
- Do đó, em dùng SITL để giả lập hệ thống GPS nhằm chứng minh tính năng vẽ Bản đồ định vị và Waypoint của hệ thống phần mềm. Sau đó, em dùng mạch Mamba và BW16 thật để chứng minh tốc độ phản hồi vật lý (độ trễ khi nghiêng mạch, còi hú thật). Đây là quá trình Test chuẩn mực từ phần mềm ra phần cứng trước khi thực sự đem máy bay ra bãi thử nghiệm."

### ❓ Câu 5: Dữ liệu Khí Gas và Nhiệt độ hiển thị dạng Bản đồ nhiệt (Heatmap) hoạt động như thế nào?
**Đáp:** "Dạ, trên Frontend (Web GCS), em sử dụng thư viện `Leaflet.js` kết hợp với plugin Heatmap.
Khi hàm `updateMap(lat, lon)` nhận được Tọa độ mới từ Drone, em sẽ lấy giá trị Nhiệt độ (hoặc Gas) được cập nhật mới nhất từ mạch BW16. Thuật toán sẽ lưu điểm này vào một mảng lịch sử (History Array). Lớp Heatmap sẽ quét qua mảng này, tính toán cường độ (Intensity) dựa trên độ lớn của nhiệt độ và nội suy màu sắc (Nóng -> Đỏ, Lạnh -> Xanh) đè lên bản đồ Google Maps."

---

## PHẦN 3: GIẢI PHẪU CÁC FILE CODE CHÍNH (CẦN NHỚ)

1. **`fusion.py` (Bộ não trung gian)**
   - Hàm `mavlink_loop()`: Vòng lặp vô tận dùng thư viện `pymavlink` để hứng gói tin từ cổng USB. Bóc tách `ATTITUDE` (độ nghiêng) và `GLOBAL_POSITION_INT` (GPS).
   - Hàm `mqtt_loop()`: Vòng lặp giữ kết nối với Paho MQTT, gửi các gói tin JSON chứa tọa độ lên mạng.

2. **`index.html` (Giao diện Web)**
   - Hàm `onMessageArrived(message)`: Trái tim của Web GCS. Nó hứng mọi gói tin JSON từ MQTT, dùng lệnh `if (topic === ...)` để phân loại xem đây là dữ liệu GPS, Nhiệt độ hay Động cơ, rồi đẩy vào các biểu đồ Chart.js tương ứng.
   - Hàm `toggleHeatmap()`: Chức năng vẽ vệt nhiệt độ đè lên bản đồ Leaflet.

3. **`bw16_sensor.ino` (Code Nhúng C++)**
   - Hàm `readSensors()`: Đọc chân Analog và Digital để lấy thông số môi trường.
   - Đã áp dụng cơ chế **Debounce** (chống nhiễu tín hiệu) hoặc giới hạn tần suất gửi bằng hàm `millis()` thay vì dùng `delay()`, giúp mạch không bị "đơ" chặn các quá trình khác.

---
*(Chúc bạn bảo vệ đồ án thành công! Hãy đọc kỹ tài liệu này, bạn đã nắm 90% cơ hội đạt điểm A+)*
