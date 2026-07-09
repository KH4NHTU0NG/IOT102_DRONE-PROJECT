#include "mqtt_wifi.h"

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

        String rollStr = parseJsonField(msgString, "roll");
        if (rollStr.length() > 0) {
            float roll = rollStr.toFloat();
            // Convert roll (radians) to angle (degrees) and map to servo
            // Negative roll (left turn) -> Servo looks left (< 90)
            // Positive roll (right turn) -> Servo looks right (> 90)
            int angle = 90 + (roll * 180.0 / 3.14159);
            // Constrain between 45 and 135 degrees to prevent wire tangling
            angle = constrain(angle, 45, 135);
            payloadServo.write(angle);
        }

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

