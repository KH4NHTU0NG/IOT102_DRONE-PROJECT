# Sơ đồ đấu dây — BW16 + DHT22 + MQ-135 (Windows host)

## Bảng kết nối

| Cảm biến | Chân cảm biến | Chân BW16 | Mức điện áp | Ghi chú |
|----------|--------------|-----------|-------------|---------|
| DHT22 | VCC | 3.3V | 3.3V | Không cấp 5V |
| DHT22 | GND | GND | 0V | |
| DHT22 | DATA | PA_26 | 3.3V logic | Pull-up 10kΩ (VCC→DATA) |
| MQ-135 | VCC | **5V** | **5V BẮT BUỘC** | Nguồn ngoài hoặc USB hub |
| MQ-135 | GND | GND | 0V | |
| MQ-135 | AOUT | PB_1 | **≤3.3V** | Dùng voltage divider! |

## ⚠️ Cảnh báo điện áp

MQ-135 AOUT = 0–5V. BW16 ADC chỉ chịu ≤3.3V.
**Bắt buộc dùng voltage divider 10kΩ/10kΩ** trước khi nối vào PB_1.

```
AOUT ──[10kΩ]──┬── PB_1 (BW16)
               [10kΩ]
               │
              GND
```

## Cách upload code lên BW16

1. Nhấn Upload trong Arduino IDE → chờ đếm ngược 05→04→03...
2. Giữ nút **BURN** (gần cổng USB)
3. Nhấn + thả **RESET** (trong khi giữ BURN)
4. Thả **BURN**
5. Chờ IDE báo `Upload Image done.`

## Lấy IP Windows để điền mqtt_server

```cmd
ipconfig
```
Tìm dòng `IPv4 Address` của card Wi-Fi, ví dụ: `192.168.1.100`

## Driver CH340 cho Windows

Tải tại: https://www.wch-ic.com/downloads/CH341SER_EXE.html
→ Cài xong → Device Manager → Ports (COM & LPT) → thấy `USB-SERIAL CH340 (COMx)`
