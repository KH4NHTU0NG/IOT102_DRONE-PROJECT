# Grafana Dashboard Queries — Drone IoT

## Cấu hình Data Source

Vào **Grafana → Connections → Data Sources → Add new → InfluxDB**

| Trường | Giá trị |
|--------|---------|
| Query Language | **Flux** (không phải InfluxQL) |
| URL | `http://influxdb:8086` |
| Organization | `drone_org` |
| Token | *(dán token từ setup script)* |
| Default Bucket | `drone_data` |

Nhấn **Save & Test** → phải hiện: `datasource is working`

> **⚠️ Quan trọng**: URL phải là `http://influxdb:8086` (tên container), không phải `localhost:8086`

---

## Panel 1: CO2 theo thời gian

**Visualization**: Time series

```flux
from(bucket: "drone_data")
  |> range(start: -10m)
  |> filter(fn: (r) => r._measurement == "drone_telemetry")
  |> filter(fn: (r) => r._field == "co2")
  |> aggregateWindow(every: 5s, fn: mean, createEmpty: false)
```

---

## Panel 2: Nhiệt độ theo thời gian

**Visualization**: Time series (hoặc Gauge)

```flux
from(bucket: "drone_data")
  |> range(start: -10m)
  |> filter(fn: (r) => r._measurement == "drone_telemetry")
  |> filter(fn: (r) => r._field == "temperature")
  |> aggregateWindow(every: 5s, fn: mean, createEmpty: false)
```

---

## Panel 3: Độ ẩm theo thời gian

**Visualization**: Time series

```flux
from(bucket: "drone_data")
  |> range(start: -10m)
  |> filter(fn: (r) => r._measurement == "drone_telemetry")
  |> filter(fn: (r) => r._field == "humidity")
  |> aggregateWindow(every: 5s, fn: mean, createEmpty: false)
```

---

## Panel 4: GPS Position (Table)

**Visualization**: Table

```flux
from(bucket: "drone_data")
  |> range(start: -1m)
  |> filter(fn: (r) => r._measurement == "drone_telemetry")
  |> filter(fn: (r) => r._field == "latitude" or r._field == "longitude" or r._field == "altitude")
  |> last()
  |> pivot(rowKey:["_time"], columnKey:["_field"], valueColumn:"_value")
```

---

## Panel 5: Altitude theo thời gian

**Visualization**: Time series

```flux
from(bucket: "drone_data")
  |> range(start: -10m)
  |> filter(fn: (r) => r._measurement == "drone_telemetry")
  |> filter(fn: (r) => r._field == "altitude")
```

---

## Panel 6: Overview (tất cả fields cùng lúc)

**Visualization**: Time series (multi-series)

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

## Thiết lập Auto-Refresh

Góc trên phải Grafana → **⚙ Dashboard Settings** → **Time range** → **Auto refresh**: `5s`

Hoặc click biểu tượng ⟳ cạnh thanh thời gian → chọn `5s`.

---

## Lỗi thường gặp với Grafana

| Lỗi | Fix |
|-----|-----|
| `datasource is working` nhưng không có data | Kiểm tra fusion.py đang chạy và ghi data |
| `Error: 401 Unauthorized` | Token sai — lấy lại token từ `setup.sh` |
| `connection refused` | URL sai — dùng `http://influxdb:8086` (không phải localhost) |
| Query trả về rỗng | `range(start: -10m)` — đảm bảo có data trong 10 phút qua |
