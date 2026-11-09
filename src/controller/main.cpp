#include <M5Unified.h>
#include <Preferences.h>
#include <ProjectConfig.h>
#include <DisplayUi.h>
#include <DebugLog.h>
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
bool joyFailing = false;
uint32_t lastJoyOk = 0;
char joyFailText[24] = "";
char joyDisarmText[24] = "";
bool calibrated = false;
bool calibrating = false;
bool calibrationButtonUsed = false;
bool haveStatus = false;
bool requested = false;
bool armSent = false;
bool confirmed = false;
bool storageReady = false;
const char* calibrationMessage = "Hold B: calibrate";
const char* lastDisarmReason = nullptr;
uint32_t disarmCount = 0;
uint32_t roverBoots = 0;
uint32_t lastStat = 0;
uint32_t maxControlGap = 0;
uint32_t maxDisplayMs = 0;

void disarm(const char* reason) {
    if (requested || confirmed) {
        ++disarmCount;
        lastDisarmReason = reason;
        if (strcmp(reason, "joy read failed") == 0) snprintf(joyDisarmText, sizeof(joyDisarmText), "%s", joyFailText);
        const uint32_t now = millis();
        const auto stats = radio.stats();
        debug::log("DISARM reason=%s stAge=%lu ackAge=%lu roverFault=%u joyFail=%lu joyErr=%u txFail=%lu ackFail=%lu",
            reason, debug::ul(now - lastStatus), debug::ul(now - lastAckProgress), unsigned(status.fault),
            debug::ul(hat.failures()), unsigned(hat.lastError()), debug::ul(stats.failed), debug::ul(stats.ackFail));
    }
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
    disarm("calibration");
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

bool inputUsable(uint32_t now) {
    return inputGood || now - lastJoyOk < settings::JoyGrace;
}

// First failing condition of the readiness check in controls(); keep both in sync.
const char* notReadyReason(uint32_t now) {
    if (!radio.ready()) return "radio not ready";
    if (!local::MotionCommissioned) return "motion locked";
    if (!inputUsable(now)) return "joy read failed";
    if (!calibrated) return "not calibrated";
    if (calibrating) return "calibrating";
    if (!haveStatus) return "no rover status";
    if (now - lastStatus >= control::StatusTimeout) return "status timeout";
    if (now - lastAckProgress >= control::StatusTimeout) return "ack stalled";
    if (status.receiver != radio.boot()) return "rx mismatch";
    if (status.fault != control::Fault::None) return "rover fault";
    return "unknown";
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
        ++roverBoots;
        debug::log("ROVER BOOT id=%08lX boots=%lu", debug::ul(packet.sender), debug::ul(roverBoots));
        disarm("rover rebooted");
        lastAckProgress = received;
    } else if (control::newer(packet.acknowledgement, status.acknowledgement)) {
        lastAckProgress = received;
    }
    if (!newBoot && packet.fault != status.fault) {
        debug::log("ROVER FAULT %u->%u", unsigned(status.fault), unsigned(packet.fault));
    }
    status = packet;
    lastStatus = received;
    haveStatus = true;
    if (packet.fault != control::Fault::None) disarm("rover fault");
    if (requested && armSent && (packet.acknowledgement == armSequence ||
        control::newer(packet.acknowledgement, armSequence))) {
        if (packet.enabled) {
            if (!confirmed) debug::log("ARM confirmed ack=%lu", debug::ul(packet.acknowledgement));
            confirmed = true;
        } else {
            disarm("rover refused");
        }
    }
}

void controls(uint32_t now) {
    // A second attempt absorbs one-off I2C errors before they count as a dropout.
    const bool read = hat.joy(joy) || hat.joy(joy);
    if (read) {
        if (joyFailing) {
            debug::log("JOY recovered after %lu ms", debug::ul(now - lastJoyOk));
            joyFailing = false;
        }
        lastJoyOk = now;
    } else if (!joyFailing) {
        joyFailing = true;
        // err 102 = axis above AxisMaximum; the raw values then show which one.
        snprintf(joyFailText, sizeof(joyFailText), "J:e%u %u %u %u %u", unsigned(hat.lastError()),
            joy.axes[0], joy.axes[1], joy.axes[2], joy.axes[3]);
        debug::log("JOY FAIL err=%u raw=%u,%u,%u,%u fails=%lu", unsigned(hat.lastError()),
            joy.axes[0], joy.axes[1], joy.axes[2], joy.axes[3], debug::ul(hat.failures()));
    }
    inputGood = read;
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
    const bool ready = radio.ready() && local::MotionCommissioned && inputUsable(now) && calibrated &&
        !calibrating && linked(now) && status.fault == control::Fault::None;
    if (!ready) disarm(notReadyReason(now));
    if (requested) {
        if (M5.BtnA.wasPressed()) disarm("button A");
    } else if (gesture.update(M5.BtnA.isPressed(), motion.neutral(), ready, now)) {
        debug::log("ARM requested");
        requested = true;
        armSent = false;
    }
    const auto click = clicks.update(inputGood && joy.left(), inputGood && joy.right(), confirmed && ready, now);
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
    if (!radio.ready()) {
        ui::text(g, radio.error(), 2, line += 12, textdatum_t::middle_left, ui::Red, &fonts::Font0);
    } else if (roverFault) {
        char text[24];
        snprintf(text, sizeof(text), "Rover fault:%u", unsigned(status.fault));
        ui::text(g, text, 2, line += 12, textdatum_t::middle_left, ui::Red, &fonts::Font0);
    }
    if (lastDisarmReason) {
        char text[28];
        snprintf(text, sizeof(text), "Off: %s (%lu)", lastDisarmReason, debug::ul(disarmCount));
        ui::text(g, text, 2, line + 12, textdatum_t::middle_left, ui::Amber, &fonts::Font0);
    }
    if (joyDisarmText[0]) ui::text(g, joyDisarmText, 2, 237, textdatum_t::middle_left, ui::Amber, &fonts::Font0);
    if (canvasReady) canvas.pushSprite(0, 0);
}

void logStats(uint32_t now) {
    const auto stats = radio.stats();
    debug::log("STAT armed=%d req=%d link=%d stAge=%lu ackAge=%lu fault=%u boots=%lu cap=%u "
               "rx=%lu drop=%lu over=%lu tx=%lu busy=%lu txFail=%lu ackOk=%lu ackFail=%lu "
               "joyFail=%lu joyErr=%u gapMax=%lu dispMax=%lu heap=%lu minHeap=%lu",
        confirmed, requested, linked(now), debug::ul(now - lastStatus), debug::ul(now - lastAckProgress),
        unsigned(status.fault), debug::ul(roverBoots), unsigned(cap),
        debug::ul(stats.received), debug::ul(stats.dropped), debug::ul(stats.overwritten),
        debug::ul(stats.sent), debug::ul(stats.busy), debug::ul(stats.failed), debug::ul(stats.ackOk),
        debug::ul(stats.ackFail), debug::ul(hat.failures()), unsigned(hat.lastError()),
        debug::ul(maxControlGap), debug::ul(maxDisplayMs), debug::ul(ESP.getFreeHeap()),
        debug::ul(ESP.getMinFreeHeap()));
    maxControlGap = 0;
    maxDisplayMs = 0;
}
}

void setup() {
    Serial.setTxBufferSize(1024);
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
    debug::log("BOOT controller reset=%s heap=%lu canvas=%d joy=%d radio=%s calibrated=%d",
        debug::resetReason(), debug::ul(ESP.getFreeHeap()), canvasReady, inputGood, radio.error(), calibrated);
}

void loop() {
    const uint32_t now = millis();
    receiveStatus(now);
    if (now - lastControl >= settings::ControlInterval) {
        maxControlGap = std::max(maxControlGap, now - lastControl);
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
        const uint32_t displayStart = millis();
        display(now);
        maxDisplayMs = std::max(maxDisplayMs, uint32_t(millis() - displayStart));
    }
    if (now - lastStat >= 1000) {
        lastStat = now;
        logStats(now);
    }
    delay(1);
}