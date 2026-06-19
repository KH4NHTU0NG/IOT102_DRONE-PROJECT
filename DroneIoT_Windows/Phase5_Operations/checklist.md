# Checklist Vận hành — Drone IoT Windows

## Thứ tự khởi động

| # | Thành phần | Hành động | Xác nhận |
|---|-----------|-----------|---------|
| 1 | Docker server | `Phase1_Docker\setup.bat` | `docker ps` → 3 container Up |
| 2 | BW16 payload | Cắm nguồn | LED nháy đều = WiFi OK |
| 3 | SITL (WSL2) | PowerShell: `.\Phase2_SITL\run_sitl.ps1` | Terminal hiện `MAV>` |
| 4 | Data Fusion | `Phase5_Operations\start_all.bat` (bước 4) | Cửa sổ mới hiện `Fusion Loop` |
| 5 | QGroundControl | Mở QGC | Hiện `Ready To Fly` |
| 6 | Grafana | http://localhost:3000 | Graph có data |

---

## Kiểm tra từng thành phần (Windows CMD)

### Docker
```cmd
docker ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"
```

### MQTT test
```cmd
REM Terminal 1 - lang nghe
docker exec -it iot_mqtt mosquitto_sub -t "drone/payload/sensors"

REM Terminal 2 - gui test
docker exec -it iot_mqtt mosquitto_pub -t "drone/payload/sensors" -m "{\"temp\":28.5,\"humidity\":65,\"co2\":412}"
```

### Port SITL (WSL2)
```cmd
netstat -an | findstr "5763"
netstat -an | findstr "14550"
```

### Lay token InfluxDB
```cmd
docker exec iot_db influx auth list --user admin --hide-headers
REM Copy cot thu 4 (token)
```

---

## Lỗi thường gặp Windows

| Triệu chứng | Fix |
|-------------|-----|
| SITL không connect qua TCP 5763 | WSL2 bind phải dùng `0.0.0.0` (run_sitl.ps1 đã xử lý) |
| fusion.py lỗi connect SITL | Kiểm tra WSL2 đang chạy: `wsl --status` |
| Docker không start | Bật WSL2 backend trong Docker Desktop Settings |
| PowerShell "execution policy" | Chạy: `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned` |
| BW16 port không hiện | Cài CH340 driver: wch-ic.com |

---

## PowerShell Execution Policy (Nếu gặp lỗi khi chạy .ps1)

```powershell
# Chạy PowerShell as Administrator:
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
# Xác nhận: Y
```

## Dừng hệ thống

```cmd
Phase5_Operations\stop_all.bat
```
