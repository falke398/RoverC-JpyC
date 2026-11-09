#pragma once
#include <Arduino.h>
#include <cstdarg>
#include <esp_system.h>

// Timestamped one-line serial log, read with `platformio device monitor -b 115200`.
namespace debug {
inline unsigned long ul(uint32_t value) { return static_cast<unsigned long>(value); }

inline void log(const char* format, ...) __attribute__((format(printf, 1, 2)));
inline void log(const char* format, ...) {
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    Serial.printf("[%lu] %s\n", ul(millis()), text);
}

inline const char* resetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXT";
        case ESP_RST_SW: return "SW";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        default: return "OTHER";
    }
}
}
