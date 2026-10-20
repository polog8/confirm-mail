// Drawing helpers on top of M5GFX: dithered gradients, alpha blending,
// signed-distance-field shapes (anti-aliased icons, rings, rounded images)
// and UTF-8 aware text layout.
#pragma once

#include <M5GFX.h>
#include <math.h>

#include "fonts.h"

namespace draw {

using Canvas = M5Canvas;

// ---- colors ---------------------------------------------------------------
constexpr uint8_t R(uint32_t c) { return (c >> 16) & 0xFF; }
constexpr uint8_t G(uint32_t c) { return (c >> 8) & 0xFF; }
constexpr uint8_t B(uint32_t c) { return c & 0xFF; }
constexpr uint32_t rgb(int r, int g, int b) {
  return (uint32_t(r < 0 ? 0 : r > 255 ? 255 : r) << 16) | (uint32_t(g < 0 ? 0 : g > 255 ? 255 : g) << 8) |
         uint32_t(b < 0 ? 0 : b > 255 ? 255 : b);
}
uint32_t mix(uint32_t a, uint32_t b, float t);  // t=0 -> a, t=1 -> b
inline uint32_t lighten(uint32_t c, float t) { return mix(c, 0xFFFFFF, t); }
inline uint32_t darken(uint32_t c, float t) { return mix(c, 0x000000, t); }
float luminance(uint32_t c);  // 0..1 (perceptual)
uint32_t withMinLuminance(uint32_t c, float minL);
uint32_t withMaxLuminance(uint32_t c, float maxL);

// ---- pixels ---------------------------------------------------------------
// alpha: 0..255
void blendPixel(Canvas& c, int x, int y, uint32_t color, uint8_t alpha);
void verticalGradient(Canvas& c, int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void radialGlow(Canvas& c, int cx, int cy, int radius, uint32_t color, float strength);

// ---- signed distance shapes ---------------------------------------------
namespace sdf {
inline float circle(float px, float py, float cx, float cy, float r) { return hypotf(px - cx, py - cy) - r; }
float roundRect(float px, float py, float x, float y, float w, float h, float r);
float segment(float px, float py, float ax, float ay, float bx, float by);  // distance to segment
float triangle(float px, float py, float ax, float ay, float bx, float by, float cx, float cy);
}  // namespace sdf

// Rasterizes an SDF over a bounding box with 1px anti-aliasing.
template <typename F>
void fillSdf(Canvas& c, int x0, int y0, int x1, int y1, uint32_t color, F&& dist, uint8_t alpha = 255) {
  int32_t clx, cly, clw, clh;
  c.getClipRect(&clx, &cly, &clw, &clh);
  if (x0 < clx) x0 = clx;
  if (y0 < cly) y0 = cly;
  if (x1 > clx + clw - 1) x1 = clx + clw - 1;
  if (y1 > cly + clh - 1) y1 = cly + clh - 1;
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      float d = dist(x + 0.5f, y + 0.5f);
      if (d >= 0.5f) continue;
      float cov = d <= -0.5f ? 1.f : 0.5f - d;
      blendPixel(c, x, y, color, static_cast<uint8_t>(cov * alpha + 0.5f));
    }
  }
}

void fillRoundRect(Canvas& c, float x, float y, float w, float h, float r, uint32_t color, uint8_t alpha = 255);
void strokeRoundRect(Canvas& c, float x, float y, float w, float h, float r, float stroke, uint32_t color,
                     uint8_t alpha = 255);
void fillCircle(Canvas& c, float cx, float cy, float r, uint32_t color, uint8_t alpha = 255);
void line(Canvas& c, float ax, float ay, float bx, float by, float width, uint32_t color, uint8_t alpha = 255);
// Arc of a ring, angles in degrees, 0 = 12 o'clock, clockwise. Round caps.
void ringArc(Canvas& c, float cx, float cy, float radius, float thickness, float a0, float a1, uint32_t color,
             uint8_t alpha = 255);
void shadow(Canvas& c, int x, int y, int w, int h, int r, int spread, uint8_t alpha, int dy = 4);
// Copies src onto dst at (x, y) clipping it to a rounded rectangle.
void blitRounded(Canvas& dst, Canvas& src, int x, int y, int r);

// ---- text -----------------------------------------------------------------
enum Align : uint8_t { Left, Center, Right };

int textWidth(Canvas& c, Font f, const char* s);
// Draws a single line with its baseline at y; truncates with an ellipsis when
// maxW > 0. Returns the drawn width.
int text(Canvas& c, Font f, const char* s, int x, int y, uint32_t color, int maxW = 0, Align align = Left);
// Word-wrapped paragraph; the last allowed line is ellipsized. Returns the
// number of lines used. With dryRun only measures.
int wrapped(Canvas& c, Font f, const char* s, int x, int y, int maxW, int lineH, int maxLines, uint32_t color,
            bool dryRun = false);
// Text with letter spacing (for small caps labels).
int spaced(Canvas& c, Font f, const char* s, int x, int y, uint32_t color, int spacing, Align align = Left);

}  // namespace draw
