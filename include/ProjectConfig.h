#pragma once
#include <cstdint>

#if __has_include("LocalConfig.h")
#include "LocalConfig.h"
#else
#include "LocalConfig.example.h"
#endif

namespace settings {
constexpr uint32_t ControlInterval = 20;
constexpr uint32_t StatusInterval = 100;
constexpr uint32_t DisplayInterval = 200;
constexpr uint32_t ServoInterval = 50;
constexpr uint8_t Address = 0x38;
constexpr int Sda = 0;
constexpr int Scl = 26;
constexpr int Deadzone = local::AxisMaximum / 20;
static_assert(local::AxisMaximum == 200 || local::AxisMaximum == 255, "Verify JoyC range: 200 or 255");
static_assert(local::Channel >= 1 && local::Channel <= 11, "Choose shared Wi-Fi channel 1..11");
static_assert(local::ServoChannel <= 1, "RoverC servo channel must be 0 or 1");
static_assert(local::ServoOpen <= 180 && local::ServoClose <= 180, "Invalid servo angles");
}