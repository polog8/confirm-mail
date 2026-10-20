// Anti-aliased Inter fonts (VLW), loaded once and shared by every canvas.
#pragma once

#include <M5GFX.h>

enum class Font : uint8_t {
  Small,     // Inter Medium 12
  Body,      // Inter Regular 14
  BodyBold,  // Inter SemiBold 15
  Title,     // Inter Bold 19
  Large,     // Inter Display SemiBold 28
  Clock,     // Inter Display Light 80 (digits only)
  Timer,     // Inter Display Light 50 (digits only)
  Count
};

namespace fonts {

void begin();
const lgfx::IFont* get(Font f);
inline void use(lgfx::LGFXBase& g, Font f) { g.setFont(get(f)); }

}  // namespace fonts
