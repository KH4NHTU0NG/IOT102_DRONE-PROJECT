// ============================================================
// servo_test.ino — Test servo SG90 trên BW16 RTL8720DN
// Upload sketch này để cô lập lỗi: không WiFi, không MQTT
// Servo sẽ quay 0° → 90° → 180° → 90° liên tục mỗi 2 giây
// Nếu vẫn re re → vấn đề 100% là PHẦN CỨNG (nguồn / dây / servo hỏng)
// ============================================================
#include <AmebaServo.h>

// ===== THỬ LẦN LƯỢT CÁC PIN NÀY NẾU PIN CŨ KHÔNG HOẠT ĐỘNG =====
// Option 1 (hiện tại):
#define SERVO_PIN  PA13   // D11

// Option 2 — thử nếu PA13 không quay:
// #define SERVO_PIN  PA26   // D8  (pin DHT22, tạm rút DHT22 ra)

// Option 3:
// #define SERVO_PIN  PA25   // D7

AmebaServo servo;

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("=== SERVO TEST ===");
    Serial.print("Servo pin: ");
    Serial.println(SERVO_PIN);

    servo.attach(SERVO_PIN);
    delay(50);  // Cho PWM ổn định

    Serial.println("Quay về 90° (trung lập)...");
    servo.write(90);
    delay(2000);
}

void loop() {
    Serial.println("[TEST] → 0°");
    servo.write(0);
    delay(2000);

    Serial.println("[TEST] → 90°");
    servo.write(90);
    delay(2000);

    Serial.println("[TEST] → 180°");
    servo.write(180);
    delay(2000);

    Serial.println("[TEST] → 90°");
    servo.write(90);
    delay(2000);
}
