#include "ControlCore.h"
#include <algorithm>
#include <cstdlib>

namespace control {

static int bounded(int value, int minimum, int maximum) {
    return std::max(minimum, std::min(maximum, value));
}

bool Motion::neutral() const {
    return sideways == 0 && forward == 0 && turn == 0;
}

bool newer(uint32_t candidate, uint32_t previous) {
    const uint32_t distance = candidate - previous;
    return distance != 0 && distance < 0x80000000u;
}

bool valid(const Packet& packet) {
    const auto bounded = [](int value) { return value >= -100 && value <= 100; };
    return (packet.kind == Kind::Command || packet.kind == Kind::Status) &&
           packet.sender != 0 && bounded(packet.motion.sideways) &&
           bounded(packet.motion.forward) && bounded(packet.motion.turn) &&
           (packet.cap == 30 || packet.cap == 50 || packet.cap == 60) &&
           static_cast<uint8_t>(packet.grip) <= 2 &&
           static_cast<uint8_t>(packet.fault) <= 4 &&
           (packet.kind != Kind::Command || packet.fault == Fault::None);
}

static void put32(Bytes& bytes, size_t offset, uint32_t value) {
    for (size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
    }
}

static uint32_t get32(const uint8_t* bytes, size_t offset) {
    uint32_t value = 0;
    for (size_t index = 0; index < 4; ++index) {
        value |= uint32_t(bytes[offset + index]) << (index * 8);
    }
    return value;
}

Bytes encode(const Packet& packet) {
    Bytes bytes{};
    bytes[0] = 0x52;
    bytes[1] = 0x43;
    bytes[2] = 1;
    bytes[3] = static_cast<uint8_t>(packet.kind);
    put32(bytes, 4, packet.sender);
    put32(bytes, 8, packet.receiver);
    put32(bytes, 12, packet.sequence);
    put32(bytes, 16, packet.acknowledgement);
    bytes[20] = static_cast<uint8_t>(packet.motion.sideways + 100);
    bytes[21] = static_cast<uint8_t>(packet.motion.forward + 100);
    bytes[22] = static_cast<uint8_t>(packet.motion.turn + 100);
    bytes[23] = packet.cap;
    bytes[24] = packet.enabled ? 1 : 0;
    bytes[25] = static_cast<uint8_t>(packet.grip);
    bytes[26] = static_cast<uint8_t>(packet.fault);
    return bytes;
}

bool decode(const uint8_t* bytes, size_t length, Packet& packet) {
    if (!bytes || length != PacketSize || bytes[0] != 0x52 || bytes[1] != 0x43 ||
        bytes[2] != 1 || bytes[24] > 1 || bytes[27] != 0) return false;
    Packet candidate;
    candidate.kind = static_cast<Kind>(bytes[3]);
    candidate.sender = get32(bytes, 4);
    candidate.receiver = get32(bytes, 8);
    candidate.sequence = get32(bytes, 12);
    candidate.acknowledgement = get32(bytes, 16);
    candidate.motion = {int(bytes[20]) - 100, int(bytes[21]) - 100, int(bytes[22]) - 100};
    candidate.cap = bytes[23];
    candidate.enabled = bytes[24] != 0;
    candidate.grip = static_cast<Grip>(bytes[25]);
    candidate.fault = static_cast<Fault>(bytes[26]);
    if (!valid(candidate)) return false;
    packet = candidate;
    return true;
}

int shapeAxis(int raw, int center, int maximum, int deadzone, bool inverted) {
    if (maximum < 100 || center <= deadzone || center >= maximum - deadzone ||
        raw < 0 || raw > maximum || deadzone < 0) return 0;
    const int delta = raw - center;
    if (std::abs(delta) <= deadzone) return 0;
    const int extent = delta > 0 ? maximum - center : center;
    const int magnitude = (std::abs(delta) - deadzone) * 100 / (extent - deadzone);
    const int result = delta < 0 ? -magnitude : magnitude;
    return inverted ? -result : result;
}

Wheels mix(const Motion& motion, int cap) {
    const int turn = bounded(motion.turn, -100, 100);
    const int sideways = bounded(motion.sideways, -100, 100) * (100 - std::abs(turn)) / 100;
    const int forward = bounded(motion.forward, -100, 100) * (100 - std::abs(turn)) / 100;
    const std::array<int, 4> mixed = {forward + sideways - turn,
                                    forward - sideways + turn,
                                    forward - sideways - turn,
                                    forward + sideways + turn};
    Wheels result{};
    cap = bounded(cap, 0, 60);
    for (size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<int8_t>(bounded(mixed[index], -100, 100) * cap / 100);
    }
    return result;
}

Safety::Safety(uint32_t boot) : boot_(boot) {}

void Safety::stop(Fault fault) {
    armed_ = false;
    released_ = false;
    if (fault_ != Fault::Bus && fault_ != Fault::Configuration) fault_ = fault;
}

void Safety::tick(uint32_t now) {
    if (haveCommand_ && now - received_ >= CommandTimeout) {
        stop(Fault::Link);
        haveCommand_ = false;
    }
}

bool Safety::accept(const Packet& packet, uint32_t received, uint32_t now) {
    tick(now);
    if (!valid(packet) || packet.kind != Kind::Command || packet.receiver != boot_ ||
        now - received >= CommandTimeout) return false;
    if (packet.sender != peer_) {
        stop(Fault::Link);
        if (packet.enabled) return false;
        peer_ = packet.sender;
        haveSequence_ = false;
    }
    if (haveSequence_ && !newer(packet.sequence, sequence_)) return false;
    sequence_ = packet.sequence;
    haveSequence_ = true;
    received_ = received;
    haveCommand_ = true;
    command_ = packet;
    if (!packet.enabled) {
        armed_ = false;
        released_ = packet.motion.neutral();
        if (fault_ != Fault::Bus && fault_ != Fault::Configuration) fault_ = Fault::None;
    } else if (!armed_ && released_ && packet.motion.neutral() && fault_ == Fault::None) {
        armed_ = true;
        released_ = false;
    } else if (!armed_) {
        released_ = false;
    }
    return true;
}

void ArmGesture::reset() {
    released_ = false;
    holding_ = false;
    neutral_ = false;
}

bool ArmGesture::update(bool pressed, bool neutral, bool ready, uint32_t now) {
    if (!ready || !neutral) {
        reset();
        return false;
    }
    if (!neutral_) {
        neutral_ = true;
        neutralSince_ = now;
    }
    if (!pressed) {
        released_ = true;
        holding_ = false;
        return false;
    }
    if (!released_) return false;
    if (!holding_) {
        holding_ = true;
        holdSince_ = now;
    }
    if (now - holdSince_ >= 1000 && now - neutralSince_ >= 300) {
        reset();
        return true;
    }
    return false;
}

Wheels Ramp::update(const Wheels& target, bool enabled, int step) {
    if (!enabled) {
        output_.fill(0);
        return output_;
    }
    step = std::max(step, 1);
    for (size_t index = 0; index < output_.size(); ++index) {
        output_[index] = static_cast<int8_t>(int(output_[index]) +
            bounded(int(target[index]) - int(output_[index]), -step, step));
    }
    return output_;
}

Grip Clicks::update(bool left, bool right, bool enabled, uint32_t now) {
    const uint8_t buttons = (left ? 1 : 0) | (right ? 2 : 0);
    if (!enabled) {
        candidate_ = stable_ = buttons;
        changed_ = now;
        return Grip::Hold;
    }
    if (buttons != candidate_) {
        candidate_ = buttons;
        changed_ = now;
    }
    if (now - changed_ < 30 || candidate_ == stable_) return Grip::Hold;
    const uint8_t previous = stable_;
    stable_ = candidate_;
    if (previous != 0) return Grip::Hold;
    return stable_ == 1 ? Grip::Open : stable_ == 2 ? Grip::Close : Grip::Hold;
}

}