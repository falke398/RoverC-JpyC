#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <ControlCore.h>

namespace hardware {

struct JoyState {
    std::array<uint8_t, 4> axes{};
    uint8_t buttons = 0;
    bool left() const { return (buttons & 0x10) != 0; }
    bool right() const { return (buttons & 0x01) != 0; }
};

class Hat {
public:
    bool begin();
    bool present();
    bool joy(JoyState& state);
    bool motors(const control::Wheels& speeds);
    bool servo(uint8_t channel, uint8_t angle);
private:
    bool write(uint8_t reg, const uint8_t* bytes, size_t length);
};

}