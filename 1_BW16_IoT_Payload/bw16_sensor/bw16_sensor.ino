// =========================================================================
// bw16_sensor.ino — BW16 Drone IoT Payload (Pure IRL Real Flight Mode)
// Vi điều khiển: Realtek AmebaD BW16 (RTL8720DN)
// Cảm biến & Cơ cấu: DHT22 + MQ-135 + OLED SSD1306 + HW-PWM Servo + Buzzer + LED
// =========================================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
extern "C" { #include "pwmout_api.h" } // Hardware PWM chống ngắt WiFi gây giật Servo
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "secrets.h"

// --- MQTT & Network Config ---
const char* ssid        = SECRET_SSID;
const char* password    = SECRET_PASS;
const char* mqtt_server = "broker.hivemq.com";
const char* topic_pub   = "iot102_drone/payload/sensors";
const char* topic_sub   = "iot102_drone/control/payload";

// --- Pin Definitions ---
#define DHT_PIN         PA30
#define MQ135_PIN       PB3
#define BUZZER_PIN      PA14
#define LED_RED_PIN     PA15
#define LED_GREEN_PIN   PA27
#define SERVO_PIN_HW    PA_13

// --- Cấu hình OLED SSD1306 (128x64 I2C) ---
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// --- Ngưỡng Cảnh báo & Chu kỳ ---
const int   CO2_THRESHOLD     = 600;   // Ngưỡng Gas ADC
const float TEMP_THRESHOLD    = 40.0;  // Ngưỡng Nhiệt độ (°C)
const unsigned long INTERVAL_READ = 200;   // Đọc cảm biến mỗi 200ms
const unsigned long INTERVAL_PUB  = 1000;  // Gửi MQTT mỗi 1000ms

// --- Biến toàn cục ---
WiFiClient   wifiClient;
PubSubClient client(wifiClient);
DHT          dht(DHT_PIN, DHT22);
pwmout_t     servo_pwm;

unsigned long lastRead = 0, lastPub = 0, lastRssi = 0;
float temp_val = 0.0, hum_val = 0.0;
int   mq_val = 0, rssi_val = 0, servo_angle = 0;
bool  env_alert = false, dht_ok = false;
bool  ovr_buzzer = false, ovr_led = false, state_buzzer = false, state_led = false;

// =========================================================================
//  1. Điều khiển góc Servo (Hardware PWM)
// =========================================================================
void setServo(int angle) {
    servo_angle = constrain(angle, 0, 180);
    int pulse = map(servo_angle, 0, 180, 500, 2500); // 0°->500us, 180°->2500us
    pwmout_pulsewidth_us(&servo_pwm, pulse);
    Serial.printf("[SERVO] Xoay góc: %d° (%d us)\n", servo_angle, pulse);
}

// =========================================================================
//  2. Hiển thị Màn hình OLED Thực địa (Chuẩn Gọn Sạch)
// =========================================================================
void updateOLED() {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.printf("PAYLOAD %s %ddBm\n", (WiFi.status() == WL_CONNECTED) ? "OK" : "ERR", rssi_val);
    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    display.setCursor(0, 14);
    if (dht_ok) display.printf("Temp: %.1fC  H:%d%%\n", temp_val, (int)hum_val);
    else        display.print("Temp: --.-C  H:--%\n");

    display.setCursor(0, 28);
    display.printf("Gas : %d PPM [%s]\n", mq_val, env_alert ? "ALERT" : "SAFE ");

    display.setCursor(0, 42);
    display.printf("Servo: %d deg [%s]\n", servo_angle, (servo_angle > 45) ? "OPEN" : "LOCK");

    display.setCursor(0, 55);
    display.print("IP: "); display.print(WiFi.localIP());
    display.display();
}

// =========================================================================
//  3. Kết nối WiFi & MQTT
// =========================================================================
void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;
    Serial.printf("[WIFI] Kết nối %s...", ssid);
    WiFi.begin(ssid, password);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
        delay(300); Serial.print(".");
        digitalWrite(LED_GREEN_PIN, !digitalRead(LED_GREEN_PIN));
    }
    if (WiFi.status() == WL_CONNECTED) {
        rssi_val = WiFi.RSSI();
        digitalWrite(LED_GREEN_PIN, HIGH); // Sáng cố định khi OK
        Serial.printf(" OK! IP: %s\n", WiFi.localIP().toString().c_str());
    } else { digitalRead(LED_GREEN_PIN); }
}

void reconnectMQTT() {
    if (client.connect(("BW16_" + String(random(0xffff), HEX)).c_str())) {
        client.subscribe(topic_sub);
        Serial.println("[MQTT] Connected & Subscribed OK!");
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int len) {
    char msg[128];
    unsigned int l = min(len, (unsigned int)sizeof(msg) - 1);
    memcpy(msg, payload, l); msg[l] = '\0';
    Serial.printf("[RECV] %s\n", msg);

    String s = String(msg);
    if (s.indexOf("\"SERVO\"") >= 0 || s.indexOf("\"angle\"") >= 0) {
        int idx = s.indexOf("\"angle\":");
        if (idx >= 0) setServo(s.substring(idx + 8).toInt());
    }
    else if (s.indexOf("\"BUZZER_ON\"") >= 0)  { ovr_buzzer = true; state_buzzer = true; }
    else if (s.indexOf("\"BUZZER_OFF\"") >= 0) { ovr_buzzer = true; state_buzzer = false; }
    else if (s.indexOf("\"LED_ON\"") >= 0)     { ovr_led = true; state_led = true; }
    else if (s.indexOf("\"LED_OFF\"") >= 0)    { ovr_led = true; state_led = false; }
    else if (s.indexOf("\"RESET_OVR\"") >= 0)  { ovr_buzzer = false; ovr_led = false; }
}

// =========================================================================
//  4. Setup
// =========================================================================
void setup() {
    Serial.begin(115200); delay(500);
    pinMode(BUZZER_PIN, OUTPUT);    digitalWrite(BUZZER_PIN, HIGH);
    pinMode(LED_RED_PIN, OUTPUT);   digitalWrite(LED_RED_PIN, LOW);
    pinMode(LED_GREEN_PIN, OUTPUT); digitalWrite(LED_GREEN_PIN, HIGH); // Sáng ngay khi bật mạch
    pinMode(MQ135_PIN, INPUT);

    if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 20); display.println("BW16 IRL Payload Boot"); display.display();
    }

    // Khởi tạo HW PWM Servo (Chu kỳ 20ms - 50Hz)
    pwmout_init(&servo_pwm, SERVO_PIN_HW);
    pwmout_period_us(&servo_pwm, 20000);
    setServo(0); // Khóa chốt ở góc 0 độ

    dht.begin();
    connectWiFi();

    client.setServer(mqtt_server, 1883);
    client.setCallback(mqttCallback);
}

// =========================================================================
//  5. Main Loop
// =========================================================================
void loop() {
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
    if (!client.connected()) reconnectMQTT();
    else client.loop();

    unsigned long now = millis();

    // Cập nhật sóng WiFi mỗi 5 giây (tránh block CPU)
    if (now - lastRssi > 5000) {
        lastRssi = now;
        if (WiFi.status() == WL_CONNECTED) rssi_val = WiFi.RSSI();
    }

    // Đọc cảm biến & kiểm tra cảnh báo mỗi 200ms
    if (now - lastRead >= INTERVAL_READ) {
        lastRead = now;
        float t = dht.readTemperature();
        float h = dht.readHumidity();
        if (!isnan(t) && !isnan(h)) { temp_val = t; hum_val = h; dht_ok = true; }
        else { dht_ok = false; }

        mq_val = analogRead(MQ135_PIN);
        env_alert = (temp_val >= TEMP_THRESHOLD || (now > 120000 && mq_val > CO2_THRESHOLD));

        // Xuất tín hiệu Buzzer & LED
        digitalWrite(BUZZER_PIN,    (ovr_buzzer ? state_buzzer : env_alert) ? LOW : HIGH);
        digitalWrite(LED_RED_PIN,   (ovr_led ? state_led : env_alert) ? HIGH : LOW);
        digitalWrite(LED_GREEN_PIN, (ovr_led ? !state_led : !env_alert) ? HIGH : LOW);

        updateOLED();
    }

    // Gửi bản tin JSON lên MQTT Cloud mỗi 1000ms
    if (now - lastPub >= INTERVAL_PUB) {
        lastPub = now;
        char json[200];
        snprintf(json, sizeof(json),
            "{\"temp\":%.1f,\"humidity\":%.1f,\"co2\":%d,\"servo\":%d,\"alert\":%d,\"rssi\":%d,\"dht_ok\":%d}",
            temp_val, hum_val, mq_val, servo_angle, env_alert ? 1 : 0, rssi_val, dht_ok ? 1 : 0);
        if (client.connected() && client.publish(topic_pub, json)) {
            Serial.printf("[SEND] %s\n", json);
        }
    }
}
