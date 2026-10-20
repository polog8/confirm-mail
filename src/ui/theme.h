// Palette and layout constants. Colors are 0xRRGGBB.
#pragma once

#include <stdint.h>

namespace theme {

constexpr int W = 320;
constexpr int H = 240;
constexpr int kStatusH = 22;
constexpr int kButtonBarY = 214;
constexpr int kButtonCx[3] = {53, 160, 267};

constexpr uint32_t kBgTop = 0x171A22;
constexpr uint32_t kBgBottom = 0x0A0B0E;
constexpr uint32_t kCard = 0x1F232D;
constexpr uint32_t kCardHi = 0x2A2F3B;
constexpr uint32_t kLine = 0x2C303B;

constexpr uint32_t kText = 0xF5F5F7;
constexpr uint32_t kText2 = 0xA6AAB6;
constexpr uint32_t kText3 = 0x6A6F7D;

constexpr uint32_t kAccent = 0x8EA6FF;   // periwinkle
constexpr uint32_t kSpotify = 0x1ED760;
constexpr uint32_t kFocus = 0xFF7A59;    // coral
constexpr uint32_t kBreak = 0x45D0C1;    // teal
constexpr uint32_t kWarn = 0xFFB547;
constexpr uint32_t kDanger = 0xFF5D62;
constexpr uint32_t kGood = 0x4ADE80;
constexpr uint32_t kSun = 0xFFC53D;
constexpr uint32_t kRain = 0x6CB4FF;

}  // namespace theme
