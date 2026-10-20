#include "icons.h"

#include "theme.h"

using draw::Canvas;
namespace sdf = draw::sdf;

namespace icons {

namespace {

inline float fmin3(float a, float b, float c) { return fminf(a, fminf(b, c)); }

template <typename F>
void shape(Canvas& c, float cx, float cy, float s, uint32_t color, uint8_t alpha, F&& f) {
  int r = static_cast<int>(s * 0.6f) + 2;
  draw::fillSdf(c, static_cast<int>(cx) - r, static_cast<int>(cy) - r, static_cast<int>(cx) + r,
                static_cast<int>(cy) + r, color, f, alpha);
}

float heartSdf(float px, float py, float cx, float cy, float s) {
  float l = sdf::circle(px, py, cx - 0.21f * s, cy - 0.12f * s, 0.24f * s);
  float r = sdf::circle(px, py, cx + 0.21f * s, cy - 0.12f * s, 0.24f * s);
  float t = sdf::triangle(px, py, cx - 0.43f * s, cy - 0.04f * s, cx + 0.43f * s, cy - 0.04f * s, cx,
                          cy + 0.42f * s);
  return fmin3(l, r, t);
}

float cloudSdf(float px, float py, float cx, float cy, float s) {
  float a = sdf::circle(px, py, cx - 0.17f * s, cy + 0.03f * s, 0.19f * s);
  float b = sdf::circle(px, py, cx + 0.06f * s, cy - 0.07f * s, 0.25f * s);
  float d = sdf::circle(px, py, cx + 0.27f * s, cy + 0.07f * s, 0.16f * s);
  float base = sdf::roundRect(px, py, cx - 0.40f * s, cy + 0.02f * s, 0.84f * s, 0.22f * s, 0.11f * s);
  return fminf(fmin3(a, b, d), base);
}

void sun(Canvas& c, float cx, float cy, float s, uint32_t color) {
  draw::fillCircle(c, cx, cy, 0.2f * s, color);
  for (int i = 0; i < 8; ++i) {
    float a = i * 3.14159265f / 4.f;
    float ca = cosf(a), sa = sinf(a);
    draw::line(c, cx + ca * 0.31f * s, cy + sa * 0.31f * s, cx + ca * 0.43f * s, cy + sa * 0.43f * s,
               fmaxf(1.5f, 0.07f * s), color);
  }
}

void moon(Canvas& c, float cx, float cy, float s, uint32_t color) {
  shape(c, cx, cy, s, color, 255, [&](float px, float py) {
    float outer = sdf::circle(px, py, cx, cy, 0.32f * s);
    float cut = sdf::circle(px, py, cx + 0.17f * s, cy - 0.13f * s, 0.27f * s);
    return fmaxf(outer, -cut);
  });
}

void cloud(Canvas& c, float cx, float cy, float s, uint32_t color) {
  shape(c, cx, cy, s, color, 255, [&](float px, float py) { return cloudSdf(px, py, cx, cy, s); });
}

}  // namespace

void draw(Canvas& c, Icon icon, float cx, float cy, float s, uint32_t color, uint8_t alpha) {
  const float lw = fmaxf(1.6f, s * 0.11f);  // stroke width
  switch (icon) {
    case Icon::None:
      return;
    case Icon::Play:
      shape(c, cx, cy, s, color, alpha, [&](float px, float py) {
        return sdf::triangle(px, py, cx - 0.28f * s, cy - 0.38f * s, cx - 0.28f * s, cy + 0.38f * s,
                             cx + 0.40f * s, cy) -
               0.6f;
      });
      return;
    case Icon::Pause:
      shape(c, cx, cy, s, color, alpha, [&](float px, float py) {
        float a = sdf::roundRect(px, py, cx - 0.32f * s, cy - 0.38f * s, 0.23f * s, 0.76f * s, 0.06f * s);
        float b = sdf::roundRect(px, py, cx + 0.09f * s, cy - 0.38f * s, 0.23f * s, 0.76f * s, 0.06f * s);
        return fminf(a, b);
      });
      return;
    case Icon::Next:
    case Icon::Prev: {
      float dir = icon == Icon::Next ? 1.f : -1.f;
      shape(c, cx, cy, s, color, alpha, [&](float px, float py) {
        float t = sdf::triangle(px, py, cx - dir * 0.36f * s, cy - 0.34f * s, cx - dir * 0.36f * s,
                                cy + 0.34f * s, cx + dir * 0.18f * s, cy) -
                  0.5f;
        float bx = icon == Icon::Next ? cx + 0.22f * s : cx - 0.34f * s;
        float bar = sdf::roundRect(px, py, bx, cy - 0.34f * s, 0.12f * s, 0.68f * s, 0.05f * s);
        return fminf(t, bar);
      });
      return;
    }
    case Icon::Skip:
      shape(c, cx, cy, s, color, alpha, [&](float px, float py) {
        float a = sdf::triangle(px, py, cx - 0.42f * s, cy - 0.3f * s, cx - 0.42f * s, cy + 0.3f * s, cx, cy);
        float b = sdf::triangle(px, py, cx - 0.02f * s, cy - 0.3f * s, cx - 0.02f * s, cy + 0.3f * s,
                                cx + 0.40f * s, cy);
        return fminf(a, b) - 0.5f;
      });
      return;
    case Icon::Heart:
      shape(c, cx, cy, s, color, alpha, [&](float px, float py) { return heartSdf(px, py, cx, cy, s); });
      return;
    case Icon::HeartOutline:
      shape(c, cx, cy, s, color, alpha, [&](float px, float py) {
        float outer = heartSdf(px, py, cx, cy, s);
        float inner = heartSdf(px, py, cx, cy - 0.01f * s, s * 0.66f);
        return fmaxf(outer, -inner);
      });
      return;
    case Icon::ChevronLeft:
    case Icon::ChevronRight: {
      float d = icon == Icon::ChevronRight ? 1.f : -1.f;
      shape(c, cx, cy, s, color, alpha, [&](float px, float py) {
        float a = sdf::segment(px, py, cx - d * 0.13f * s, cy - 0.3f * s, cx + d * 0.15f * s, cy);
        float b = sdf::segment(px, py, cx + d * 0.15f * s, cy, cx - d * 0.13f * s, cy + 0.3f * s);
        return fminf(a, b) - lw * 0.55f;
      });
      return;
    }
    case Icon::Today:
      shape(c, cx, cy, s, color, alpha, [&](float px, float py) {
        float ring = fabsf(sdf::circle(px, py, cx, cy, 0.36f * s)) - lw * 0.5f;
        float dot = sdf::circle(px, py, cx, cy, 0.14f * s);
        return fminf(ring, dot);
      });
      return;
    case Icon::Clock:
      shape(c, cx, cy, s, color, alpha, [&](float px, float py) {
        float ring = fabsf(sdf::circle(px, py, cx, cy, 0.38f * s)) - lw * 0.5f;
        float h = sdf::segment(px, py, cx, cy, cx, cy - 0.22f * s) - lw * 0.5f;
        float m = sdf::segment(px, py, cx, cy, cx + 0.16f * s, cy + 0.08f * s) - lw * 0.5f;
        return fmin3(ring, h, m);
      });
      return;
    case Icon::Reset:
    case Icon::Sync: {
      draw::ringArc(c, cx, cy, 0.32f * s, lw, icon == Icon::Reset ? 70.f : 40.f, icon == Icon::Reset ? 340.f : 160.f,
                    color, alpha);
      if (icon == Icon::Sync) draw::ringArc(c, cx, cy, 0.32f * s, lw, 220.f, 340.f, color, alpha);
      // Arrow heads at the end of each arc.
      auto head = [&](float angDeg, float dir) {
        float a = angDeg * 3.14159265f / 180.f;
        float hx = cx + 0.32f * s * sinf(a), hy = cy - 0.32f * s * cosf(a);
        float tx = cosf(a) * dir, ty = sinf(a) * dir;  // tangent (clockwise when dir = 1)
        float nx = sinf(a), ny = -cosf(a);              // outward normal
        float k = 0.17f * s;
        shape(c, hx, hy, s * 0.5f, color, alpha, [&](float px, float py) {
          return sdf::triangle(px, py, hx + tx * k, hy + ty * k, hx + nx * k * 0.9f - tx * k * 0.3f,
                               hy + ny * k * 0.9f - ty * k * 0.3f, hx - nx * k * 0.9f - tx * k * 0.3f,
                               hy - ny * k * 0.9f - ty * k * 0.3f);
        });
      };
      if (icon == Icon::Reset) {
        head(70.f, -1.f);
      } else {
        head(160.f, 1.f);
        head(340.f, 1.f);
      }
      return;
    }
    case Icon::Minus:
      draw::line(c, cx - 0.3f * s, cy, cx + 0.3f * s, cy, lw, color, alpha);
      return;
    case Icon::Plus:
      draw::line(c, cx - 0.3f * s, cy, cx + 0.3f * s, cy, lw, color, alpha);
      draw::line(c, cx, cy - 0.3f * s, cx, cy + 0.3f * s, lw, color, alpha);
      return;
    case Icon::Check:
      shape(c, cx, cy, s, color, alpha, [&](float px, float py) {
        float a = sdf::segment(px, py, cx - 0.3f * s, cy + 0.02f * s, cx - 0.08f * s, cy + 0.24f * s);
        float b = sdf::segment(px, py, cx - 0.08f * s, cy + 0.24f * s, cx + 0.32f * s, cy - 0.24f * s);
        return fminf(a, b) - lw * 0.55f;
      });
      return;
    case Icon::Video:
      shape(c, cx, cy, s, color, alpha, [&](float px, float py) {
        float body = sdf::roundRect(px, py, cx - 0.42f * s, cy - 0.26f * s, 0.56f * s, 0.52f * s, 0.1f * s);
        float lens = sdf::triangle(px, py, cx + 0.12f * s, cy, cx + 0.44f * s, cy - 0.24f * s, cx + 0.44f * s,
                                   cy + 0.24f * s) -
                     0.5f;
        return fminf(body, lens);
      });
      return;
    case Icon::Pin:
      shape(c, cx, cy, s, color, alpha, [&](float px, float py) {
        float head = sdf::circle(px, py, cx, cy - 0.1f * s, 0.27f * s);
        float tip = sdf::triangle(px, py, cx - 0.22f * s, cy + 0.02f * s, cx + 0.22f * s, cy + 0.02f * s, cx,
                                  cy + 0.44f * s);
        float hole = sdf::circle(px, py, cx, cy - 0.1f * s, 0.1f * s);
        return fmaxf(fminf(head, tip), -hole);
      });
      return;
    case Icon::Speaker:
      shape(c, cx, cy, s, color, alpha, [&](float px, float py) {
        float box = sdf::roundRect(px, py, cx - 0.4f * s, cy - 0.12f * s, 0.2f * s, 0.24f * s, 0.04f * s);
        float tri = sdf::triangle(px, py, cx - 0.26f * s, cy, cx + 0.0f * s, cy - 0.34f * s, cx + 0.0f * s,
                                  cy + 0.34f * s);
        float waves = fabsf(sdf::circle(px, py, cx + 0.02f * s, cy, 0.3f * s)) - lw * 0.45f;
        waves = fmaxf(waves, -(px - (cx + 0.14f * s)));
        return fmin3(box, tri, waves);
      });
      return;
    case Icon::Bell:
      shape(c, cx, cy, s, color, alpha, [&](float px, float py) {
        float dome = sdf::roundRect(px, py, cx - 0.28f * s, cy - 0.34f * s, 0.56f * s, 0.62f * s, 0.26f * s);
        float lip = sdf::roundRect(px, py, cx - 0.4f * s, cy + 0.16f * s, 0.8f * s, 0.12f * s, 0.06f * s);
        float clap = sdf::circle(px, py, cx, cy + 0.36f * s, 0.09f * s);
        return fmin3(dome, lip, clap);
      });
      return;
    case Icon::Sun:
      sun(c, cx, cy, s, color);
      return;
    case Icon::Moon:
      moon(c, cx, cy, s, color);
      return;
    case Icon::Bolt:
      shape(c, cx, cy, s, color, alpha, [&](float px, float py) {
        float a = sdf::triangle(px, py, cx + 0.12f * s, cy - 0.45f * s, cx - 0.28f * s, cy + 0.06f * s,
                                cx + 0.04f * s, cy + 0.06f * s);
        float b = sdf::triangle(px, py, cx - 0.12f * s, cy + 0.45f * s, cx + 0.28f * s, cy - 0.06f * s,
                                cx - 0.04f * s, cy - 0.06f * s);
        return fminf(a, b) - 0.3f;
      });
      return;
  }
}

void weather(Canvas& c, int code, bool isDay, float cx, float cy, float s) {
  const uint32_t kCloud = 0xE6EAF2;
  const uint32_t kDarkCloud = 0xA3ACBD;
  const uint32_t kMoon = 0xE9E3C9;
  if (code == 0) {
    if (isDay) sun(c, cx, cy, s, theme::kSun);
    else moon(c, cx, cy, s, kMoon);
    return;
  }
  if (code == 1 || code == 2) {
    if (isDay) sun(c, cx - 0.16f * s, cy - 0.14f * s, s * 0.72f, theme::kSun);
    else moon(c, cx - 0.14f * s, cy - 0.14f * s, s * 0.75f, kMoon);
    cloud(c, cx + 0.06f * s, cy + 0.1f * s, s * 0.82f, kCloud);
    return;
  }
  if (code == 3) {
    cloud(c, cx - 0.12f * s, cy - 0.1f * s, s * 0.7f, kDarkCloud);
    cloud(c, cx + 0.04f * s, cy + 0.06f * s, s * 0.9f, kCloud);
    return;
  }
  if (code == 45 || code == 48) {
    for (int i = 0; i < 4; ++i) {
      float y = cy - 0.24f * s + i * 0.16f * s;
      float off = (i % 2) ? 0.08f * s : 0.f;
      draw::line(c, cx - 0.38f * s + off, y, cx + 0.38f * s - (i % 2 ? 0 : 0.08f * s), y, fmaxf(2.f, 0.07f * s),
                 kDarkCloud);
    }
    return;
  }
  const bool storm = code >= 95;
  const bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
  const bool drizzle = code >= 51 && code <= 57;
  cloud(c, cx, cy - 0.12f * s, s * 0.9f, storm ? kDarkCloud : kCloud);
  const float top = cy + 0.16f * s;
  if (storm) {
    draw(c, Icon::Bolt, cx + 0.02f * s, top + 0.14f * s, s * 0.5f, theme::kSun);
  } else if (snow) {
    for (int i = 0; i < 3; ++i) draw::fillCircle(c, cx + (i - 1) * 0.22f * s, top + (i % 2) * 0.12f * s + 0.06f * s,
                                                 fmaxf(1.6f, 0.055f * s), 0xFFFFFF);
  } else if (drizzle) {
    for (int i = 0; i < 3; ++i) draw::fillCircle(c, cx + (i - 1) * 0.2f * s, top + 0.08f * s + (i % 2) * 0.1f * s,
                                                 fmaxf(1.3f, 0.04f * s), theme::kRain);
  } else {
    for (int i = 0; i < 3; ++i) {
      float x = cx + (i - 1) * 0.22f * s;
      draw::line(c, x + 0.05f * s, top, x - 0.03f * s, top + 0.22f * s, fmaxf(1.8f, 0.065f * s), theme::kRain);
    }
  }
}

void wifi(Canvas& c, int x, int y, int rssi, bool connected, uint32_t color) {
  int level = !connected ? 0 : rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
  for (int i = 0; i < 4; ++i) {
    int h = 3 + i * 2;
    draw::fillRoundRect(c, static_cast<float>(x + i * 3), static_cast<float>(y + 9 - h), 2.f, static_cast<float>(h),
                        0.8f, color, i < level ? 255 : 70);
  }
}

int battery(Canvas& c, int x, int y, int pct, bool charging, uint32_t color) {
  uint32_t fill = charging ? theme::kGood : (pct >= 0 && pct < 15 ? theme::kDanger : color);
  draw::strokeRoundRect(c, x + 0.5f, y + 0.5f, 18.f, 9.f, 2.5f, 1.f, color, 160);
  draw::fillRoundRect(c, x + 19.5f, y + 3.f, 1.8f, 4.f, 0.8f, color, 160);
  if (pct >= 0) {
    float w = 15.f * (pct > 100 ? 100 : pct) / 100.f;
    if (w > 0.5f) draw::fillRoundRect(c, x + 2.f, y + 2.f, w, 6.f, 1.5f, fill);
  }
  return 22;
}

const char* weatherLabel(int code) {
  switch (code) {
    case 0: return "Ciel dégagé";
    case 1: return "Peu nuageux";
    case 2: return "Éclaircies";
    case 3: return "Couvert";
    case 45:
    case 48: return "Brouillard";
    case 51:
    case 53:
    case 55: return "Bruine";
    case 56:
    case 57: return "Bruine verglaçante";
    case 61: return "Pluie faible";
    case 63: return "Pluie";
    case 65: return "Forte pluie";
    case 66:
    case 67: return "Pluie verglaçante";
    case 71: return "Neige faible";
    case 73: return "Neige";
    case 75: return "Forte neige";
    case 77: return "Grésil";
    case 80:
    case 81: return "Averses";
    case 82: return "Fortes averses";
    case 85:
    case 86: return "Averses de neige";
    case 95: return "Orage";
    case 96:
    case 99: return "Orage de grêle";
    default: return "";
  }
}

}  // namespace icons
