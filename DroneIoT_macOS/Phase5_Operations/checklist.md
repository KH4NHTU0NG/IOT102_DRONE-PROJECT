# Checklist Vận hành — Drone IoT macOS

Tuân theo đúng thứ tự này mỗi lần khởi động hệ thống.

---

## ✅ Thứ tự khởi động

| # | Thành phần | Lệnh / Hành động | Xác nhận |
|---|-----------|-------------------|---------|
| 1 | Docker server | `cd Phase1_Docker && docker-compose up -d` | `docker ps` → thấy 3 container Up |
| 2 | BW16 payload | Cắm nguồn board | LED nháy đều = WiFi OK |
| 3 | SITL drone ảo | `bash Phase2_SITL/run_sitl.sh` | Terminal hiện `MAV>` |
| 4 | Data Fusion | `source venv && python3 fusion.py` | Hiện `Bat dau Fusion Loop` |
| 5 | QGroundControl | Mở QGC app | Hiện `Ready To Fly` |
| 6 | Grafana | http://localhost:3000 | Graph có data realtime |

---

## Checklist kiểm tra từng thành phần

### 1. Docker (3 containers)
```bash
docker ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"
# Phải thấy: iot_mqtt, iot_db, iot_grafana — đều Up
```

### 2. MQTT broker
```bash
# Terminal 1 — lắng nghe
docker exec -it iot_mqtt mosquitto_sub -t "drone/payload/sensors"

# Terminal 2 — gửi test
docker exec -it iot_mqtt mosquitto_pub -t "drone/payload/sensors" \
  -m '{"temp":28.5,"humidity":65,"co2":412}'
# Terminal 1 phải in ra message trên
```

### 3. SITL đang chạy
```bash
lsof -i :5760   # Phải thấy process đang giữ port
lsof -i :14550  # Phải thấy process đang giữ port
```

### 4. BW16 gửi data
- Mở Arduino IDE → Tools → Serial Monitor (115200 baud)
- Phải thấy: `[SEND] {"temp":xx.x,"humidity":xx.x,"co2":xxxx}`

### 5. fusion.py ghi data
- Log phải hiện: `[FUSION] #0001  GPS: (-35.xxxx, 149.xxxx, xxx.xm)`

### 6. InfluxDB có data
```bash
docker exec iot_db influx query \
  --org drone_org \
  --token $(cat Phase4_Fusion/.influx_token) \
  'from(bucket:"drone_data") |> range(start:-1m) |> limit(n:3)'
```

### 7. Grafana Dashboard
- http://localhost:3000 → admin / admin (đổi mật khẩu lần đầu)
- Graphs phải có data chạy realtime

---

## Lỗi thường gặp & cách fix nhanh

| Triệu chứng | Nguyên nhân | Fix |
|-------------|-------------|-----|
| fusion.py báo `INFLUX_TOKEN chua set` | Token chưa điền | Chạy `Phase1_Docker/setup.sh` lấy token |
| fusion.py báo `SITL connection refused` | SITL chưa chạy | Chạy `Phase2_SITL/run_sitl.sh` trước |
| fusion.py hiện `Cho cam bien` mãi | BW16 không gửi data | Kiểm tra Serial Monitor + IP mqtt_server |
| QGC không thấy drone | SITL chưa sẵn sàng | Chờ `AP: ArduPilot Ready` trong SITL terminal |
| Grafana `connection refused` | URL data source sai | Dùng `http://influxdb:8086` (không phải localhost) |
| Port 5760 bị chiếm | Zombie process | `lsof -ti :5760 \| xargs kill -9` |
| BW16 không upload được | Sai thời điểm BURN+RESET | Làm lại trong đúng 5s đếm ngược của IDE |

---

## Dừng hệ thống an toàn

```bash
bash Phase5_Operations/stop_all.sh
```

Hoặc thủ công:
```bash
# 1. Ctrl+C trong terminal fusion.py
# 2. Ctrl+C trong terminal SITL
# 3. docker-compose -f Phase1_Docker/docker-compose.yml down
```
