"""
main.py — Entry point cho Phase 4 Data Fusion Gateway
"""
import time
import json
import threading
from influxdb_client import Point
import config
import mqtt_handler
import mavlink_handler
import db_logger

def watchdog_loop():
    print("[WATCHDOG] 👁️  Khởi động Failsafe Watchdog (timeout=15s)")
    while True:
        time.sleep(5)
        elapsed = time.time() - config.last_heartbeat_time
        if elapsed > config.FAILSAFE_TIMEOUT and config.master is not None:
            print(f"[WATCHDOG] ⚠️  Mất kết nối Web {elapsed:.0f}s → Gọi RTL!")
            mavlink_handler.handle_mode("RTL")

def main():
    print("=" * 60)
    print("  Drone IoT — main.py (Refactored)")
    print("=" * 60)

    influx, write_api = db_logger.init_db()
    mqtt_client = mqtt_handler.start_mqtt()

    try:
        config.master = mavlink_handler.connect_sitl()
    except Exception:
        print("[SITL] ⚠️  Không có kết nối SITL. Sẽ thử lại trong background.")

    threading.Thread(target=mavlink_handler.mavlink_loop, daemon=True).start()
    threading.Thread(target=watchdog_loop, daemon=True).start()

    print(f"\n{'=' * 60}")
    print("  🚀 Fusion Loop — Ctrl+C để dừng")
    print(f"{'=' * 60}\n")

    frames_written = 0

    try:
        while True:
            try:
                time.sleep(1.0)

                with config.state_lock:
                    gps = dict(config.gps_data)
                    sensor = dict(config.sensor_data)

                lat = gps.get("lat", 0.0)
                lon = gps.get("lon", 0.0)
                alt = gps.get("alt", 0.0)

                temp = max(0.0, float(sensor.get("temp", 0.0)))
                hum  = max(0.0, float(sensor.get("humidity", 0.0)))
                co2  = max(0, int(sensor.get("co2", 0)))
                alert = sensor.get("alert", 0)
                rssi  = sensor.get("rssi", 0)

                point = (
                    Point("drone_telemetry")
                    .field("latitude",    float(lat))
                    .field("longitude",   float(lon))
                    .field("altitude",    float(alt))
                    .field("temperature", float(temp))
                    .field("humidity",    float(hum))
                    .field("co2",         float(co2))
                    .field("alert",       float(alert))
                )

                dist_to_home = config.get_distance_meters(lat, lon, -35.363261, 149.165230)
                fence_status = 0
                if config.current_fence_enabled == 1:
                    fence_status = 2 if dist_to_home > 50.0 else 1

                telemetry_payload = json.dumps({
                    "mode": config.current_mode,
                    "armed": 1 if config.current_armed else 0,
                    "alt": float(config.current_alt),
                    "spd": float(config.current_spd),
                    "batt": float(config.current_batt),
                    "wind": float(config.current_wind),
                    "fence": int(fence_status)
                })
                
                try:
                    if mqtt_client and mqtt_client.is_connected():
                        # [FIX] Dùng topic riêng để không xung đột với SERVO command
                        mqtt_client.publish(config.TOPIC_TELEM_DOWN, telemetry_payload)
                except Exception as e:
                    print(f"[MQTT] Gửi Downstream thất bại: {e}")
                
                point.field("wifi_rssi",   float(rssi))

                try:
                    write_api.write(bucket=config.INFLUX_BUCKET, org=config.INFLUX_ORG, record=point)
                    frames_written += 1
                    print(f"[FUSION] ✅ #{frames_written:04d} "
                          f"GPS: ({lat:.5f}, {lon:.5f}, {alt:.1f}m) "
                          f"T={temp}°C, H={hum}%, CO2={co2}, Alert={alert}")
                except Exception as db_err:
                    print(f"[INFLUX] ❌ Ghi DB thất bại: {db_err}")

            except KeyboardInterrupt:
                print("\n\n[FUSION] Đã dừng bởi người dùng (Ctrl+C)")
                break
            except Exception as e:
                print(f"[LOOP] Lỗi: {e}")
    finally:
        influx.close()
        print("[CLEANUP] Đã đóng InfluxDB.")
        if mqtt_client:
            mqtt_client.loop_stop()
            mqtt_client.disconnect()
            print("[MQTT] Disconnected.")
        if config.master:
            config.master.close()
            print("[MAVLINK] Disconnected.")

if __name__ == "__main__":
    main()
