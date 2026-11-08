#pragma once
#include <M5Unified.h>
#include <algorithm>
#include <cstdlib>

// Shared drawing helpers for the 135x240 portrait layout (setRotation(0)).
namespace ui {
constexpr int Width = 135;
constexpr int Height = 240;
constexpr uint16_t Black = 0x0000;
constexpr uint16_t White = 0xFFFF;
constexpr uint16_t Green = 0x064A;
constexpr uint16_t Amber = 0xFD80;
constexpr uint16_t Red = 0xD945;
constexpr uint16_t Grey = 0x7BCF;
constexpr uint16_t Dark = 0x2945;

inline void text(LovyanGFX& g, const char* value, int x, int y, textdatum_t datum, uint16_t color,
                 const lgfx::IFont* font = &fonts::Font2) {
    g.setFont(font);
    g.setTextSize(1);
    g.setTextColor(color);
    g.setTextDatum(datum);
    g.drawString(value, x, y);
}

inline void header(LovyanGFX& g, const char* left, const char* right, uint16_t color) {
    const uint16_t ink = color == Red ? White : Black;
    g.fillRect(0, 0, Width, 26, color);
    text(g, left, 6, 13, textdatum_t::middle_left, ink);
    text(g, right, Width - 6, 13, textdatum_t::middle_right, ink);
}

// Bar of width w: signed bars grow from the center, unsigned ones from the left edge.
inline void bar(LovyanGFX& g, int x, int y, int w, int h, int value, int range, bool isSigned) {
    g.drawRect(x, y, w + 1, h + 1, Grey);
    if (isSigned) {
        const int mid = x + w / 2;
        const int end = mid + std::clamp(value, -range, range) * (w / 2) / range;
        if (end != mid) g.fillRect(std::min(mid, end), y + 1, std::abs(end - mid), h - 1, end > mid ? Green : Amber);
        g.drawFastVLine(mid, y, h + 1, Grey);
    } else {
        const int length = std::clamp(value, 0, range) * (w - 1) / range;
        if (length > 0) g.fillRect(x + 1, y + 1, length, h - 1, Green);
    }
}

inline void statusRow(LovyanGFX& g, int y, bool good, const char* label) {
    g.fillCircle(14, y, 4, good ? Green : Red);
    text(g, label, 26, y, textdatum_t::middle_left, White);
}
}
