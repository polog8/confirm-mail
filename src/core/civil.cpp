#include "civil.h"

namespace civ {

int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int& y, int& m, int& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  y = static_cast<int>(static_cast<int64_t>(yoe) + era * 400 + (m <= 2));
}

int weekdayFromDays(int64_t z) {
  // 1970-01-01 was a Thursday (ISO index 3).
  int64_t w = (z + 3) % 7;
  if (w < 0) w += 7;
  return static_cast<int>(w);
}

bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int daysInMonth(int y, int m) {
  static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m == 2 && isLeap(y)) return 29;
  return kDays[(m - 1) % 12];
}

int64_t toEpoch(const DateTime& dt) {
  return daysFromCivil(dt.y, dt.m, dt.d) * 86400 + dt.hh * 3600 + dt.mm * 60 + dt.ss;
}

DateTime fromEpoch(int64_t t) {
  DateTime dt;
  int64_t days = floorDiv(t, 86400);
  int64_t rem = t - days * 86400;
  civilFromDays(days, dt.y, dt.m, dt.d);
  dt.hh = static_cast<int>(rem / 3600);
  dt.mm = static_cast<int>((rem % 3600) / 60);
  dt.ss = static_cast<int>(rem % 60);
  return dt;
}

}  // namespace civ
