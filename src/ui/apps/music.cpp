#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../chrome.h"
#include "../format.h"
#include "../theme.h"
#include "apps.h"

namespace {

constexpr int kArtX = 12;
constexpr int kArtY = 28;
constexpr int kArt = 150;
constexpr int kArtR = 12;
constexpr int kColX = 174;
constexpr int kColW = 134;
constexpr int kBarX = 12;
constexpr int kBarW = 296;
constexpr int kBarY = 190;

// Dominant "vivid" color of the cover: hue histogram weighted by saturation.
uint32_t dominantColor(draw::Canvas& art) {
  auto* px = static_cast<lgfx::swap565_t*>(art.getBuffer());
  if (!px) return theme::kAccent;
  float sumR[12] = {0}, sumG[12] = {0}, sumB[12] = {0}, weight[12] = {0};
  float avgR = 0, avgG = 0, avgB = 0;
  int n = 0;
  const int w = art.width(), h = art.height();
  for (int y = 0; y < h; y += 3) {
    for (int x = 0; x < w; x += 3) {
      const lgfx::swap565_t& p = px[y * w + x];
      float r = p.R8() / 255.f, g = p.G8() / 255.f, b = p.B8() / 255.f;
      avgR += r;
      avgG += g;
      avgB += b;
      ++n;
      float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
      float sat = mx > 0 ? (mx - mn) / mx : 0;
      if (sat < 0.25f || mx < 0.2f) continue;
      float hue;
      float d = mx - mn;
      if (mx == r) hue = fmodf((g - b) / d, 6.f);
      else if (mx == g) hue = (b - r) / d + 2.f;
      else hue = (r - g) / d + 4.f;
      if (hue < 0) hue += 6.f;
      int bucket = static_cast<int>(hue * 2.f) % 12;
      float wgt = sat * mx;
      sumR[bucket] += r * wgt;
      sumG[bucket] += g * wgt;
      sumB[bucket] += b * wgt;
      weight[bucket] += wgt;
    }
  }
  int best = -1;
  float bestW = 0;
  for (int i = 0; i < 12; ++i) {
    if (weight[i] > bestW) {
      bestW = weight[i];
      best = i;
    }
  }
  if (best >= 0 && bestW > n * 0.02f) {
    return draw::rgb(static_cast<int>(sumR[best] / bestW * 255), static_cast<int>(sumG[best] / bestW * 255),
                     static_cast<int>(sumB[best] / bestW * 255));
  }
  // Mostly grey artwork: muted average with a hint of the theme accent.
  uint32_t avg = draw::rgb(static_cast<int>(avgR / n * 255), static_cast<int>(avgG / n * 255),
                           static_cast<int>(avgB / n * 255));
  return draw::mix(avg, theme::kAccent, 0.25f);
}

}  // namespace

MusicApp::MusicApp() : accent_(theme::kAccent), barColor_(theme::kText) {}

void MusicApp::setArt(const uint8_t* jpg, size_t len) {
  if (!art_.getBuffer()) {
    art_.setColorDepth(16);
#if defined(ESP_PLATFORM)
    art_.setPsram(true);
#endif
    art_.createSprite(kArt, kArt);
  }
  artOk_ = false;
  if (jpg && len && art_.getBuffer()) {
    art_.fillScreen(0x000000u);
    artOk_ = art_.drawJpg(jpg, len, 0, 0, kArt, kArt, 0, 0, 0.f, 0.f);
  }
  accent_ = artOk_ ? dominantColor(art_) : theme::kAccent;
  backdropOk_ = false;
  dirty_ = true;
}

void MusicApp::rebuildBackdrop() {
  if (!backdrop_.getBuffer() && !chrome::createScreenCanvas(backdrop_)) return;
  const uint32_t top = draw::withMaxLuminance(draw::withMinLuminance(accent_, 0.12f), 0.16f);
  const uint32_t deep = draw::withMaxLuminance(accent_, 0.24f);
  draw::verticalGradient(backdrop_, 0, 0, theme::W, theme::H, draw::mix(top, 0x000000, 0.15f),
                         draw::mix(top, 0x050506, 0.88f));
  draw::radialGlow(backdrop_, kArtX + kArt / 2, kArtY + kArt / 2, 190, deep, 0.45f);
  draw::shadow(backdrop_, kArtX, kArtY, kArt, kArt, kArtR, 14, 150, 6);
  if (artOk_) {
    draw::blitRounded(backdrop_, art_, kArtX, kArtY, kArtR);
  } else {
    // Placeholder sleeve.
    draw::fillRoundRect(backdrop_, kArtX, kArtY, kArt, kArt, kArtR, draw::mix(accent_, 0x15161A, 0.75f));
    draw::radialGlow(backdrop_, kArtX + 40, kArtY + 30, 120, accent_, 0.25f);
    draw::text(backdrop_, Font::Large, "\xE2\x99\xAB", kArtX + kArt / 2, kArtY + kArt / 2 + 10,
               draw::mix(accent_, 0xFFFFFF, 0.5f), 0, draw::Center);
  }
  barColor_ = draw::withMinLuminance(draw::lighten(accent_, 0.35f), 0.6f);
  backdropOk_ = true;
}

bool MusicApp::consumeDirty() {
  bool d = dirty_;
  dirty_ = false;
  return d;
}

uint32_t MusicApp::refreshMs(const Frame& f) const {
  if (likeAnimOn_ || volumeTarget_ >= 0) return 40;
  return f.s->play.isPlaying ? 250 : 1000;
}

void MusicApp::renderMessage(draw::Canvas& c, const Frame& f, const char* title, const char* body, bool qr) {
  int lines = draw::wrapped(c, Font::Title, title, kColX, 50, kColW, 22, 3, theme::kText);
  draw::wrapped(c, Font::Body, body, kColX, 50 + lines * 22 + 4, kColW, 17, 6, theme::kText2);
  if (qr && f.s->sys.ip[0]) {
    char url[40];
    snprintf(url, sizeof(url), "http://%s/", f.s->sys.ip);
    draw::fillRoundRect(c, kArtX, kArtY, kArt, kArt, kArtR, 0xFFFFFF);
    c.qrcode(url, kArtX + 11, kArtY + 11, kArt - 22, 2);
    draw::text(c, Font::Small, url, kArtX + kArt / 2, kArtY + kArt + 20, theme::kText2, kArt + 20, draw::Center);
  }
}

void MusicApp::render(draw::Canvas& c, const Frame& f) {
  const PlaybackState& p = f.s->play;
  if (!backdropOk_) rebuildBackdrop();
  if (backdropOk_) memcpy(c.getBuffer(), backdrop_.getBuffer(), static_cast<size_t>(theme::W) * theme::H * 2);
  else chrome::background(c);

  switch (p.status) {
    case SpotifyStatus::Disabled:
      renderMessage(c, f, "Spotify désactivé", "Renseignez la section Spotify de include/config.h.", false);
      return;
    case SpotifyStatus::Connecting:
      renderMessage(c, f, "Connexion…", "Wi-Fi et jeton Spotify en cours.", false);
      return;
    case SpotifyStatus::NeedsAuth:
      renderMessage(c, f, "Autoriser Spotify", "Scannez le QR code ou ouvrez l'adresse depuis un ordinateur du réseau.",
                    true);
      return;
    case SpotifyStatus::AuthError:
      renderMessage(c, f, "Autorisation expirée", "Reconnectez Spotify via le QR code (page de la station).", true);
      return;
    case SpotifyStatus::PremiumRequired:
      renderMessage(c, f, "Premium requis", "Spotify réserve le contrôle de lecture aux comptes Premium.", false);
      return;
    default:
      break;
  }
  if (!p.hasItem) {
    renderMessage(c, f, "Rien en lecture",
                  p.status == SpotifyStatus::Error ? "Spotify injoignable, nouvel essai en cours."
                                                   : "Lancez Spotify sur un appareil, ou appuyez sur \xE2\x96\xB6 pour "
                                                     "reprendre sur le dernier utilisé.",
                  false);
    return;
  }

  // Text column
  int y = 47;
  int n = draw::wrapped(c, Font::Title, p.title, kColX, y, kColW, 22, 3, theme::kText);
  y += (n - 1) * 22 + 21;
  n = draw::wrapped(c, Font::Body, p.artist, kColX, y, kColW, 17, 2, theme::kText2);
  y += (n - 1) * 17 + 18;
  if (y < 160) draw::text(c, Font::Small, p.album, kColX, y, theme::kText3, kColW);
  if (p.status == SpotifyStatus::Error) {
    draw::text(c, Font::Small, "Hors ligne \xC2\xB7 reconnexion…", kColX, 175, theme::kWarn, kColW);
  } else if (p.device[0]) {
    icons::draw(c, Icon::Speaker, kColX + 6, 171, 12, theme::kText3);
    draw::text(c, Font::Small, p.device, kColX + 16, 175, theme::kText3, kColW - 16);
  }

  // Liked badge
  if (p.liked) {
    draw::fillCircle(c, kArtX + kArt - 16, kArtY + kArt - 16, 11, 0x000000, 150);
    icons::draw(c, Icon::Heart, kArtX + kArt - 16, kArtY + kArt - 15, 12, theme::kSpotify);
  }
  // Like animation
  if (likeAnimOn_) {
    float t = (f.ms - likeAnimAt_) / 700.f;
    if (t >= 1.f) {
      likeAnimOn_ = false;
    } else {
      float size = 40.f + 40.f * t;
      uint8_t alpha = static_cast<uint8_t>(230 * (1.f - t));
      icons::draw(c, p.liked ? Icon::Heart : Icon::HeartOutline, kArtX + kArt / 2, kArtY + kArt / 2, size,
                  p.liked ? theme::kSpotify : theme::kText, alpha);
    }
  }

  // Progress
  const uint32_t pos = p.positionAt(f.ms);
  const float frac = p.durationMs ? static_cast<float>(pos) / p.durationMs : 0.f;
  draw::fillRoundRect(c, kBarX, kBarY - 2, kBarW, 4, 2, theme::kText, 45);
  if (frac > 0.f) draw::fillRoundRect(c, kBarX, kBarY - 2, fmaxf(4.f, kBarW * frac), 4, 2, barColor_);
  draw::fillCircle(c, kBarX + kBarW * frac, kBarY, p.isPlaying ? 5.f : 4.f, theme::kText);
  char a[12], b[14];
  fmt::mmss(a, sizeof(a), pos);
  fmt::mmss(b + 1, sizeof(b) - 1, p.durationMs > pos ? p.durationMs - pos : 0);
  b[0] = '-';
  draw::text(c, Font::Small, a, kBarX, 207, theme::kText2);
  draw::text(c, Font::Small, b, kBarX + kBarW, 207, theme::kText2, 0, draw::Right);

  // Volume overlay
  if (volumeTarget_ >= 0) {
    if (f.ms - volumeShownAt_ > 1500) {
      volumeTarget_ = -1;
    } else {
      const float x = 284, top = 40, h = 120;
      draw::fillRoundRect(c, x - 6, top - 6, 30, h + 32, 15, 0x000000, 150);
      draw::fillRoundRect(c, x + 3, top, 12, h, 6, theme::kText, 50);
      float fh = h * volumeTarget_ / 100.f;
      if (fh > 1) draw::fillRoundRect(c, x + 3, top + h - fh, 12, fh, 6, barColor_);
      char v[8];
      snprintf(v, sizeof(v), "%d", volumeTarget_);
      draw::text(c, Font::Small, v, static_cast<int>(x + 9), static_cast<int>(top + h + 18), theme::kText, 0,
                 draw::Center);
    }
  }
}

Buttons MusicApp::buttons(const Frame& f) const {
  Buttons b;
  const PlaybackState& p = f.s->play;
  switch (p.status) {
    case SpotifyStatus::Ok:
    case SpotifyStatus::NoDevice:
    case SpotifyStatus::Error:
      b.b[0].icon = Icon::Prev;
      b.b[1].icon = p.isPlaying ? Icon::Pause : Icon::Play;
      b.b[1].accent = true;
      b.b[2].icon = Icon::Next;
      b.b[0].disabled = b.b[2].disabled = !p.hasItem;
      break;
    default:
      break;
  }
  return b;
}

void MusicApp::onButton(int idx, bool longPress, const Frame& f, Actions& a) {
  if (idx == 0) a.spotify(longPress ? SpotifyCmd::Seek : SpotifyCmd::Previous, 0);
  if (idx == 1) {
    if (longPress) {
      a.spotify(SpotifyCmd::ToggleLike);
      likeAnimOn_ = true;
      likeAnimAt_ = f.ms;
    } else {
      a.spotify(SpotifyCmd::PlayPause);
    }
  }
  if (idx == 2) a.spotify(SpotifyCmd::Next);
  dirty_ = true;
}

void MusicApp::onTap(int x, int y, const Frame& f, Actions& a) {
  const PlaybackState& p = f.s->play;
  if (!p.hasItem) return;
  if (x >= kArtX && x < kArtX + kArt && y >= kArtY && y < kArtY + kArt) {
    a.spotify(SpotifyCmd::ToggleLike);
    likeAnimOn_ = true;
    likeAnimAt_ = f.ms;
    dirty_ = true;
  } else if (y >= kBarY - 12 && y <= kBarY + 12 && p.durationMs) {
    float frac = static_cast<float>(x - kBarX) / kBarW;
    frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
    a.spotify(SpotifyCmd::Seek, static_cast<int32_t>(frac * p.durationMs));
    dirty_ = true;
  }
}

void MusicApp::onVSwipe(int dy, const Frame& f, Actions& a) {
  const PlaybackState& p = f.s->play;
  if (!p.supportsVolume || p.volume < 0) return;
  int base = volumeTarget_ >= 0 ? volumeTarget_ : p.volume;
  int step = dy < 0 ? 10 : -10;  // swipe up = louder
  int v = base + step;
  v = v < 0 ? 0 : v > 100 ? 100 : v;
  volumeTarget_ = v;
  volumeShownAt_ = f.ms;
  a.spotify(SpotifyCmd::Volume, v);
  dirty_ = true;
}
