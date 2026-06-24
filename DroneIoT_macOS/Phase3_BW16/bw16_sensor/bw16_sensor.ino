// ============================================================
// bw16_sensor.ino — Phase 3: Board BW16 (RTL8720DN)
// Đọc DHT22 (nhiệt độ/độ ẩm) + MQ-135 (CO2/không khí)
// Gửi JSON qua WiFi lên MQTT Broker
//
// Thư viện cần cài (Sketch → Include Library → Manage Libraries):
//   - PubSubClient (Nick O'Leary)
//   - DHT sensor library (Adafruit)
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <AmebaServo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "secrets.h"

// ── Cấu hình kết nối ─────────────────────────────────────
const char* ssid        = SECRET_SSID;       // Tên WiFi (đọc từ secrets.h)
const char* password    = SECRET_PASS;      // Mật khẩu WiFi (đọc từ secrets.h)
const char* mqtt_server = "broker.hivemq.com"; // Đổi topic thành duy nhất để không bị trùng lặp trên mạng Public
const char* topic_sensors = "tuonghuy_drone/payload/sensors";
const char* topic_payload = "tuonghuy_drone/control/payload";
const int   mqtt_port   = 1883;

// ── Pin definitions ───────────────────────────────────────
// Màn hình OLED I2C bắt buộc sử dụng PA25 (SCL) và PA26 (SDA)
#define DHT_PIN         PA30   // Chân DATA của DHT22 (Dời sang PA30 theo yêu cầu)
#define DHT_TYPE        DHT22  // Loại cảm biến DHT22
#define MQ135_PIN       PB3    // Chân đọc ADC (Khí gas)

#define BUZZER_PIN      PA14   // Chân điều khiển Còi Buzzer
#define LED_PIN         PA15   // Đèn LED Đỏ (Cảnh báo Môi trường)
#define LED_GREEN_PIN   PA27   // Đèn LED Xanh (Trạng thái an toàn - Đã khôi phục)

// Cấu hình Cảm biến siêu âm SRF05 (Radar Va Chạm)
#define TRIG_PIN        PB2    // Chân phát sóng âm
#define ECHO_PIN        PB1    // Chân nhận sóng âm (Dời từ PA13)
#define COLLISION_LED_PIN PA12 // Đèn LED riêng biệt báo va chạm (Dời từ PB1)

// Cấu hình Động cơ Servo thả hàng
#define SERVO_PIN       PA13   // Chân PWM điều khiển góc Servo (Dời từ PA25 để nhường I2C)

// Cấu hình màn hình OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Cấu hình mức tích cực (Active Level)
// Nếu còi/đèn bị ngược (bấm Bật thì Tắt, bấm Tắt thì Bật), hãy đổi HIGH thành LOW
#define BUZZER_ACTIVE   LOW    // Đổi thành LOW (Buzzer kích ở mức thấp)
#define LED_ACTIVE      HIGH   // Đổi thành HIGH (Đèn sáng khi cấp 3.3V)

// Phái sinh mức bật tắt
#define BUZZER_ON       BUZZER_ACTIVE
#define BUZZER_OFF      (!BUZZER_ACTIVE)
#define LED_ON          LED_ACTIVE
#define LED_OFF         (!LED_ACTIVE)

// Ngưỡng cảnh báo & Hằng số cấu hình
const int   CO2_THRESHOLD      = 600;    // Ngưỡng ADC 12-bit (0-4095)
const int   COLLISION_DIST_CM  = 30;     // Ngưỡng khoảng cách va chạm (cm)
const int   WIFI_MAX_RETRIES   = 30;     // Số lần thử kết nối WiFi
const int   MQTT_MAX_RETRIES   = 5;      // Số lần thử kết nối MQTT
const int   MQTT_RETRY_DELAY   = 3000;   // Thời gian chờ giữa các lần thử MQTT (ms)
const int   MQTT_KEEPALIVE     = 60;     // Thời gian keepalive MQTT (s)
const unsigned long PULSE_TIMEOUT_US = 12000; // Giảm từ 20000 xuống 12000us (~2m, đủ cho drone) — giải phóng CPU sớm hơn
const unsigned long MQ135_WARMUP_MS  = 30000; // Thời gian khởi động MQ-135 (ms)
const unsigned long BLINK_INTERVAL   = 200;   // Nhịp chớp đèn va chạm (ms)
const uint8_t OLED_I2C_ADDR    = 0x3C;   // Địa chỉ I2C màn hình OLED

// Trạng thái ghi đè từ MQTT (Quyền ưu tiên cao nhất)
bool mqtt_buzzer_override = false;
bool mqtt_buzzer_state    = false;
bool mqtt_led_override    = false;
bool mqtt_led_state       = false;

// ── Khởi tạo đối tượng ───────────────────────────────────
WiFiClient   wifiClient;
PubSubClient client(wifiClient);
DHT          dht(DHT_PIN, DHT_TYPE);
AmebaServo   payloadServo;

unsigned long lastMsg = 0;
unsigned long lastOLEDUpdate = 0;
const long    interval = 2000;   // Gửi dữ liệu MQTT mỗi 2 giây
const long    oledInterval = 200; // Cập nhật màn hình mỗi 200ms (5fps)

// Biến cho Radar Va chạm (non-blocking)
long  distance_cm = -1;
unsigned long lastBlinkTime = 0;

// Biến lưu trữ dữ liệu cảm biến toàn cục cho OLED
float temp_val = 0.0;
float hum_val  = 0.0;
int   mq_raw_val = 0;
bool  env_alert = false;
bool  collisionBlinkState = false;

// ── Hàm trích xuất giá trị JSON đơn giản ────────────────
// Fix F-005: Dùng String thay vì VLA để an toàn trên mọi compiler
String parseJsonField(const String& json, const String& key) {
    int keyIndex = json.indexOf("\"" + key + "\"");
    if (keyIndex == -1) return "";
    int colonIndex = json.indexOf(":", keyIndex);
    if (colonIndex == -1) return "";
    
    // Tìm ký tự có nghĩa đầu tiên sau dấu hai chấm
    int valStartIndex = colonIndex + 1;
    while (valStartIndex < json.length() && (json[valStartIndex] == ' ' || json[valStartIndex] == '\t' || json[valStartIndex] == '\r' || json[valStartIndex] == '\n')) {
        valStartIndex++;
    }
    if (valStartIndex >= json.length()) return "";
    
    if (json[valStartIndex] == '"') {
        // Trường là chuỗi (string)
        int valEndIndex = json.indexOf("\"", valStartIndex + 1);
        if (valEndIndex == -1) return "";
        return json.substring(valStartIndex + 1, valEndIndex);
    } else {
        // Trường là số hoặc boolean hoặc null
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

// ── Hàm Callback nhận lệnh MQTT ─────────────────────────
// Fix F-005: Dùng String thay VLA char message[length+1]
void callback(char* topic, byte* payload, unsigned int length) {
    String msgString;
    msgString.reserve(length);
    for (unsigned int i = 0; i < length; i++) {
        msgString += (char)payload[i];
    }

    Serial.print("[MQTT] Nhan lenh tu topic [");
    Serial.print(topic);
    Serial.print("]: ");
    Serial.println(msgString);

    String command = parseJsonField(msgString, "command");

    if (command == "BUZZER_ON") {
        mqtt_buzzer_override = true;
        mqtt_buzzer_state    = true;
        digitalWrite(BUZZER_PIN, BUZZER_ON);
        Serial.println("[CONTROL] MQTT override: BAT COI");
    } else if (command == "BUZZER_OFF") {
        mqtt_buzzer_override = true;
        mqtt_buzzer_state    = false;
        digitalWrite(BUZZER_PIN, BUZZER_OFF);
        Serial.println("[CONTROL] MQTT override: TAT COI");
    } else if (command == "LED_ON") {
        mqtt_led_override = true;
        mqtt_led_state    = true;
        digitalWrite(LED_PIN, LED_ON);
        digitalWrite(LED_GREEN_PIN, LED_OFF);
        Serial.println("[CONTROL] MQTT override: BAT LED DO");
    } else if (command == "LED_OFF") {
        mqtt_led_override = true;
        mqtt_led_state    = false;
        digitalWrite(LED_PIN, LED_OFF);
        digitalWrite(LED_GREEN_PIN, LED_ON);
        Serial.println("[CONTROL] MQTT override: BAT LED XANH");
    } else if (command == "DISARM") {
        // Tắt động cơ: chỉ an toàn khi drone đang nhàn (nếu cần)
        // Không có hành động ở firmware BW16 — lệnh DISARM đi qua MQTT tới gateway
        Serial.println("[CONTROL] DISARM command received at BW16 (no local action)");
    } else if (command == "RESET") {
        mqtt_buzzer_override = false;
        mqtt_led_override    = false;
        Serial.println("[CONTROL] Khoi phuc che do tu dong");
    } else if (command == "SERVO") {
        String angleStr = parseJsonField(msgString, "angle");
        if (angleStr.length() > 0) {
            int angle = constrain(angleStr.toInt(), 0, 180);
            payloadServo.write(angle);
            Serial.print("[CONTROL] Da quay Servo goc: ");
            Serial.println(angle);
        }
    }
}

// ── Kết nối WiFi ──────────────────────────────────────────
void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.print("[WiFi] Dang ket noi toi: ");
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
        Serial.print("[WiFi] Ket noi thanh cong! IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println();
        Serial.println("[WiFi] That bai sau 30 lan thu. Se thu lai sau...");
    }
}

// ── Kết nối MQTT ─────────────────────────────────────────
void connectMQTT() {
    int retry = 0;
    while (!client.connected() && retry < MQTT_MAX_RETRIES) {
        Serial.print("[MQTT] Dang ket noi broker...");

        String clientId = "TuongHuy_BW16_" + String(random(0xffff), HEX);
        if (client.connect(clientId.c_str())) {
            Serial.println(" OK!");
            client.subscribe(topic_payload);
            Serial.print("[MQTT] Da subscribe: ");
            Serial.println(topic_payload);
            break;
        } else {
            Serial.print(" That bai, rc=");
            Serial.print(client.state());
            Serial.println(" - Thu lai sau 3s...");
            delay(MQTT_RETRY_DELAY);
        }
        retry++;
    }
}

// ── Setup ─────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("==============================");
    Serial.println("  BW16 Drone IoT Payload");
    Serial.println("  DHT22 + MQ-135 + alert Node");
    Serial.println("  v3.0 - Full Audit & Optimization");
    Serial.println("==============================");

    Serial.println("-> Cau hinh Buzzer (PA14)..."); delay(50);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, BUZZER_OFF);
    Serial.println("   [OK] Buzzer");

    Serial.println("-> Cau hinh LED Do (PA15)..."); delay(50);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_OFF);
    Serial.println("   [OK] LED Đỏ");

    Serial.println("-> Cau hinh LED Xanh (PA27)..."); delay(50);
    pinMode(LED_GREEN_PIN, OUTPUT);
    digitalWrite(LED_GREEN_PIN, LED_ON);
    Serial.println("   [OK] LED Xanh");

    
    pinMode(COLLISION_LED_PIN, OUTPUT);
    digitalWrite(COLLISION_LED_PIN, LED_OFF);
    
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(MQ135_PIN, INPUT);

    Serial.println("-> Khoi tao man hinh OLED...");
    if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
        Serial.println("[ERROR] SSD1306 allocation failed");
    } else {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0,10);
        display.println("Drone IoT Booting...");
        display.display();
    }

    Serial.println("-> Cau hinh Dong co Servo...");
    payloadServo.attach(SERVO_PIN);
    payloadServo.write(0); // Khởi tạo ở góc 0 độ (Khóa hàng)
    Serial.println("   [OK] Servo Payload");

    Serial.println("[INIT] GPIO OK");

    // Khởi tạo DHT22
    dht.begin();
    delay(500);
    Serial.println("[INIT] DHT22 init xong");

    // Kết nối WiFi
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        Serial.print("[INIT] WiFi MAC: ");
        for (int i = 0; i < 6; i++) {
            if (mac[i] < 0x10) Serial.print("0");
            Serial.print(mac[i], HEX);
            if (i < 5) Serial.print(":");
        }
        Serial.println();
    }

    // Cấu hình MQTT
    wifiClient.setBlockingMode();
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    client.setKeepAlive(MQTT_KEEPALIVE);

    Serial.println("[SYSTEM] Setup hoan tat!");
}

// ── Loop ──────────────────────────────────────────────────
void loop() {
    // Tự động reconnect WiFi nếu mất kết nối
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }

    // Tự động reconnect MQTT
    if (WiFi.status() == WL_CONNECTED && !client.connected()) {
        connectMQTT();
    }

    // Gọi client.loop() đầu tiên — đảm bảo MQTT keepalive và nhận lệnh nhanh nhất
    client.loop();

    unsigned long now = millis();

    // ── XỬ LÝ RADAR VA CHẠM (Chạy liên tục không bị block) ──
    // 1. Kích hoạt SRF05
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    // 2. Đọc thời gian vọng (timeout giảm xuống 12000us tương đương ~2m)
    long duration = pulseIn(ECHO_PIN, HIGH, PULSE_TIMEOUT_US);
    if (duration == 0) {
        distance_cm = -1; // Không có vật cản gần (ngoài tầm)
    } else {
        distance_cm = duration * 0.034 / 2;
    }

    // Gọi loop() lần 2 sau pulseIn (đảm bảo xử lý MQTT trong khi được khóa 12ms)
    client.loop();

    bool collision_alert = (distance_cm > 0 && distance_cm < COLLISION_DIST_CM);

    // 3. Xử lý Chớp đèn / Còi báo va chạm (Non-blocking Blink 200ms)
    if (collision_alert) {
        if (now - lastBlinkTime >= BLINK_INTERVAL) {
            lastBlinkTime = now;
            collisionBlinkState = !collisionBlinkState;
        }
        // Biến toàn cục distance_cm, is_alert, collision_alert sẽ được sử dụng cho OLED

        // ── ĐIỀU KHIỂN ĐÈN LED VA CHẠM ──
        digitalWrite(COLLISION_LED_PIN, collisionBlinkState ? LED_ON : LED_OFF);
            
        // Còi hú theo nhịp nếu không bị Web chèn quyền
        if (!mqtt_buzzer_override) {
            digitalWrite(BUZZER_PIN, collisionBlinkState ? BUZZER_ON : BUZZER_OFF);
        }
    } else {
        digitalWrite(COLLISION_LED_PIN, LED_OFF);
    }


    // ── XỬ LÝ ĐỌC CẢM BIẾN MÔI TRƯỜNG & GỬI MQTT (Mỗi 2 giây) ──
    // is_alert is now global `env_alert`
    if (now - lastMsg >= interval) {
        lastMsg = now;

        // Đọc DHT22 (Nhiệt độ, Độ ẩm)
        temp_val = dht.readTemperature();
        hum_val  = dht.readHumidity();

        bool dht_ok = true;
        if (isnan(temp_val) || isnan(hum_val)) {
            temp_val   = 0.0;
            hum_val    = 0.0;
            dht_ok = false;
        }

        // Đọc MQ-135 (ADC giá trị thô 0-4095)
        mq_raw_val = analogRead(MQ135_PIN);

        // Xác định trạng thái cảnh báo tự động onboard
        if (now > MQ135_WARMUP_MS) {
            env_alert = (mq_raw_val > CO2_THRESHOLD);
        }

        // ── ĐIỀU KHIỂN CÒI BUZZER (Ưu tiên: MQTT > Va Chạm > Tự động Môi trường) ──
        if (mqtt_buzzer_override) {
            digitalWrite(BUZZER_PIN, mqtt_buzzer_state ? BUZZER_ON : BUZZER_OFF);
        } else if (!collision_alert) {
            digitalWrite(BUZZER_PIN, env_alert ? BUZZER_ON : BUZZER_OFF);
        }

        // ── ĐIỀU KHIỂN ĐÈN LED MÔI TRƯỜNG (Ưu tiên MQTT > Tự động) ──
        if (mqtt_led_override) {
            digitalWrite(LED_PIN,       mqtt_led_state ? LED_ON : LED_OFF);
            digitalWrite(LED_GREEN_PIN, mqtt_led_state ? LED_OFF : LED_ON);
        } else {
            digitalWrite(LED_PIN,       env_alert ? LED_ON : LED_OFF);
            digitalWrite(LED_GREEN_PIN, env_alert ? LED_OFF : LED_ON);
        }

        // Đóng gói dữ liệu JSON (dùng snprintf tránh heap fragmentation)
        char jsonBuf[256];
        snprintf(jsonBuf, sizeof(jsonBuf),
            "{\"temp\":%.1f,\"humidity\":%.1f,\"co2\":%d,\"alert\":%d,\"distance\":%ld,\"rssi\":%d,\"dht_ok\":%d}",
            temp_val, hum_val, mq_raw_val, env_alert ? 1 : 0, distance_cm, (int)WiFi.RSSI(), dht_ok ? 1 : 0);

        // Publish lên MQTT broker
        if (client.connected() && client.publish(topic_sensors, jsonBuf)) {
            Serial.print("[SEND] ");
            Serial.println(jsonBuf);
        } else {
            Serial.println("[SEND] Loi! Khong the publish len MQTT.");
        }
    }

    // ── Cập nhật màn hình OLED (5fps) ──
    if (now - lastOLEDUpdate >= oledInterval) {
        lastOLEDUpdate = now;
        updateOLED(env_alert, collision_alert);
    }
}

// ── Hàm vẽ giao diện OLED ──
void updateOLED(bool env_alert, bool col_alert) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Header (Trạng thái mạng)
    display.setCursor(0, 0);
    display.print("WIFI:");
    display.print(WiFi.status() == WL_CONNECTED ? "OK" : "NO");
    display.print(" MQTT:");
    display.print(client.connected() ? "OK" : "NO");
    
    display.drawLine(0, 10, 128, 10, SSD1306_WHITE);
    
    // Dữ liệu cảm biến
    display.setCursor(0, 14);
    display.print("Temp: "); display.print(temp_val, 1); display.println(" C");
    display.print("Hum : "); display.print(hum_val, 1); display.println(" %");
    display.print("CO2 : "); display.print(mq_raw_val); display.println(" ADC");
    display.print("Dist: "); 
    if (distance_cm > 0) { display.print(distance_cm); display.println(" cm"); }
    else { display.println("OUT / SAFE"); }
    
    // Khung cảnh báo (Footer)
    display.drawLine(0, 50, 128, 50, SSD1306_WHITE);
    display.setCursor(0, 54);
    if (col_alert) {
        display.print("! COLLISION ALERT !");
    } else if (env_alert) {
        display.print("! GAS/ENV ALERT !");
    } else {
        display.print("SYSTEM NORMAL");
    }
    
    display.display();
}
