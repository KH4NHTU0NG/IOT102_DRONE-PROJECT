#ifndef CONFIG_H
#define CONFIG_H

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <AmebaServo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "secrets.h"

// --- Network Config ---
extern const char* ssid;
extern const char* password;
extern const char* mqtt_server;
extern const char* topic_sensors;
extern const char* topic_payload;
extern const int   mqtt_port;

// --- Pin Definitions ---
#define DHT_PIN         PA30
#define DHT_TYPE        DHT22
#define MQ135_PIN       PB3

#define TRIG_PIN        PB2
#define ECHO_PIN        PB1

#define BUZZER_PIN      PA14
#define LED_PIN         PA15
#define LED_GREEN_PIN   PA27

#define SERVO_PIN       PA13

// --- OLED Config ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

// --- Active Levels ---
#define BUZZER_ACTIVE   LOW
#define LED_ACTIVE      HIGH

#define BUZZER_ON       BUZZER_ACTIVE
#define BUZZER_OFF      (!BUZZER_ACTIVE)
#define LED_ON          LED_ACTIVE
#define LED_OFF         (!LED_ACTIVE)

// --- Thresholds & Constants ---
extern const int   CO2_THRESHOLD;
extern const int   WIFI_MAX_RETRIES;
extern const int   MQTT_MAX_RETRIES;
extern const int   MQTT_RETRY_DELAY;
extern const int   CUSTOM_MQTT_KEEPALIVE;
extern const unsigned long MQ135_WARMUP_MS;
extern const uint8_t OLED_I2C_ADDR;

// --- MQTT Override State ---
extern bool mqtt_buzzer_override;
extern bool mqtt_buzzer_state;
extern bool mqtt_led_override;
extern bool mqtt_led_state;

// --- SITL Flight State ---
extern String flight_mode;
extern bool   drone_armed;
extern float  flight_alt;
extern float  flight_spd;
extern float  flight_batt;
extern float  flight_wind;
extern int    flight_fence;
extern unsigned long lastTelemetryTime;

// --- Objects ---
extern WiFiClient   wifiClient;
extern PubSubClient client;
extern DHT          dht;
extern AmebaServo   payloadServo;
extern Adafruit_SSD1306 display;

// --- Sensor globals ---
extern float temp_val;
extern float hum_val;
extern int   mq_raw_val;
extern float sonar_dist;
extern bool  env_alert;

// --- Timers ---
extern unsigned long lastMsg;
extern unsigned long lastOLEDUpdate;
extern const long    interval;
extern const long    oledInterval;

#endif // CONFIG_H
