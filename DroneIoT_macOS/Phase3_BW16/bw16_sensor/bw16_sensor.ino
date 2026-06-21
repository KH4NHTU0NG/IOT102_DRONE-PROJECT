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

// ── Cấu hình kết nối ─────────────────────────────────────
// !! Sửa SSID và PASSWORD thành WiFi của bạn !!
const char* ssid        = "TuongHuy";       // Tên WiFi (phân biệt hoa thường)
const char* password    = "kminh1983";      // Mật khẩu WiFi
const char* mqtt_server = "192.168.1.156";  // IP máy Mac chạy Mosquitto broker
const int   mqtt_port   = 1883;

// ── Pin definitions ───────────────────────────────────────
// QUAN TRỌNG: Không dùng PA12 (TX Log Console), PA30 (JTAG), PA28 (SWD)
#define DHT_PIN         PA26   // Chân DATA của DHT22
#define DHT_TYPE        DHT22  // Loại cảm biến DHT22
#define MQ135_PIN       PB3    // Chân đọc ADC (Khí gas) - Đang cắm thực tế ở đây

#define BUZZER_PIN      PA15   // Chân điều khiển Còi Buzzer
#define LED_PIN         PA25   // Đèn LED Đỏ (Cảnh báo Môi trường / CO2) - Cắm thực tế
#define LED_GREEN_PIN   PA27   // Đèn LED Xanh (Trạng thái an toàn) - Cắm thực tế

// Cấu hình Cảm biến siêu âm SRF05 (Radar Va Chạm)
#define TRIG_PIN        PA14   // Chân phát sóng âm
#define ECHO_PIN        PA13   // Chân nhận sóng âm
#define COLLISION_LED_PIN PB1  // Đèn LED riêng biệt báo va chạm (Vàng hoặc Đỏ 2)

// Cấu hình mức tích cực (Active Level)
// Nếu còi/đèn bị ngược (bấm Bật thì Tắt, bấm Tắt thì Bật), hãy đổi HIGH thành LOW
#define BUZZER_ACTIVE   HIGH   // Đổi lại thành HIGH (Còi hú khi nhận 3.3V)
#define LED_ACTIVE      LOW    // Giữ nguyên LOW cho LED vì LED bị ngược

// Phái sinh mức bật tắt
#define BUZZER_ON       BUZZER_ACTIVE
#define BUZZER_OFF      (!BUZZER_ACTIVE)
#define LED_ON          LED_ACTIVE
#define LED_OFF         (!LED_ACTIVE)

// Ngưỡng cảnh báo khí CO2 tự động (thang đo ADC 12-bit thô 0-4095)
const int CO2_THRESHOLD = 600;

// Trạng thái ghi đè từ MQTT (Quyền ưu tiên cao nhất)
bool mqtt_buzzer_override = false;
bool mqtt_buzzer_state    = false;
bool mqtt_led_override    = false;
bool mqtt_led_state       = false;

// ── Khởi tạo đối tượng ───────────────────────────────────
WiFiClient   wifiClient;
PubSubClient client(wifiClient);
DHT          dht(DHT_PIN, DHT_TYPE);

unsigned long lastMsg = 0;
const long    interval = 2000;   // Gửi dữ liệu MQTT mỗi 2 giây

// Biến cho Radar Va chạm (non-blocking)
long  distance_cm = -1;
unsigned long lastBlinkTime = 0;
bool  collisionBlinkState = false;

// ── Hàm trích xuất giá trị JSON đơn giản ────────────────
// Fix F-005: Dùng String thay vì VLA để an toàn trên mọi compiler
String parseJsonField(const String& json, const String& key) {
    int keyIndex = json.indexOf("\"" + key + "\"");
    if (keyIndex == -1) return "";
    int colonIndex = json.indexOf(":", keyIndex);
    if (colonIndex == -1) return "";
    int startIndex = json.indexOf("\"", colonIndex);
    int endIndex;
    if (startIndex == -1 || startIndex > json.indexOf(",", colonIndex)) {
        int commaIndex = json.indexOf(",", colonIndex);
        int braceIndex = json.indexOf("}", colonIndex);
        if (commaIndex == -1) endIndex = braceIndex;
        else if (braceIndex == -1) endIndex = commaIndex;
        else endIndex = min(commaIndex, braceIndex);
        String val = json.substring(colonIndex + 1, endIndex);
        val.trim();
        return val;
    } else {
        endIndex = json.indexOf("\"", startIndex + 1);
        if (endIndex == -1) return "";
        return json.substring(startIndex + 1, endIndex);
    }
}

// ── Hàm Callback nhận lệnh MQTT ─────────────────────────
// Fix F-005: Dùng String thay VLA char message[length+1]
void callback(char* topic, byte* payload, unsigned int length) {
    String msgString = "";
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
    } else if (command == "RESET") {
        mqtt_buzzer_override = false;
        mqtt_led_override    = false;
        Serial.println("[CONTROL] Khoi phuc che do tu dong");
    }
}

// ── Kết nối WiFi ──────────────────────────────────────────
void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.print("[WiFi] Dang ket noi toi: ");
    Serial.println(ssid);
    WiFi.begin(const_cast<char*>(ssid), const_cast<char*>(password));

    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 30) {
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
    while (!client.connected() && retry < 5) {
        Serial.print("[MQTT] Dang ket noi broker...");

        if (client.connect("BW16_Payload")) {
            Serial.println(" OK!");
            client.subscribe("drone/control/payload");
            Serial.println("[MQTT] Da subscribe: drone/control/payload");
        } else {
            Serial.print(" That bai, rc=");
            Serial.print(client.state());
            Serial.println(" - Thu lai sau 3s...");
            delay(3000);
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
    Serial.println("  v2.6 - Fixed Out-Of-Bounds Pin Bug & MQTT Blocking Mode");
    Serial.println("==============================");

    Serial.println("-> Cấu hình Buzzer (PA15)..."); delay(50);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, BUZZER_OFF);
    Serial.println("   [OK] Buzzer");

    Serial.println("-> Cấu hình LED Đỏ (PA25)..."); delay(50);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LED_OFF);
    Serial.println("   [OK] LED Đỏ");

    Serial.println("-> Cấu hình LED Xanh (PA27)..."); delay(50);
    pinMode(LED_GREEN_PIN, OUTPUT);
    digitalWrite(LED_GREEN_PIN, LED_ON);
    Serial.println("   [OK] LED Xanh");

    Serial.println("-> Cấu hình SRF05 & LED Va Chạm..."); delay(50);
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(COLLISION_LED_PIN, OUTPUT);
    digitalWrite(COLLISION_LED_PIN, LED_OFF);
    Serial.println("   [OK] Radar System");

    Serial.println("-> Cấu hình Cảm biến Khí MQ-135..."); delay(50);
    pinMode(MQ135_PIN, INPUT); // Bắt buộc set INPUT cho chân ADC trên dòng Ameba
    Serial.println("   [OK] MQ-135 ADC");

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
    client.setKeepAlive(60);

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

    client.loop();

    unsigned long now = millis();

    // ── XỬ LÝ RADAR VA CHẠM (Chạy liên tục không bị block) ──
    // 1. Kích hoạt SRF05
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    // 2. Đọc thời gian vọng (timeout 20000us tương đương ~3.4m)
    long duration = pulseIn(ECHO_PIN, HIGH, 20000);
    if (duration == 0) {
        distance_cm = -1; // Không có vật cản gần
    } else {
        distance_cm = duration * 0.034 / 2;
    }

    bool collision_alert = (distance_cm > 0 && distance_cm < 30);

    // 3. Xử lý Chớp đèn / Còi báo va chạm (Non-blocking Blink 200ms)
    if (collision_alert) {
        if (now - lastBlinkTime >= 200) {
            lastBlinkTime = now;
            collisionBlinkState = !collisionBlinkState;
            digitalWrite(COLLISION_LED_PIN, collisionBlinkState ? LED_ON : LED_OFF);
            
            // Còi hú theo nhịp nếu không bị Web chèn quyền
            if (!mqtt_buzzer_override) {
                digitalWrite(BUZZER_PIN, collisionBlinkState ? BUZZER_ON : BUZZER_OFF);
            }
        }
    } else {
        digitalWrite(COLLISION_LED_PIN, LED_OFF);
    }


    // ── XỬ LÝ ĐỌC CẢM BIẾN MÔI TRƯỜNG & GỬI MQTT (Mỗi 2 giây) ──
    if (now - lastMsg >= interval) {
        lastMsg = now;

        // Đọc DHT22 (Nhiệt độ, Độ ẩm)
        float temp = dht.readTemperature();
        float hum  = dht.readHumidity();

        // Fix F-002: Không dùng delay() trong loop — thử 1 lần rồi thôi
        // Fix F-003: Gán 0.0 khi lỗi (không phải -1.0) để UI hiển thị đúng
        bool dht_ok = true;
        if (isnan(temp) || isnan(hum)) {
            Serial.println("[DHT22] Loi doc cam bien! Kiem tra chan PA_26.");
            temp   = 0.0;
            hum    = 0.0;
            dht_ok = false;
        }

        // Đọc MQ-135 (ADC giá trị thô 0-4095)
        int mq_raw = analogRead(MQ135_PIN);

        // Xác định trạng thái cảnh báo tự động onboard
        bool is_alert = (mq_raw > CO2_THRESHOLD);

        // ── ĐIỀU KHIỂN CÒI BUZZER (Ưu tiên: MQTT > Va Chạm > Tự động Môi trường) ──
        if (mqtt_buzzer_override) {
            digitalWrite(BUZZER_PIN, mqtt_buzzer_state ? BUZZER_ON : BUZZER_OFF);
        } else if (!collision_alert) { // Chỉ gán theo Môi trường nếu không có va chạm
            digitalWrite(BUZZER_PIN, is_alert ? BUZZER_ON : BUZZER_OFF);
        }

        // ── ĐIỀU KHIỂN ĐÈN LED MÔI TRƯỜNG (Ưu tiên MQTT > Tự động) ──
        if (mqtt_led_override) {
            digitalWrite(LED_PIN,       mqtt_led_state ? LED_ON : LED_OFF);
            digitalWrite(LED_GREEN_PIN, mqtt_led_state ? LED_OFF : LED_ON);
        } else {
            digitalWrite(LED_PIN,       is_alert ? LED_ON : LED_OFF);
            digitalWrite(LED_GREEN_PIN, is_alert ? LED_OFF : LED_ON);
        }

        // Đóng gói dữ liệu JSON
        String payload = "{";
        payload += "\"temp\":"     + String(temp, 1);
        payload += ",\"humidity\":" + String(hum, 1);
        payload += ",\"co2\":"     + String(mq_raw);
        payload += ",\"alert\":"   + String(is_alert ? 1 : 0);
        payload += ",\"distance\":" + String(distance_cm);
        payload += ",\"rssi\":"    + String(WiFi.RSSI());
        payload += ",\"dht_ok\":"  + String(dht_ok ? 1 : 0);
        payload += "}";

        // Publish lên MQTT broker
        if (client.connected() && client.publish("drone/payload/sensors", payload.c_str())) {
            Serial.print("[SEND] ");
            Serial.println(payload);
        } else {
            Serial.println("[SEND] Loi! Khong the publish len MQTT.");
        }
    }
}
