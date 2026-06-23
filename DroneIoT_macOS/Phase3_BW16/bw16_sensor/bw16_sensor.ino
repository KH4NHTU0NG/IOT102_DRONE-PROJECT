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

// ── Cấu hình kết nối ─────────────────────────────────────
// !! Sửa SSID và PASSWORD thành WiFi của bạn !!
const char* ssid        = "TuongHuy";       // Tên WiFi (phân biệt hoa thường)
const char* password    = "kminh1983";      // Mật khẩu WiFi
const char* mqtt_server = "192.168.1.185";  // IP máy Mac chạy Mosquitto broker
const int   mqtt_port   = 1883;

// ── Pin definitions ───────────────────────────────────────
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
    } else if (command == "SERVO") {
        String angleStr = parseJsonField(msgString, "angle");
        if (angleStr.length() > 0) {
            int angle = angleStr.toInt();
            payloadServo.write(angle);
            Serial.print("[CONTROL] Đã quay Servo góc: ");
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

    
    pinMode(COLLISION_LED_PIN, OUTPUT);
    digitalWrite(COLLISION_LED_PIN, LED_OFF);
    
    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    pinMode(MQ135_PIN, INPUT);

    Serial.println("-> Khoi tao man hinh OLED...");
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
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
    bool is_alert = false;
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
        int mq_raw = analogRead(MQ135_PIN);

        // Xác định trạng thái cảnh báo tự động onboard
        if (now > 30000) {
            is_alert = (mq_raw > CO2_THRESHOLD);
        }

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
        payload += "\"temp\":"     + String(temp_val, 1);
        payload += ",\"humidity\":" + String(hum_val, 1);
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

    // ── Cập nhật màn hình OLED (5fps) ──
    if (now - lastOLEDUpdate > oledInterval) {
        lastOLEDUpdate = now;
        updateOLED(is_alert, collision_alert);
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
    display.print("CO2 : "); display.print(analogRead(MQ135_PIN)); display.println(" ADC");
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
