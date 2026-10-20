// Common screen furniture: background, status bar, touch-button bar.
#pragma once

#include "app.h"

namespace chrome {

// Fills the canvas with the default (cached) backdrop.
void background(draw::Canvas& c);
void statusBar(draw::Canvas& c, const Frame& f, const char* title, int page, int pages, bool clock);
// pressed: index of the button to highlight (-1 for none)
void buttonBar(draw::Canvas& c, const Buttons& b, int pressed);
// Creates a full-screen 16-bit canvas (PSRAM on the device).
bool createScreenCanvas(draw::Canvas& c, int w = 320, int h = 240);

}  // namespace chrome
