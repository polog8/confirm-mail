#include <stdio.h>
#include <string.h>

#include "../format.h"
#include "../theme.h"
#include "apps.h"

namespace {
constexpr int kRowH = 44;
constexpr int kListBottom = 211;
constexpr int kMaxDays = 7;
}  // namespace

int AgendaApp::collect(const Frame& f, int64_t day, int* timed, int maxTimed, int* allDay, int maxAllDay,
                       int* nAllDay) const {
  const CalendarState& cal = f.s->cal;
  const int64_t dayStart = f.tz->dayStartUtc(day);
  const int64_t dayEnd = f.tz->dayStartUtc(day + 1);
  int n = 0;
  *nAllDay = 0;
  for (int i = 0; i < cal.count; ++i) {
    const CalendarEvent& e = cal.events[i];
    const bool overlaps = e.start < dayEnd && (e.end > dayStart || (e.end == e.start && e.start >= dayStart));
    if (!overlaps) continue;
    if (e.allDay || (e.start <= dayStart && e.end >= dayEnd)) {
      if (*nAllDay < maxAllDay) allDay[(*nAllDay)++] = i;
    } else if (n < maxTimed) {
      timed[n++] = i;
    }
  }
  return n;
}

void AgendaApp::onShow(const Frame& f) {
  dayOffset_ = 0;
  scroll_ = 0;
  autoScroll_ = true;
}

bool AgendaApp::consumeDirty() {
  bool d = dirty_;
  dirty_ = false;
  return d;
}

void AgendaApp::render(draw::Canvas& c, const Frame& f) {
  const CalendarState& cal = f.s->cal;
  const int64_t day = f.lt.day + dayOffset_;
  char buf[96];

  fmt::dayLabel(buf, sizeof(buf), day, f.lt.day);
  draw::text(c, Font::Title, buf, 14, 46, theme::kText, 200);
  if (dayOffset_ <= 1) {
    int y, m, d;
    civ::civilFromDays(day, y, m, d);
    snprintf(buf, sizeof(buf), "%s %d %s", fmt::dayShort(civ::weekdayFromDays(day)), d, fmt::monthShort(m));
    draw::text(c, Font::Small, buf, 306, 45, theme::kText3, 0, draw::Right);
  }

  int timed[24], allDay[8], nAllDay = 0;
  const int n = collect(f, day, timed, 24, allDay, 8, &nAllDay);

  int listTop = 56;
  if (nAllDay > 0) {
    int x = 14;
    for (int k = 0; k < nAllDay; ++k) {
      const CalendarEvent& e = cal.events[allDay[k]];
      uint32_t col = evt::color(cal, e);
      int w = draw::textWidth(c, Font::Small, e.title) + 18;
      if (w > 200) w = 200;
      if (x + w > 306) {
        snprintf(buf, sizeof(buf), "+%d", nAllDay - k);
        draw::text(c, Font::Small, buf, 306, listTop + 14, theme::kText3, 0, draw::Right);
        break;
      }
      draw::fillRoundRect(c, x, listTop, w, 19, 9.5f, col, 60);
      draw::fillCircle(c, x + 8, listTop + 9.5f, 2.5f, col);
      draw::text(c, Font::Small, e.title, x + 14, listTop + 14, theme::kText, w - 18);
      x += w + 6;
    }
    listTop += 25;
  }

  if (n == 0) {
    const int cy = (listTop + kListBottom) / 2;
    if (!cal.updatedAt) {
      draw::text(c, Font::BodyBold, "Synchronisation…", 160, cy, theme::kText2, 0, draw::Center);
    } else {
      icons::draw(c, Icon::Check, 160, cy - 18, 22, theme::kGood);
      draw::text(c, Font::BodyBold, nAllDay ? "Aucun rendez-vous" : "Journée libre", 160, cy + 14, theme::kText2,
                 0, draw::Center);
    }
  } else {
    const int listH = kListBottom - listTop;
    maxScroll_ = n * kRowH - listH;
    if (maxScroll_ < 0) maxScroll_ = 0;
    if (autoScroll_) {
      // Bring the first event that is not over yet into view.
      int first = 0;
      while (first < n && cal.events[timed[first]].end <= f.now) ++first;
      scroll_ = first >= n ? maxScroll_ : first * kRowH - 6;
      autoScroll_ = false;
    }
    if (scroll_ > maxScroll_) scroll_ = maxScroll_;
    if (scroll_ < 0) scroll_ = 0;

    c.setClipRect(0, listTop, theme::W, listH);
    bool nowDrawn = dayOffset_ != 0;
    for (int k = 0; k < n; ++k) {
      const CalendarEvent& e = cal.events[timed[k]];
      const int y0 = listTop + k * kRowH - scroll_;
      if (!nowDrawn && e.start > f.now) {
        // "Now" marker between the previous and this row.
        const int ny = y0 - 2;
        draw::fillCircle(c, 59, ny, 3.5f, theme::kDanger);
        draw::fillRoundRect(c, 62, ny - 0.75f, 244, 1.5f, 0.75f, theme::kDanger, 200);
        nowDrawn = true;
      }
      if (y0 > kListBottom || y0 + kRowH < listTop) continue;
      const bool past = e.end <= f.now && e.end != e.start;
      const bool ongoing = e.start <= f.now && f.now < e.end;
      const uint32_t col = evt::color(cal, e);
      if (ongoing) draw::fillRoundRect(c, 6, y0 + 1, 308, kRowH - 4, 10, col, 34);
      const uint32_t t1 = past ? theme::kText3 : theme::kText;
      const uint32_t t2 = past ? theme::kText3 : theme::kText2;

      char a[8];
      fmt::hhmm(a, sizeof(a), *f.tz, e.start);
      draw::text(c, Font::Small, a, 14, y0 + 17, t1);
      if (e.end > e.start) {
        fmt::hhmm(a, sizeof(a), *f.tz, e.end);
        draw::text(c, Font::Small, a, 14, y0 + 33, theme::kText3);
      }
      draw::fillRoundRect(c, 56, y0 + 5, 3, kRowH - 14, 1.5f, col, past ? 110 : 255);
      int textX = 68;
      draw::text(c, Font::BodyBold, e.title, textX, y0 + 18, t1, 306 - textX);
      int subX = textX;
      if (e.url[0]) {
        icons::draw(c, Icon::Video, subX + 6, y0 + 29, 11, past ? theme::kText3 : draw::withMinLuminance(col, 0.5f));
        subX += 16;
      }
      const char* sub = e.location[0] ? e.location : (e.url[0] ? "Visio" : "");
      draw::text(c, Font::Small, sub, subX, y0 + 34, t2, 306 - subX);
    }
    if (!nowDrawn && dayOffset_ == 0) {
      const int ny = listTop + n * kRowH - scroll_ - 2;
      if (ny < kListBottom) {
        draw::fillCircle(c, 59, ny, 3.5f, theme::kDanger);
        draw::fillRoundRect(c, 62, ny - 0.75f, 244, 1.5f, 0.75f, theme::kDanger, 200);
      }
    }
    c.clearClipRect();
    // Scroll hint
    if (maxScroll_ > 0) {
      const float trackH = static_cast<float>(listH - 8);
      const float thumbH = trackH * listH / (n * kRowH);
      const float thumbY = listTop + 4 + (trackH - thumbH) * scroll_ / maxScroll_;
      draw::fillRoundRect(c, 313, thumbY, 3, thumbH, 1.5f, theme::kText, 60);
    }
  }

  if (cal.error[0]) {
    draw::text(c, Font::Small, cal.error, 160, 210, theme::kWarn, 290, draw::Center);
  }
}

Buttons AgendaApp::buttons(const Frame& f) const {
  Buttons b;
  b.b[0].icon = Icon::ChevronLeft;
  b.b[0].disabled = dayOffset_ == 0;
  if (dayOffset_ == 0) {
    b.b[1].icon = Icon::Sync;
  } else {
    b.b[1].icon = Icon::Today;
    b.b[1].label = "Auj.";
  }
  b.b[2].icon = Icon::ChevronRight;
  b.b[2].disabled = dayOffset_ >= kMaxDays;
  return b;
}

void AgendaApp::onButton(int idx, bool longPress, const Frame& f, Actions& a) {
  if (idx == 0 && dayOffset_ > 0) --dayOffset_;
  if (idx == 2 && dayOffset_ < kMaxDays) ++dayOffset_;
  if (idx == 1) {
    if (dayOffset_ == 0) a.refreshAll();
    dayOffset_ = 0;
  }
  scroll_ = 0;
  autoScroll_ = dayOffset_ == 0;
  dirty_ = true;
}

void AgendaApp::onDrag(int dy, const Frame& f) {
  scroll_ -= dy;
  if (scroll_ < 0) scroll_ = 0;
  if (scroll_ > maxScroll_) scroll_ = maxScroll_;
  dirty_ = true;
}
