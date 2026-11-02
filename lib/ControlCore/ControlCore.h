#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace control {

constexpr uint32_t CommandTimeout = 250;
constexpr uint32_t StatusTimeout = 300;
constexpr size_t PacketSize = 28;
enum class Kind : uint8_t { Command = 1, Status = 2 };
enum class Grip : uint8_t { Hold, Open, Close };
enum class Fault : uint8_t { None, Link, LocalStop, Bus, Configuration };

struct Motion {
    int sideways = 0;
    int forward = 0;
    int turn = 0;
    bool neutral() const;
};

struct Packet {
    Kind kind = Kind::Command;
    uint32_t sender = 0;
    uint32_t receiver = 0;
    uint32_t sequence = 0;
    uint32_t acknowledgement = 0;
    Motion motion;
    uint8_t cap = 30;
    bool enabled = false;
    Grip grip = Grip::Hold;
    Fault fault = Fault::None;
};

using Bytes = std::array<uint8_t, PacketSize>;
using Wheels = std::array<int8_t, 4>;
bool newer(uint32_t candidate, uint32_t previous);
bool valid(const Packet& packet);
Bytes encode(const Packet& packet);
bool decode(const uint8_t* bytes, size_t length, Packet& packet);
int shapeAxis(int raw, int center, int maximum, int deadzone, bool inverted);
Wheels mix(const Motion& motion, int cap);

class Safety {
public:
    explicit Safety(uint32_t boot);
    bool accept(const Packet& packet, uint32_t received, uint32_t now);
    void tick(uint32_t now);
    void stop(Fault fault);
    bool armed() const { return armed_; }
    Fault fault() const { return fault_; }
    uint32_t peer() const { return peer_; }
    uint32_t sequence() const { return sequence_; }
    const Packet& command() const { return command_; }
private:
    uint32_t boot_;
    uint32_t peer_ = 0;
    uint32_t sequence_ = 0;
    uint32_t received_ = 0;
    bool haveSequence_ = false;
    bool haveCommand_ = false;
    bool released_ = false;
    bool armed_ = false;
    Fault fault_ = Fault::None;
    Packet command_;
};

class ArmGesture {
public:
    void reset();
    bool update(bool pressed, bool neutral, bool ready, uint32_t now);
private:
    bool released_ = false;
    bool holding_ = false;
    bool neutral_ = false;
    uint32_t holdSince_ = 0;
    uint32_t neutralSince_ = 0;
};

class Ramp {
public:
    Wheels update(const Wheels& target, bool enabled, int step = 3);
private:
    Wheels output_{};
};

class Clicks {
public:
    Grip update(bool left, bool right, bool enabled, uint32_t now);
private:
    uint8_t candidate_ = 0;
    uint8_t stable_ = 0;
    uint32_t changed_ = 0;
};

}