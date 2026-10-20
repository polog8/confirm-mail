#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../format.h"
#include "../theme.h"
#include "apps.h"

namespace evt {

int current(const CalendarState& cal, int64_t now) {
  for (int i = 0; i < cal.count; ++i) {
    const CalendarEvent& e = cal.events[i];
    if (!e.allDay && e.start <= now && now < e.end) return i;
  }
  return -1;
}

int next(const CalendarState& cal, int64_t now) {
  for (int i = 0; i < cal.count; ++i) {
    const CalendarEvent& e = cal.events[i];
    if (!e.allDay && e.start > now) return i;
  }
  return -1;
}

uint32_t color(const CalendarState& cal, const CalendarEvent& e) {
  if (e.calendar < cal.numCalendars) return cal.calendars[e.calendar].color;
  return theme::kAccent;
}

void range(char* out, size_t n, const PosixTz& tz, const CalendarEvent& e) {
  char a[8], b[8];
  fmt::hhmm(a, sizeof(a), tz, e.start);
  fmt::hhmm(b, sizeof(b), tz, e.end);
  if (e.end > e.start) snprintf(out, n, "%s \xE2\x80\x93 %s", a, b);  // en dash
  else snprintf(out, n, "%s", a);
}

}  // namespace evt

namespace {

// "Pluie probable vers 17 h" when an upcoming hour crosses 50 %.
bool rainHint(const WeatherState& w, const Frame& f, char* out, size_t n) {
  if (!w.valid || w.hourStart == 0) return false;
  for (int i = 0; i < 12; ++i) {
    int64_t t = w.hourStart + i * 3600;
    if (t + 3600 <= f.now) continue;
    if (w.precip[i] >= 50) {
      LocalTime lt = fmt::localTime(*f.tz, t);
      if (t <= f.now) snprintf(out, n, "Pluie en cours ou imminente (%d %%)", w.precip[i]);
      else snprintf(out, n, "Pluie probable vers %d h (%d %%)", lt.hh, w.precip[i]);
      return true;
    }
  }
  return false;
}

}  // namespace

uint32_t HomeApp::refreshMs(const Frame& f) const { return f.s->play.isPlaying ? 1000 : 2000; }

void HomeApp::render(draw::Canvas& c, const Frame& f) {
  const AppSnapshot& s = *f.s;
  char buf[96];
  if (s.sys.timeValid) snprintf(buf, sizeof(buf), "%02d:%02d", f.lt.hh, f.lt.mm);
  else snprintf(buf, sizeof(buf), "--:--");
  draw::text(c, Font::Clock, buf, 10, 96, theme::kText);
  if (s.sys.timeValid) {
    fmt::longDate(buf, sizeof(buf), f.lt);
    draw::text(c, Font::BodyBold, buf, 15, 122, theme::kText2);
  } else {
    draw::text(c, Font::BodyBold, "Synchronisation de l'heure…", 15, 122, theme::kText3);
  }
  if (settings_.weatherEnabled) renderWeather(c, f);
  renderEventCard(c, f, 10, 134, 300, 52);
  renderFooter(c, f);
}

void HomeApp::renderWeather(draw::Canvas& c, const Frame& f) {
  const WeatherState& w = f.s->weather;
  if (!w.valid) return;
  char buf[48];
  icons::weather(c, w.code, w.isDay, 226, 47, 36);
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", static_cast<int>(lroundf(w.temp)));
  draw::text(c, Font::Large, buf, 308, 60, theme::kText, 0, draw::Right);
  snprintf(buf, sizeof(buf), "\xE2\x86\x91%d\xC2\xB0  \xE2\x86\x93%d\xC2\xB0", static_cast<int>(lroundf(w.tmax)),
           static_cast<int>(lroundf(w.tmin)));
  draw::text(c, Font::Small, buf, 308, 81, theme::kText2, 0, draw::Right);
  draw::text(c, Font::Small, icons::weatherLabel(w.code), 308, 98, theme::kText3, 110, draw::Right);
}

void HomeApp::renderEventCard(draw::Canvas& c, const Frame& f, int x, int y, int w, int h) {
  const CalendarState& cal = f.s->cal;
  draw::fillRoundRect(c, x, y, w, h, 12, theme::kText, 14);
  char line1[96], line2[48];
  if (!settings_.calendarEnabled) {
    icons::draw(c, Icon::Today, x + 22, y + h / 2, 16, theme::kText3);
    draw::text(c, Font::BodyBold, "Agenda non configuré", x + 40, y + 22, theme::kText2);
    draw::text(c, Font::Small, "iCloud ou lien iCal : voir config.h", x + 40, y + 40, theme::kText3);
    return;
  }
  const int cur = evt::current(cal, f.now);
  const int nxt = evt::next(cal, f.now);
  const int idx = cur >= 0 ? cur : nxt;
  if (idx < 0) {
    icons::draw(c, Icon::Check, x + 22, y + h / 2, 16, theme::kGood);
    draw::text(c, Font::BodyBold, cal.updatedAt ? "Rien de prévu" : "Agenda en cours de synchro…", x + 40, y + 22,
               theme::kText);
    draw::text(c, Font::Small, cal.updatedAt ? "Aucun rendez-vous dans les 7 jours" : "", x + 40, y + 40,
               theme::kText3);
    return;
  }
  const CalendarEvent& e = cal.events[idx];
  const uint32_t col = evt::color(cal, e);
  draw::fillRoundRect(c, x + 10, y + 10, 4, h - 20, 2, col);

  uint32_t line1Color = theme::kText2;
  char range[24];
  evt::range(range, sizeof(range), *f.tz, e);
  if (idx == cur) {
    char end[8];
    fmt::hhmm(end, sizeof(end), *f.tz, e.end);
    snprintf(line1, sizeof(line1), "En cours \xC2\xB7 jusqu'à %s", end);
    line1Color = draw::withMinLuminance(col, 0.55f);
  } else {
    const int64_t eventDay = f.tz->localDay(e.start);
    const int64_t delta = e.start - f.now;
    if (eventDay == f.lt.day && delta <= 3 * 3600) {
      fmt::until(line2, sizeof(line2), delta);
      fmt::capitalize(line2);
      snprintf(line1, sizeof(line1), "%s \xC2\xB7 %s", line2, range);
      if (delta <= settings_.alertLeadMin * 60) line1Color = theme::kWarn;
    } else if (eventDay == f.lt.day) {
      snprintf(line1, sizeof(line1), "Aujourd'hui \xC2\xB7 %s", range);
    } else if (eventDay == f.lt.day + 1) {
      snprintf(line1, sizeof(line1), "Demain \xC2\xB7 %s", range);
    } else {
      int yy, mm, dd;
      civ::civilFromDays(eventDay, yy, mm, dd);
      snprintf(line1, sizeof(line1), "%s %d \xC2\xB7 %s", fmt::dayShort(civ::weekdayFromDays(eventDay)), dd, range);
      fmt::capitalize(line1);
    }
  }

  // Remaining timed events today (besides the one shown).
  int remaining = 0;
  for (int i = 0; i < cal.count; ++i) {
    const CalendarEvent& o = cal.events[i];
    if (i != idx && !o.allDay && o.start > f.now && f.tz->localDay(o.start) == f.lt.day) ++remaining;
  }
  const int right = x + w - 12;
  int textRight = right;
  if (e.url[0]) {
    icons::draw(c, Icon::Video, right - 7, y + 37, 14, draw::withMinLuminance(col, 0.5f));
    textRight -= 22;
  }
  if (remaining > 0) {
    char more[24];
    snprintf(more, sizeof(more), "+%d auj.", remaining);
    draw::text(c, Font::Small, more, right, y + 20, theme::kText3, 0, draw::Right);
  }
  draw::text(c, Font::Small, line1, x + 22, y + 20, line1Color, w - 90);
  draw::text(c, Font::BodyBold, e.title, x + 22, y + 41, theme::kText, textRight - (x + 22));
}

void HomeApp::renderFooter(draw::Canvas& c, const Frame& f) {
  const PlaybackState& p = f.s->play;
  char buf[200];
  if (settings_.musicEnabled && p.hasItem && (p.status == SpotifyStatus::Ok || p.status == SpotifyStatus::Error)) {
    const uint32_t col = p.isPlaying ? theme::kText2 : theme::kText3;
    icons::draw(c, p.isPlaying ? Icon::Play : Icon::Pause, 19, 199, 9, col);
    snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s", p.title, p.artist);
    draw::text(c, Font::Small, buf, 28, 203, col, 278);
    if (p.durationMs) {
      float frac = static_cast<float>(p.positionAt(f.ms)) / p.durationMs;
      draw::fillRoundRect(c, 14, 207, 292, 2, 1, theme::kText, 26);
      draw::fillRoundRect(c, 14, 207, 292 * frac, 2, 1, theme::kText, p.isPlaying ? 150 : 70);
    }
    return;
  }
  if (settings_.weatherEnabled && rainHint(f.s->weather, f, buf, sizeof(buf))) {
    icons::weather(c, 61, true, 22, 200, 16);
    draw::text(c, Font::Small, buf, 36, 204, theme::kText2, 270);
  }
}

Buttons HomeApp::buttons(const Frame& f) const {
  Buttons b;
  if (!settings_.musicEnabled) return b;
  const PlaybackState& p = f.s->play;
  b.b[0].icon = Icon::Prev;
  b.b[1].icon = p.isPlaying ? Icon::Pause : Icon::Play;
  b.b[1].accent = true;
  b.b[2].icon = Icon::Next;
  return b;
}

void HomeApp::onButton(int idx, bool longPress, const Frame& f, Actions& a) {
  if (!settings_.musicEnabled) return;
  if (idx == 0) a.spotify(SpotifyCmd::Previous);
  if (idx == 1) a.spotify(longPress ? SpotifyCmd::ToggleLike : SpotifyCmd::PlayPause);
  if (idx == 2) a.spotify(SpotifyCmd::Next);
}
