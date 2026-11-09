#include <M5Unified.h>
#include <ProjectConfig.h>
#include <DisplayUi.h>
#include <DebugLog.h>
#include <Hardware.h>
#include <RadioLink.h>
#include <algorithm>
#include <memory>

namespace {
M5Canvas canvas(&M5.Display);
bool canvasReady = false;
hardware::Hat hat;
RadioLink radio;
std::unique_ptr<control::Safety> safety;
control::Ramp ramp;
control::Wheels applied{};
control::Grip appliedGrip = control::Grip::Hold;
uint32_t lastControl = 0;
uint32_t lastStatus = 0;
uint32_t lastDisplay = 0;
uint32_t lastServo = 0;
uint32_t statusSequence = 0;
bool hardwareGood = false;
bool wasArmed = false;
uint32_t lastAccepted = 0;
uint32_t acceptedCount = 0;
uint32_t rejectedCount = 0;
uint32_t disarmCount = 0;
uint32_t lastStat = 0;
uint32_t lastTick = 0;
uint32_t maxTickGap = 0;
const char* lastDisarmReason = nullptr;
const char* resetReason = "";

void i2cFailed(const char* what) {
    debug::log("I2C FAIL %s err=%u fails=%lu", what, unsigned(hat.lastError()), debug::ul(hat.failures()));
}

// Why the rover left the armed state; mirrors the checks in controlTick().
const char* disarmReason(uint32_t now) {
    if (M5.BtnA.isPressed()) return "button A";
    if (!hardwareGood) return "i2c fault";
    if (!local::MotionCommissioned || !radio.ready()) return "config";
    if (safety->fault() == control::Fault::Link) {
        return now - lastAccepted >= control::CommandTimeout ? "cmd timeout" : "peer changed";
    }
    return "cmd disarm";
}

void stopMotors() {
    applied.fill(0);
    ramp.update(applied, false);
    if (!hat.motors(applied)) {
        i2cFailed("stop");
        hardwareGood = false;
        safety->stop(control::Fault::Bus);
    }
}

void controlTick(uint32_t now) {
    M5.update();
    maxTickGap = std::max(maxTickGap, now - lastTick);
    lastTick = now;
    control::Packet packet;
    uint32_t received;
    safety->tick(now);
    if (radio.receive(packet, received)) {
        if (safety->accept(packet, received, now)) {
            ++acceptedCount;
            lastAccepted = now;
        } else {
            ++rejectedCount;
        }
    }
    if (M5.BtnA.isPressed()) safety->stop(control::Fault::LocalStop);
    if (!local::MotionCommissioned || !radio.ready()) safety->stop(control::Fault::Configuration);
    if (!hardwareGood) safety->stop(control::Fault::Bus);
    const bool armed = safety->armed();
    const auto& command = safety->command();
    if (!armed) {
        stopMotors();
    } else {
        applied = ramp.update(control::mix(command.motion, command.cap), true);
        for (auto& speed : applied) {
            speed = static_cast<int8_t>(std::clamp(int(speed), -int(command.cap), int(command.cap)));
        }
        if (!hat.motors(applied)) {
            i2cFailed("motors");
            hardwareGood = false;
            safety->stop(control::Fault::Bus);
            stopMotors();
        }
    }
    if (safety->armed() && local::GripperCommissioned && command.grip != control::Grip::Hold &&
        (command.grip != appliedGrip || !wasArmed) && now - lastServo >= settings::ServoInterval) {
        lastServo = now;
        const uint8_t target = command.grip == control::Grip::Open ? local::ServoOpen : local::ServoClose;
        if (hat.servo(local::ServoChannel, target)) appliedGrip = command.grip;
        else {
            i2cFailed("servo");
            hardwareGood = false;
            safety->stop(control::Fault::Bus);
            stopMotors();
        }
    }
    const bool armedNow = safety->armed();
    if (armedNow != wasArmed) {
        if (armedNow) {
            debug::log("ARMED peer=%08lX cap=%u", debug::ul(safety->peer()), unsigned(command.cap));
        } else {
            ++disarmCount;
            lastDisarmReason = disarmReason(now);
            debug::log("DISARM reason=%s fault=%u cmdAge=%lu enabled=%d seq=%lu i2cFail=%lu i2cErr=%u",
                lastDisarmReason, unsigned(safety->fault()), debug::ul(now - lastAccepted), command.enabled,
                debug::ul(safety->sequence()), debug::ul(hat.failures()), unsigned(hat.lastError()));
        }
    }
    wasArmed = armedNow;
}

void logStats(uint32_t now) {
    const auto stats = radio.stats();
    debug::log("STAT armed=%d fault=%u cmdAge=%lu seq=%lu cap=%u acc=%lu rej=%lu "
               "rx=%lu drop=%lu over=%lu tx=%lu busy=%lu txFail=%lu ackOk=%lu ackFail=%lu "
               "i2cFail=%lu i2cErr=%u gapMax=%lu heap=%lu minHeap=%lu",
        safety->armed(), unsigned(safety->fault()), debug::ul(now - lastAccepted), debug::ul(safety->sequence()),
        unsigned(safety->command().cap), debug::ul(acceptedCount), debug::ul(rejectedCount),
        debug::ul(stats.received), debug::ul(stats.dropped), debug::ul(stats.overwritten),
        debug::ul(stats.sent), debug::ul(stats.busy), debug::ul(stats.failed), debug::ul(stats.ackOk),
        debug::ul(stats.ackFail), debug::ul(hat.failures()), unsigned(hat.lastError()),
        debug::ul(maxTickGap), debug::ul(ESP.getFreeHeap()), debug::ul(ESP.getMinFreeHeap()));
    maxTickGap = 0;
}

void sendStatus() {
    control::Packet packet;
    packet.kind = control::Kind::Status;
    packet.sender = radio.boot();
    packet.receiver = safety->peer();
    packet.sequence = ++statusSequence;
    packet.acknowledgement = safety->sequence();
    packet.cap = safety->command().cap;
    packet.enabled = safety->armed();
    packet.grip = appliedGrip;
    packet.fault = safety->fault();
    radio.send(packet);
}

void display() {
    LovyanGFX& g = canvasReady ? static_cast<LovyanGFX&>(canvas) : static_cast<LovyanGFX&>(M5.Display);
    const auto fault = safety->fault();
    const char* label = "DISARMED";
    uint16_t color = ui::Amber;
    if (safety->armed()) {
        label = "ARMED";
        color = ui::Green;
    } else if (fault != control::Fault::None) {
        color = ui::Red;
        label = fault == control::Fault::Link ? "LINK LOST" : fault == control::Fault::LocalStop ? "LOCAL STOP" :
            fault == control::Fault::Bus ? "I2C ERROR" : "CONFIG";
    }
    const int cap = std::max(1, int(safety->command().cap));
    char capText[8];
    snprintf(capText, sizeof(capText), "%d%%", cap);
    g.fillScreen(ui::Black);
    ui::header(g, "ROVER", capText, color);
    ui::text(g, "FRONT", ui::Width / 2, 36, textdatum_t::middle_center, ui::Grey, &fonts::Font0);
    g.fillRect(56, 46, 24, 151, ui::Dark);
    g.drawRect(56, 46, 24, 151, ui::Grey);
    // Wheels follow the mix() order: front-left, front-right, rear-left, rear-right.
    const struct { int x; int y; } wheels[4] = {{32, 46}, {83, 46}, {32, 136}, {83, 136}};
    for (size_t index = 0; index < std::size(wheels); ++index) {
        const int x = wheels[index].x;
        const int y = wheels[index].y;
        const int mid = y + 30;
        const int length = std::clamp(applied[index] * 30 / cap, -30, 30);
        g.drawRect(x, y, 22, 61, ui::White);
        if (length > 0) g.fillRect(x + 1, mid - length, 20, length, ui::Green);
        else if (length < 0) g.fillRect(x + 1, mid, 20, -length, ui::Amber);
        char speed[8];
        snprintf(speed, sizeof(speed), "%d", applied[index]);
        if (index % 2 == 0) ui::text(g, speed, x - 4, mid, textdatum_t::middle_right, ui::White);
        else ui::text(g, speed, x + 26, mid, textdatum_t::middle_left, ui::White);
    }
    ui::text(g, label, ui::Width / 2, 213, textdatum_t::middle_center, color);
    char text[28];
    if (lastDisarmReason) {
        snprintf(text, sizeof(text), "Off: %s (%lu)", lastDisarmReason, debug::ul(disarmCount));
        ui::text(g, text, 2, 227, textdatum_t::middle_left, ui::Amber, &fonts::Font0);
    }
    snprintf(text, sizeof(text), "rst:%s up:%lus", resetReason, debug::ul(millis() / 1000));
    ui::text(g, text, 2, 236, textdatum_t::middle_left, ui::Grey, &fonts::Font0);
    if (canvasReady) canvas.pushSprite(0, 0);
}
}

void setup() {
    Serial.setTxBufferSize(1024);
    Serial.begin(115200);
    Serial.println("ROVER: startup");
    resetReason = debug::resetReason();
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
    M5.Lcd.println("ROVER start");
    // Allocate before Wi-Fi so the 64 KB sprite finds contiguous heap; otherwise draw directly.
    canvas.setColorDepth(16);
    canvasReady = canvas.createSprite(ui::Width, ui::Height) != nullptr;
    hardwareGood = hat.begin();
    const bool stopped = hat.motors({});
    hardwareGood = hardwareGood && stopped;
    radio.begin(false);
    safety.reset(new control::Safety(radio.boot()));
    if (!hardwareGood) safety->stop(control::Fault::Bus);
    else if (!local::MotionCommissioned || !radio.ready()) safety->stop(control::Fault::Configuration);
    debug::log("BOOT rover reset=%s heap=%lu canvas=%d i2c=%d radio=%s boot=%08lX",
        resetReason, debug::ul(ESP.getFreeHeap()), canvasReady, hardwareGood, radio.error(),
        debug::ul(radio.boot()));
}

void loop() {
    const uint32_t now = millis();
    if (now - lastControl >= settings::ControlInterval) {
        lastControl = now;
        controlTick(now);
    }
    if (now - lastStatus >= settings::StatusInterval) {
        lastStatus = now;
        sendStatus();
    }
    if (now - lastDisplay >= settings::DisplayInterval) {
        lastDisplay = now;
        display();
    }
    if (now - lastStat >= 1000) {
        lastStat = now;
        logStats(now);
    }
    delay(1);
}