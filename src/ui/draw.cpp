#include "draw.h"

#include <string.h>

namespace draw {

namespace {

const uint8_t kBayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

inline lgfx::swap565_t* pixelPtr(Canvas& c, int x, int y) {
  auto* p = static_cast<lgfx::swap565_t*>(c.getBuffer());
  return p ? p + y * c.width() + x : nullptr;
}

inline int quantize(float v8, float levels, float t) {
  int q = static_cast<int>(v8 * (levels / 255.f) + t - 0.05f);
  return q < 0 ? 0 : q > static_cast<int>(levels) ? static_cast<int>(levels) : q;
}

// Ordered dithering before the 565 quantisation removes gradient banding.
// Unchanged pixels (read back through R8/G8/B8) quantize to themselves, so
// re-dithering an area leaves no visible edge.
inline void setDithered(lgfx::swap565_t* p, int x, int y, float r, float g, float b) {
  float t = (kBayer4[y & 3][x & 3] + 0.5f) / 16.f;
  p->set(quantize(r, 31.f, t) << 3, quantize(g, 63.f, t) << 2, quantize(b, 31.f, t) << 3);
}

inline float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

}  // namespace

uint32_t mix(uint32_t a, uint32_t b, float t) {
  t = clamp01(t);
  return rgb(static_cast<int>(R(a) + (R(b) - R(a)) * t + 0.5f), static_cast<int>(G(a) + (G(b) - G(a)) * t + 0.5f),
             static_cast<int>(B(a) + (B(b) - B(a)) * t + 0.5f));
}

float luminance(uint32_t c) { return (0.2126f * R(c) + 0.7152f * G(c) + 0.0722f * B(c)) / 255.f; }

uint32_t withMinLuminance(uint32_t c, float minL) {
  for (int i = 0; i < 20 && luminance(c) < minL; ++i) c = lighten(c, 0.08f);
  return c;
}

uint32_t withMaxLuminance(uint32_t c, float maxL) {
  for (int i = 0; i < 20 && luminance(c) > maxL; ++i) c = darken(c, 0.08f);
  return c;
}

void blendPixel(Canvas& c, int x, int y, uint32_t color, uint8_t a) {
  if (a == 0 || static_cast<unsigned>(x) >= static_cast<unsigned>(c.width()) ||
      static_cast<unsigned>(y) >= static_cast<unsigned>(c.height()))
    return;
  lgfx::swap565_t* p = pixelPtr(c, x, y);
  if (!p) return;
  if (a == 255) {
    p->set(R(color), G(color), B(color));
    return;
  }
  int r = p->R8(), g = p->G8(), b = p->B8();
  r += ((R(color) - r) * a + 127) / 255;
  g += ((G(color) - g) * a + 127) / 255;
  b += ((B(color) - b) * a + 127) / 255;
  p->set(r, g, b);
}

void verticalGradient(Canvas& c, int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
  if (!c.getBuffer()) return;
  for (int j = 0; j < h; ++j) {
    int yy = y + j;
    if (yy < 0 || yy >= c.height()) continue;
    float t = h > 1 ? static_cast<float>(j) / (h - 1) : 0.f;
    float r = R(top) + (R(bottom) - R(top)) * t;
    float g = G(top) + (G(bottom) - G(top)) * t;
    float b = B(top) + (B(bottom) - B(top)) * t;
    for (int i = 0; i < w; ++i) {
      int xx = x + i;
      if (xx < 0 || xx >= c.width()) continue;
      setDithered(pixelPtr(c, xx, yy), xx, yy, r, g, b);
    }
  }
}

void radialGlow(Canvas& c, int cx, int cy, int radius, uint32_t color, float strength) {
  if (!c.getBuffer()) return;
  for (int y = cy - radius; y <= cy + radius; ++y) {
    if (y < 0 || y >= c.height()) continue;
    for (int x = cx - radius; x <= cx + radius; ++x) {
      if (x < 0 || x >= c.width()) continue;
      float d = hypotf(static_cast<float>(x - cx), static_cast<float>(y - cy)) / radius;
      if (d >= 1.f) continue;
      float a = strength * (1.f - d) * (1.f - d);
      lgfx::swap565_t* p = pixelPtr(c, x, y);
      float r = p->R8() + (R(color) - p->R8()) * a;
      float g = p->G8() + (G(color) - p->G8()) * a;
      float b = p->B8() + (B(color) - p->B8()) * a;
      setDithered(p, x, y, r, g, b);
    }
  }
}

namespace sdf {

float roundRect(float px, float py, float x, float y, float w, float h, float r) {
  float hw = w * 0.5f, hh = h * 0.5f;
  float qx = fabsf(px - (x + hw)) - (hw - r);
  float qy = fabsf(py - (y + hh)) - (hh - r);
  float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  float inside = qx > qy ? qx : qy;
  return hypotf(ox, oy) + (inside < 0 ? inside : 0) - r;
}

float segment(float px, float py, float ax, float ay, float bx, float by) {
  float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
  float len2 = bax * bax + bay * bay;
  float h = len2 > 0 ? clamp01((pax * bax + pay * bay) / len2) : 0.f;
  return hypotf(pax - bax * h, pay - bay * h);
}

static float edge(float px, float py, float ax, float ay, float bx, float by) {
  float ex = bx - ax, ey = by - ay;
  float len = hypotf(ex, ey);
  return len > 0 ? ((px - ax) * ey - (py - ay) * ex) / len : 0.f;
}

float triangle(float px, float py, float ax, float ay, float bx, float by, float cx, float cy) {
  float s = edge(cx, cy, ax, ay, bx, by) > 0 ? -1.f : 1.f;
  float d1 = s * edge(px, py, ax, ay, bx, by);
  float d2 = s * edge(px, py, bx, by, cx, cy);
  float d3 = s * edge(px, py, cx, cy, ax, ay);
  float d = d1 > d2 ? d1 : d2;
  return d > d3 ? d : d3;
}

}  // namespace sdf

void fillRoundRect(Canvas& c, float x, float y, float w, float h, float r, uint32_t color, uint8_t alpha) {
  fillSdf(c, static_cast<int>(x) - 1, static_cast<int>(y) - 1, static_cast<int>(x + w) + 1,
          static_cast<int>(y + h) + 1, color,
          [&](float px, float py) { return sdf::roundRect(px, py, x, y, w, h, r); }, alpha);
}

void strokeRoundRect(Canvas& c, float x, float y, float w, float h, float r, float stroke, uint32_t color,
                     uint8_t alpha) {
  fillSdf(c, static_cast<int>(x - stroke) - 1, static_cast<int>(y - stroke) - 1,
          static_cast<int>(x + w + stroke) + 1, static_cast<int>(y + h + stroke) + 1, color,
          [&](float px, float py) { return fabsf(sdf::roundRect(px, py, x, y, w, h, r)) - stroke * 0.5f; },
          alpha);
}

void fillCircle(Canvas& c, float cx, float cy, float r, uint32_t color, uint8_t alpha) {
  fillSdf(c, static_cast<int>(cx - r) - 1, static_cast<int>(cy - r) - 1, static_cast<int>(cx + r) + 1,
          static_cast<int>(cy + r) + 1, color, [&](float px, float py) { return sdf::circle(px, py, cx, cy, r); },
          alpha);
}

void line(Canvas& c, float ax, float ay, float bx, float by, float width, uint32_t color, uint8_t alpha) {
  float hw = width * 0.5f;
  fillSdf(c, static_cast<int>(fminf(ax, bx) - hw) - 1, static_cast<int>(fminf(ay, by) - hw) - 1,
          static_cast<int>(fmaxf(ax, bx) + hw) + 1, static_cast<int>(fmaxf(ay, by) + hw) + 1, color,
          [&](float px, float py) { return sdf::segment(px, py, ax, ay, bx, by) - hw; }, alpha);
}

void ringArc(Canvas& c, float cx, float cy, float radius, float thickness, float a0, float a1, uint32_t color,
             uint8_t alpha) {
  const float ht = thickness * 0.5f;
  const float span = a1 - a0;
  if (span <= 0.f) return;
  const float kDeg = 3.14159265f / 180.f;
  const float e0x = cx + radius * sinf(a0 * kDeg), e0y = cy - radius * cosf(a0 * kDeg);
  const float e1x = cx + radius * sinf(a1 * kDeg), e1y = cy - radius * cosf(a1 * kDeg);
  const int r = static_cast<int>(radius + ht) + 2;
  fillSdf(c, static_cast<int>(cx) - r, static_cast<int>(cy) - r, static_cast<int>(cx) + r, static_cast<int>(cy) + r,
          color,
          [&](float px, float py) {
            float dx = px - cx, dy = py - cy;
            float dRing = fabsf(hypotf(dx, dy) - radius) - ht;
            if (dRing > 1.f) return dRing;  // cheap reject, no atan needed
            if (span >= 360.f) return dRing;
            float ang = atan2f(dx, -dy) / kDeg;
            if (ang < 0) ang += 360.f;
            float rel = fmodf(ang - a0 + 720.f, 360.f);
            if (rel <= span) return dRing;
            float d0 = hypotf(px - e0x, py - e0y), d1 = hypotf(px - e1x, py - e1y);
            return (d0 < d1 ? d0 : d1) - ht;
          },
          alpha);
}

void shadow(Canvas& c, int x, int y, int w, int h, int r, int spread, uint8_t alpha, int dy) {
  const float fy = static_cast<float>(y + dy);
  for (int py = y + dy - spread; py <= y + dy + h + spread; ++py) {
    if (py < 0 || py >= c.height()) continue;
    for (int px = x - spread; px <= x + w + spread; ++px) {
      if (px < 0 || px >= c.width()) continue;
      float d = sdf::roundRect(px + 0.5f, py + 0.5f, static_cast<float>(x), fy, static_cast<float>(w),
                               static_cast<float>(h), static_cast<float>(r));
      if (d < -2.f || d >= spread) continue;
      float k = d <= 0 ? 1.f : 1.f - d / spread;
      blendPixel(c, px, py, 0x000000, static_cast<uint8_t>(alpha * k * k));
    }
  }
}

void blitRounded(Canvas& dst, Canvas& src, int x, int y, int r) {
  auto* sp = static_cast<lgfx::swap565_t*>(src.getBuffer());
  if (!sp || !dst.getBuffer()) return;
  const int w = src.width(), h = src.height();
  for (int j = 0; j < h; ++j) {
    int yy = y + j;
    if (yy < 0 || yy >= dst.height()) continue;
    for (int i = 0; i < w; ++i) {
      int xx = x + i;
      if (xx < 0 || xx >= dst.width()) continue;
      const lgfx::swap565_t& s = sp[j * w + i];
      bool corner = (i < r || i >= w - r) && (j < r || j >= h - r);
      if (!corner) {
        *pixelPtr(dst, xx, yy) = s;
        continue;
      }
      float d = sdf::roundRect(i + 0.5f, j + 0.5f, 0, 0, static_cast<float>(w), static_cast<float>(h),
                               static_cast<float>(r));
      if (d >= 0.5f) continue;
      float cov = d <= -0.5f ? 1.f : 0.5f - d;
      blendPixel(dst, xx, yy, rgb(s.R8(), s.G8(), s.B8()), static_cast<uint8_t>(cov * 255));
    }
  }
}

// ---- text ---------------------------------------------------------------------

namespace {

size_t utf8Prev(const char* s, size_t i) {
  if (i == 0) return 0;
  do {
    --i;
  } while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
  return i;
}

size_t utf8Next(const char* s, size_t i) {
  if (!s[i]) return i;
  do {
    ++i;
  } while (s[i] && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80);
  return i;
}

constexpr const char* kEllipsis = "\xE2\x80\xA6";  // …

int widthOf(Canvas& c, const char* s, size_t n, char* buf, size_t cap) {
  if (n >= cap) n = cap - 1;
  memcpy(buf, s, n);
  buf[n] = '\0';
  return c.textWidth(buf);
}

}  // namespace

int textWidth(Canvas& c, Font f, const char* s) {
  fonts::use(c, f);
  return c.textWidth(s);
}

int text(Canvas& c, Font f, const char* s, int x, int y, uint32_t color, int maxW, Align align) {
  if (!s || !*s) return 0;
  fonts::use(c, f);
  c.setTextColor(color);
  c.setTextDatum(lgfx::textdatum_t::baseline_left);
  char buf[224];
  const char* out = s;
  int w = c.textWidth(s);
  if (maxW > 0 && w > maxW) {
    const int ell = c.textWidth(kEllipsis);
    size_t n = strlen(s);
    if (n > sizeof(buf) - 4) n = utf8Prev(s, sizeof(buf) - 4);
    // Proportional first guess, then walk to the exact boundary.
    size_t cut = n * static_cast<size_t>(maxW > ell ? maxW - ell : 0) / static_cast<size_t>(w);
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    while (cut > 0 && widthOf(c, s, cut, buf, sizeof(buf)) + ell > maxW) cut = utf8Prev(s, cut);
    for (;;) {
      size_t next = utf8Next(s, cut);
      if (next > n || next == cut || widthOf(c, s, next, buf, sizeof(buf)) + ell > maxW) break;
      cut = next;
    }
    while (cut > 0 && s[cut - 1] == ' ') --cut;
    memcpy(buf, s, cut);
    strcpy(buf + cut, kEllipsis);
    out = buf;
    w = c.textWidth(buf);
  }
  int dx = align == Center ? x - w / 2 : align == Right ? x - w : x;
  c.drawString(out, dx, y);
  return w;
}

int wrapped(Canvas& c, Font f, const char* s, int x, int y, int maxW, int lineH, int maxLines, uint32_t color,
            bool dryRun) {
  if (!s) return 0;
  fonts::use(c, f);
  char buf[224];
  int lines = 0;
  const char* p = s;
  while (*p && lines < maxLines) {
    while (*p == ' ') ++p;
    if (!*p) break;
    const bool last = lines == maxLines - 1;
    // Greedy: extend word by word while it fits.
    const char* end = p;
    const char* q = p;
    for (;;) {
      const char* w = q;
      while (*w == ' ') ++w;
      while (*w && *w != ' ') ++w;
      if (w == q) break;
      if (widthOf(c, p, static_cast<size_t>(w - p), buf, sizeof(buf)) <= maxW) {
        end = w;
        q = w;
        if (!*w) break;
      } else {
        break;
      }
    }
    if (end == p) {
      // A single word wider than the line: hard break between characters.
      size_t i = 0;
      while (p[i]) {
        size_t nx = utf8Next(p, i);
        if (widthOf(c, p, nx, buf, sizeof(buf)) > maxW) break;
        i = nx;
      }
      if (i == 0) i = utf8Next(p, 0);
      end = p + i;
    }
    const char* rest = end;
    while (*rest == ' ') ++rest;
    if (!dryRun) {
      if (last && *rest) {
        text(c, f, p, x, y + lines * lineH, color, maxW);
      } else {
        size_t n = static_cast<size_t>(end - p);
        if (n >= sizeof(buf)) n = utf8Prev(p, sizeof(buf) - 1);
        memcpy(buf, p, n);
        buf[n] = '\0';
        text(c, f, buf, x, y + lines * lineH, color);
      }
    }
    ++lines;
    p = rest;
  }
  return lines;
}

int spaced(Canvas& c, Font f, const char* s, int x, int y, uint32_t color, int spacing, Align align) {
  fonts::use(c, f);
  c.setTextColor(color);
  c.setTextDatum(lgfx::textdatum_t::baseline_left);
  char ch[8];
  int total = 0;
  for (size_t i = 0; s[i];) {
    size_t n = utf8Next(s, i);
    total += widthOf(c, s + i, n - i, ch, sizeof(ch)) + spacing;
    i = n;
  }
  if (total > 0) total -= spacing;
  int cx = align == Center ? x - total / 2 : align == Right ? x - total : x;
  for (size_t i = 0; s[i];) {
    size_t n = utf8Next(s, i);
    int w = widthOf(c, s + i, n - i, ch, sizeof(ch));
    c.drawString(ch, cx, y);
    cx += w + spacing;
    i = n;
  }
  return total;
}

}  // namespace draw
