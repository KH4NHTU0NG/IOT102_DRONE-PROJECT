// bw16_sensor.ino — BW16 Drone IoT Payload (Real Flight Deployment - IRL_test)
// Vi điều khiển Realtek Ameba BW16 (RTL8720DN)
// Cảm biến: DHT22 + MQ-135 + OLED SSD1306 + Hardware PWM Servo SG90 + Buzzer + LED

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
// Sử dụng Hardware PWM từ bộ đếm phần cứng Ameba SDK chống ngắt WiFi gây co giật Servo
extern "C" {
#include "pwmout_api.h"
}
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "secrets.h"

// --- Network Config ---
const char* ssid        = SECRET_SSID;
const char* password    = SECRET_PASS;
const char* mqtt_server = "broker.hivemq.com";
const char* topic_sensors  = "iot102_drone/payload/sensors";
const char* topic_payload  = "iot102_drone/control/payload";
const int   mqtt_port   = 1883;

// --- Pin Definitions (Chuản theo sơ đồ Hardware Wiring) ---
#define DHT_PIN         PA30
#define DHT_TYPE        DHT22
#define MQ135_PIN       PB3

#define BUZZER_PIN      PA14
#define LED_PIN         PA15    // LED Đỏ (Cảnh báo DANGER)
#define LED_GREEN_PIN   PA27    // LED Xanh Lá (Trạng thái SAFE)

#define SERVO_PIN       PA13
#define SERVO_PIN_HW    PA_13   // Hardware PWM pin name
#define SERVO_PERIOD_US 20000   // 50Hz (20ms)
#define SERVO_MIN_US    500     // 0 độ (Khóa chốt)
#define SERVO_MAX_US    2500    // 180 độ (Mở chốt)

// --- OLED SSD1306 Config ---
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Active Levels ---
#define BUZZER_ACTIVE   LOW
#define LED_ACTIVE      HIGH

#define BUZZER_ON       BUZZER_ACTIVE
#define BUZZER_OFF      (!BUZZER_ACTIVE)
#define LED_ON          LED_ACTIVE
#define LED_OFF         (!LED_ACTIVE)

// --- Thresholds & Constants ---
const int   CO2_THRESHOLD      = 600;
const float TEMP_THRESHOLD     = 40.0;
const unsigned long SENSOR_INTERVAL   = 200;
const unsigned long MQTT_INTERVAL     = 1000;
const unsigned long RECONNECT_DELAY   = 5000;
const unsigned long MQ135_WARMUP_MS   = 120000;

// --- Globals ---
WiFiClient   wifiClient;
PubSubClient client(wifiClient);
DHT          dht(DHT_PIN, DHT_TYPE);
pwmout_t     servo_pwm;

unsigned long lastSensorRead = 0;
unsigned long lastMqttPub    = 0;
unsigned long lastReconnect  = 0;

int servo_current_angle = 0;
bool mqtt_buzzer_override = false;
bool mqtt_buzzer_state    = false;
bool mqtt_led_override    = false;
bool mqtt_led_state       = false;

// Sensor data globals
float temp_val   = 0.0;
float hum_val    = 0.0;
int   mq_raw_val = 0;
bool  env_alert  = false;
bool  dht_ok     = false;

// Cache RSSI (chỉ đọc 5s/lần tránh block xung nhịp PWM Servo)
int           cached_rssi    = 0;
unsigned long lastRssiUpdate = 0;
const long    rssiInterval   = 5000;

// ============================================================
//  Hardware PWM Servo Helper
// ============================================================
void setServoAngle(int angle) {
    angle = constrain(angle, 0, 180);
    int pulse_us = map(angle, 0, 180, SERVO_MIN_US, SERVO_MAX_US);
    pwmout_pulsewidth_us(&servo_pwm, pulse_us);
    servo_current_angle = angle;
    Serial.print("[SERVO] Angle set to: ");
    Serial.print(angle);
    Serial.print("° (");
    Serial.print(pulse_us);
    Serial.println(" us)");
}

// ============================================================
//  OLED Display Helper (Giao diện chuẩn thực địa siêu sạch)
// ============================================================
void updateOLED() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Header: Trạng thái & Sóng WiFi
    display.setCursor(0, 0);
    display.print("PAYLOAD ");
    if (WiFi.status() == WL_CONNECTED) {
        display.print("OK ");
        display.print(cached_rssi);
        display.print("dBm");
    } else {
        display.print("OFFLINE");
    }

    // Dòng kẻ ngang phân cách
    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    // Dòng 1 (y=14): Nhiệt độ & Độ ẩm
    display.setCursor(0, 14);
    display.print("Temp: ");
    if (dht_ok) display.print(temp_val, 1);
    else display.print("--");
    display.print("C  H:");
    if (dht_ok) display.print(hum_val, 0);
    else display.print("--");
    display.print("%");

    // Dòng 2 (y=28): Khí Gas / CO2
    display.setCursor(0, 28);
    display.print("Gas : ");
    display.print(mq_raw_val);
    display.print(" PPM ");
    if (env_alert) {
        display.print("[ALERT]");
    } else {
        display.print("[SAFE]");
    }

    // Dòng 3 (y=42): Trạng thái chốt Servo
    display.setCursor(0, 42);
    display.print("Servo: ");
    display.print(servo_current_angle);
    display.print(" deg ");
    if (servo_current_angle > 45) display.print("[OPEN]");
    else display.print("[LOCK]");

    // Dòng 4 (y=55): Địa chỉ IP mạch
    display.setCursor(0, 55);
    display.print("IP: ");
    display.print(WiFi.localIP());

    display.display();
}

// ============================================================
//  Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=================================");
    Serial.println("  BW16 IRL Flight IoT Payload");
    Serial.println("=================================");

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, BUZZER_OFF);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_OFF);

    pinMode(LED_GREEN_PIN, OUTPUT);
    digitalWrite(LED_GREEN_PIN, LED_ON); // Bật sáng LED xanh ngay khi khởi động

    pinMode(MQ135_PIN, INPUT);

    // Khởi tạo OLED I2C
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("[ERROR] OLED SSD1306 init failed!");
    } else {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 20);
        display.println("BW16 Payload Booting");
        display.setCursor(0, 35);
        display.println("Initializing hardware");
        display.display();
    }

    // Khởi tạo Hardware PWM Servo
    pwmout_init(&servo_pwm, SERVO_PIN_HW);
    pwmout_period_us(&servo_pwm, SERVO_PERIOD_US);
    setServoAngle(0); // Khóa chốt ban đầu ở 0 độ
    Serial.println("[INIT] HW-PWM Servo OK");

    dht.begin();
    delay(500);

    connectWiFi();

    wifiClient.setBlockingMode();
    wifiClient.setSSLTimeout(30);

    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(mqttCallback);
    client.setBufferSize(256);
    client.setKeepAlive(15);
}

// ============================================================
//  Main Loop (Non-blocking millis)
// ============================================================
void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }

    if (!client.connected()) {
        unsigned long now = millis();
        if (now - lastReconnect > RECONNECT_DELAY) {
            lastReconnect = now;
            reconnectMQTT();
        }
    } else {
        client.loop();
    }

    unsigned long now = millis();

    // Cập nhật sóng WiFi RSSI mỗi 5s
    if (now - lastRssiUpdate > rssiInterval) {
        lastRssiUpdate = now;
        if (WiFi.status() == WL_CONNECTED) {
            cached_rssi = WiFi.RSSI();
        }
    }

    // Đọc cảm biến mỗi 200ms
    if (now - lastSensorRead >= SENSOR_INTERVAL) {
        lastSensorRead = now;

        float t = dht.readTemperature();
        float h = dht.readHumidity();
        if (!isnan(t) && !isnan(h)) {
            temp_val = t;
            hum_val  = h;
            dht_ok   = true;
        } else {
            dht_ok   = false;
        }

        mq_raw_val = analogRead(MQ135_PIN);

        // Kiểm tra ngưỡng báo động
        env_alert = (temp_val >= TEMP_THRESHOLD);
        if (now > MQ135_WARMUP_MS) {
            env_alert = env_alert || (mq_raw_val > CO2_THRESHOLD);
        }

        // Điều khiển Còi & LED Đỏ/Xanh theo trạng thái
        if (mqtt_buzzer_override) {
            digitalWrite(BUZZER_PIN, mqtt_buzzer_state ? BUZZER_ON : BUZZER_OFF);
        } else {
            digitalWrite(BUZZER_PIN, env_alert ? BUZZER_ON : BUZZER_OFF);
        }

        if (mqtt_led_override) {
            digitalWrite(LED_PIN,       mqtt_led_state ? LED_ON : LED_OFF);
            digitalWrite(LED_GREEN_PIN, mqtt_led_state ? LED_OFF : LED_ON);
        } else {
            digitalWrite(LED_PIN,       env_alert ? LED_ON : LED_OFF);
            digitalWrite(LED_GREEN_PIN, env_alert ? LED_OFF : LED_ON);
        }

        updateOLED();
    }

    // Publish JSON MQTT lên Cloud mỗi 1000ms
    if (now - lastMqttPub >= MQTT_INTERVAL) {
        lastMqttPub = now;

        char jsonBuf[256];
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"temp\":%.1f,\"humidity\":%.1f,\"co2\":%d,\"servo\":%d,\"alert\":%d,\"rssi\":%d,\"dht_ok\":%d}",
            temp_val, hum_val, mq_raw_val, servo_current_angle, env_alert ? 1 : 0, cached_rssi, dht_ok ? 1 : 0);

        if (client.connected() && client.publish(topic_sensors, jsonBuf)) {
            Serial.print("[SEND] ");
            Serial.println(jsonBuf);
        } else {
            Serial.println("[SEND] MQTT publish failed.");
        }
    }
}

// ============================================================
//  WiFi Connect
// ============================================================
void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.print("[WIFI] Connecting to ");
    Serial.print(ssid);

    WiFi.begin(ssid, password);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(500);
        Serial.print(".");
        digitalWrite(LED_GREEN_PIN, !digitalRead(LED_GREEN_PIN)); // Nháy đèn xanh khi đang tìm WiFi
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(" Connected!");
        Serial.print("[WIFI] IP Address: ");
        Serial.println(WiFi.localIP());
        cached_rssi = WiFi.RSSI();
        digitalWrite(LED_GREEN_PIN, LED_ON); // Sáng cố định khi đã kết nối OK
    } else {
        Serial.println(" Failed!");
        digitalWrite(LED_GREEN_PIN, LED_OFF);
    }
}

// ============================================================
//  MQTT Reconnect & Callback
// ============================================================
void reconnectMQTT() {
    Serial.print("[MQTT] Connecting to broker... ");
    String clientId = "BW16_Payload_" + String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
        Serial.println("Connected!");
        client.subscribe(topic_payload);
        Serial.print("[MQTT] Subscribed to: ");
        Serial.println(topic_payload);
    } else {
        Serial.print("failed, rc=");
        Serial.println(client.state());
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    char msg[128];
    unsigned int len = length < sizeof(msg) - 1 ? length : sizeof(msg) - 1;
    memcpy(msg, payload, len);
    msg[len] = '\0';

    Serial.print("[RECV] Topic: ");
    Serial.print(topic);
    Serial.print(" | Payload: ");
    Serial.println(msg);

    // Parse JSON đơn giản
    String s = String(msg);

    if (s.indexOf("\"SERVO\"") >= 0 || s.indexOf("\"angle\"") >= 0) {
        int idx = s.indexOf("\"angle\":");
        if (idx >= 0) {
            int val = s.substring(idx + 8).toInt();
            setServoAngle(val);
        }
    }
    else if (s.indexOf("\"BUZZER_ON\"") >= 0) {
        mqtt_buzzer_override = true;
        mqtt_buzzer_state    = true;
    }
    else if (s.indexOf("\"BUZZER_OFF\"") >= 0) {
        mqtt_buzzer_override = true;
        mqtt_buzzer_state    = false;
    }
    else if (s.indexOf("\"LED_ON\"") >= 0) {
        mqtt_led_override = true;
        mqtt_led_state    = true;
    }
    else if (s.indexOf("\"LED_OFF\"") >= 0) {
        mqtt_led_override = true;
        mqtt_led_state    = false;
    }
    else if (s.indexOf("\"RESET_OVR\"") >= 0) {
        mqtt_buzzer_override = false;
        mqtt_led_override    = false;
    }
}
