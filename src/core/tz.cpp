#include "tz.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace {

bool parseName(const char*& p) {
  if (*p == '<') {
    const char* q = strchr(p, '>');
    if (!q) return false;
    p = q + 1;
    return true;
  }
  const char* s = p;
  while (isalpha(static_cast<unsigned char>(*p))) ++p;
  return p - s >= 3;
}

// [+|-]hh[:mm[:ss]] -> seconds. Returns false if no digits.
bool parseHms(const char*& p, int32_t& out) {
  int sign = 1;
  if (*p == '+' || *p == '-') {
    if (*p == '-') sign = -1;
    ++p;
  }
  if (!isdigit(static_cast<unsigned char>(*p))) return false;
  int32_t parts[3] = {0, 0, 0};
  for (int i = 0; i < 3; ++i) {
    int v = 0;
    if (!isdigit(static_cast<unsigned char>(*p))) break;
    while (isdigit(static_cast<unsigned char>(*p))) v = v * 10 + (*p++ - '0');
    parts[i] = v;
    if (*p != ':') break;
    ++p;
  }
  out = sign * (parts[0] * 3600 + parts[1] * 60 + parts[2]);
  return true;
}

int parseInt(const char*& p) {
  int v = 0;
  while (isdigit(static_cast<unsigned char>(*p))) v = v * 10 + (*p++ - '0');
  return v;
}

}  // namespace

bool PosixTz::parse(const char* spec) {
  stdOffset_ = dstOffset_ = 0;
  hasDst_ = false;
  if (!spec || !*spec) return false;
  const char* p = spec;
  if (*p == ':') return false;  // file-based spec unsupported
  if (!parseName(p)) return false;
  int32_t off;
  if (!parseHms(p, off)) return false;
  stdOffset_ = -off;  // POSIX offsets are positive west of Greenwich
  dstOffset_ = stdOffset_;
  if (*p == '\0') return true;

  if (!parseName(p)) return true;
  hasDst_ = true;
  dstOffset_ = stdOffset_ + 3600;
  if (*p && *p != ',') {
    if (parseHms(p, off)) dstOffset_ = -off;
  }

  auto parseRule = [&](Rule& r) -> bool {
    if (*p != ',') return false;
    ++p;
    if (*p == 'M') {
      ++p;
      r.kind = 'M';
      r.month = parseInt(p);
      if (*p++ != '.') return false;
      r.week = parseInt(p);
      if (*p++ != '.') return false;
      r.wday = parseInt(p);
    } else if (*p == 'J') {
      ++p;
      r.kind = 'J';
      r.day = parseInt(p);
    } else if (isdigit(static_cast<unsigned char>(*p))) {
      r.kind = 'D';
      r.day = parseInt(p);
    } else {
      return false;
    }
    r.time = 7200;
    if (*p == '/') {
      ++p;
      int32_t t;
      if (parseHms(p, t)) r.time = t;
    }
    return true;
  };

  if (*p == ',') {
    if (!parseRule(start_) || !parseRule(end_)) {
      hasDst_ = false;
      dstOffset_ = stdOffset_;
      return false;
    }
  } else {
    // Default US rules, per POSIX convention.
    start_ = Rule{'M', 3, 2, 0, 0, 7200};
    end_ = Rule{'M', 11, 1, 0, 0, 7200};
  }
  return true;
}

int64_t PosixTz::ruleLocal(const Rule& r, int year) {
  int64_t jan1 = civ::daysFromCivil(year, 1, 1);
  int64_t day;
  switch (r.kind) {
    case 'J':
      day = jan1 + r.day - 1;
      if (civ::isLeap(year) && r.day >= 60) ++day;
      break;
    case 'D':
      day = jan1 + r.day;
      break;
    default: {
      int64_t first = civ::daysFromCivil(year, r.month, 1);
      int dim = civ::daysInMonth(year, r.month);
      int iso = (r.wday + 6) % 7;  // POSIX 0=Sunday -> ISO 0=Monday
      day = first + ((iso - civ::weekdayFromDays(first) + 7) % 7) + (r.week - 1) * 7;
      while (day >= first + dim) day -= 7;
      break;
    }
  }
  return day * 86400 + r.time;
}

int32_t PosixTz::offsetAt(int64_t utc) const {
  if (!hasDst_) return stdOffset_;
  int y, m, d;
  civ::civilFromDays(civ::floorDiv(utc + stdOffset_, 86400), y, m, d);
  int64_t startUtc = ruleLocal(start_, y) - stdOffset_;
  int64_t endUtc = ruleLocal(end_, y) - dstOffset_;
  bool dst;
  if (startUtc < endUtc) {
    dst = utc >= startUtc && utc < endUtc;
  } else {
    dst = !(utc >= endUtc && utc < startUtc);
  }
  return dst ? dstOffset_ : stdOffset_;
}

int64_t PosixTz::localToUtc(const civ::DateTime& local) const {
  int64_t asUtc = civ::toEpoch(local);
  if (!hasDst_) return asUtc - stdOffset_;
  int64_t tDst = asUtc - dstOffset_;
  int64_t tStd = asUtc - stdOffset_;
  int64_t first = tDst < tStd ? tDst : tStd;
  int64_t second = tDst < tStd ? tStd : tDst;
  if (asUtc - offsetAt(first) == first) return first;
  if (asUtc - offsetAt(second) == second) return second;
  return tStd;
}

int64_t PosixTz::dayStartUtc(int64_t localDay) const {
  civ::DateTime dt;
  civ::civilFromDays(localDay, dt.y, dt.m, dt.d);
  return localToUtc(dt);
}

// ---------------------------------------------------------------------------

namespace {

struct TzAlias {
  const char* name;
  const char* posix;
};

constexpr const char* kCET = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr const char* kEET = "EET-2EEST,M3.5.0/3,M10.5.0/4";
constexpr const char* kUK = "GMT0BST,M3.5.0/1,M10.5.0";
constexpr const char* kWET = "WET0WEST,M3.5.0/1,M10.5.0";
constexpr const char* kUSE = "EST5EDT,M3.2.0,M11.1.0";
constexpr const char* kUSC = "CST6CDT,M3.2.0,M11.1.0";
constexpr const char* kUSM = "MST7MDT,M3.2.0,M11.1.0";
constexpr const char* kUSP = "PST8PDT,M3.2.0,M11.1.0";
constexpr const char* kAUE = "AEST-10AEDT,M10.1.0,M4.1.0/3";

const TzAlias kAliases[] = {
    {"UTC", "UTC0"}, {"Etc/UTC", "UTC0"}, {"GMT", "UTC0"}, {"Etc/GMT", "UTC0"}, {"Z", "UTC0"},
    {"Europe/Paris", kCET}, {"Europe/Brussels", kCET}, {"Europe/Berlin", kCET},
    {"Europe/Madrid", kCET}, {"Europe/Rome", kCET}, {"Europe/Amsterdam", kCET},
    {"Europe/Zurich", kCET}, {"Europe/Luxembourg", kCET}, {"Europe/Monaco", kCET},
    {"Europe/Vienna", kCET}, {"Europe/Stockholm", kCET}, {"Europe/Oslo", kCET},
    {"Europe/Copenhagen", kCET}, {"Europe/Prague", kCET}, {"Europe/Warsaw", kCET},
    {"Europe/Budapest", kCET}, {"Europe/Belgrade", kCET}, {"Europe/Andorra", kCET},
    {"Europe/Malta", kCET}, {"Europe/Bratislava", kCET}, {"Europe/Ljubljana", kCET},
    {"Europe/Zagreb", kCET}, {"Africa/Ceuta", kCET},
    {"Europe/London", kUK}, {"Europe/Dublin", "GMT0IST,M3.5.0/1,M10.5.0"},
    {"Europe/Lisbon", kWET}, {"Atlantic/Canary", kWET},
    {"Europe/Athens", kEET}, {"Europe/Helsinki", kEET}, {"Europe/Kyiv", kEET},
    {"Europe/Kiev", kEET}, {"Europe/Bucharest", kEET}, {"Europe/Sofia", kEET},
    {"Europe/Riga", kEET}, {"Europe/Tallinn", kEET}, {"Europe/Vilnius", kEET},
    {"Europe/Istanbul", "<+03>-3"}, {"Europe/Moscow", "MSK-3"},
    {"America/New_York", kUSE}, {"America/Toronto", kUSE}, {"America/Detroit", kUSE},
    {"America/Montreal", kUSE}, {"America/Chicago", kUSC}, {"America/Winnipeg", kUSC},
    {"America/Denver", kUSM}, {"America/Edmonton", kUSM}, {"America/Phoenix", "MST7"},
    {"America/Los_Angeles", kUSP}, {"America/Vancouver", kUSP},
    {"America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"}, {"Pacific/Honolulu", "HST10"},
    {"America/Halifax", "AST4ADT,M3.2.0,M11.1.0"}, {"America/Mexico_City", "CST6"},
    {"America/Sao_Paulo", "<-03>3"}, {"America/Argentina/Buenos_Aires", "<-03>3"},
    {"America/Bogota", "<-05>5"}, {"America/Lima", "<-05>5"},
    {"America/Martinique", "AST4"}, {"America/Guadeloupe", "AST4"},
    {"America/Cayenne", "<-03>3"}, {"Indian/Reunion", "<+04>-4"},
    {"Indian/Mayotte", "EAT-3"}, {"Pacific/Tahiti", "<-10>10"}, {"Pacific/Noumea", "<+11>-11"},
    {"Africa/Casablanca", "<+01>-1"}, {"Africa/Algiers", "CET-1"}, {"Africa/Tunis", "CET-1"},
    {"Africa/Abidjan", "GMT0"}, {"Africa/Dakar", "GMT0"}, {"Africa/Lagos", "WAT-1"},
    {"Africa/Johannesburg", "SAST-2"}, {"Africa/Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24"},
    {"Asia/Dubai", "<+04>-4"}, {"Asia/Riyadh", "<+03>-3"}, {"Asia/Kolkata", "IST-5:30"},
    {"Asia/Calcutta", "IST-5:30"}, {"Asia/Bangkok", "<+07>-7"}, {"Asia/Jakarta", "WIB-7"},
    {"Asia/Singapore", "<+08>-8"}, {"Asia/Shanghai", "CST-8"}, {"Asia/Hong_Kong", "HKT-8"},
    {"Asia/Taipei", "CST-8"}, {"Asia/Tokyo", "JST-9"}, {"Asia/Seoul", "KST-9"},
    {"Asia/Jerusalem", "IST-2IDT,M3.4.4/26,M10.5.0"},
    {"Australia/Sydney", kAUE}, {"Australia/Melbourne", kAUE}, {"Australia/Canberra", kAUE},
    {"Australia/Brisbane", "AEST-10"}, {"Australia/Perth", "AWST-8"},
    {"Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    // Windows / Exchange names
    {"Romance Standard Time", kCET}, {"W. Europe Standard Time", kCET},
    {"Central Europe Standard Time", kCET}, {"Central European Standard Time", kCET},
    {"GMT Standard Time", kUK}, {"Greenwich Standard Time", "GMT0"},
    {"E. Europe Standard Time", kEET}, {"FLE Standard Time", kEET}, {"GTB Standard Time", kEET},
    {"Russian Standard Time", "MSK-3"}, {"Eastern Standard Time", kUSE},
    {"Central Standard Time", kUSC}, {"Mountain Standard Time", kUSM},
    {"Pacific Standard Time", kUSP}, {"UTC", "UTC0"}, {"Coordinated Universal Time", "UTC0"},
    {"Tokyo Standard Time", "JST-9"}, {"China Standard Time", "CST-8"},
    {"India Standard Time", "IST-5:30"}, {"Singapore Standard Time", "<+08>-8"},
    {"Arabian Standard Time", "<+04>-4"}, {"AUS Eastern Standard Time", kAUE},
};

bool endsWithPath(const char* s, size_t len, const char* name) {
  size_t n = strlen(name);
  if (len < n) return false;
  if (strncasecmp(s + len - n, name, n) != 0) return false;
  return len == n || s[len - n - 1] == '/';
}

}  // namespace

const char* posixForTzid(const char* tzid) {
  if (!tzid) return nullptr;
  // Strip surrounding quotes and whitespace.
  while (*tzid == '"' || *tzid == ' ') ++tzid;
  size_t len = strlen(tzid);
  while (len && (tzid[len - 1] == '"' || tzid[len - 1] == ' ')) --len;
  if (!len) return nullptr;

  for (const auto& a : kAliases) {
    if (endsWithPath(tzid, len, a.name)) return a.posix;
  }

  // Outlook style: "(UTC+01:00) Brussels, Copenhagen, Madrid, Paris"
  if (len > 5 && strncmp(tzid, "(UTC", 4) == 0) {
    if (strncmp(tzid, "(UTC+01:00)", 11) == 0) return kCET;
    if (strncmp(tzid, "(UTC+02:00)", 11) == 0) return kEET;
    if (strncmp(tzid, "(UTC) ", 6) == 0 || strncmp(tzid, "(UTC+00:00)", 11) == 0) return kUK;
    if (strncmp(tzid, "(UTC-05:00) Eastern", 19) == 0) return kUSE;
    if (strncmp(tzid, "(UTC-06:00) Central", 19) == 0) return kUSC;
    if (strncmp(tzid, "(UTC-07:00) Mountain", 20) == 0) return kUSM;
    if (strncmp(tzid, "(UTC-08:00) Pacific", 19) == 0) return kUSP;
  }
  return nullptr;
}
