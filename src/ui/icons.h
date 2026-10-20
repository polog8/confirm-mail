// Vector icons drawn with anti-aliased signed distance fields.
#pragma once

#include "draw.h"

enum class Icon : uint8_t {
  None,
  Prev,
  Next,
  Play,
  Pause,
  Heart,
  HeartOutline,
  ChevronLeft,
  ChevronRight,
  Today,
  Reset,
  Skip,
  Minus,
  Plus,
  Sync,
  Check,
  Video,
  Pin,
  Speaker,
  Bell,
  Sun,
  Moon,
  Bolt,
  Clock,
};

namespace icons {

void draw(draw::Canvas& c, Icon icon, float cx, float cy, float size, uint32_t color, uint8_t alpha = 255);
void weather(draw::Canvas& c, int wmoCode, bool isDay, float cx, float cy, float size);
void wifi(draw::Canvas& c, int x, int y, int rssi, bool connected, uint32_t color);
// x, y = top-left. Returns width.
int battery(draw::Canvas& c, int x, int y, int pct, bool charging, uint32_t color);

// French label for a WMO weather code.
const char* weatherLabel(int wmoCode);

}  // namespace icons
