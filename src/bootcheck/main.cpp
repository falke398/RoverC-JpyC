#include <Arduino.h>
#include <esp_system.h>

#ifdef BOOTCHECK_M5
#include <M5Unified.h>
#endif

void setup() {
    Serial.begin(115200);
    Serial.printf("\nBOOTCHECK: setup reached; reset reason=%d\n", int(esp_reset_reason()));
    Serial.flush();
#ifdef BOOTCHECK_M5
    Serial.println("DISPLAYCHECK: before M5Unified.begin");
    Serial.flush();
    auto config = M5.config();
    config.serial_baudrate = 0;
    config.external_imu = false;
    M5.begin(config);
    Serial.println("DISPLAYCHECK: M5Unified.begin returned");
    Serial.flush();
    M5.Display.setRotation(1);
    M5.Display.fillScreen(BLUE);
    M5.Display.setTextColor(WHITE, BLUE);
    M5.Display.setTextSize(2);
    M5.Display.setCursor(8, 20);
    M5.Display.println("DISPLAY CHECK");
    Serial.println("DISPLAYCHECK: screen drawn");
    Serial.flush();
#endif
}

void loop() {
#ifdef BOOTCHECK_M5
    Serial.printf("DISPLAYCHECK: alive; uptime=%lu ms\n", static_cast<unsigned long>(millis()));
#else
    Serial.printf("BOOTCHECK: alive; uptime=%lu ms\n", static_cast<unsigned long>(millis()));
#endif
    delay(1000);
}