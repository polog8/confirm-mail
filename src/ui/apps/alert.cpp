#include <stdio.h>
#include <string.h>

#include "../chrome.h"
#include "../format.h"
#include "../theme.h"
#include "apps.h"

void AlertApp::show(const CalendarEvent& e, uint32_t color) {
  event_ = e;
  color_ = color;
  active_ = true;
  snoozed_ = false;
}

void AlertApp::render(draw::Canvas& c, const Frame& f) {
  static draw::Canvas* bg = nullptr;
  static uint32_t bgColor = 0xFFFFFFFF;
  if (!bg) {
    bg = new draw::Canvas();
    if (!chrome::createScreenCanvas(*bg)) {
      delete bg;
      bg = nullptr;
    }
  }
  if (bg && bgColor != color_) {
    uint32_t top = draw::withMaxLuminance(draw::mix(color_, 0x000000, 0.45f), 0.2f);
    draw::verticalGradient(*bg, 0, 0, theme::W, theme::H, top, 0x060608);
    draw::radialGlow(*bg, 40, 30, 200, color_, 0.25f);
    bgColor = color_;
  }
  if (bg) memcpy(c.getBuffer(), bg->getBuffer(), static_cast<size_t>(theme::W) * theme::H * 2);
  else chrome::background(c);

  const bool qr = event_.url[0] != '\0';
  const int textW = qr ? 180 : 290;
  char buf[64];
  const int64_t delta = event_.start - f.now;
  if (delta > 0) {
    fmt::until(buf, sizeof(buf), delta);
    fmt::capitalize(buf);
  } else {
    snprintf(buf, sizeof(buf), "Maintenant");
  }
  draw::text(c, Font::Large, buf, 16, 66, theme::kText, textW);
  evt::range(buf, sizeof(buf), *f.tz, event_);
  draw::text(c, Font::BodyBold, buf, 16, 90, draw::withMinLuminance(color_, 0.6f), textW);
  int lines = draw::wrapped(c, Font::Title, event_.title, 16, 122, textW, 23, 3, theme::kText);
  if (event_.location[0]) {
    int y = 122 + lines * 23 + 2;
    icons::draw(c, Icon::Pin, 22, y - 5, 12, theme::kText2);
    draw::text(c, Font::Body, event_.location, 32, y, theme::kText2, textW - 16);
  }
  if (qr) {
    draw::fillRoundRect(c, 204, 34, 106, 106, 12, 0xFFFFFF);
    c.qrcode(event_.url, 209, 39, 96, 1);
    icons::draw(c, Icon::Video, 220, 156, 12, theme::kText2);
    draw::text(c, Font::Small, "Rejoindre", 230, 160, theme::kText2);
  }
}

Buttons AlertApp::buttons(const Frame& f) const {
  Buttons b;
  if (event_.start - f.now > 60) {
    b.b[0].icon = Icon::Bell;
    b.b[0].label = "Après";
  }
  b.b[1].icon = Icon::Check;
  b.b[1].label = "OK";
  b.b[1].accent = true;
  return b;
}

void AlertApp::onButton(int idx, bool longPress, const Frame& f, Actions& a) {
  if (idx == 0 && event_.start - f.now > 60) {
    snoozed_ = true;
    active_ = false;
  } else if (idx == 1) {
    active_ = false;
  }
}
