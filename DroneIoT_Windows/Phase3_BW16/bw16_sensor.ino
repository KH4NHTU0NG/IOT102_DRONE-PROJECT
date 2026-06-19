// ============================================================
// bw16_sensor.ino — Phase 3: Board BW16 (RTL8720DN)
// Phiên bản: Windows (code giống macOS, chỉ khác hướng dẫn IP)
//
// BUG FIX #7: Thêm WiFi reconnect tự động
// BUG FIX #8: Thêm comment giải thích ADC range BW16
//
// Thư viện cần cài (Sketch → Include Library → Manage Libraries):
//   - PubSubClient (Nick O'Leary)
//   - DHT sensor library (Adafruit)
//
// LẤY IP MÁY TÍNH WINDOWS:
//   Mở CMD, gõ: ipconfig
//   Tìm dòng "IPv4 Address" của card WiFi đang dùng
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ── Cấu hình — CHỈNH SỬA PHẦN NÀY ──────────────────────
const char* ssid        = "TEN_WIFI";          // Tên WiFi của bạn
const char* password    = "MAT_KHAU_WIFI";     // Mật khẩu WiFi
const char* mqtt_server = "192.168.1.100";     // IP máy Windows (cmd: ipconfig)
const int   mqtt_port   = 1883;
// ─────────────────────────────────────────────────────────

// ── Pin definitions ───────────────────────────────────────
#define DHT_PIN  PA_26
#define DHT_TYPE DHT22
#define MQ135_PIN PB_1

// BW16 ADC: 12-bit, range 0–4095
// MQ-135 AOUT: 0–5V → dùng voltage divider (10kΩ/10kΩ) để giảm xuống 0–2.5V
// Nếu không có divider, đọc giá trị thô và chuẩn hóa ở server

WiFiClient   wifiClient;
PubSubClient client(wifiClient);
DHT          dht(DHT_PIN, DHT_TYPE);

unsigned long lastMsg = 0;
const long    interval = 2000;

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;
    Serial.print("[WiFi] Ket noi toi: ");
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
        Serial.print("[WiFi] OK - IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n[WiFi] That bai, thu lai...");
        delay(5000);
    }
}

void connectMQTT() {
    int retry = 0;
    while (!client.connected() && retry < 5) {
        Serial.print("[MQTT] Dang ket noi...");
        if (client.connect("BW16_Payload_Win")) {
            Serial.println(" OK!");
        } else {
            Serial.print(" That bai rc=");
            Serial.print(client.state());
            Serial.println(" - thu lai 3s...");
            delay(3000);
        }
        retry++;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("=== BW16 Drone IoT (Windows host) ===");
    dht.begin();
    connectWiFi();
    client.setServer(mqtt_server, mqtt_port);
    client.setKeepAlive(60);
    Serial.println("[OK] Setup xong. Bat dau gui du lieu...");
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Mat ket noi! Reconnecting...");
        connectWiFi();
    }
    if (!client.connected()) connectMQTT();
    client.loop();

    unsigned long now = millis();
    if (now - lastMsg >= interval) {
        lastMsg = now;
        float temp = dht.readTemperature();
        float hum  = dht.readHumidity();
        if (isnan(temp) || isnan(hum)) {
            Serial.println("[DHT22] Loi doc! Kiem tra day noi PA_26.");
            return;
        }
        int mq_raw = analogRead(MQ135_PIN);
        String payload = "{";
        payload += "\"temp\":"      + String(temp, 1);
        payload += ",\"humidity\":" + String(hum, 1);
        payload += ",\"co2\":"      + String(mq_raw);
        payload += ",\"rssi\":"     + String(WiFi.RSSI());
        payload += "}";
        if (client.publish("drone/payload/sensors", payload.c_str())) {
            Serial.print("[SEND] ");
            Serial.println(payload);
        } else {
            Serial.println("[SEND] FAIL! Kiem tra MQTT.");
        }
    }
}
