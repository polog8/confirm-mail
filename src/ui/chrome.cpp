#include "chrome.h"

#include <stdio.h>
#include <string.h>

#include "theme.h"

namespace chrome {

bool createScreenCanvas(draw::Canvas& c, int w, int h) {
  c.setColorDepth(16);
#if defined(ESP_PLATFORM)
  c.setPsram(true);
#endif
  return c.createSprite(w, h) != nullptr;
}

void background(draw::Canvas& c) {
  static draw::Canvas* cache = nullptr;
  if (!cache) {
    cache = new draw::Canvas();
    if (!createScreenCanvas(*cache)) {
      delete cache;
      cache = nullptr;
      c.fillScreen(theme::kBgBottom);
      return;
    }
    draw::verticalGradient(*cache, 0, 0, theme::W, theme::H, theme::kBgTop, theme::kBgBottom);
    draw::radialGlow(*cache, 40, 10, 180, theme::kAccent, 0.07f);
  }
  memcpy(c.getBuffer(), cache->getBuffer(), static_cast<size_t>(theme::W) * theme::H * 2);
}

void statusBar(draw::Canvas& c, const Frame& f, const char* title, int page, int pages, bool clock) {
  const SystemState& sys = f.s->sys;
  if (title && *title) draw::spaced(c, Font::Small, title, 12, 15, theme::kText3, 1);

  // Page indicator
  if (pages > 1) {
    const int gap = 9;
    int total = (pages - 1) * gap + 10;
    float x = 160.f - total / 2.f;
    for (int i = 0; i < pages; ++i) {
      if (i == page) {
        draw::fillRoundRect(c, x - 0.5f, 9.f, 11.f, 4.f, 2.f, theme::kText, 230);
        x += 10.f + gap - 4.f;
      } else {
        draw::fillCircle(c, x + 2.f, 11.f, 2.f, theme::kText, 80);
        x += gap;
      }
    }
  }

  int x = 306;
  x -= icons::battery(c, x - 20, 6, sys.battery, sys.charging, theme::kText2);
  x -= 18;
  icons::wifi(c, x, 6, sys.rssi, sys.wifi, theme::kText2);
  if (clock && f.s->sys.timeValid) {
    char t[8];
    snprintf(t, sizeof(t), "%02d:%02d", f.lt.hh, f.lt.mm);
    draw::text(c, Font::Small, t, x - 6, 15, theme::kText2, 0, draw::Right);
  }
}

void buttonBar(draw::Canvas& c, const Buttons& b, int pressed) {
  for (int i = 0; i < 3; ++i) {
    const ButtonSpec& s = b.b[i];
    if (s.icon == Icon::None && !s.label) continue;
    const float cx = static_cast<float>(theme::kButtonCx[i]);
    const float cy = 228.f;
    uint8_t bgAlpha = i == pressed ? 70 : (s.accent ? 34 : 18);
    draw::fillRoundRect(c, cx - 31.f, cy - 10.f, 62.f, 20.f, 10.f, theme::kText, bgAlpha);
    uint32_t fg = s.disabled ? theme::kText3 : theme::kText;
    if (s.icon != Icon::None && s.label) {
      int tw = draw::textWidth(c, Font::Small, s.label);
      float total = 12.f + 4.f + tw;
      float ix = cx - total / 2.f + 6.f;
      icons::draw(c, s.icon, ix, cy, 12.f, fg);
      draw::text(c, Font::Small, s.label, static_cast<int>(ix + 10.f), static_cast<int>(cy + 4.f), fg);
    } else if (s.icon != Icon::None) {
      icons::draw(c, s.icon, cx, cy, 13.f, fg);
    } else {
      draw::text(c, Font::Small, s.label, static_cast<int>(cx), static_cast<int>(cy + 4.f), fg, 58, draw::Center);
    }
  }
}

}  // namespace chrome
