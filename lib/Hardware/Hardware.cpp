#include "Hardware.h"
#include <ProjectConfig.h>
#include <algorithm>

namespace hardware {

bool Hat::begin() {
    const bool started = Wire.begin(settings::Sda, settings::Scl, 100000);
    Wire.setTimeOut(10);
    return started && present();
}

bool Hat::present() {
    Wire.beginTransmission(settings::Address);
    return Wire.endTransmission() == 0;
}

bool Hat::fail(uint8_t code) {
    lastError_ = code;
    ++failures_;
    return false;
}

bool Hat::write(uint8_t reg, const uint8_t* bytes, size_t length) {
    Wire.beginTransmission(settings::Address);
    const bool queued = Wire.write(reg) == 1 && Wire.write(bytes, length) == length;
    const uint8_t status = Wire.endTransmission();
    if (!queued) return fail(100);
    return status == 0 || fail(status);
}

bool Hat::joy(JoyState& state) {
    Wire.beginTransmission(settings::Address);
    if (Wire.write(0x60) != 1) return fail(100);
    const uint8_t status = Wire.endTransmission();
    if (status != 0) return fail(status);
    if (Wire.requestFrom(settings::Address, uint8_t(5)) != 5) {
        while (Wire.available()) Wire.read();
        return fail(101);
    }
    for (auto& axis : state.axes) axis = static_cast<uint8_t>(Wire.read());
    state.buttons = static_cast<uint8_t>(Wire.read());
    for (auto& axis : state.axes) {
        if (axis > local::AxisMaximum + settings::AxisTolerance) return fail(102);
        axis = static_cast<uint8_t>(std::min<int>(axis, local::AxisMaximum));
    }
    return true;
}

bool Hat::motors(const control::Wheels& speeds) {
    uint8_t bytes[4];
    for (size_t index = 0; index < speeds.size(); ++index) {
        if (speeds[index] < -60 || speeds[index] > 60) return fail(103);
        bytes[index] = static_cast<uint8_t>(speeds[index]);
    }
    return write(0x00, bytes, sizeof(bytes));
}

bool Hat::servo(uint8_t channel, uint8_t angle) {
    if (channel > 1 || angle > 180) return fail(103);
    return write(0x10 + channel, &angle, 1);
}

}