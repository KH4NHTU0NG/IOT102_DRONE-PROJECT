"""
config.py — Cấu hình hệ thống chung cho Phase 4
"""
import os
import threading

# --- InfluxDB Config ---
INFLUX_URL    = "http://localhost:8086"
INFLUX_TOKEN  = "YOUR_INFLUXDB_TOKEN_HERE"
INFLUX_ORG    = "drone_org"
INFLUX_BUCKET = "drone_data"

# --- MQTT Config ---
MQTT_BROKER = "broker.hivemq.com"
MQTT_PORT   = 1883

TOPIC_SENSORS     = "iot102_drone/payload/sensors"
TOPIC_FLIGHT_CMD  = "iot102_drone/control/flight"
TOPIC_PAYLOAD_CMD = "iot102_drone/control/payload"
TOPIC_MOTOR_DATA  = "iot102_drone/telemetry/motors"
TOPIC_WEATHER_CMD = "iot102_drone/control/weather"
TOPIC_HEARTBEAT   = "iot102_drone/control/heartbeat"
TOPIC_MISSION_CMD = "iot102_drone/control/mission"
TOPIC_SIM_CMD     = "iot102_drone/control/sim_param"
TOPIC_ATTITUDE    = "iot102_drone/telemetry/attitude"
TOPIC_GPS         = "iot102_drone/telemetry/gps"
TOPIC_STATUS      = "iot102_drone/telemetry/status"
# [FIX] Topic siêu ngắn để tránh MQTT buffer 128 bytes overflow
TOPIC_TELEM_DOWN  = "iot102/dn"

# --- SITL Config ---
SITL_HOST = "127.0.0.1"
SITL_PORT = 5763

FAILSAFE_TIMEOUT = 15  # Giây không có heartbeat → RTL

# --- Shared State ---
gps_data   = {}
sensor_data = {}
motor_data  = {"m1": 1000, "m2": 1000, "m3": 1000, "m4": 1000}

state_lock  = threading.Lock()
master_lock = threading.Lock()

last_heartbeat_time = 0.0

master  = None
mqtt_pub = None

current_mode = "DISCONN"
current_armed = False
current_alt = 0.0
current_spd = 0.0
current_batt = 12.6
current_wind = 0.0
current_fence_enabled = 0
is_uploading_mission = False

def get_distance_meters(lat1, lon1, lat2, lon2):
    import math
    R = 6371000.0
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = math.sin(dlat/2)**2 + math.cos(math.radians(lat1)) * math.cos(math.radians(lat2)) * math.sin(dlon/2)**2
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1-a))
    return R * c
