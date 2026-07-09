#include "display.h"

// --- OLED Display ---
void updateOLED(bool env_alert) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // 1. Dòng 1: WiFi/MQTT Status & Flight Mode
    display.setCursor(0, 0);
    display.print(WiFi.status() == WL_CONNECTED ? "W:OK" : "W:NO");
    display.print(client.connected() ? " M:OK" : " M:NO");
    display.print("|");
    
    bool link_lost = (millis() - lastTelemetryTime > 5000);
    if (link_lost) {
        display.print("L-LOST");
    } else {
        display.print(flight_mode);
        display.print(drone_armed ? "*" : "");
    }

    display.drawLine(0, 9, 128, 9, SSD1306_WHITE);

    // 2. Dòng 2: Độ cao (ALT) và Tốc độ bay (SPD)
    display.setCursor(0, 12);
    display.print("ALT:");
    if (link_lost) display.print("--");
    else { display.print(flight_alt, 1); display.print("m"); }
    
    display.setCursor(64, 12);
    display.print("SPD:");
    if (link_lost) display.print("--");
    else { display.print(flight_spd, 1); display.print("m/s"); }

    // 3. Dòng 3: Điện áp PIN (BATT) và Gió (WND)
    display.setCursor(0, 22);
    display.print("BAT:");
    if (link_lost) display.print("--");
    else { display.print(flight_batt, 1); display.print("V"); }
    
    display.setCursor(64, 22);
    display.print("WND:");
    if (link_lost) display.print("--");
    else { display.print(flight_wind, 1); display.print("m/s"); }

    // 4. Dòng 4: Nhiệt độ & CO2
    display.setCursor(0, 32);
    display.print("Tmp:");
    display.print(temp_val, 1);
    display.print("C");

    display.setCursor(64, 32);
    display.print("CO2:");
    display.print(mq_raw_val);

    // 5. Dòng 5: Sonar Distance & Air Quality
    display.setCursor(0, 42);
    display.print("Dst:");
    if (sonar_dist < 0) display.print("ERR");
    else { display.print(sonar_dist, 1); display.print("cm"); }

    display.setCursor(64, 42);
    display.print("Air:");
    display.print(env_alert ? "WARN" : "SAFE");

    display.drawLine(0, 52, 128, 52, SSD1306_WHITE);

    // 6. Dòng 6: Trạng thái tổng
    display.setCursor(0, 55);
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
