#include "RadioLink.h"
#include <ProjectConfig.h>
#include <esp_wifi.h>
#include <cstring>

RadioLink* RadioLink::instance_ = nullptr;

static bool allSame(const uint8_t* bytes, size_t size) {
    for (size_t index = 1; index < size; ++index) {
        if (bytes[index] != bytes[0]) return false;
    }
    return true;
}

bool RadioLink::begin(bool controller) {
    boot_ = esp_random();
    if (boot_ == 0) boot_ = 1;
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    Serial.printf("STA MAC: %s\n", WiFi.macAddress().c_str());
    error_ = "Local config missing";
    if (!local::Provisioned) return false;
    uint8_t ownMac[6];
    WiFi.macAddress(ownMac);
    const uint8_t* expected = controller ? local::ControllerMac : local::RoverMac;
    const uint8_t* peer = controller ? local::RoverMac : local::ControllerMac;
    error_ = "Invalid MAC/key config";
    if (memcmp(ownMac, expected, 6) != 0 || memcmp(peer, ownMac, 6) == 0 ||
        (peer[0] & 1) != 0 || allSame(peer, 6) ||
        allSame(local::PrimaryKey, 16) || allSame(local::LinkKey, 16) ||
        memcmp(local::PrimaryKey, local::LinkKey, 16) == 0) return false;
    memcpy(peer_, peer, sizeof(peer_));
    error_ = "ESP-NOW init failed";
    if (esp_wifi_set_channel(local::Channel, WIFI_SECOND_CHAN_NONE) != ESP_OK ||
        esp_now_init() != ESP_OK || esp_now_set_pmk(local::PrimaryKey) != ESP_OK) return false;
    esp_now_peer_info_t info{};
    memcpy(info.peer_addr, peer_, sizeof(peer_));
    memcpy(info.lmk, local::LinkKey, sizeof(info.lmk));
    info.channel = local::Channel;
    info.ifidx = WIFI_IF_STA;
    info.encrypt = true;
    if (esp_now_add_peer(&info) != ESP_OK) return false;
    instance_ = this;
    if (esp_now_register_recv_cb(onReceive) != ESP_OK ||
        esp_now_register_send_cb(onSent) != ESP_OK) return false;
    ready_ = true;
    error_ = "OK";
    return true;
}

void RadioLink::onReceive(const uint8_t* mac, const uint8_t* data, int length) {
    auto* link = instance_;
    if (!link || length != int(control::PacketSize) || memcmp(mac, link->peer_, 6) != 0) return;
    portENTER_CRITICAL(&link->mutex_);
    memcpy(link->mailbox_.data(), data, control::PacketSize);
    link->received_ = millis();
    link->pending_ = true;
    portEXIT_CRITICAL(&link->mutex_);
}

void RadioLink::onSent(const uint8_t*, esp_now_send_status_t) {
    if (!instance_) return;
    portENTER_CRITICAL(&instance_->mutex_);
    instance_->sending_ = false;
    portEXIT_CRITICAL(&instance_->mutex_);
}

bool RadioLink::send(const control::Packet& packet) {
    if (!ready_ || !control::valid(packet)) return false;
    portENTER_CRITICAL(&mutex_);
    const bool busy = sending_;
    if (!busy) sending_ = true;
    portEXIT_CRITICAL(&mutex_);
    if (busy) return false;
    const auto bytes = control::encode(packet);
    if (esp_now_send(peer_, bytes.data(), bytes.size()) == ESP_OK) return true;
    portENTER_CRITICAL(&mutex_);
    sending_ = false;
    portEXIT_CRITICAL(&mutex_);
    return false;
}

bool RadioLink::receive(control::Packet& packet, uint32_t& received) {
    control::Bytes bytes;
    portENTER_CRITICAL(&mutex_);
    const bool available = pending_;
    if (available) {
        bytes = mailbox_;
        received = received_;
        pending_ = false;
    }
    portEXIT_CRITICAL(&mutex_);
    return available && control::decode(bytes.data(), bytes.size(), packet);
}