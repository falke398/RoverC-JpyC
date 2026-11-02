#include <unity.h>
#include <ControlCore.h>
#include <climits>
#include <cstdlib>

using namespace control;
void setUp() {}
void tearDown() {}

Packet command(uint32_t sequence = 1, bool enabled = false) {
    Packet packet;
    packet.sender = 11;
    packet.receiver = 22;
    packet.sequence = sequence;
    packet.enabled = enabled;
    return packet;
}

void axis_deadzone_and_endpoints() {
    TEST_ASSERT_EQUAL(0, shapeAxis(109, 100, 200, 10, false));
    TEST_ASSERT_EQUAL(0, shapeAxis(90, 100, 200, 10, false));
    TEST_ASSERT_EQUAL(100, shapeAxis(200, 100, 200, 10, false));
    TEST_ASSERT_EQUAL(-100, shapeAxis(0, 100, 200, 10, false));
    TEST_ASSERT_EQUAL(100, shapeAxis(0, 100, 200, 10, true));
    TEST_ASSERT_EQUAL(0, shapeAxis(201, 100, 200, 10, false));
    TEST_ASSERT_EQUAL(100, shapeAxis(255, 128, 255, 12, false));
}

void wheel_directions() {
    const Wheels crab = {30, -30, -30, 30};
    const Wheels forward = {30, 30, 30, 30};
    const Wheels backward = {-30, -30, -30, -30};
    const Wheels turn = {-30, 30, -30, 30};
    TEST_ASSERT_EQUAL_INT8_ARRAY(crab.data(), mix({100, 0, 0}, 30).data(), 4);
    TEST_ASSERT_EQUAL_INT8_ARRAY(forward.data(), mix({0, 100, 0}, 30).data(), 4);
    TEST_ASSERT_EQUAL_INT8_ARRAY(backward.data(), mix({0, -100, 0}, 30).data(), 4);
    TEST_ASSERT_EQUAL_INT8_ARRAY(turn.data(), mix({0, 0, 100}, 30).data(), 4);
}

void motor_caps_and_turn_priority() {
    for (int sideways = -100; sideways <= 100; sideways += 10) {
        for (int forward = -100; forward <= 100; forward += 10) {
            for (int turn = -100; turn <= 100; turn += 10) {
                for (int cap : {30, 50, 60}) {
                    for (auto speed : mix({sideways, forward, turn}, cap)) {
                        TEST_ASSERT_LESS_OR_EQUAL(cap, std::abs(int(speed)));
                    }
                }
            }
        }
    }
    const auto rotation = mix({0, 0, 100}, 60);
    TEST_ASSERT_EQUAL_INT8_ARRAY(rotation.data(), mix({100, 100, 100}, 60).data(), 4);
    const Wheels forward50 = {50, 50, 50, 50};
    TEST_ASSERT_EQUAL_INT8_ARRAY(forward50.data(), mix({0, 100, 0}, 50).data(), 4);
}

void packet_roundtrip_and_rejection() {
    auto packet = command();
    packet.motion = {-100, 23, 100};
    packet.grip = Grip::Close;
    packet.cap = 50;
    auto bytes = encode(packet);
    Packet decoded;
    TEST_ASSERT_TRUE(decode(bytes.data(), bytes.size(), decoded));
    TEST_ASSERT_EQUAL(-100, decoded.motion.sideways);
    TEST_ASSERT_EQUAL(23, decoded.motion.forward);
    TEST_ASSERT_EQUAL_UINT32(22, decoded.receiver);
    TEST_ASSERT_EQUAL_UINT8(50, decoded.cap);
    auto status = packet;
    status.kind = Kind::Status;
    const auto statusBytes = encode(status);
    TEST_ASSERT_TRUE(decode(statusBytes.data(), statusBytes.size(), decoded));
    TEST_ASSERT_EQUAL_UINT8(50, decoded.cap);
    for (uint8_t invalidCap : {0, 49, 51, 100}) {
        auto invalid = bytes;
        invalid[23] = invalidCap;
        TEST_ASSERT_FALSE(decode(invalid.data(), invalid.size(), decoded));
    }
    TEST_ASSERT_FALSE(decode(bytes.data(), bytes.size() - 1, decoded));
    TEST_ASSERT_FALSE(decode(nullptr, bytes.size(), decoded));
    for (size_t offset : {size_t(0), size_t(2), size_t(3), size_t(20), size_t(23),
                          size_t(24), size_t(25), size_t(26), size_t(27)}) {
        auto corrupt = bytes;
        corrupt[offset] = 255;
        TEST_ASSERT_FALSE(decode(corrupt.data(), corrupt.size(), decoded));
    }
}

void timeout_requires_disarm_handshake() {
    Safety safety(22);
    TEST_ASSERT_TRUE(safety.accept(command(), 0, 0));
    TEST_ASSERT_TRUE(safety.accept(command(2, true), 1, 1));
    TEST_ASSERT_TRUE(safety.armed());
    safety.tick(250);
    TEST_ASSERT_TRUE(safety.armed());
    safety.tick(251);
    TEST_ASSERT_FALSE(safety.armed());
    safety.accept(command(3, true), 252, 252);
    TEST_ASSERT_FALSE(safety.armed());
    safety.accept(command(4), 253, 253);
    safety.accept(command(5, true), 254, 254);
    TEST_ASSERT_TRUE(safety.armed());
}

void rejects_stale_duplicate_wrong_session() {
    Safety safety(22);
    safety.accept(command(), 0, 0);
    safety.accept(command(2, true), 1, 1);
    TEST_ASSERT_FALSE(safety.accept(command(2, true), 200, 200));
    auto wrong = command(3, true);
    wrong.receiver = 23;
    TEST_ASSERT_FALSE(safety.accept(wrong, 200, 200));
    TEST_ASSERT_FALSE(safety.accept(command(3, true), 1, 251));
    TEST_ASSERT_FALSE(safety.armed());
    safety.accept(command(4, true), 252, 252);
    TEST_ASSERT_FALSE(safety.armed());
}

void boot_and_off_center_never_arm() {
    Safety safety(22);
    safety.accept(command(1, true), 0, 0);
    TEST_ASSERT_FALSE(safety.armed());
    safety.accept(command(2), 1, 1);
    auto moving = command(3, true);
    moving.motion.forward = 10;
    safety.accept(moving, 2, 2);
    TEST_ASSERT_FALSE(safety.armed());
    safety.accept(command(4, true), 3, 3);
    TEST_ASSERT_FALSE(safety.armed());
    safety.accept(command(5), 4, 4);
    safety.accept(command(6, true), 5, 5);
    TEST_ASSERT_TRUE(safety.armed());
    auto reboot = command(1, true);
    reboot.sender = 99;
    safety.accept(reboot, 6, 6);
    TEST_ASSERT_FALSE(safety.armed());
}

void faults_and_wraparound() {
    Safety safety(22);
    safety.accept(command(UINT32_MAX), UINT32_MAX - 100, UINT32_MAX - 100);
    safety.accept(command(0, true), UINT32_MAX - 99, UINT32_MAX - 99);
    TEST_ASSERT_TRUE(safety.armed());
    safety.tick(149);
    TEST_ASSERT_TRUE(safety.armed());
    safety.tick(150);
    TEST_ASSERT_FALSE(safety.armed());
    safety.stop(Fault::Bus);
    safety.accept(command(1), 151, 151);
    safety.accept(command(2, true), 152, 152);
    TEST_ASSERT_FALSE(safety.armed());
    safety.tick(500);
    TEST_ASSERT_EQUAL(int(Fault::Bus), int(safety.fault()));
    TEST_ASSERT_TRUE(newer(0, UINT32_MAX));
    TEST_ASSERT_FALSE(newer(UINT32_MAX, 0));
}

void gesture_requires_release_and_neutral() {
    ArmGesture gesture;
    TEST_ASSERT_FALSE(gesture.update(true, true, true, 0));
    TEST_ASSERT_FALSE(gesture.update(true, true, true, 2000));
    gesture.update(false, true, true, 2001);
    gesture.update(true, true, true, 2002);
    TEST_ASSERT_FALSE(gesture.update(true, true, true, 3001));
    TEST_ASSERT_TRUE(gesture.update(true, true, true, 3002));
    TEST_ASSERT_FALSE(gesture.update(true, true, true, 5000));
    gesture.update(false, true, true, 5001);
    gesture.update(true, true, true, 5002);
    TEST_ASSERT_FALSE(gesture.update(true, false, true, 6002));
}

void ramp_and_gripper() {
    Ramp ramp;
    const Wheels target = {30, -30, 30, -30};
    TEST_ASSERT_EQUAL(3, ramp.update(target, true)[0]);
    TEST_ASSERT_EQUAL(0, ramp.update(target, false)[0]);
    Clicks clicks;
    clicks.update(true, false, false, 0);
    TEST_ASSERT_EQUAL(int(Grip::Hold), int(clicks.update(true, false, true, 100)));
    clicks.update(false, false, true, 101);
    clicks.update(false, false, true, 131);
    clicks.update(true, false, true, 132);
    TEST_ASSERT_EQUAL(int(Grip::Open), int(clicks.update(true, false, true, 162)));
    TEST_ASSERT_EQUAL(int(Grip::Hold), int(clicks.update(true, false, true, 300)));
    clicks.update(true, true, true, 301);
    TEST_ASSERT_EQUAL(int(Grip::Hold), int(clicks.update(true, true, true, 331)));
    clicks.update(false, false, true, 332);
    clicks.update(false, false, true, 362);
    clicks.update(false, true, true, 363);
    TEST_ASSERT_EQUAL(int(Grip::Close), int(clicks.update(false, true, true, 393)));
    TEST_ASSERT_EQUAL(int(Grip::Hold), int(clicks.update(false, true, false, 394)));
    TEST_ASSERT_EQUAL(int(Grip::Hold), int(clicks.update(false, true, true, 424)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(axis_deadzone_and_endpoints);
    RUN_TEST(wheel_directions);
    RUN_TEST(motor_caps_and_turn_priority);
    RUN_TEST(packet_roundtrip_and_rejection);
    RUN_TEST(timeout_requires_disarm_handshake);
    RUN_TEST(rejects_stale_duplicate_wrong_session);
    RUN_TEST(boot_and_off_center_never_arm);
    RUN_TEST(faults_and_wraparound);
    RUN_TEST(gesture_requires_release_and_neutral);
    RUN_TEST(ramp_and_gripper);
    return UNITY_END();
}