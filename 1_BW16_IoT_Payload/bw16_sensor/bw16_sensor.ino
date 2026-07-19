// =========================================================================
// bw16_sensor.ino — BW16 Drone IoT Payload (IRL Real Flight Mode)
// Vi điều khiển: Realtek AmebaD BW16 (RTL8720DN)
// Cảm biến: DHT22 + MQ-135 + OLED SSD1306 + HW-PWM Servo + Buzzer + LED
// =========================================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
// Hardware PWM API — chống ngắt WiFi gây co giật Servo
extern "C" {
#include "pwmout_api.h"
}
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "secrets.h"

// --- MQTT & Network Config ---
char ssid[]             = SECRET_SSID;
char pass[]             = SECRET_PASS;
const char* mqtt_server = "broker.hivemq.com";
const char* topic_pub   = "iot102_drone/payload/sensors";
const char* topic_sub   = "iot102_drone/control/payload";

// --- Pin Definitions ---
#define DHT_PIN         PA26
#define MQ135_PIN       PB1
#define BUZZER_PIN      PA15
#define LED_RED_PIN     PA30
#define LED_GREEN_PIN   PA27
#define SERVO_PIN_HW    PA_13

// --- OLED SSD1306 (128x64 I2C) ---
#define OLED_I2C_ADDR   0x3C
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// --- Ngưỡng Cảnh báo & Chu kỳ ---
#define CO2_THRESHOLD    600
#define TEMP_THRESHOLD   40.0
#define INTERVAL_DHT     2000
#define INTERVAL_READ    200
#define INTERVAL_PUB     1000
#define RSSI_INTERVAL    5000
#define MQ135_WARMUP_MS  10000  // Giảm xuống 10s để test ngay

// --- Biến toàn cục ---
WiFiClient   wifiClient;
PubSubClient client(wifiClient);
DHT          dht(DHT_PIN, DHT22);
pwmout_t     servo_pwm;

unsigned long lastRead = 0, lastPub = 0, lastRssi = 0, lastDht = 0;
float temp_val = 0.0, hum_val = 0.0;
int   mq_val = 0, rssi_val = 0, servo_angle = 0;
bool  env_alert = false, dht_ok = false;
bool  ovr_buzzer = false, ovr_led = false;
bool  state_buzzer = false, state_led = false;

// =========================================================================
//  1. Điều khiển góc Servo (Hardware PWM)
// =========================================================================
void setServo(int angle) {
    servo_angle = constrain(angle, 0, 180);
    int pulse = map(servo_angle, 0, 180, 500, 2500);
    pwmout_pulsewidth_us(&servo_pwm, pulse);
    Serial.print("[SERVO] Angle: ");
    Serial.print(servo_angle);
    Serial.print("deg (");
    Serial.print(pulse);
    Serial.println("us)");
}

// =========================================================================
//  2. Hiển thị OLED Thực địa (Chuẩn Gọn Sạch — dùng print/println)
// =========================================================================
void updateOLED() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Dòng Header: Trạng thái & Sóng WiFi
    display.setCursor(0, 0);
    display.print("PAYLOAD ");
    if (WiFi.status() == WL_CONNECTED) {
        display.print("OK ");
        display.print(rssi_val);
        display.print("dBm");
    } else {
        display.print("OFFLINE");
    }
    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    // Dòng 1 (y=14): Nhiệt độ & Độ ẩm
    display.setCursor(0, 14);
    display.print("Temp:");
    if (dht_ok) {
        display.print(temp_val, 1);
        display.print("C H:");
        display.print(hum_val, 0);
        display.print("%");
    } else {
        display.print("--.-C H:--%");
    }

    // Dòng 2 (y=28): Khí Gas / CO2
    display.setCursor(0, 28);
    display.print("Gas: ");
    display.print(mq_val);
    display.print(" ");
    if (env_alert) {
        display.print("[ALERT!]");
    } else {
        display.print("[SAFE]");
    }

    // Dòng 3 (y=42): Trạng thái chốt Servo
    display.setCursor(0, 42);
    display.print("Servo:");
    display.print(servo_angle);
    display.print("deg ");
    if (servo_angle > 45) {
        display.print("[OPEN]");
    } else {
        display.print("[LOCK]");
    }

    // Dòng 4 (y=55): Địa chỉ IP
    display.setCursor(0, 55);
    display.print("IP:");
    display.print(WiFi.localIP());

    display.display();
}

// =========================================================================
//  3. Kết nối WiFi & MQTT
// =========================================================================
void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;
    Serial.print("[WIFI] Connecting to ");
    Serial.print(ssid);
    WiFi.begin(ssid, pass);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
        delay(300);
        Serial.print(".");
        digitalWrite(LED_GREEN_PIN, !digitalRead(LED_GREEN_PIN));
    }
    if (WiFi.status() == WL_CONNECTED) {
        rssi_val = WiFi.RSSI();
        digitalWrite(LED_GREEN_PIN, HIGH);
        Serial.println(" Connected!");
        Serial.print("[WIFI] IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println(" Failed!");
        digitalWrite(LED_GREEN_PIN, LOW);
    }
}

void reconnectMQTT() {
    String clientId = "BW16_" + String(random(0xffff), HEX);
    Serial.print("[MQTT] Connecting... ");
    if (client.connect(clientId.c_str())) {
        client.subscribe(topic_sub);
        Serial.println("OK! Subscribed.");
    } else {
        Serial.print("Failed rc=");
        Serial.println(client.state());
    }
}

void mqttCallback(char* topic, byte* payload, unsigned int len) {
    char msg[128];
    unsigned int l = (len < sizeof(msg) - 1) ? len : sizeof(msg) - 1;
    memcpy(msg, payload, l);
    msg[l] = '\0';
    Serial.print("[RECV] ");
    Serial.println(msg);

    String s = String(msg);
    if (s.indexOf("\"angle\"") >= 0) {
        int idx = s.indexOf("\"angle\":");
        if (idx >= 0) setServo(s.substring(idx + 8).toInt());
    }
    else if (s.indexOf("\"BUZZER_ON\"") >= 0)  { ovr_buzzer = true;  state_buzzer = true;  }
    else if (s.indexOf("\"BUZZER_OFF\"") >= 0) { ovr_buzzer = true;  state_buzzer = false; }
    else if (s.indexOf("\"LED_ON\"") >= 0)     { ovr_led = true;     state_led = true;     }
    else if (s.indexOf("\"LED_OFF\"") >= 0)    { ovr_led = true;     state_led = false;    }
    else if (s.indexOf("\"RESET_OVR\"") >= 0)  { ovr_buzzer = false; ovr_led = false;      }
}

// =========================================================================
//  4. Setup
// =========================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=================================");
    Serial.println("  BW16 IRL Flight IoT Payload");
    Serial.println("=================================");

    pinMode(BUZZER_PIN, OUTPUT);    digitalWrite(BUZZER_PIN, HIGH);  // Buzzer tắt (Active LOW)
    pinMode(LED_RED_PIN, OUTPUT);   digitalWrite(LED_RED_PIN, LOW);  // LED đỏ tắt
    pinMode(LED_GREEN_PIN, OUTPUT); digitalWrite(LED_GREEN_PIN, HIGH); // LED xanh sáng ngay
    pinMode(MQ135_PIN, INPUT);

    // Khởi tạo OLED
    if (display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 20);
        display.println("BW16 IRL Payload");
        display.setCursor(0, 35);
        display.println("Booting...");
        display.display();
    } else {
        Serial.println("[ERROR] OLED init failed!");
    }

    // Khởi tạo Hardware PWM Servo (50Hz)
    pwmout_init(&servo_pwm, SERVO_PIN_HW);
    pwmout_period_us(&servo_pwm, 20000);
    setServo(0);  // Khóa chốt ở 0 độ

    dht.begin();
    delay(500);
    connectWiFi();

    client.setServer(mqtt_server, 1883);
    client.setCallback(mqttCallback);
    client.setBufferSize(256);
}

// =========================================================================
//  5. Main Loop (Non-blocking millis)
// =========================================================================
void loop() {
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
    if (!client.connected()) reconnectMQTT();
    else client.loop();

    unsigned long now = millis();

    // Cập nhật sóng WiFi mỗi 5s
    if (now - lastRssi > RSSI_INTERVAL) {
        lastRssi = now;
        if (WiFi.status() == WL_CONNECTED) rssi_val = WiFi.RSSI();
    }

    // Đọc cảm biến mỗi 200ms (Riêng DHT22 đọc mỗi 2s để tránh bị NaN)
    if (now - lastRead >= INTERVAL_READ) {
        lastRead = now;
        
        if (now - lastDht >= INTERVAL_DHT) {
            lastDht = now;
            float t = dht.readTemperature();
            float h = dht.readHumidity();
            if (!isnan(t) && !isnan(h)) { temp_val = t; hum_val = h; dht_ok = true; }
            else { dht_ok = false; }
        }

        mq_val = analogRead(MQ135_PIN);
        env_alert = (temp_val >= TEMP_THRESHOLD);
        if (now > MQ135_WARMUP_MS) env_alert = env_alert || (mq_val > CO2_THRESHOLD);

        // Buzzer & LED logic (Nhấp nháy & Beep khi có cảnh báo)
        bool blink_state = (now / 500) % 2 == 0; // Đảo trạng thái mỗi 500ms
        
        bool is_alerting = (ovr_led ? state_led : env_alert);
        bool is_buzzing = (ovr_buzzer ? state_buzzer : env_alert);
        
        digitalWrite(BUZZER_PIN,    is_buzzing ? (blink_state ? LOW : HIGH) : HIGH); // Active LOW
        digitalWrite(LED_RED_PIN,   is_alerting ? (blink_state ? HIGH : LOW) : LOW); // Active HIGH
        digitalWrite(LED_GREEN_PIN, is_alerting ? LOW : HIGH); // Tắt xanh khi có cảnh báo

        updateOLED();
    }

    // Gửi JSON MQTT mỗi 1000ms
    if (now - lastPub >= INTERVAL_PUB) {
        lastPub = now;
        char json[200];
        snprintf(json, sizeof(json),
            "{\"temp\":%.1f,\"humidity\":%.1f,\"co2\":%d,\"servo\":%d,\"alert\":%d,\"rssi\":%d,\"dht_ok\":%d}",
            temp_val, hum_val, mq_val, servo_angle, env_alert ? 1 : 0, rssi_val, dht_ok ? 1 : 0);
        if (client.connected() && client.publish(topic_pub, json)) {
            Serial.print("[SEND] ");
            Serial.println(json);
        }
    }
}
