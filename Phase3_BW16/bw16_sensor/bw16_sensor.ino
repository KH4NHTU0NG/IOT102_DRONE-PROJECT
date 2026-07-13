// bw16_sensor.ino — BW16 Drone IoT Payload
// DHT22 + MQ-135 + Servo + OLED → MQTT

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
// [FIX] Dùng Hardware PWM thay AmebaServo vì WiFi stack phá software PWM
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
// [FIX] Topic siêu ngắn để tránh MQTT buffer 128 bytes overflow
const char* topic_telem_dn = "iot102/dn";
const int   mqtt_port   = 1883;

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
#define SERVO_PIN_HW    PA_13   // Hardware PWM pin name
#define SERVO_PERIOD_US 20000   // 50Hz
#define SERVO_MIN_US    544     // 0 dộ
#define SERVO_MAX_US    2400    // 180 dộ

// --- OLED Config ---
#define SCREEN_WIDTH 128
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
const int   WIFI_MAX_RETRIES   = 30;
const int   MQTT_MAX_RETRIES   = 5;
const int   MQTT_RETRY_DELAY   = 3000;
const int   CUSTOM_MQTT_KEEPALIVE = 60;
const unsigned long MQ135_WARMUP_MS  = 30000;
const uint8_t OLED_I2C_ADDR    = 0x3C;

// --- MQTT Override State ---
bool mqtt_buzzer_override = false;
bool mqtt_buzzer_state    = false;
bool mqtt_led_override    = false;
bool mqtt_led_state       = false;

// --- SITL Flight State for Downstream ---
String flight_mode = "DISCONN";
bool   drone_armed = false;
float  flight_alt  = 0.0;
float  flight_spd  = 0.0;
float  flight_batt = 12.6;
float  flight_wind = 0.0;
int    flight_fence = 0;
unsigned long lastTelemetryTime = 0;

// --- Objects ---
WiFiClient   wifiClient;
PubSubClient client(wifiClient);
DHT          dht(DHT_PIN, DHT_TYPE);
// [FIX] Hardware PWM servo — không bị WiFi interrupt phá
pwmout_t     servo_pwm;

unsigned long lastMsg = 0;
unsigned long lastOLEDUpdate = 0;
const long    interval = 2000;
const long    oledInterval = 200;

// Servo pending (callback chỉ lưu, loop() thực thi)
int           servo_pending_angle = -1;

// Sensor globals
float temp_val = 0.0;
float hum_val  = 0.0;
int   mq_raw_val = 0;
float sonar_dist = 0.0;
bool  env_alert = false;

// [FIX] Cache RSSI để tránh WiFi.RSSI() block 50ms mỗi 200ms → phá PWM servo
int   cached_rssi = 0;
unsigned long lastRssiUpdate = 0;
const long    rssiInterval = 5000;  // Cập nhật RSSI mỗi 5 giây

// ============================================================
//  JSON Parser
// ============================================================
String parseJsonField(const String& json, const String& key) {
    int keyIndex = json.indexOf("\"" + key + "\"");
    if (keyIndex == -1) return "";
    int colonIndex = json.indexOf(":", keyIndex);
    if (colonIndex == -1) return "";

    int valStartIndex = colonIndex + 1;
    while (valStartIndex < json.length() && (json[valStartIndex] == ' ' || json[valStartIndex] == '\t' || json[valStartIndex] == '\r' || json[valStartIndex] == '\n')) {
        valStartIndex++;
    }
    if (valStartIndex >= json.length()) return "";

    if (json[valStartIndex] == '"') {
        int valEndIndex = json.indexOf("\"", valStartIndex + 1);
        if (valEndIndex == -1) return "";
        return json.substring(valStartIndex + 1, valEndIndex);
    } else {
        int commaIndex = json.indexOf(",", valStartIndex);
        int braceIndex = json.indexOf("}", valStartIndex);
        int valEndIndex;
        if (commaIndex == -1) valEndIndex = braceIndex;
        else if (braceIndex == -1) valEndIndex = commaIndex;
        else valEndIndex = min(commaIndex, braceIndex);

        if (valEndIndex == -1) valEndIndex = json.length();
        String val = json.substring(valStartIndex, valEndIndex);
        val.trim();
        return val;
    }
}

// ============================================================
//  MQTT Callback
// ============================================================
void callback(char* topic, byte* payload, unsigned int length) {
    String msgString;
    msgString.reserve(length);
    for (unsigned int i = 0; i < length; i++) {
        msgString += (char)payload[i];
    }

    Serial.print("[MQTT] topic=");
    Serial.print(topic);
    Serial.print(" msg=");
    Serial.println(msgString);

    // ── Route theo topic ──────────────────────────────────────
    String topicStr(topic);

    // Helper: xử lý telemetry (dùng cho cả 2 topic)
    auto handleTelemetry = [&]() {
        String modeStr = parseJsonField(msgString, "md");
        if (modeStr.length() == 0) modeStr = parseJsonField(msgString, "mode"); // fallback
        if (modeStr.length() > 0) {
            flight_mode = modeStr;
            lastTelemetryTime = millis();
        }
        String armedStr = parseJsonField(msgString, "ar");
        if (armedStr.length() == 0) armedStr = parseJsonField(msgString, "armed");
        drone_armed = (armedStr == "1" || armedStr == "true");

        String altStr  = parseJsonField(msgString, "al");
        if (altStr.length() == 0) altStr = parseJsonField(msgString, "alt");
        if (altStr.length()  > 0) flight_alt  = altStr.toFloat();

        String spdStr  = parseJsonField(msgString, "sp");
        if (spdStr.length() == 0) spdStr = parseJsonField(msgString, "spd");
        if (spdStr.length()  > 0) flight_spd  = spdStr.toFloat();

        String battStr = parseJsonField(msgString, "bt");
        if (battStr.length() == 0) battStr = parseJsonField(msgString, "batt");
        if (battStr.length() > 0) flight_batt = battStr.toFloat();

        String windStr = parseJsonField(msgString, "wi");
        if (windStr.length() == 0) windStr = parseJsonField(msgString, "wind");
        if (windStr.length() > 0) flight_wind = windStr.toFloat();

        String fenceStr= parseJsonField(msgString, "fc");
        if (fenceStr.length() == 0) fenceStr = parseJsonField(msgString, "fence");
        if (fenceStr.length()> 0) flight_fence= fenceStr.toInt();
    };

    // 1. Topic telemetry mới (Fusion bridge đã restart)
    if (topicStr == String(topic_telem_dn)) {
        handleTelemetry();
        return;
    }

    // 2. Topic payload — kiểm tra có phải telemetry cũ không
    //    (Fusion chưa restart → vẫn publish "mode" lên topic_payload)
    String command = parseJsonField(msgString, "command");
    String modeCheck = parseJsonField(msgString, "mode");

    if (command.length() == 0 && modeCheck.length() > 0) {
        // Đây là telemetry cũ từ Fusion (chưa restart)
        handleTelemetry();
        return;
    }

    // 3. Command từ Web Dashboard
    if (command == "BUZZER_ON") {
        mqtt_buzzer_override = true;
        mqtt_buzzer_state    = true;
        digitalWrite(BUZZER_PIN, BUZZER_ON);
        Serial.println("[CMD] BUZZER ON");
    } else if (command == "BUZZER_OFF") {
        mqtt_buzzer_override = true;
        mqtt_buzzer_state    = false;
        digitalWrite(BUZZER_PIN, BUZZER_OFF);
        Serial.println("[CMD] BUZZER OFF");
    } else if (command == "LED_ON") {
        mqtt_led_override = true;
        mqtt_led_state    = true;
        digitalWrite(LED_PIN, LED_ON);
        digitalWrite(LED_GREEN_PIN, LED_OFF);
        Serial.println("[CMD] LED RED ON");
    } else if (command == "LED_OFF") {
        mqtt_led_override = true;
        mqtt_led_state    = false;
        digitalWrite(LED_PIN, LED_OFF);
        digitalWrite(LED_GREEN_PIN, LED_ON);
        Serial.println("[CMD] LED GREEN ON");
    } else if (command == "DISARM") {
        Serial.println("[CMD] DISARM (no local action)");
    } else if (command == "RESET") {
        mqtt_buzzer_override = false;
        mqtt_led_override    = false;
        Serial.println("[CMD] RESET auto mode");
    } else if (command == "SERVO") {
        String angleStr = parseJsonField(msgString, "angle");
        if (angleStr.length() > 0) {
            int angle = constrain(angleStr.toInt(), 0, 180);
            servo_pending_angle = angle;
            // [DEBUG] Nháy LED đỏ 2 lần — xác nhận BW16 đã nhận lệnh SERVO
            for (int i = 0; i < 2; i++) {
                digitalWrite(LED_PIN, LED_ON);
                delay(100);
                digitalWrite(LED_PIN, LED_OFF);
                delay(100);
            }
        }
    }
}

// ============================================================
//  WiFi Connect
// ============================================================
void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.print("[WiFi] Connecting to: ");
    Serial.println(ssid);
    WiFi.begin(const_cast<char*>(ssid), const_cast<char*>(password));

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < WIFI_MAX_RETRIES) {
        delay(500);
        Serial.print(".");
        retry++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("[WiFi] OK! IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println();
        Serial.println("[WiFi] Failed. Will retry later.");
    }
}

// ============================================================
//  MQTT Connect
// ============================================================
void connectMQTT() {
    int retry = 0;
    while (!client.connected() && retry < MQTT_MAX_RETRIES) {
        Serial.print("[MQTT] Connecting...");

        String clientId = "DroneIoT_BW16_" + String(random(0xffff), HEX);
        if (client.connect(clientId.c_str())) {
            Serial.println(" OK!");
            client.subscribe(topic_payload);   // lệnh từ Web
            client.subscribe(topic_telem_dn);  // telemetry từ Fusion
            break;
        } else {
            Serial.print(" Failed, rc=");
            Serial.print(client.state());
            Serial.println(" retry in 3s...");
            delay(MQTT_RETRY_DELAY);
        }
        retry++;
    }
}

// ============================================================
//  OLED Display
// ============================================================
void updateOLED(bool env_alert) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // ═══════════════════════════════════════════════════
    // VÙNG VÀNG (y = 0–15): Tiêu đề + Trạng thái alert
    // ═══════════════════════════════════════════════════
    display.setTextSize(1);
    display.setCursor(0, 3);  // Căn giữa dọc vùng vàng
    if (env_alert) {
        display.print("** CANH BAO O NHIEM **");
    } else {
        display.print("DroneIoT ");
        display.print(drone_armed ? "[ARMED]" : "[DISARM]");
    }

    // ═══════════════════════════════════════════════════
    // VÙNG XANH (y = 16–63): Toàn bộ thông số cảm biến
    // ═══════════════════════════════════════════════════

    // Dòng 1 (y=16): Mode + Altitude
    display.setCursor(0, 16);
    display.print(flight_mode);
    display.print(" Alt:");
    display.print(flight_alt, 1);
    display.print("m");

    // Dòng 2 (y=26): Nhiệt độ + Độ ẩm
    display.setCursor(0, 26);
    display.print("T:");
    if (temp_val == 0.0 && hum_val == 0.0) {
        display.print("--");
    } else {
        display.print(temp_val, 1);
    }
    display.print("C H:");
    if (hum_val == 0.0 && temp_val == 0.0) {
        display.print("--");
    } else {
        display.print(hum_val, 0);
    }
    display.print("%");

    // Dòng 3 (y=36): CO2 + Sonar
    display.setCursor(0, 36);
    display.print("CO2:");
    display.print(mq_raw_val);
    display.print(" D:");
    if (sonar_dist < 0) {
        display.print("---");
    } else {
        display.print(sonar_dist, 0);
    }
    display.print("cm");

    // Dòng 4 (y=46): Battery + Speed
    display.setCursor(0, 46);
    display.print("Bat:");
    display.print(flight_batt, 1);
    display.print("V Spd:");
    display.print(flight_spd, 0);
    display.print("m/s");

    // Dòng 5 (y=56): Fence + WiFi RSSI (dùng cached, không gọi WiFi.RSSI() trực tiếp)
    display.setCursor(0, 56);
    display.print(flight_fence == 2 ? "!FENCE!" :
                  flight_fence == 1 ? "FenceOK" : "Fence-");
    display.print(" R:");
    display.print(cached_rssi);

    display.display();
}

// ============================================================
//  Setup
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("==============================");
    Serial.println("  BW16 Drone IoT Payload");
    Serial.println("==============================");

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, BUZZER_OFF);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_OFF);

    pinMode(LED_GREEN_PIN, OUTPUT);
    digitalWrite(LED_GREEN_PIN, LED_ON);

    pinMode(MQ135_PIN, INPUT);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        Serial.println("[ERROR] OLED init failed");
    } else {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0,10);
        display.println("Drone IoT Booting...");
        display.display();
    }

    // [FIX] Hardware PWM servo init — chống WiFi interrupt
    pwmout_init(&servo_pwm, SERVO_PIN_HW);
    pwmout_period_us(&servo_pwm, SERVO_PERIOD_US);
    pwmout_pulsewidth_us(&servo_pwm, 1500); // 90 dộ (trung lập)
    Serial.println("[INIT] HW-PWM Servo OK");

    dht.begin();
    delay(500);

    connectWiFi();

    wifiClient.setBlockingMode();
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    client.setKeepAlive(CUSTOM_MQTT_KEEPALIVE);

    Serial.println("[INIT] Setup complete!");
}

// ============================================================
//  Main Loop
// ============================================================
void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }
    if (WiFi.status() == WL_CONNECTED && !client.connected()) {
        connectMQTT();
    }

    client.loop();

    unsigned long now = millis();

    if (now - lastMsg >= interval) {
        lastMsg = now;

        float t_new = dht.readTemperature();
        float h_new = dht.readHumidity();

        bool dht_ok = true;
        if (isnan(t_new) || isnan(h_new)) {
            // [FIX] Giữ nguyên giá trị cũ thay vì về 0
            // → Tránh OLED và MQTT nhảy 0 xen kẽ
            dht_ok = false;
            Serial.println("[DHT] Read failed, keeping last value");
        } else {
            temp_val = t_new;
            hum_val  = h_new;
        }

        mq_raw_val = analogRead(MQ135_PIN);

        digitalWrite(TRIG_PIN, LOW);
        delayMicroseconds(2);
        digitalWrite(TRIG_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(TRIG_PIN, LOW);
        // [FIX] Giới hạn pulseIn xuống 8ms (tương đương ~140cm)
        // Tránh block 30ms khi không có phản hồi → gây nhiễu PWM servo
        long duration = pulseIn(ECHO_PIN, HIGH, 8000);
        if (duration == 0) {
            sonar_dist = -1.0;
        } else {
            sonar_dist = (duration * 0.0343) / 2.0;
        }

        if (now > MQ135_WARMUP_MS) {
            env_alert = (mq_raw_val > CO2_THRESHOLD);
        }

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

        char jsonBuf[256];
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"temp\":%.1f,\"humidity\":%.1f,\"co2\":%d,\"distance\":%.1f,\"alert\":%d,\"rssi\":%d,\"dht_ok\":%d}",
            temp_val, hum_val, mq_raw_val, sonar_dist, env_alert ? 1 : 0, (int)WiFi.RSSI(), dht_ok ? 1 : 0);

        if (client.connected() && client.publish(topic_sensors, jsonBuf)) {
            Serial.print("[SEND] ");
            Serial.println(jsonBuf);
        } else {
            Serial.println("[SEND] MQTT publish failed.");
        }
    }

    if (now - lastOLEDUpdate >= oledInterval) {
        lastOLEDUpdate = now;
        updateOLED(env_alert);
    }

    // [FIX] Xử lý lệnh servo từ loop() — Hardware PWM, không bị WiFi phá
    if (servo_pending_angle >= 0) {
        int angle = servo_pending_angle;
        servo_pending_angle = -1;

        // Hardware PWM: tính pulse width theo góc
        angle = constrain(angle, 0, 180);
        int pulse_us = SERVO_MIN_US + (long)(angle) * (SERVO_MAX_US - SERVO_MIN_US) / 180;
        pwmout_pulsewidth_us(&servo_pwm, pulse_us);

        Serial.print("[SERVO] HW-PWM ");
        Serial.print(angle);
        Serial.print(" deg (");
        Serial.print(pulse_us);
        Serial.println(" us)");
    }

    // [FIX] Cập nhật RSSI riêng mỗi 5s để không block PWM servo
    if (now - lastRssiUpdate >= rssiInterval) {
        lastRssiUpdate = now;
        if (WiFi.status() == WL_CONNECTED) {
            cached_rssi = (int)WiFi.RSSI();
        }
    }
}
