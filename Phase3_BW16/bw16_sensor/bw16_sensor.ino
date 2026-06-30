// bw16_sensor.ino — BW16 Drone IoT Payload
// DHT22 + MQ-135 + Servo + OLED → MQTT

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <AmebaServo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "secrets.h"

// --- Network Config ---
const char* ssid        = SECRET_SSID;
const char* password    = SECRET_PASS;
const char* mqtt_server = "broker.emqx.io";
const char* topic_sensors = "iot102_drone/payload/sensors";
const char* topic_payload = "iot102_drone/control/payload";
const int   mqtt_port   = 1883;

// --- Pin Definitions ---
#define DHT_PIN         PA30
#define DHT_TYPE        DHT22
#define MQ135_PIN       PB3

#define BUZZER_PIN      PA14
#define LED_PIN         PA15   // LED Đỏ (cảnh báo)
#define LED_GREEN_PIN   PA27   // LED Xanh (an toàn)

#define SERVO_PIN       PA13

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
int    flight_fence = 0; // 0: OFF, 1: OK, 2: BREACH
unsigned long lastTelemetryTime = 0;

// --- Objects ---
WiFiClient   wifiClient;
PubSubClient client(wifiClient);
DHT          dht(DHT_PIN, DHT_TYPE);
AmebaServo   payloadServo;

unsigned long lastMsg = 0;
unsigned long lastOLEDUpdate = 0;
const long    interval = 2000;
const long    oledInterval = 200;

// Sensor globals
float temp_val = 0.0;
float hum_val  = 0.0;
int   mq_raw_val = 0;
bool  env_alert = false;

// --- JSON Parser ---
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

// --- MQTT Callback ---
void callback(char* topic, byte* payload, unsigned int length) {
    String msgString;
    msgString.reserve(length);
    for (unsigned int i = 0; i < length; i++) {
        msgString += (char)payload[i];
    }

    Serial.print("[MQTT] ");
    Serial.println(msgString);

    // 1. Check if this is a downstream telemetry JSON packet from SITL
    String modeStr = parseJsonField(msgString, "mode");
    if (modeStr.length() > 0) {
        flight_mode = modeStr;
        lastTelemetryTime = millis();
        
        String armedStr = parseJsonField(msgString, "armed");
        drone_armed = (armedStr == "1" || armedStr == "true");

        String altStr = parseJsonField(msgString, "alt");
        if (altStr.length() > 0) flight_alt = altStr.toFloat();

        String spdStr = parseJsonField(msgString, "spd");
        if (spdStr.length() > 0) flight_spd = spdStr.toFloat();

        String battStr = parseJsonField(msgString, "batt");
        if (battStr.length() > 0) flight_batt = battStr.toFloat();

        String windStr = parseJsonField(msgString, "wind");
        if (windStr.length() > 0) flight_wind = windStr.toFloat();

        String fenceStr = parseJsonField(msgString, "fence");
        if (fenceStr.length() > 0) flight_fence = fenceStr.toInt();
        return; // Terminate callback processing for telemetry packets
    }

    // 2. Otherwise process command payload overrides
    String command = parseJsonField(msgString, "command");

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
            payloadServo.write(angle);
            Serial.print("[CMD] SERVO ");
            Serial.println(angle);
        }
    }
}

// --- WiFi Connect ---
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

// --- MQTT Connect ---
void connectMQTT() {
    int retry = 0;
    while (!client.connected() && retry < MQTT_MAX_RETRIES) {
        Serial.print("[MQTT] Connecting...");

        String clientId = "DroneIoT_BW16_" + String(random(0xffff), HEX);
        if (client.connect(clientId.c_str())) {
            Serial.println(" OK!");
            client.subscribe(topic_payload);
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

// --- Setup ---
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("==============================");
    Serial.println("  BW16 Drone IoT Payload");
    Serial.println("==============================");

    // Init all GPIO
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, BUZZER_OFF);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_OFF);

    pinMode(LED_GREEN_PIN, OUTPUT);
    digitalWrite(LED_GREEN_PIN, LED_ON);

    pinMode(MQ135_PIN, INPUT);

    // OLED
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

    // Servo
    payloadServo.attach(SERVO_PIN);
    payloadServo.write(0);

    Serial.println("[INIT] GPIO OK");

    dht.begin();
    delay(500);

    connectWiFi();

    // MQTT config
    wifiClient.setBlockingMode();
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    client.setKeepAlive(CUSTOM_MQTT_KEEPALIVE);

    Serial.println("[INIT] Setup complete!");
}

// --- Main Loop ---
void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }
    if (WiFi.status() == WL_CONNECTED && !client.connected()) {
        connectMQTT();
    }

    client.loop();

    unsigned long now = millis();

    // --- Sensor Read & MQTT Publish (every 2s) ---
    if (now - lastMsg >= interval) {
        lastMsg = now;

        temp_val = dht.readTemperature();
        hum_val  = dht.readHumidity();

        bool dht_ok = true;
        if (isnan(temp_val) || isnan(hum_val)) {
            temp_val   = 0.0;
            hum_val    = 0.0;
            dht_ok = false;
        }

        mq_raw_val = analogRead(MQ135_PIN);

        if (now > MQ135_WARMUP_MS) {
            env_alert = (mq_raw_val > CO2_THRESHOLD);
        }

        // Buzzer: MQTT > Auto
        if (mqtt_buzzer_override) {
            digitalWrite(BUZZER_PIN, mqtt_buzzer_state ? BUZZER_ON : BUZZER_OFF);
        } else {
            digitalWrite(BUZZER_PIN, env_alert ? BUZZER_ON : BUZZER_OFF);
        }

        // LED: MQTT > Auto
        if (mqtt_led_override) {
            digitalWrite(LED_PIN,       mqtt_led_state ? LED_ON : LED_OFF);
            digitalWrite(LED_GREEN_PIN, mqtt_led_state ? LED_OFF : LED_ON);
        } else {
            digitalWrite(LED_PIN,       env_alert ? LED_ON : LED_OFF);
            digitalWrite(LED_GREEN_PIN, env_alert ? LED_OFF : LED_ON);
        }

        // Build & publish JSON
        char jsonBuf[256];
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"temp\":%.1f,\"humidity\":%.1f,\"co2\":%d,\"alert\":%d,\"rssi\":%d,\"dht_ok\":%d}",
            temp_val, hum_val, mq_raw_val, env_alert ? 1 : 0, (int)WiFi.RSSI(), dht_ok ? 1 : 0);

        if (client.connected() && client.publish(topic_sensors, jsonBuf)) {
            Serial.print("[SEND] ");
            Serial.println(jsonBuf);
        } else {
            Serial.println("[SEND] MQTT publish failed.");
        }
    }

    // --- OLED Update (5fps) ---
    if (now - lastOLEDUpdate >= oledInterval) {
        lastOLEDUpdate = now;
        updateOLED(env_alert);
    }
}

// --- OLED Display ---
void updateOLED(bool env_alert) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // 1. Dòng 1: WiFi/MQTT Status & Flight Mode (Nằm trọn trong vùng màu VÀNG)
    display.setCursor(0, 2);
    display.print(WiFi.status() == WL_CONNECTED ? "W:OK" : "W:NO");
    display.print(client.connected() ? " M:OK" : " M:NO");
    display.print(" | ");
    
    // Kiểm tra Watchdog Link Lost (5 giây không nhận được telemetry)
    bool link_lost = (millis() - lastTelemetryTime > 5000);
    if (link_lost) {
        display.print("L-LOST");
    } else {
        display.print(flight_mode);
        display.print(drone_armed ? "*" : "");
    }

    // Đường kẻ phân cách 1: Đặt tại y=14 để phân tách ranh giới màu Vàng/Xanh (y=16)
    display.drawLine(0, 14, 128, 14, SSD1306_WHITE);

    // 2. Dòng 2: Độ cao (ALT) và Tốc độ bay (SPD) (Nằm trọn trong vùng màu XANH)
    display.setCursor(0, 17);
    display.print("ALT: ");
    if (link_lost) display.print("--");
    else { display.print(flight_alt, 1); display.print("m"); }
    
    display.setCursor(68, 17);
    display.print("SPD:");
    if (link_lost) display.print("--");
    else { display.print(flight_spd, 1); display.print("m/s"); }

    // 3. Dòng 3: Điện áp PIN (BATT) và Tốc độ gió giật (WIND)
    display.setCursor(0, 26);
    display.print("BAT: ");
    if (link_lost) display.print("--");
    else { display.print(flight_batt, 1); display.print("V"); }
    
    display.setCursor(68, 26);
    display.print("WND:");
    if (link_lost) display.print("--");
    else { display.print(flight_wind, 1); display.print("m/s"); }

    display.drawLine(0, 35, 128, 35, SSD1306_WHITE);

    // 4. Dòng 4: Hàng rào ảo (FENCE) và Trạng thái cảm biến thật (DHT22)
    display.setCursor(0, 38);
    display.print("FENCE: ");
    if (link_lost || flight_fence == 0) display.print("OFF");
    else if (flight_fence == 1) display.print("OK");
    else display.print("BREACH");

    display.setCursor(68, 38);
    // Kiểm tra xem DHT22 có hoạt động bình thường không
    bool dht_ok = !(isnan(temp_val) || isnan(hum_val) || (temp_val == 0.0 && hum_val == 0.0));
    display.print("DHT: ");
    display.print(dht_ok ? "OK" : "ERR");

    display.drawLine(0, 47, 128, 47, SSD1306_WHITE);

    // 5. Dòng 5 & 6: Khí Gas/CO2 và Trạng thái cảnh báo tổng thể
    display.setCursor(0, 50);
    display.print("GAS/CO2: ");
    display.print(mq_raw_val);
    display.print(" ADC");

    display.setCursor(0, 57);
    if (env_alert) {
        display.print("! GAS/ENV ALERT !");
    } else if (link_lost) {
        display.print("CONNECTING SITL...");
    } else if (flight_fence == 2) {
        display.print("! FENCE BREACH (RTL) !");
    } else {
        display.print("SYSTEM NORMAL");
    }

    display.display();
}
