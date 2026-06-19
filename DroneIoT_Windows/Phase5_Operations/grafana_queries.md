# Grafana Dashboard Queries — Drone IoT (Windows)

## Cấu hình Data Source

Vào **Grafana → Connections → Data Sources → Add new → InfluxDB**

| Trường | Giá trị |
|--------|---------|
| Query Language | **Flux** (không phải InfluxQL) |
| URL | `http://influxdb:8086` |
| Organization | `drone_org` |
| Token | *(dán token từ setup.bat)* |
| Default Bucket | `drone_data` |

> **⚠️ Quan trọng**: URL phải là `http://influxdb:8086` (tên container Docker), không phải `localhost:8086`

---

## Panel 1: CO2

```flux
from(bucket: "drone_data")
  |> range(start: -10m)
  |> filter(fn: (r) => r._measurement == "drone_telemetry")
  |> filter(fn: (r) => r._field == "co2")
  |> aggregateWindow(every: 5s, fn: mean, createEmpty: false)
```

## Panel 2: Nhiệt độ

```flux
from(bucket: "drone_data")
  |> range(start: -10m)
  |> filter(fn: (r) => r._measurement == "drone_telemetry")
  |> filter(fn: (r) => r._field == "temperature")
  |> aggregateWindow(every: 5s, fn: mean, createEmpty: false)
```

## Panel 3: Độ ẩm

```flux
from(bucket: "drone_data")
  |> range(start: -10m)
  |> filter(fn: (r) => r._measurement == "drone_telemetry")
  |> filter(fn: (r) => r._field == "humidity")
  |> aggregateWindow(every: 5s, fn: mean, createEmpty: false)
```

## Panel 4: GPS Table

```flux
from(bucket: "drone_data")
  |> range(start: -1m)
  |> filter(fn: (r) => r._measurement == "drone_telemetry")
  |> filter(fn: (r) => r._field == "latitude" or r._field == "longitude" or r._field == "altitude")
  |> last()
  |> pivot(rowKey:["_time"], columnKey:["_field"], valueColumn:"_value")
```

## Panel 5: Altitude

```flux
from(bucket: "drone_data")
  |> range(start: -10m)
  |> filter(fn: (r) => r._measurement == "drone_telemetry")
  |> filter(fn: (r) => r._field == "altitude")
```

## Panel 6: Overview (CO2 + Temp + Humidity)

```flux
from(bucket: "drone_data")
  |> range(start: -10m)
  |> filter(fn: (r) => r._measurement == "drone_telemetry")
  |> filter(fn: (r) =>
    r._field == "temperature" or
    r._field == "humidity" or
    r._field == "co2"
  )
  |> aggregateWindow(every: 5s, fn: mean, createEmpty: false)
```

---

## Lỗi thường gặp

| Lỗi | Fix |
|-----|-----|
| `401 Unauthorized` | Token sai — lấy lại từ `setup.bat` |
| `connection refused` | Dùng `http://influxdb:8086` không phải `localhost` |
| Không có data | Kiểm tra fusion.py đang chạy trong cửa sổ riêng |
