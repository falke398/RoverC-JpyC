#pragma once
#include <cstdint>

namespace local {
constexpr bool Provisioned = false;
constexpr bool MotionCommissioned = false;
constexpr bool GripperCommissioned = false;
constexpr uint8_t ControllerMac[6] = {0, 0, 0, 0, 0, 0};
constexpr uint8_t RoverMac[6] = {0, 0, 0, 0, 0, 0};
constexpr uint8_t PrimaryKey[16] = {};
constexpr uint8_t LinkKey[16] = {};
constexpr uint8_t Channel = 1;
constexpr int AxisMaximum = 200;
constexpr bool InvertSideways = false;
constexpr bool InvertForward = false;
constexpr bool InvertTurn = false;
constexpr uint8_t ServoChannel = 0;
constexpr uint8_t ServoOpen = 10;
constexpr uint8_t ServoClose = 30;
}