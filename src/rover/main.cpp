#include <M5Unified.h>
#include <ProjectConfig.h>
#include <Hardware.h>
#include <RadioLink.h>
#include <algorithm>
#include <memory>

namespace {
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

void stopMotors() {
    applied.fill(0);
    ramp.update(applied, false);
    if (!hat.motors(applied)) {
        hardwareGood = false;
        safety->stop(control::Fault::Bus);
    }
}

void controlTick(uint32_t now) {
    M5.update();
    control::Packet packet;
    uint32_t received;
    safety->tick(now);
    if (radio.receive(packet, received)) safety->accept(packet, received, now);
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
            hardwareGood = false;
            safety->stop(control::Fault::Bus);
            stopMotors();
        }
    }
    wasArmed = safety->armed();
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
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.printf("ROVER %s\n", safety->armed() ? "ARMED" : "DISARMED");
    M5.Lcd.printf("Fault:%u I2C:%s\n", unsigned(safety->fault()), hardwareGood ? "OK" : "ERROR");
    M5.Lcd.setTextSize(1);
    M5.Lcd.printf("MAC %s\n", WiFi.macAddress().c_str());
    M5.Lcd.printf("Radio: %s\n", radio.error());
    M5.Lcd.printf("Wheels %d %d %d %d\n", applied[0], applied[1], applied[2], applied[3]);
    M5.Lcd.printf("Motion:%s Grip:%s\n", local::MotionCommissioned ? "enabled" : "locked",
        local::GripperCommissioned ? "enabled" : "locked");
    M5.Lcd.printf("Peer %08lX Ack %lu Grip:%u\n", static_cast<unsigned long>(safety->peer()),
        static_cast<unsigned long>(safety->sequence()), unsigned(appliedGrip));
    M5.Lcd.setTextSize(2);
}
}

void setup() {
    Serial.begin(115200);
    Serial.println("ROVER: startup");
    auto config = M5.config();
    config.serial_baudrate = 0;
    config.external_imu = false;
    config.external_rtc = false;
    config.external_display_value = 0;
    M5.begin(config);
    if (M5.Ex_I2C.isEnabled() && M5.Ex_I2C.getPort() != M5.In_I2C.getPort()) {
        M5.Ex_I2C.release();
    }
    M5.Lcd.setRotation(1);
    M5.Lcd.setTextColor(WHITE, BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("ROVER starting");
    hardwareGood = hat.begin();
    const bool stopped = hat.motors({});
    hardwareGood = hardwareGood && stopped;
    radio.begin(false);
    safety.reset(new control::Safety(radio.boot()));
    if (!hardwareGood) safety->stop(control::Fault::Bus);
    else if (!local::MotionCommissioned || !radio.ready()) safety->stop(control::Fault::Configuration);
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
    delay(1);
}