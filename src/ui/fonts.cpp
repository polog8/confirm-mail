#include "fonts.h"

#include "fonts/body.h"
#include "fonts/body_bold.h"
#include "fonts/clock.h"
#include "fonts/large.h"
#include "fonts/small.h"
#include "fonts/timer.h"
#include "fonts/title.h"

namespace fonts {
namespace {

struct Slot {
  const uint8_t* data;
  uint32_t size;
  lgfx::PointerWrapper wrapper;
  lgfx::VLWfont font;
  bool loaded = false;
};

Slot g_slots[static_cast<int>(Font::Count)] = {
    {kFontSmall, sizeof(kFontSmall), {}, {}},
    {kFontBody, sizeof(kFontBody), {}, {}},
    {kFontBodyBold, sizeof(kFontBodyBold), {}, {}},
    {kFontTitle, sizeof(kFontTitle), {}, {}},
    {kFontLarge, sizeof(kFontLarge), {}, {}},
    {kFontClock, sizeof(kFontClock), {}, {}},
    {kFontTimer, sizeof(kFontTimer), {}, {}},
};

}  // namespace

void begin() {
  for (auto& s : g_slots) {
    if (s.loaded) continue;
    s.wrapper.set(s.data, s.size);
    s.loaded = s.font.loadFont(&s.wrapper);
  }
}

const lgfx::IFont* get(Font f) {
  Slot& s = g_slots[static_cast<int>(f)];
  if (!s.loaded) return &lgfx::fonts::Font2;
  return &s.font;
}

}  // namespace fonts
