#include "config.h"

const char* ssid        = SECRET_SSID;
const char* password    = SECRET_PASS;
const char* mqtt_server = "broker.hivemq.com";
const char* topic_sensors = "iot102_drone/payload/sensors";
const char* topic_payload = "iot102_drone/control/payload";
const int   mqtt_port   = 1883;

const int   CO2_THRESHOLD      = 600;
const int   WIFI_MAX_RETRIES   = 30;
const int   MQTT_MAX_RETRIES   = 5;
const int   MQTT_RETRY_DELAY   = 3000;
const int   CUSTOM_MQTT_KEEPALIVE = 60;
const unsigned long MQ135_WARMUP_MS  = 30000;
const uint8_t OLED_I2C_ADDR    = 0x3C;

bool mqtt_buzzer_override = false;
bool mqtt_buzzer_state    = false;
bool mqtt_led_override    = false;
bool mqtt_led_state       = false;

String flight_mode = "DISCONN";
bool   drone_armed = false;
float  flight_alt  = 0.0;
float  flight_spd  = 0.0;
float  flight_batt = 12.6;
float  flight_wind = 0.0;
int    flight_fence = 0;
unsigned long lastTelemetryTime = 0;

WiFiClient   wifiClient;
PubSubClient client(wifiClient);
DHT          dht(DHT_PIN, DHT_TYPE);
AmebaServo   payloadServo;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

float temp_val = 0.0;
float hum_val  = 0.0;
int   mq_raw_val = 0;
float sonar_dist = 0.0;
bool  env_alert = false;

unsigned long lastMsg = 0;
unsigned long lastOLEDUpdate = 0;
const long    interval = 2000;
const long    oledInterval = 200;
