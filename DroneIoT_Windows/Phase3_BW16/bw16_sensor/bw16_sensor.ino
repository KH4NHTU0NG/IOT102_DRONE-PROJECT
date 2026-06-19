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

const char* ssid        = "TuongHuy";          // Tên WiFi của bạn
const char* password    = "kminh1983";     // Mật khẩu WiFi
const char* mqtt_server = "192.168.1.120";     // IP máy tính chạy Broker
const int   mqtt_port   = 1883;

// ── Pin definitions ───────────────────────────────────────
#define DHT_PIN    PA_26   // Chân DATA của DHT22
#define DHT_TYPE   DHT22
#define MQ135_PIN  PB_1    // Chân AOUT của MQ-135 (ADC)

// Định nghĩa chân còi và LED thực tế trên board (tránh chân PA_12 trùng TX Log Console)
#define BUZZER_PIN    PA_15  // Còi Buzzer (Active High)
#define LED_PIN       PA_30  // LED Đỏ (Cảnh báo / LED_ON)
#define LED_GREEN_PIN PA_27  // LED Xanh (An toàn / LED_OFF)

// Ngưỡng cảnh báo khí CO2 tự động (thang đo ADC 12-bit thô 0-4095)
const int CO2_THRESHOLD = 600;

// Các trạng thái ghi đè từ MQTT (Quyền ưu tiên cao nhất)
bool mqtt_buzzer_override = false;
bool mqtt_buzzer_state    = false;
bool mqtt_led_override    = false;
bool mqtt_led_state       = false;

// ── Khởi tạo đối tượng ───────────────────────────────────
WiFiClient   wifiClient;
PubSubClient client(wifiClient);
DHT          dht(DHT_PIN, DHT_TYPE);

unsigned long lastMsg = 0;
const long    interval = 2000;   // Gửi dữ liệu mỗi 2 giây (không chặn)

// ── Hàm trích xuất giá trị JSON đơn giản không dùng thư viện ngoài ──
String parseJsonField(String json, String key) {
    int keyIndex = json.indexOf("\"" + key + "\"");
    if (keyIndex == -1) return "";
    int colonIndex = json.indexOf(":", keyIndex);
    if (colonIndex == -1) return "";
    int startIndex = json.indexOf("\"", colonIndex);
    int endIndex;
    if (startIndex == -1 || startIndex > json.indexOf(",", colonIndex)) {
        // Giá trị số hoặc boolean (không có dấu ngoặc kép)
        int commaIndex = json.indexOf(",", colonIndex);
        int braceIndex = json.indexOf("}", colonIndex);
        if (commaIndex == -1) endIndex = braceIndex;
        else if (braceIndex == -1) endIndex = commaIndex;
        else endIndex = min(commaIndex, braceIndex);
        
        String val = json.substring(colonIndex + 1, endIndex);
        val.trim();
        return val;
    } else {
        // Giá trị chuỗi (nằm trong dấu ngoặc kép)
        endIndex = json.indexOf("\"", startIndex + 1);
        if (endIndex == -1) return "";
        return json.substring(startIndex + 1, endIndex);
    }
}

// ── Hàm Callback nhận lệnh MQTT ─────────────────────────────
void callback(char* topic, byte* payload, unsigned int length) {
    char message[length + 1];
    memcpy(message, payload, length);
    message[length] = '\0';

    Serial.print("[MQTT] Nhận lệnh từ topic [");
    Serial.print(topic);
    Serial.print("]: ");
    Serial.println(message);

    String msgString = String(message);
    String command = parseJsonField(msgString, "command");

    if (command == "BUZZER_ON") {
        mqtt_buzzer_override = true;
        mqtt_buzzer_state    = true;
        digitalWrite(BUZZER_PIN, HIGH);
        Serial.println("[CONTROL] MQTT override: BẬT CÒI");
    } else if (command == "BUZZER_OFF") {
        mqtt_buzzer_override = true;
        mqtt_buzzer_state    = false;
        digitalWrite(BUZZER_PIN, LOW);
        Serial.println("[CONTROL] MQTT override: TẮT CÒI");
    } else if (command == "LED_ON") {
        mqtt_led_override = true;
        mqtt_led_state    = true;
        digitalWrite(LED_PIN, HIGH);
        digitalWrite(LED_GREEN_PIN, LOW);
        Serial.println("[CONTROL] MQTT override: BẬT LED ĐỎ (Cảnh báo)");
    } else if (command == "LED_OFF") {
        mqtt_led_override = true;
        mqtt_led_state    = false;
        digitalWrite(LED_PIN, LOW);
        digitalWrite(LED_GREEN_PIN, HIGH);
        Serial.println("[CONTROL] MQTT override: BẬT LED XANH (An toàn)");
    } else if (command == "RESET") {
        mqtt_buzzer_override = false;
        mqtt_led_override    = false;
        Serial.println("[CONTROL] Khôi phục chế độ tự động");
    }
}

// ── Kết nối WiFi (có tự động reconnect) ───────────────────────
void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.print("[WiFi] Đang kết nối tới: ");
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
        Serial.print("[WiFi] Kết nối thành công! IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println();
        Serial.println("[WiFi] Thất bại sau 30 lần thử. Sẽ thử lại sau...");
    }
}

// ── Kết nối MQTT (có tự động reconnect) ───────────────────────
void connectMQTT() {
    int retry = 0;
    while (!client.connected() && retry < 5) {
        Serial.print("[MQTT] Đang kết nối broker...");

        if (client.connect("BW16_Payload")) {
            Serial.println(" OK!");
            // Đăng ký topic nhận lệnh điều khiển còi/LED từ Web/Gateway
            client.subscribe("drone/control/payload"); 
            Serial.println("[MQTT] Đã subscribe topic: drone/control/payload");
        } else {
            Serial.print(" Thất bại, rc=");
            Serial.print(client.state());
            Serial.println(" — Thử lại sau 3s...");
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
    Serial.println("==============================");

    // [DEBUG 1] Kiểm tra GPIO
    Serial.println("[DEBUG 1] Cau hinh GPIO...");
    pinMode(LED_PIN, OUTPUT);
    pinMode(LED_GREEN_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(LED_GREEN_PIN, HIGH);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("[DEBUG 1] GPIO OK");

    // [DEBUG 2] Khởi tạo DHT22 (an toàn kể cả khi chưa cắm)
    Serial.println("[DEBUG 2] Khoi tao DHT22...");
    dht.begin();
    delay(500); // Chờ DHT ổn định
    Serial.println("[DEBUG 2] DHT22 init xong");

    // [DEBUG 3] Kết nối WiFi
    Serial.println("[DEBUG 3] Bat dau ket noi WiFi...");
    connectWiFi();
    Serial.println("[DEBUG 3] WiFi done");

    // [DEBUG 4] Cấu hình MQTT
    Serial.println("[DEBUG 4] Cau hinh MQTT...");
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    client.setKeepAlive(60);
    Serial.println("[DEBUG 4] MQTT config xong");

    Serial.println("[SYSTEM] Setup hoan tat. Bat dau doc cam bien...");
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

    // Đọc cảm biến và gửi dữ liệu không chặn bằng millis()
    unsigned long now = millis();
    if (now - lastMsg >= interval) {
        lastMsg = now;

        // Đọc DHT22 (Nhiệt độ, Độ ẩm)
        float temp = dht.readTemperature();
        float hum  = dht.readHumidity();

        // Kiểm tra lỗi cảm biến DHT22
        if (isnan(temp) || isnan(hum)) {
            // Cảm biến chưa cắm hoặc lỗi — thử đọc lại 1 lần
            delay(200);
            temp = dht.readTemperature();
            hum  = dht.readHumidity();
        }
        if (isnan(temp) || isnan(hum)) {
            Serial.println("[DHT22] Chua cam cam bien hoac loi! Kiem tra chan PA_26.");
            temp = -1.0; // -1 để phân biệt "chưa cắm" vs "đang bình thường 0 độ"
            hum  = -1.0;
        }

        // Đọc MQ-135 (ADC giá trị thô 0-4095)
        int mq_raw = analogRead(MQ135_PIN);

        // Xác định trạng thái cảnh báo tự động onboard
        bool is_alert = (mq_raw > CO2_THRESHOLD);

        // ── ĐIỀU KHIỂN CÒI BUZZER (Ưu tiên MQTT > Tự động) ──
        if (mqtt_buzzer_override) {
            digitalWrite(BUZZER_PIN, mqtt_buzzer_state ? HIGH : LOW);
        } else {
            // Tự động kêu còi khi CO2 vượt ngưỡng
            if (is_alert) {
                digitalWrite(BUZZER_PIN, HIGH);
            } else {
                digitalWrite(BUZZER_PIN, LOW);
            }
        }

        // ── ĐIỀU KHIỂN ĐÈN LED (Ưu tiên MQTT > Tự động) ──
        if (mqtt_led_override) {
            if (mqtt_led_state) {
                digitalWrite(LED_PIN, HIGH);
                digitalWrite(LED_GREEN_PIN, LOW);
            } else {
                digitalWrite(LED_PIN, LOW);
                digitalWrite(LED_GREEN_PIN, HIGH);
            }
        } else {
            // Tự động bật LED đỏ nếu có cảnh báo tự động, ngược lại xanh
            if (is_alert) {
                digitalWrite(LED_PIN, HIGH);
                digitalWrite(LED_GREEN_PIN, LOW);
            } else {
                digitalWrite(LED_PIN, LOW);
                digitalWrite(LED_GREEN_PIN, HIGH);
            }
        }

        // Đóng gói dữ liệu JSON
        String payload = "{";
        payload += "\"temp\":"     + String(temp, 1);
        payload += ",\"humidity\":" + String(hum, 1);
        payload += ",\"co2\":"     + String(mq_raw);
        payload += ",\"alert\":"   + String(is_alert ? 1 : 0);
        payload += ",\"rssi\":"    + String(WiFi.RSSI());
        payload += "}";

        // Publish lên MQTT broker
        if (client.connected() && client.publish("drone/payload/sensors", payload.c_str())) {
            Serial.print("[SEND] Topic [drone/payload/sensors]: ");
            Serial.println(payload);
        } else {
            Serial.println("[SEND] Lỗi! Không thể publish dữ liệu lên MQTT.");
        }
    }
}
