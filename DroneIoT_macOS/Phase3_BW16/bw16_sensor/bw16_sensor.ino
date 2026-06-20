// ============================================================
// bw16_sensor.ino — Phase 3: Board BW16 (RTL8720DN)
// Đọc cảm biến DHT22 + MQ-135, đồng bộ trạng thái drone ảo lên OLED,
// điều khiển cơ cấu nhả phao Servo SG90, vòng LED RGB và giọng nói DFPlayer.
//
// Thư viện cần cài trong Arduino IDE:
//   - PubSubClient (Nick O'Leary)
//   - DHT sensor library (Adafruit)
//   - Adafruit GFX Library (Adafruit)
//   - Adafruit SSD1306 (Adafruit)
//   - Adafruit NeoPixel (Adafruit)
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>
#include <SoftwareSerial.h>
#include <AmebaServo.h>

// ── Cấu hình kết nối ─────────────────────────────────────
const char* ssid        = "TuongHuy";       // Tên WiFi
const char* password    = "kminh1983";      // Mật khẩu WiFi
const char* mqtt_server = "192.168.1.120";  // IP máy chạy MQTT Broker
const int   mqtt_port   = 1883;

// ── Pin definitions ───────────────────────────────────────
#define DHT_PIN       PA_14   // DATA của DHT22 (Chuyển từ PA_26 để tránh xung đột I2C)
#define DHT_TYPE      DHT22
#define MQ135_PIN     PB_1    // AOUT của MQ-135 (ADC)
#define BUZZER_PIN    PA_15   // Còi Buzzer (Dự phòng)
#define SERVO_PIN     PA_12   // Chân tín hiệu điều khiển Servo SG90
#define NEOPIXEL_PIN  PA_13   // Chân tín hiệu điều khiển LED Ring WS2812B
#define MP3_RX_PIN    PA_27   // RX của SoftwareSerial (kết nối với TX của DFPlayer)
#define MP3_TX_PIN    PB_3    // TX của SoftwareSerial (kết nối với RX của DFPlayer qua trở 1k)

// ── Khởi tạo màn hình OLED SSD1306 ───────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1     // Share RESET pin (hoặc -1 nếu không dùng reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── Khởi tạo LED Ring ────────────────────────────────────
#define NUMPIXELS      8      // Thay đổi thành số lượng bóng LED thực tế (8 hoặc 12)
Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// ── Khởi tạo Servo ──────────────────────────────────────
AmebaServo payloadServo;

// ── Khởi tạo SoftwareSerial cho DFPlayer ─────────────────
SoftwareSerial mp3Serial(MP3_RX_PIN, MP3_TX_PIN); // RX, TX

// ── Biến trạng thái hệ thống ─────────────────────────────
const int CO2_THRESHOLD = 600;

enum SystemState {
  STATE_NORMAL,
  STATE_ALARM,
  STATE_DROPPING,
  STATE_RESETTING
};
SystemState current_state = STATE_NORMAL;

// Trạng thái điều khiển thủ công qua MQTT (Ưu tiên cao nhất)
bool mqtt_buzzer_override = false;
bool mqtt_buzzer_state    = false;

// Trạng thái Drone ảo (Cập nhật từ gateway)
bool   drone_armed = false;
String drone_mode  = "DISARMED";
float  drone_alt   = 0.0;

// Trạng thái chốt thả phao
bool payload_released = false;

// ── Khởi tạo đối tượng ───────────────────────────────────
WiFiClient   wifiClient;
PubSubClient client(wifiClient);
DHT          dht(DHT_PIN, DHT_TYPE);

unsigned long lastMsg = 0;
const long    interval = 2000;   // Gửi dữ liệu mỗi 2 giây

// ── Hàm gửi lệnh Hex tới DFPlayer Mini ────────────────────
void sendMp3Cmd(byte cmd, byte param1, byte param2) {
  uint16_t checksum = 0xFFFF - (0xFF + 0x06 + cmd + 0x00 + param1 + param2) + 1;
  byte packet[10] = {
    0x7E,                       // Start byte
    0xFF,                       // Version
    0x06,                       // Length
    cmd,                        // Command
    0x00,                       // Feedback (0x00 = no feedback)
    param1,                     // Parameter High
    param2,                     // Parameter Low
    (byte)(checksum >> 8),      // Checksum High
    (byte)(checksum & 0xFF),    // Checksum Low
    0xEF                        // End byte
  };
  for (int i = 0; i < 10; i++) {
    mp3Serial.write(packet[i]);
  }
}

// ── Hàm trích xuất giá trị JSON đơn giản ────────────────
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

// ── Hàm cập nhật hiển thị OLED ───────────────────────────
void drawOLED(float temp, float hum, int mq_raw, bool is_alert) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Dòng 1: WiFi & RSSI
    display.setCursor(0, 0);
    if (WiFi.status() == WL_CONNECTED) {
        display.print("WiFi: OK [");
        display.print(WiFi.RSSI());
        display.print("dBm]");
    } else {
        display.print("WiFi: LOI");
    }
    
    // Dòng 2: Cảm biến DHT22
    display.setCursor(0, 16);
    display.print("Temp: ");
    display.print(temp, 1);
    display.print("C Hum: ");
    display.print(hum, 0);
    display.print("%");
    
    // Dòng 3: Cảm biến Khí gas MQ-135
    display.setCursor(0, 32);
    display.print("Air: ");
    display.print(mq_raw);
    display.print(is_alert ? " [BAO DONG]" : " [AN TOAN]");
    
    // Dòng 4: Telemetry Drone ảo
    display.setCursor(0, 48);
    display.print("Drone: ");
    if (drone_armed) {
        display.print("ARM (");
        display.print(drone_mode);
        display.print(") ");
        display.print(drone_alt, 1);
        display.print("m");
    } else {
        display.print("DISARMED");
    }
    
    // Dòng 5: Trạng thái chốt nhả phao
    display.setCursor(0, 56);
    display.print("Phao: ");
    if (payload_released) {
        display.print("DA THA [SERVO 90]");
    } else {
        display.print("READY  [SERVO 0]");
    }
    
    display.display();
}

// ── Hàm hiệu ứng vòng LED NeoPixel (Không chặn - Non-blocking) ──
void animateLEDs() {
    static unsigned long lastUpdate = 0;
    static int pixelIndex = 0;
    unsigned long now = millis();
    
    if (current_state == STATE_NORMAL) {
        // Hiệu ứng "nhịp thở" màu xanh lá
        if (now - lastUpdate > 30) {
            lastUpdate = now;
            static int brightness = 10;
            static int fadeAmount = 2;
            brightness += fadeAmount;
            if (brightness <= 5 || brightness >= 80) {
                fadeAmount = -fadeAmount;
            }
            for (int i = 0; i < NUMPIXELS; i++) {
                pixels.setPixelColor(i, pixels.Color(0, brightness, 0));
            }
            pixels.show();
        }
    } else if (current_state == STATE_ALARM) {
        // Nháy đỏ dồn dập
        if (now - lastUpdate > 200) {
            lastUpdate = now;
            static bool toggle = false;
            toggle = !toggle;
            for (int i = 0; i < NUMPIXELS; i++) {
                pixels.setPixelColor(i, toggle ? pixels.Color(180, 0, 0) : pixels.Color(0, 0, 0));
            }
            pixels.show();
        }
    } else if (current_state == STATE_DROPPING) {
        // Đèn đuổi vòng màu cam
        if (now - lastUpdate > 80) {
            lastUpdate = now;
            pixels.clear();
            pixels.setPixelColor(pixelIndex, pixels.Color(200, 60, 0));
            pixels.setPixelColor((pixelIndex + 1) % NUMPIXELS, pixels.Color(230, 90, 0));
            pixels.setPixelColor((pixelIndex + 2) % NUMPIXELS, pixels.Color(255, 120, 0));
            pixels.show();
            pixelIndex = (pixelIndex + 1) % NUMPIXELS;
        }
    } else if (current_state == STATE_RESETTING) {
        // Nháy xanh dương xác nhận
        if (now - lastUpdate > 100) {
            lastUpdate = now;
            static int flashCount = 0;
            static bool toggle = false;
            toggle = !toggle;
            for (int i = 0; i < NUMPIXELS; i++) {
                pixels.setPixelColor(i, toggle ? pixels.Color(0, 0, 180) : pixels.Color(0, 0, 0));
            }
            pixels.show();
            flashCount++;
            if (flashCount > 10) { // 5 lần chớp nháy
                flashCount = 0;
                current_state = STATE_NORMAL;
            }
        }
    }
}

// ── Hàm Callback nhận lệnh MQTT ─────────────────────────
void callback(char* topic, byte* payload, unsigned int length) {
    String msgString = "";
    for (unsigned int i = 0; i < length; i++) {
        msgString += (char)payload[i];
    }

    Serial.print("[MQTT] Nhan tu [");
    Serial.print(topic);
    Serial.print("]: ");
    Serial.println(msgString);

    // 1. Xử lý dữ liệu đồng bộ Telemetry của Drone ảo
    if (String(topic) == "drone/status/telemetry") {
        String armedStr = parseJsonField(msgString, "armed");
        String modeStr  = parseJsonField(msgString, "mode");
        String altStr   = parseJsonField(msgString, "alt");

        if (armedStr == "true" || armedStr == "1") {
            drone_armed = true;
        } else {
            drone_armed = false;
        }
        if (modeStr != "") drone_mode = modeStr;
        if (altStr != "")  drone_alt  = altStr.toFloat();
        return;
    }

    // 2. Xử lý lệnh điều khiển thiết bị
    if (String(topic) == "drone/control/payload") {
        String command = parseJsonField(msgString, "command");

        if (command == "DROP") {
            payload_released = true;
            current_state = STATE_DROPPING;
            payloadServo.write(90); // Xoay Servo mở chốt thả phao
            sendMp3Cmd(0x12, 0x00, 2); // Phát file nhạc số 2 (Cảnh báo thả phao)
            Serial.println("[PAYLOAD] Kich hoat tha phao cuu sinh!");
        } else if (command == "RESET_PAYLOAD") {
            payload_released = false;
            current_state = STATE_RESETTING;
            payloadServo.write(0);  // Xoay Servo đóng chốt về 0 độ
            sendMp3Cmd(0x12, 0x00, 3); // Phát file nhạc số 3 (Đã reset chốt thả)
            Serial.println("[PAYLOAD] Reset chot tha phao ve vi tri khoa.");
        } else if (command == "BUZZER_ON") {
            mqtt_buzzer_override = true;
            mqtt_buzzer_state    = true;
            digitalWrite(BUZZER_PIN, HIGH);
        } else if (command == "BUZZER_OFF") {
            mqtt_buzzer_override = true;
            mqtt_buzzer_state    = false;
            digitalWrite(BUZZER_PIN, LOW);
        } else if (command == "RESET") {
            mqtt_buzzer_override = false;
            current_state = STATE_NORMAL;
            payloadServo.write(0);
            payload_released = false;
            Serial.println("[CONTROL] Khoi phuc che do tu dong");
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
        Serial.println("[WiFi] That bai sau 30 lan thu.");
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
            client.subscribe("drone/status/telemetry");
            Serial.println("[MQTT] Da dang ky cac topic dieu khien & telemetry");
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
    Serial.println("  BW16 Drone IoT - Extended");
    Serial.println("  SSD1306 + SG90 + WS2812B + DFPlayer");
    Serial.println("==============================");

    // Cấu hình còi
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // Khởi tạo I2C và Màn hình OLED
    Wire.begin(); // Sử dụng chân mặc định SCL=PA_25, SDA=PA_26
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Địa chỉ I2C thông dụng của OLED là 0x3C
        Serial.println(F("[OLED] SSD1306 init that bai!"));
    } else {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("System Booting...");
        display.display();
        Serial.println("[INIT] OLED Screen OK");
    }

    // Khởi tạo LED Ring WS2812B
    pixels.begin();
    pixels.setBrightness(40); // Đặt độ sáng vừa phải tránh sụt áp nguồn
    pixels.clear();
    pixels.show();
    Serial.println("[INIT] WS2812B LED Ring OK");

    // Khởi tạo Servo SG90
    payloadServo.attach(SERVO_PIN);
    payloadServo.write(0); // Chốt ban đầu khóa ở góc 0 độ
    Serial.println("[INIT] SG90 Servo OK");

    // Khởi tạo SoftwareSerial cho DFPlayer Mini
    mp3Serial.begin(9600); // DFPlayer Mini giao tiếp ở baudrate mặc định 9600
    delay(500);
    sendMp3Cmd(0x06, 0x00, 20); // Thiết lập âm lượng ở mức 20 (max 30)
    delay(100);
    sendMp3Cmd(0x12, 0x00, 1); // Phát file nhạc số 1 (Chào mừng hệ thống hoạt động)
    Serial.println("[INIT] DFPlayer Mini MP3 OK");

    // Khởi tạo DHT22
    dht.begin();
    delay(500);

    // Kết nối WiFi
    connectWiFi();

    // Cấu hình MQTT
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
    client.setKeepAlive(60);

    Serial.println("[SYSTEM] Setup hoan tat!");
}

// ── Loop ──────────────────────────────────────────────────
void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }

    if (WiFi.status() == WL_CONNECTED && !client.connected()) {
        connectMQTT();
    }

    client.loop();

    // Cập nhật hiệu ứng LED (Non-blocking)
    animateLEDs();

    // Đọc cảm biến và gửi dữ liệu định kỳ
    unsigned long now = millis();
    if (now - lastMsg >= interval) {
        lastMsg = now;

        // Đọc cảm biến DHT22
        float temp = dht.readTemperature();
        float hum  = dht.readHumidity();
        bool dht_ok = true;
        if (isnan(temp) || isnan(hum)) {
            temp   = 0.0;
            hum    = 0.0;
            dht_ok = false;
        }

        // Đọc cảm biến khí gas MQ-135 (ADC thô)
        int mq_raw = analogRead(MQ135_PIN);

        // Logic báo động tự động dựa trên cảm biến khí gas thật
        bool is_alert = (mq_raw > CO2_THRESHOLD);

        if (is_alert) {
            if (current_state == STATE_NORMAL) {
                current_state = STATE_ALARM;
                sendMp3Cmd(0x12, 0x00, 4); // Phát file nhạc báo động số 4 (Cảnh báo khí gas nguy hiểm)
            }
        } else {
            if (current_state == STATE_ALARM) {
                current_state = STATE_NORMAL;
            }
        }

        // Điều khiển còi (Ưu tiên lệnh MQTT > Tự động)
        if (mqtt_buzzer_override) {
            digitalWrite(BUZZER_PIN, mqtt_buzzer_state ? HIGH : LOW);
        } else {
            digitalWrite(BUZZER_PIN, is_alert ? HIGH : LOW);
        }

        // Cập nhật màn hình OLED SSD1306
        drawOLED(temp, hum, mq_raw, is_alert);

        // Đóng gói JSON gửi dữ liệu
        String payload = "{";
        payload += "\"temp\":"      + String(temp, 1);
        payload += ",\"humidity\":"  + String(hum, 1);
        payload += ",\"co2\":"      + String(mq_raw);
        payload += ",\"alert\":"    + String(is_alert ? 1 : 0);
        payload += ",\"rssi\":"     + String(WiFi.RSSI());
        payload += ",\"payload_released\":" + String(payload_released ? 1 : 0);
        payload += ",\"dht_ok\":"   + String(dht_ok ? 1 : 0);
        payload += "}";

        if (client.connected() && client.publish("drone/payload/sensors", payload.c_str())) {
            Serial.print("[SEND] ");
            Serial.println(payload);
        } else {
            Serial.println("[SEND] Loi! Khong the publish len MQTT.");
        }
    }
}
