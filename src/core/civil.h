// Calendar arithmetic on the proleptic Gregorian calendar, independent of the
// C library time zone machinery (thread-safe, identical on ESP32 and host).
#pragma once

#include <stdint.h>

namespace civ {

struct DateTime {
  int y = 1970;
  int m = 1;   // 1..12
  int d = 1;   // 1..31
  int hh = 0;
  int mm = 0;
  int ss = 0;
};

// Days since 1970-01-01 (Howard Hinnant's algorithms).
int64_t daysFromCivil(int y, int m, int d);
void civilFromDays(int64_t z, int& y, int& m, int& d);

// 0 = Monday ... 6 = Sunday (ISO order, matches RRULE BYDAY indexes below).
int weekdayFromDays(int64_t z);

bool isLeap(int y);
int daysInMonth(int y, int m);

// Treats the fields as UTC.
int64_t toEpoch(const DateTime& dt);
DateTime fromEpoch(int64_t t);

inline int64_t floorDiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
  return q;
}

}  // namespace civ
