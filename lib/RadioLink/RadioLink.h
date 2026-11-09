#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <ControlCore.h>

class RadioLink {
public:
    struct Stats {
        uint32_t received = 0;     // packets stored in the mailbox
        uint32_t dropped = 0;      // wrong length or wrong sender
        uint32_t overwritten = 0;  // mailbox packet replaced before it was read
        uint32_t sent = 0;
        uint32_t busy = 0;         // send skipped, previous one still outstanding
        uint32_t failed = 0;       // esp_now_send returned an error
        uint32_t ackOk = 0;        // MAC-level delivery confirmed
        uint32_t ackFail = 0;      // MAC-level delivery not confirmed
    };
    bool begin(bool controller);
    bool send(const control::Packet& packet);
    bool receive(control::Packet& packet, uint32_t& received);
    Stats stats();
    bool ready() const { return ready_; }
    uint32_t boot() const { return boot_; }
    const char* error() const { return error_; }
private:
    static void onReceive(const uint8_t* mac, const uint8_t* data, int length);
    static void onSent(const uint8_t* mac, esp_now_send_status_t status);
    static RadioLink* instance_;
    portMUX_TYPE mutex_ = portMUX_INITIALIZER_UNLOCKED;
    uint8_t peer_[6]{};
    control::Bytes mailbox_{};
    Stats stats_;
    uint32_t received_ = 0;
    uint32_t boot_ = 0;
    bool pending_ = false;
    bool sending_ = false;
    bool ready_ = false;
    const char* error_ = "Not initialized";
};