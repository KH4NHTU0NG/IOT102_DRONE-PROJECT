// bw16_sensor.ino — BW16 Drone IoT Payload
// Modularized Architecture

#include "config.h"
#include "mqtt_wifi.h"
#include "display.h"

// --- Setup ---
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

    payloadServo.attach(SERVO_PIN);
    payloadServo.write(90);

    Serial.println("[INIT] GPIO OK");

    dht.begin();
    delay(500);

    connectWiFi();

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

        digitalWrite(TRIG_PIN, LOW);
        delayMicroseconds(2);
        digitalWrite(TRIG_PIN, HIGH);
        delayMicroseconds(10);
        digitalWrite(TRIG_PIN, LOW);
        long duration = pulseIn(ECHO_PIN, HIGH, 30000);
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
}
