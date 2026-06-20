# Sơ đồ đấu dây — BW16 + DHT22 + MQ-135

## Bảng kết nối chi tiết

| Cảm biến | Chân cảm biến | Chân BW16 | Mức điện áp | Ghi chú |
|----------|--------------|-----------|-------------|---------|
| DHT22 | VCC | 3.3V | 3.3V | Không cấp 5V — sẽ hỏng |
| DHT22 | GND | GND | 0V | |
| DHT22 | DATA | PA_26 | 3.3V logic | Cần điện trở pull-up 10kΩ (VCC → DATA) |
| DHT22 | NC | — | — | Chân thứ 3 để trống |
| MQ-135 | VCC | **5V** | **5V BẮT BUỘC** | Dùng nguồn ngoài hoặc pin USB |
| MQ-135 | GND | GND | 0V | |
| MQ-135 | AOUT | PB_1 | **≤3.3V** | **Xem cảnh báo điện áp bên dưới** |
| MQ-135 | DOUT | — | — | Không dùng (chỉ dùng tín hiệu analog) |

---

## ⚠️ Cảnh báo điện áp MQ-135 → BW16

**Vấn đề**: MQ-135 AOUT xuất tín hiệu 0–5V, nhưng ADC của BW16 chỉ chịu **tối đa 3.3V**.
Cấp quá 3.3V vào chân ADC sẽ **hỏng vi điều khiển**.

**Giải pháp — Voltage Divider (khuyến nghị):**

```
MQ-135 AOUT ──┬──[10kΩ]──── 3.3V (đo tại đây → PB_1)
              └──[10kΩ]──── GND
```

Điện áp tại điểm đo = AOUT × (10k / (10k+10k)) = AOUT × 0.5
→ 5V × 0.5 = **2.5V max** → An toàn cho BW16.

---

## Cách vào Upload Mode đúng cho BW16

Board BW16 có **2 nút nhỏ**:
- **BURN** — nút gần cổng USB Type-C
- **RESET** — nút cạnh còn lại

### Quy trình upload (làm trong 5 giây đếm ngược của Arduino IDE):

1. Nhấn **Upload** trong Arduino IDE
2. Chờ IDE hiện đếm ngược: `05 → 04 → 03...`
3. Trong khi đếm ngược: **giữ nút BURN** (không thả)
4. Nhấn + thả **RESET** trong khi vẫn giữ BURN
5. **Thả BURN**
6. IDE tự upload — không cần làm gì thêm

✅ Upload thành công khi thấy: `Uploading image(s) completed. Upload Image done.`

---

## Cài Board BW16 trong Arduino IDE

1. **Preferences** → dán vào "Additional Boards Manager URLs":
   ```
   https://github.com/ambiot/ambd_arduino/raw/master/Arduino_package/package_realtek.com_amebad_index.json
   ```
2. **Tools → Board → Boards Manager** → tìm `AmebaD` → Install
3. **Tools → Board** → chọn `AmebaD(RTL8720DN)` → chọn `BW16`
4. **Tools → Port** → chọn `/dev/cu.usbserial-xxxx` (macOS) hoặc `COM3` (Windows)

---

## Lấy IP máy tính để điền vào `mqtt_server`

**macOS:**
```bash
ipconfig getifaddr en0   # WiFi thường
ipconfig getifaddr en1   # Nếu dùng adapter khác
```

**Windows:**
```cmd
ipconfig
# Tìm dòng "IPv4 Address" của card "Wi-Fi" hoặc "Wireless LAN adapter"
```

---

## Lỗi upload thường gặp

| Lỗi | Nguyên nhân | Fix |
|-----|-------------|-----|
| `error: Enter Uart Download Mode` | Nhấn nút không đúng lúc | Làm lại BURN+RESET trong 5s đếm ngược |
| `Cannot access /dev/cu.usbserial-xxxx` | Board không phản hồi | Kiểm tra cáp data, thử lại Upload Mode |
| Port không hiện trong danh sách | Thiếu driver CH340 | **macOS**: Cài tại wch-ic.com → CH341SER_MAC |
| Port không hiện (Windows) | Thiếu driver | **Windows**: Cài CH340 driver từ wch-ic.com |
| `[not connected]` ở status bar | Cáp chỉ sạc | Đổi sang cáp **data** (có dây D+/D-) |

### Kiểm tra port (macOS):
```bash
ls /dev/cu.*
# Phải thấy: /dev/cu.usbserial-xxxx sau khi cắm board
```

### Kiểm tra port (Windows):
```
Device Manager → Ports (COM & LPT)
# Phải thấy: USB-SERIAL CH340 (COMx)
```
