#include "format.h"

#include <ctype.h>
#include <stdio.h>

namespace fmt {

namespace {
const char* const kDays[7] = {"lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche"};
const char* const kDaysShort[7] = {"lun.", "mar.", "mer.", "jeu.", "ven.", "sam.", "dim."};
const char* const kMonths[12] = {"janvier", "février", "mars",      "avril",   "mai",      "juin",
                                 "juillet", "août",    "septembre", "octobre", "novembre", "décembre"};
const char* const kMonthsShort[12] = {"janv.", "févr.", "mars", "avr.", "mai",  "juin",
                                      "juil.", "août",  "sept.", "oct.", "nov.", "déc."};
}  // namespace

LocalTime localTime(const PosixTz& tz, int64_t utc) {
  LocalTime lt;
  civ::DateTime d = civ::fromEpoch(utc + tz.offsetAt(utc));
  lt.y = d.y;
  lt.m = d.m;
  lt.d = d.d;
  lt.hh = d.hh;
  lt.mm = d.mm;
  lt.ss = d.ss;
  lt.day = civ::daysFromCivil(d.y, d.m, d.d);
  lt.wday = civ::weekdayFromDays(lt.day);
  return lt;
}

const char* dayName(int wday) { return kDays[((wday % 7) + 7) % 7]; }
const char* dayShort(int wday) { return kDaysShort[((wday % 7) + 7) % 7]; }
const char* monthName(int m) { return kMonths[(m - 1 + 12) % 12]; }
const char* monthShort(int m) { return kMonthsShort[(m - 1 + 12) % 12]; }

void hhmm(char* out, size_t n, const PosixTz& tz, int64_t utc) {
  LocalTime lt = localTime(tz, utc);
  snprintf(out, n, "%02d:%02d", lt.hh, lt.mm);
}

void mmss(char* out, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s >= 3600) {
    snprintf(out, n, "%u:%02u:%02u", static_cast<unsigned>(s / 3600), static_cast<unsigned>((s / 60) % 60),
             static_cast<unsigned>(s % 60));
  } else {
    snprintf(out, n, "%u:%02u", static_cast<unsigned>(s / 60), static_cast<unsigned>(s % 60));
  }
}

void until(char* out, size_t n, int64_t seconds) {
  if (seconds < 60) {
    snprintf(out, n, "dans moins d'1 min");
    return;
  }
  int64_t minutes = (seconds + 59) / 60;
  if (minutes < 60) {
    snprintf(out, n, "dans %d min", static_cast<int>(minutes));
  } else if (minutes < 24 * 60) {
    int h = static_cast<int>(minutes / 60), m = static_cast<int>(minutes % 60);
    if (m == 0) snprintf(out, n, "dans %d h", h);
    else snprintf(out, n, "dans %d h %02d", h, m);
  } else {
    snprintf(out, n, "dans %d j", static_cast<int>(minutes / (24 * 60)));
  }
}

void dayLabel(char* out, size_t n, int64_t day, int64_t today) {
  if (day == today) {
    snprintf(out, n, "Aujourd'hui");
  } else if (day == today + 1) {
    snprintf(out, n, "Demain");
  } else if (day == today - 1) {
    snprintf(out, n, "Hier");
  } else {
    int y, m, d;
    civ::civilFromDays(day, y, m, d);
    snprintf(out, n, "%s %d %s", dayName(civ::weekdayFromDays(day)), d, monthName(m));
    capitalize(out);
  }
}

void longDate(char* out, size_t n, const LocalTime& lt) {
  snprintf(out, n, "%s %d %s", dayName(lt.wday), lt.d, monthName(lt.m));
  capitalize(out);
}

void capitalize(char* s) {
  if (s && *s && static_cast<unsigned char>(*s) < 0x80) *s = static_cast<char>(toupper(*s));
}

}  // namespace fmt
