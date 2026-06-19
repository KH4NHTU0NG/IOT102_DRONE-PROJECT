// ============================================================
// bw16_sensor.ino — Phase 3: Board BW16 (RTL8720DN)
// Đọc DHT22 (nhiệt độ/độ ẩm) + MQ-135 (CO2/không khí)
// Gửi JSON qua WiFi lên MQTT Broker
//
// BUG FIX #7: Thêm WiFi reconnect tự động
// BUG FIX #8: Thêm comment giải thích ADC range BW16
//
// Thư viện cần cài (Sketch → Include Library → Manage Libraries):
//   - PubSubClient (Nick O'Leary)
//   - DHT sensor library (Adafruit)
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ── Cấu hình — CHỈNH SỬA PHẦN NÀY ──────────────────────
const char* ssid        = "TEN_WIFI";          // Tên WiFi của bạn
const char* password    = "MAT_KHAU_WIFI";     // Mật khẩu WiFi
const char* mqtt_server = "192.168.1.252";     // IP máy tính (chạy: ipconfig getifaddr en0)
const int   mqtt_port   = 1883;
// ─────────────────────────────────────────────────────────

// ── Pin definitions ───────────────────────────────────────
#define DHT_PIN  PA_26   // Chân DATA của DHT22
#define DHT_TYPE DHT22
#define MQ135_PIN PB_1   // Chân AOUT của MQ-135 (ADC)

// BW16 ADC Note:
// PB_1 là ADC pin, đọc giá trị 0–4095 (12-bit ADC)
// MQ-135 AOUT cấp 0–VCC (0–5V) nhưng BW16 chỉ chịu 3.3V!
// → Dùng voltage divider (10kΩ / 10kΩ) nếu dùng AOUT trực tiếp
// → Hoặc đọc giá trị thô (0–4095) và map sang ppm ở server

// ── Khởi tạo đối tượng ───────────────────────────────────
WiFiClient   wifiClient;
PubSubClient client(wifiClient);
DHT          dht(DHT_PIN, DHT_TYPE);

unsigned long lastMsg = 0;
const long    interval = 2000;   // Gửi mỗi 2 giây

// ── Kết nối WiFi (có reconnect) ───────────────────────────
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
        Serial.println("[WiFi] That bai sau 30 lan thu. Thu lai sau 5s...");
        delay(5000);
    }
}

// ── Kết nối MQTT (có reconnect) ───────────────────────────
void connectMQTT() {
    int retry = 0;
    while (!client.connected() && retry < 5) {
        Serial.print("[MQTT] Dang ket noi broker...");

        if (client.connect("BW16_Payload")) {
            Serial.println(" OK!");
        } else {
            Serial.print(" That bai, rc=");
            Serial.print(client.state());
            /*
             * MQTT State codes:
             * -4 = MQTT_CONNECTION_TIMEOUT
             * -3 = MQTT_CONNECTION_LOST
             * -2 = MQTT_CONNECT_FAILED (IP sai hoac broker chua chay)
             * -1 = MQTT_DISCONNECTED
             *  1 = MQTT_CONNECT_BAD_PROTOCOL
             *  2 = MQTT_CONNECT_BAD_CLIENT_ID
             *  5 = MQTT_CONNECT_UNAUTHORIZED
             */
            Serial.println(" — Thu lai sau 3s...");
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
    Serial.println("  DHT22 + MQ-135 Sensor Node");
    Serial.println("==============================");

    dht.begin();

    connectWiFi();

    client.setServer(mqtt_server, mqtt_port);
    client.setKeepAlive(60);

    Serial.println("[SYSTEM] Setup hoan tat. Bat dau doc cam bien...");
}

// ── Loop ──────────────────────────────────────────────────
void loop() {
    // Bug fix #7: Tự động reconnect WiFi nếu mất kết nối
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Mat ket noi! Dang reconnect...");
        connectWiFi();
    }

    // Tự động reconnect MQTT
    if (!client.connected()) {
        connectMQTT();
    }
    client.loop();

    // Gửi dữ liệu theo interval
    unsigned long now = millis();
    if (now - lastMsg >= interval) {
        lastMsg = now;

        // Đọc DHT22
        float temp = dht.readTemperature();   // Độ C
        float hum  = dht.readHumidity();      // %

        // Kiểm tra lỗi đọc DHT22
        if (isnan(temp) || isnan(hum)) {
            Serial.println("[DHT22] Loi doc cam bien! Kiem tra day noi PA_26.");
            return;
        }

        // Đọc MQ-135 (giá trị ADC thô 0–4095)
        // BW16 ADC là 12-bit → range 0–4095
        int mq_raw = analogRead(MQ135_PIN);

        // Build JSON payload
        String payload = "{";
        payload += "\"temp\":"     + String(temp, 1);
        payload += ",\"humidity\":" + String(hum, 1);
        payload += ",\"co2\":"     + String(mq_raw);
        payload += ",\"rssi\":"    + String(WiFi.RSSI()); // Thêm WiFi signal strength
        payload += "}";

        // Publish lên MQTT
        if (client.publish("drone/payload/sensors", payload.c_str())) {
            Serial.print("[SEND] ");
            Serial.println(payload);
        } else {
            Serial.println("[SEND] Loi! Kiem tra ket noi MQTT.");
        }
    }
}
