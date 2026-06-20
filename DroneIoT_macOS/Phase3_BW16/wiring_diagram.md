# Sơ đồ đấu dây — Hệ thống Drone IoT Mở rộng (BW16 + Cảm biến + Servo + OLED + LED Ring + DFPlayer)

Đây là sơ đồ kết nối chi tiết cho toàn bộ các thiết bị phần cứng trong hệ thống sau khi bổ sung thêm OLED, Servo, LED Ring và DFPlayer Mini.

---

## 1. Bảng kết nối chân chi tiết (Pinout Mapping)

| Thiết bị | Chân thiết bị | Chân BW16 | Mức điện áp | Ghi chú |
| :--- | :--- | :--- | :--- | :--- |
| **DHT22** (Nhiệt độ) | VCC / GND | 3.3V / GND | 3.3V | Cần điện trở kéo lên 10kΩ (VCC -> DATA) |
| | DATA | **PA_14 (D10)** | 3.3V Logic | **Đã chuyển từ PA_26 để tránh xung đột I2C** |
| **MQ-135** (Khí gas) | VCC / GND | 5V / GND | 5V | Bắt buộc cấp nguồn 5V ngoài hoặc từ VBUS USB |
| | AOUT | **PB_1 (D4)** | ≤ 3.3V | **Phải dùng cầu phân áp giảm thế (xem bên dưới)** |
| **Buzzer** (Còi kêu) | VCC / GND | 3.3V / GND | 3.3V | Còi báo động dự phòng |
| | I/O Pin | **PA_15 (D9)** | 3.3V Logic | |
| **OLED SSD1306** | VCC / GND | 3.3V / GND | 3.3V | Màn hình hiển thị trạng thái hệ thống |
| | SCL / SDA | **PA_25 (D7) / PA_26 (D8)** | 3.3V Logic | Giao tiếp I²C mặc định |
| **Servo SG90** | VCC / GND | 5V / GND | 5V | Mô phỏng cơ cấu chốt thả phao cứu nạn |
| | PWM (Vàng/Cam) | **PA_12 (D12)** | 3.3V Logic | Điều khiển góc quay bằng xung PWM |
| **LED WS2812B Ring**| VCC / GND | 5V / GND | 5V | Vòng LED RGB chỉ thị trạng thái thông minh |
| | DATA (In) | **PA_13 (D11)** | 3.3V Logic | |
| **DFPlayer Mini** | VCC / GND | 5V / GND | 5V | Module phát âm thanh cảnh báo |
| | RX / TX | **PB_3 (D6) / PA_27 (D2)**| 3.3V Logic | Giao tiếp UART qua SoftwareSerial |

---

## 2. Lưu ý đặc biệt về phần cứng

### ⚠️ A. Cầu phân áp cho MQ-135 (Voltage Divider)
MQ-135 sử dụng nguồn 5V nên chân AOUT sẽ xuất ra dải điện áp 0-5V. Tuy nhiên, chân ADC của BW16 (`PB_1`) chỉ chịu được điện áp tối đa 3.3V. Cắm thẳng 5V sẽ gây hỏng vĩnh viễn chân ADC của vi điều khiển.
*   **Sơ đồ đấu nối cầu phân áp:**
    ```
    MQ-135 AOUT ──┬──[10kΩ]──── 3.3V (Đo tại đây nối vào PB_1)
                  └──[10kΩ]──── GND
    ```
    Điện áp thực tế cấp vào `PB_1` sẽ giảm đi một nửa (tối đa là 2.5V), đảm bảo an toàn tuyệt đối cho BW16.

### ⚠️ B. Kết nối RX/TX của DFPlayer Mini
*   Chân **TX của BW16 (`PB_3`)** phải được nối vào chân **RX của DFPlayer Mini** thông qua một điện trở **1kΩ** để lọc nhiễu và bảo vệ cổng logic.
*   Chân **RX của BW16 (`PA_27`)** nối trực tiếp vào chân **TX của DFPlayer Mini**.

---

## 3. Cách vào Upload Mode cho BW16

Board BW16 có 2 nút nhấn: **BURN** (gần cổng USB Type-C) và **RESET** (cạnh đối diện).
1. Nhấn nút **Upload** trong Arduino IDE.
2. Khi IDE bắt đầu hiện đếm ngược `05 -> 04 -> 03...`, thực hiện:
   - **Giữ nút BURN** (không thả ra).
   - **Nhấn và thả nút RESET** một lần.
   - **Thả nút BURN** ra.
3. IDE sẽ nhận được cổng nạp và tự động biên dịch, ghi firmware.
