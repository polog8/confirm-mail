#include <stdio.h>
#include <string.h>

#include "../format.h"
#include "../theme.h"
#include "apps.h"

namespace {

const char* spotifyText(const PlaybackState& p) {
  switch (p.status) {
    case SpotifyStatus::Disabled: return "Désactivé";
    case SpotifyStatus::Connecting: return "Connexion…";
    case SpotifyStatus::NeedsAuth: return "À autoriser (page web)";
    case SpotifyStatus::AuthError: return "Autorisation refusée";
    case SpotifyStatus::PremiumRequired: return "Premium requis";
    case SpotifyStatus::NoDevice: return "Connecté \xC2\xB7 aucun appareil";
    case SpotifyStatus::Ok: return p.isPlaying ? "Connecté \xC2\xB7 lecture" : "Connecté";
    case SpotifyStatus::Error: return "Erreur réseau";
  }
  return "";
}

void row(draw::Canvas& c, int y, const char* label, const char* value, uint32_t color = theme::kText) {
  draw::text(c, Font::Small, label, 14, y, theme::kText3);
  draw::text(c, Font::Small, value, 306, y, color, 210, draw::Right);
}

}  // namespace

bool SystemApp::consumeDirty() {
  bool d = dirty_;
  dirty_ = false;
  return d;
}

void SystemApp::render(draw::Canvas& c, const Frame& f) {
  const AppSnapshot& s = *f.s;
  char buf[96], t[8];
  int y = 44;
  const int step = 17;

  if (s.sys.wifi) snprintf(buf, sizeof(buf), "%s \xC2\xB7 \xE2\x88\x92%d dBm", s.sys.ssid, -s.sys.rssi);
  else snprintf(buf, sizeof(buf), "Déconnecté");
  row(c, y, "Wi-Fi", buf, s.sys.wifi ? theme::kText : theme::kWarn);
  y += step;

  if (s.sys.ip[0]) snprintf(buf, sizeof(buf), "http://%s/", s.sys.ip);
  else snprintf(buf, sizeof(buf), "\xE2\x80\x94");
  row(c, y, "Page web", buf);
  y += step;

  if (s.sys.battery >= 0) {
    snprintf(buf, sizeof(buf), "%d %% \xC2\xB7 %.2f V%s", s.sys.battery, s.sys.batteryVolts,
             s.sys.charging ? " \xC2\xB7 en charge" : (s.sys.usbPower ? " \xC2\xB7 secteur" : ""));
    for (char* p = buf; *p; ++p) if (*p == '.') *p = ',';
  } else {
    snprintf(buf, sizeof(buf), "\xE2\x80\x94");
  }
  row(c, y, "Batterie", buf);
  y += step;

  const PlaybackState& p = s.play;
  row(c, y, "Spotify", spotifyText(p),
      p.status == SpotifyStatus::Ok || p.status == SpotifyStatus::NoDevice ? theme::kText : theme::kWarn);
  y += step;

  if (s.cal.error[0]) {
    row(c, y, "Agenda", s.cal.error, theme::kWarn);
  } else if (s.cal.updatedAt) {
    fmt::hhmm(t, sizeof(t), *f.tz, s.cal.updatedAt);
    snprintf(buf, sizeof(buf), "%d évén. \xC2\xB7 sync %s", s.cal.count, t);
    row(c, y, "Agenda", buf);
  } else {
    row(c, y, "Agenda", s.cal.numCalendars ? "En attente…" : "Non configuré", theme::kText2);
  }
  y += step;

  if (s.weather.updatedAt) {
    fmt::hhmm(t, sizeof(t), *f.tz, s.weather.updatedAt);
    snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s", s.weather.place, t);
    row(c, y, "Météo", buf);
  } else {
    row(c, y, "Météo", "En attente…", theme::kText2);
  }
  y += step;

  snprintf(buf, sizeof(buf), "RAM %u Ko \xC2\xB7 PSRAM %u,%u Mo", static_cast<unsigned>(s.sys.freeHeap / 1024),
           static_cast<unsigned>(s.sys.freePsram / 1048576u),
           static_cast<unsigned>((s.sys.freePsram % 1048576u) * 10u / 1048576u));
  row(c, y, "Mémoire", buf);
  y += step;

  const uint32_t up = s.sys.uptimeS;
  if (up >= 86400) snprintf(buf, sizeof(buf), "%s \xC2\xB7 %u j %u h", version_, up / 86400, (up / 3600) % 24);
  else snprintf(buf, sizeof(buf), "%s \xC2\xB7 %u h %02u", version_, up / 3600, (up / 60) % 60);
  row(c, y, "Version", buf);
  y += step;

  if (s.sys.resetReason[0]) {
    row(c, y, "Démarrage", s.sys.resetReason, s.sys.abnormalReset ? theme::kWarn : theme::kText);
    y += step;
  }

  // Brightness slider
  draw::text(c, Font::Small, "Luminosité", 14, y + 4, theme::kText3);
  const float bx = 186, bw = 90;
  draw::fillRoundRect(c, bx, y - 1, bw, 6, 3, theme::kText, 40);
  draw::fillRoundRect(c, bx, y - 1, bw * brightness_ / 100.f, 6, 3, theme::kAccent);
  snprintf(buf, sizeof(buf), "%d %%", brightness_);
  draw::text(c, Font::Small, buf, 306, y + 4, theme::kText, 0, draw::Right);
}

Buttons SystemApp::buttons(const Frame& f) const {
  Buttons b;
  b.b[0].icon = Icon::Minus;
  b.b[1].icon = Icon::Sync;
  b.b[1].label = "Sync";
  b.b[2].icon = Icon::Plus;
  return b;
}

void SystemApp::onButton(int idx, bool longPress, const Frame& f, Actions& a) {
  if (idx == 0) a.changeBrightness(-10);
  if (idx == 2) a.changeBrightness(+10);
  if (idx == 1) {
    a.refreshAll();
    a.feedback(Feedback::Success);
  }
  brightness_ = a.brightnessPercent();
  dirty_ = true;
}
