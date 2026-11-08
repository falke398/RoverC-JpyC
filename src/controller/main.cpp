#include <M5Unified.h>
#include <Preferences.h>
#include <ProjectConfig.h>
#include <DisplayUi.h>
#include <Hardware.h>
#include <RadioLink.h>
#include <algorithm>

namespace {
M5Canvas canvas(&M5.Display);
bool canvasReady = false;
hardware::Hat hat;
hardware::JoyState joy;
RadioLink radio;
control::ArmGesture gesture;
control::Clicks clicks;
control::Packet status;
control::Motion motion;
control::Grip grip = control::Grip::Hold;
Preferences preferences;
std::array<uint16_t, 4> calibration = {local::AxisMaximum,
    local::AxisMaximum / 2, local::AxisMaximum / 2, local::AxisMaximum / 2};
std::array<int, 3> sums{};
std::array<int, 3> minima{};
std::array<int, 3> maxima{};
uint32_t lastControl = 0;
uint32_t lastDisplay = 0;
uint32_t lastStatus = 0;
uint32_t lastAckProgress = 0;
uint32_t sequence = 0;
uint32_t armSequence = 0;
uint32_t buttonBSince = 0;
uint32_t samples = 0;
uint8_t cap = 50;
bool inputGood = false;
bool calibrated = false;
bool calibrating = false;
bool calibrationButtonUsed = false;
bool haveStatus = false;
bool requested = false;
bool armSent = false;
bool confirmed = false;
bool storageReady = false;
const char* calibrationMessage = "Hold B: calibrate";

void disarm() {
    requested = false;
    armSent = false;
    confirmed = false;
    grip = control::Grip::Hold;
    gesture.reset();
}

bool plausible(int center) {
    return center >= local::AxisMaximum * 4 / 10 && center <= local::AxisMaximum * 6 / 10;
}

void startCalibration() {
    disarm();
    calibrating = true;
    samples = 0;
    sums.fill(0);
    minima.fill(255);
    maxima.fill(0);
    calibrationMessage = "Release sticks...";
}

void sampleCalibration() {
    if (!calibrating) return;
    if (!inputGood) {
        calibrating = false;
        calibrationMessage = "Calibration: I2C/range";
        return;
    }
    for (size_t index = 0; index < sums.size(); ++index) {
        const int value = joy.axes[index];
        sums[index] += value;
        minima[index] = (std::min)(minima[index], value);
        maxima[index] = (std::max)(maxima[index], value);
    }
    if (++samples < 50) return;
    calibrating = false;
    auto candidate = calibration;
    for (size_t index = 0; index < sums.size(); ++index) {
        const int center = sums[index] / int(samples);
        if (!plausible(center) || maxima[index] - minima[index] > 4) {
            calibrationMessage = "Unstable/off-center";
            return;
        }
        candidate[index + 1] = static_cast<uint16_t>(center);
    }
    if (!storageReady || preferences.putBytes("centers", candidate.data(), sizeof(candidate)) != sizeof(candidate)) {
        calibrated = false;
        calibrationMessage = "Calibration not saved";
        return;
    }
    calibration = candidate;
    calibrated = true;
    calibrationMessage = "Calibration saved";
}

bool linked(uint32_t now) {
    return haveStatus && now - lastStatus < control::StatusTimeout &&
           now - lastAckProgress < control::StatusTimeout && status.receiver == radio.boot();
}

void receiveStatus(uint32_t now) {
    control::Packet packet;
    uint32_t received;
    if (!radio.receive(packet, received) || packet.kind != control::Kind::Status ||
        now - received >= control::StatusTimeout ||
        (packet.receiver != 0 && packet.receiver != radio.boot())) return;
    const bool newBoot = !haveStatus || packet.sender != status.sender;
    if (!newBoot && !control::newer(packet.sequence, status.sequence)) return;
    if (newBoot) {
        disarm();
        lastAckProgress = received;
    } else if (control::newer(packet.acknowledgement, status.acknowledgement)) {
        lastAckProgress = received;
    }
    status = packet;
    lastStatus = received;
    haveStatus = true;
    if (packet.fault != control::Fault::None) disarm();
    if (requested && armSent && (packet.acknowledgement == armSequence ||
        control::newer(packet.acknowledgement, armSequence))) {
        if (packet.enabled) confirmed = true;
        else disarm();
    }
}

void controls(uint32_t now) {
    inputGood = hat.joy(joy);
    sampleCalibration();
    if (inputGood) {
        motion.sideways = control::shapeAxis(joy.axes[2], calibration[3], local::AxisMaximum,
                                             settings::Deadzone, local::InvertSideways);
        motion.forward = control::shapeAxis(joy.axes[1], calibration[2], local::AxisMaximum,
                                            settings::Deadzone, local::InvertForward);
        motion.turn = control::shapeAxis(joy.axes[0], calibration[1], local::AxisMaximum,
                                         settings::Deadzone, local::InvertTurn);
    } else {
        motion = {};
    }
    const bool ready = radio.ready() && local::MotionCommissioned && inputGood && calibrated &&
        !calibrating && linked(now) && status.fault == control::Fault::None;
    if (!ready) disarm();
    if (requested) {
        if (M5.BtnA.wasPressed()) disarm();
    } else if (gesture.update(M5.BtnA.isPressed(), motion.neutral(), ready, now)) {
        requested = true;
        armSent = false;
    }
    const auto click = clicks.update(joy.left(), joy.right(), confirmed && ready, now);
    if (local::GripperCommissioned && click != control::Grip::Hold) grip = click;
    control::Packet packet;
    packet.sender = radio.boot();
    packet.receiver = haveStatus ? status.sender : 0;
    packet.sequence = sequence + 1;
    packet.motion = requested && confirmed ? motion : control::Motion{};
    packet.enabled = requested;
    packet.cap = cap;
    packet.grip = requested && confirmed ? grip : control::Grip::Hold;
    if (radio.send(packet)) {
        sequence = packet.sequence;
        if (requested && !armSent) {
            armSequence = sequence;
            armSent = true;
        }
    }
}

void display(uint32_t now) {
    LovyanGFX& g = canvasReady ? static_cast<LovyanGFX&>(canvas) : static_cast<LovyanGFX&>(M5.Display);
    const bool isLinked = linked(now);
    const bool roverFault = isLinked && status.fault != control::Fault::None;
    const char* label = "DISARMED";
    uint16_t color = ui::Amber;
    if (confirmed) {
        label = "ARMED";
        color = ui::Green;
    } else if (!isLinked) {
        label = "NO LINK";
        color = ui::Red;
    } else if (roverFault) {
        label = "FAULT";
        color = ui::Red;
    }
    g.fillScreen(ui::Black);
    ui::header(g, "JOYC", label, color);
    const struct { const char* name; int value; int range; bool isSigned; } rows[] = {
        {"SPEED", cap, 60, false}, {"FWD", motion.forward, 100, true},
        {"TURN", motion.turn, 100, true}, {"CRAB", motion.sideways, 100, true}};
    int y = 38;
    for (const auto& row : rows) {
        ui::text(g, row.name, 6, y + 5, textdatum_t::middle_left, ui::White, &fonts::Font0);
        ui::bar(g, 44, y, 84, 10, row.value, row.range, row.isSigned);
        y += 24;
    }
    y += 6;
    ui::statusRow(g, y, isLinked, isLinked ? "Link OK" : "No link");
    ui::statusRow(g, y + 20, radio.ready(), radio.ready() ? "Radio OK" : "Radio ERR");
    ui::statusRow(g, y + 40, inputGood, inputGood ? "Joy OK" : "Joy ERROR");
    int line = 202;
    ui::text(g, calibrationMessage, 2, line, textdatum_t::middle_left, ui::Grey, &fonts::Font0);
    if (!radio.ready()) ui::text(g, radio.error(), 2, line += 12, textdatum_t::middle_left, ui::Red, &fonts::Font0);
    if (roverFault) {
        char text[24];
        snprintf(text, sizeof(text), "Rover fault:%u", unsigned(status.fault));
        ui::text(g, text, 2, line + 12, textdatum_t::middle_left, ui::Red, &fonts::Font0);
    }
    if (canvasReady) canvas.pushSprite(0, 0);
    Serial.printf("Joy valid=%d raw=%u,%u,%u,%u buttons=%02X calibrated=%d link=%d armed=%d\n",
        inputGood, joy.axes[0], joy.axes[1], joy.axes[2], joy.axes[3], joy.buttons,
        calibrated, linked(now), confirmed);
}
}

void setup() {
    Serial.begin(115200);
    Serial.println("JOYC: startup");
    auto config = M5.config();
    config.serial_baudrate = 0;
    config.external_imu = false;
    config.external_rtc = false;
    config.external_display_value = 0;
    M5.begin(config);
    if (M5.Ex_I2C.isEnabled() && M5.Ex_I2C.getPort() != M5.In_I2C.getPort()) {
        M5.Ex_I2C.release();
    }
    M5.Lcd.setRotation(0);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("JOYC start");
    // Allocate before Wi-Fi so the 64 KB sprite finds contiguous heap; otherwise draw directly.
    canvas.setColorDepth(16);
    canvasReady = canvas.createSprite(ui::Width, ui::Height) != nullptr;
    inputGood = hat.begin();
    radio.begin(true);
    storageReady = preferences.begin("joyc", false);
    std::array<uint16_t, 4> saved{};
    if (storageReady && preferences.getBytes("centers", saved.data(), sizeof(saved)) == sizeof(saved) &&
        saved[0] == local::AxisMaximum && plausible(saved[1]) && plausible(saved[2]) && plausible(saved[3])) {
        calibration = saved;
        calibrated = true;
        calibrationMessage = "Calibration loaded";
    }
}

void loop() {
    const uint32_t now = millis();
    receiveStatus(now);
    if (now - lastControl >= settings::ControlInterval) {
        lastControl = now;
        M5.update();
        if (M5.BtnB.wasPressed()) {
            buttonBSince = now;
            calibrationButtonUsed = false;
        }
        if (!requested && M5.BtnB.isPressed() && !calibrationButtonUsed && now - buttonBSince >= 2000) {
            calibrationButtonUsed = true;
            startCalibration();
        }
        if (M5.BtnB.wasReleased() && !calibrationButtonUsed && now - buttonBSince < 1000) {
            cap = cap == 30 ? 50 : 30;
        }
        controls(now);
    }
    if (now - lastDisplay >= settings::DisplayInterval) {
        lastDisplay = now;
        display(now);
    }
    delay(1);
}