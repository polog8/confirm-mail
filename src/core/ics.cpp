#include "ics.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace {

uint32_t fnv1a(const char* s) {
  uint32_t h = 2166136261u;
  while (*s) {
    h ^= static_cast<uint8_t>(*s++);
    h *= 16777619u;
  }
  return h;
}

bool ieq(const char* a, const char* b) { return strcasecmp(a, b) == 0; }

int weekdayCode(const char* s) {
  static const char* kCodes[7] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
  for (int i = 0; i < 7; ++i) {
    if (toupper(static_cast<unsigned char>(s[0])) == kCodes[i][0] &&
        toupper(static_cast<unsigned char>(s[1])) == kCodes[i][1])
      return i;
  }
  return -1;
}

// RFC 5545 TEXT unescaping; line breaks become spaces since we only show
// single-line labels.
void unescapeText(const char* in, char* out, size_t cap) {
  size_t o = 0;
  while (*in == ' ') ++in;
  for (; *in && o + 1 < cap; ++in) {
    char c = *in;
    if (c == '\\' && in[1]) {
      ++in;
      switch (*in) {
        case 'n':
        case 'N':
          c = ' ';
          break;
        default:
          c = *in;  // \, \; \\ and unknown escapes
          break;
      }
    }
    out[o++] = c;
  }
  if (*in && (static_cast<unsigned char>(*in) & 0xC0) == 0x80) {
    // Output full in the middle of a multi-byte sequence: drop the partial one.
    while (o > 0 && (static_cast<unsigned char>(out[o - 1]) & 0xC0) == 0x80) --o;
    if (o > 0 && (static_cast<unsigned char>(out[o - 1]) & 0xC0) == 0xC0) --o;
  }
  while (o > 0 && out[o - 1] == ' ') --o;
  out[o] = '\0';
}

// Extracts a parameter value (e.g. TZID) from a ';'-separated parameter list.
bool getParam(const char* params, const char* key, char* out, size_t cap) {
  if (!params) return false;
  size_t klen = strlen(key);
  const char* p = params;
  while (*p) {
    if (strncasecmp(p, key, klen) == 0 && p[klen] == '=') {
      p += klen + 1;
      size_t o = 0;
      bool quoted = false;
      if (*p == '"') {
        quoted = true;
        ++p;
      }
      while (*p && o + 1 < cap) {
        if (quoted && *p == '"') break;
        if (!quoted && (*p == ';' || *p == ',')) break;
        out[o++] = *p++;
      }
      out[o] = '\0';
      return true;
    }
    // skip to next parameter (respecting quotes)
    bool q = false;
    while (*p) {
      if (*p == '"') q = !q;
      if (*p == ';' && !q) {
        ++p;
        break;
      }
      ++p;
    }
  }
  return false;
}

const char* const kMeetingHosts[] = {
    "meet.google.com", "zoom.us",        "teams.microsoft.com", "teams.live.com",
    "webex.com",       "whereby.com",    "meet.jit.si",         "facetime.apple.com",
};

void findMeetingUrl(const char* text, char* out, size_t cap) {
  if (out[0]) return;
  for (const char* p = strstr(text, "https://"); p; p = strstr(p + 8, "https://")) {
    size_t len = 0;
    while (p[len] && p[len] != ' ' && p[len] != '\\' && p[len] != '"' && p[len] != '<' &&
           p[len] != '>' && p[len] != ',' && p[len] != ';')
      ++len;
    // host part
    const char* host = p + 8;
    size_t hostLen = 0;
    while (hostLen < len - 8 && host[hostLen] != '/' && host[hostLen] != '?') ++hostLen;
    bool known = false;
    for (const char* h : kMeetingHosts) {
      size_t hl = strlen(h);
      if (hostLen >= hl && strncasecmp(host + hostLen - hl, h, hl) == 0) {
        known = true;
        break;
      }
    }
    if (known && len < cap) {
      memcpy(out, p, len);
      out[len] = '\0';
      return;
    }
  }
}

void sortUnique(int32_t* a, int& n) {
  for (int i = 1; i < n; ++i) {
    int32_t v = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > v) {
      a[j + 1] = a[j];
      --j;
    }
    a[j + 1] = v;
  }
  int w = 0;
  for (int i = 0; i < n; ++i) {
    if (w == 0 || a[w - 1] != a[i]) a[w++] = a[i];
  }
  n = w;
}

}  // namespace

void utf8Copy(char* dst, size_t cap, const char* src) {
  if (!cap) return;
  size_t n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && (static_cast<unsigned char>(src[n]) & 0xC0) == 0x80) --n;
  }
  memmove(dst, src, n);
  dst[n] = '\0';
}

IcsParser::IcsParser(const PosixTz& localTz, int64_t windowStart, int64_t windowEnd)
    : localTz_(localTz), winStart_(windowStart), winEnd_(windowEnd), utcTz_("UTC0") {
  summary_[0] = location_[0] = url_[0] = '\0';
}

void IcsParser::feed(const char* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    char c = data[i];
    if (c == '\r') continue;
    if (c == '\n') {
      atLineStart_ = true;
      continue;
    }
    if (atLineStart_) {
      atLineStart_ = false;
      if ((c == ' ' || c == '\t') && haveLine_) continue;  // folded continuation
      if (haveLine_) {
        line_[lineLen_] = '\0';
        processLine(line_, lineLen_);
      }
      lineLen_ = 0;
      haveLine_ = true;
    }
    if (lineLen_ < sizeof(line_) - 1) line_[lineLen_++] = c;
  }
}

void IcsParser::finish() {
  if (haveLine_) {
    line_[lineLen_] = '\0';
    processLine(line_, lineLen_);
    haveLine_ = false;
  }
  // Drop series instances replaced by RECURRENCE-ID overrides.
  int w = 0;
  for (int i = 0; i < count_; ++i) {
    bool drop = false;
    if (resultFromSeries_[i]) {
      for (int k = 0; k < nOverrides_ && !drop; ++k) {
        const Override& o = overrides_[k];
        if (o.uid != results_[i].uid) continue;
        if (o.when.isDate) {
          drop = localTz_.localDay(results_[i].start) == o.when.day;
        } else {
          drop = o.when.utc == results_[i].start;
        }
      }
    }
    if (!drop) {
      if (w != i) {
        results_[w] = results_[i];
        resultFromSeries_[w] = resultFromSeries_[i];
      }
      ++w;
    }
  }
  count_ = w;
}

void IcsParser::processLine(char* line, size_t len) {
  (void)len;
  char* p = line;
  while (*p && *p != ';' && *p != ':') ++p;
  char sep = *p;
  if (!sep) return;
  *p = '\0';
  const char* name = line;
  char* params = nullptr;
  char* value = nullptr;
  if (sep == ':') {
    value = p + 1;
  } else {
    params = p + 1;
    bool q = false;
    for (char* s = params; *s; ++s) {
      if (*s == '"') q = !q;
      if (*s == ':' && !q) {
        *s = '\0';
        value = s + 1;
        break;
      }
    }
    if (!value) return;
  }

  if (ieq(name, "BEGIN")) {
    if (ieq(value, "VCALENDAR")) {
      sawCalendar_ = true;
    } else if (inEvent_) {
      ++nested_;
    } else if (ieq(value, "VEVENT")) {
      beginEvent();
    }
    return;
  }
  if (ieq(name, "END")) {
    if (!inEvent_) return;
    if (nested_ > 0) {
      --nested_;
    } else if (ieq(value, "VEVENT")) {
      endEvent();
    }
    return;
  }
  if (inEvent_ && nested_ == 0) handleProperty(name, params, value);
}

void IcsParser::beginEvent() {
  inEvent_ = true;
  nested_ = 0;
  dtStart_ = DtValue();
  dtEnd_ = DtValue();
  hasDuration_ = false;
  durationSec_ = 0;
  cancelled_ = false;
  hasRRule_ = false;
  rrule_ = RRule();
  hasRecurrenceId_ = false;
  nExdates_ = 0;
  uid_ = 0;
  summary_[0] = location_[0] = url_[0] = '\0';
}

void IcsParser::handleProperty(const char* name, char* params, char* value) {
  if (ieq(name, "DTSTART")) {
    parseDt(params, value, dtStart_);
  } else if (ieq(name, "DTEND")) {
    parseDt(params, value, dtEnd_);
  } else if (ieq(name, "DURATION")) {
    hasDuration_ = parseDuration(value, durationSec_);
  } else if (ieq(name, "SUMMARY")) {
    unescapeText(value, summary_, sizeof(summary_));
  } else if (ieq(name, "LOCATION")) {
    unescapeText(value, location_, sizeof(location_));
    findMeetingUrl(value, url_, sizeof(url_));
  } else if (ieq(name, "DESCRIPTION") || ieq(name, "URL") || ieq(name, "X-GOOGLE-CONFERENCE") ||
             ieq(name, "X-MICROSOFT-SKYPETEAMSMEETINGURL")) {
    findMeetingUrl(value, url_, sizeof(url_));
  } else if (ieq(name, "STATUS")) {
    cancelled_ = ieq(value, "CANCELLED");
  } else if (ieq(name, "UID")) {
    uid_ = fnv1a(value);
  } else if (ieq(name, "RRULE")) {
    parseRRule(value);
  } else if (ieq(name, "EXDATE") || ieq(name, "RECURRENCE-ID")) {
    bool isRid = ieq(name, "RECURRENCE-ID");
    char* save = value;
    while (save && *save) {
      char* comma = strchr(save, ',');
      if (comma) *comma = '\0';
      DtValue v;
      if (parseDt(params, save, v)) {
        Exdate e;
        e.isDate = v.isDate;
        e.day = civ::daysFromCivil(v.local.y, v.local.m, v.local.d);
        e.utc = dtToUtc(v);
        if (isRid) {
          hasRecurrenceId_ = true;
          recurrenceId_ = e;
        } else if (nExdates_ < kMaxExdates) {
          exdates_[nExdates_++] = e;
        }
      }
      save = comma ? comma + 1 : nullptr;
    }
  }
}

bool IcsParser::parseDateTime(const char* s, DtValue& out) {
  while (*s == ' ') ++s;
  size_t n = 0;
  while (isdigit(static_cast<unsigned char>(s[n]))) ++n;
  if (n != 8) return false;
  auto num = [](const char* p, int len) {
    int v = 0;
    for (int i = 0; i < len; ++i) v = v * 10 + (p[i] - '0');
    return v;
  };
  out.local.y = num(s, 4);
  out.local.m = num(s + 4, 2);
  out.local.d = num(s + 6, 2);
  out.local.hh = out.local.mm = out.local.ss = 0;
  if (out.local.m < 1 || out.local.m > 12 || out.local.d < 1 || out.local.d > 31) return false;
  if (s[8] == 'T' || s[8] == 't') {
    const char* t = s + 9;
    for (int i = 0; i < 6; ++i) {
      if (!isdigit(static_cast<unsigned char>(t[i]))) return false;
    }
    out.local.hh = num(t, 2);
    out.local.mm = num(t + 2, 2);
    out.local.ss = num(t + 4, 2);
    out.isDate = false;
    out.isUtc = (t[6] == 'Z' || t[6] == 'z');
  } else {
    out.isDate = true;
    out.isUtc = false;
  }
  out.set = true;
  return true;
}

bool IcsParser::parseDt(const char* params, const char* value, DtValue& out) {
  DtValue v;
  if (!parseDateTime(value, v)) return false;
  char buf[96];
  if (getParam(params, "VALUE", buf, sizeof(buf)) && ieq(buf, "DATE")) v.isDate = true;
  if (!v.isUtc && !v.isDate && getParam(params, "TZID", buf, sizeof(buf))) v.tz = resolveTz(buf);
  out = v;
  return true;
}

bool IcsParser::parseDuration(const char* s, int64_t& seconds) {
  int sign = 1;
  if (*s == '+' || *s == '-') {
    if (*s == '-') sign = -1;
    ++s;
  }
  if (*s != 'P' && *s != 'p') return false;
  ++s;
  int64_t total = 0;
  bool inTime = false;
  while (*s) {
    if (*s == 'T' || *s == 't') {
      inTime = true;
      ++s;
      continue;
    }
    if (!isdigit(static_cast<unsigned char>(*s))) return false;
    int64_t v = 0;
    while (isdigit(static_cast<unsigned char>(*s))) v = v * 10 + (*s++ - '0');
    switch (toupper(static_cast<unsigned char>(*s))) {
      case 'W':
        total += v * 7 * 86400;
        break;
      case 'D':
        total += v * 86400;
        break;
      case 'H':
        total += v * 3600;
        break;
      case 'M':
        total += inTime ? v * 60 : v * 30 * 86400;
        break;
      case 'S':
        total += v;
        break;
      default:
        return false;
    }
    ++s;
  }
  seconds = sign * total;
  return true;
}

void IcsParser::parseRRule(char* value) {
  RRule r;
  char* part = value;
  while (part && *part) {
    char* semi = strchr(part, ';');
    if (semi) *semi = '\0';
    char* eq = strchr(part, '=');
    if (eq) {
      *eq = '\0';
      const char* key = part;
      char* val = eq + 1;
      if (ieq(key, "FREQ")) {
        if (ieq(val, "DAILY")) r.freq = 1;
        else if (ieq(val, "WEEKLY")) r.freq = 2;
        else if (ieq(val, "MONTHLY")) r.freq = 3;
        else if (ieq(val, "YEARLY")) r.freq = 4;
        else r.freq = 0;  // sub-daily rules are not expanded
      } else if (ieq(key, "INTERVAL")) {
        int v = atoi(val);
        r.interval = v > 0 ? static_cast<uint16_t>(v) : 1;
      } else if (ieq(key, "COUNT")) {
        r.count = atoi(val);
      } else if (ieq(key, "UNTIL")) {
        r.hasUntil = parseDateTime(val, r.until);
      } else if (ieq(key, "WKST")) {
        int wd = weekdayCode(val);
        if (wd >= 0) r.wkst = static_cast<uint8_t>(wd);
      } else {
        // comma separated lists
        char* item = val;
        while (item && *item) {
          char* comma = strchr(item, ',');
          if (comma) *comma = '\0';
          if (ieq(key, "BYDAY") && r.nByDay < 14) {
            int ord = 0;
            char* p = item;
            if (*p == '+' || *p == '-' || isdigit(static_cast<unsigned char>(*p))) {
              ord = static_cast<int>(strtol(item, &p, 10));
            }
            int wd = weekdayCode(p);
            if (wd >= 0) r.byDay[r.nByDay++] = ByDay{static_cast<int8_t>(ord), static_cast<uint8_t>(wd)};
          } else if (ieq(key, "BYMONTHDAY") && r.nByMonthDay < 16) {
            int v = atoi(item);
            if (v != 0 && v >= -31 && v <= 31) r.byMonthDay[r.nByMonthDay++] = static_cast<int8_t>(v);
          } else if (ieq(key, "BYMONTH")) {
            int v = atoi(item);
            if (v >= 1 && v <= 12) r.byMonthMask |= 1u << (v - 1);
          } else if (ieq(key, "BYSETPOS") && r.nBySetPos < 8) {
            int v = atoi(item);
            if (v != 0) r.bySetPos[r.nBySetPos++] = static_cast<int16_t>(v);
          }
          item = comma ? comma + 1 : nullptr;
        }
      }
    }
    part = semi ? semi + 1 : nullptr;
  }
  rrule_ = r;
  hasRRule_ = r.freq != 0;
}

const PosixTz* IcsParser::resolveTz(const char* tzid) {
  const char* posix = posixForTzid(tzid);
  if (!posix) return nullptr;
  for (int i = 0; i < nTz_; ++i) {
    if (tzCache_[i].posix == posix) return &tzCache_[i].tz;
  }
  if (nTz_ >= static_cast<int>(sizeof(tzCache_) / sizeof(tzCache_[0]))) return nullptr;
  tzCache_[nTz_].posix = posix;
  tzCache_[nTz_].tz.parse(posix);
  return &tzCache_[nTz_++].tz;
}

int64_t IcsParser::dtToUtc(const DtValue& v) const {
  if (v.isDate) {
    return localTz_.dayStartUtc(civ::daysFromCivil(v.local.y, v.local.m, v.local.d));
  }
  if (v.isUtc) return civ::toEpoch(v.local);
  const PosixTz* tz = v.tz ? v.tz : &localTz_;
  return tz->localToUtc(v.local);
}

bool IcsParser::isExcluded(int64_t startUtc, int64_t day) const {
  for (int i = 0; i < nExdates_; ++i) {
    const Exdate& e = exdates_[i];
    if (e.isDate ? e.day == day : e.utc == startUtc) return true;
  }
  return false;
}

void IcsParser::emit(int64_t start, int64_t end, bool allDay, bool fromSeries) {
  bool overlaps = start < winEnd_ && (end > winStart_ || (end == start && start >= winStart_));
  if (!overlaps) return;
  int slot = count_;
  if (count_ >= kMaxResults) {
    // Keep the earliest occurrences: replace the latest one if we are earlier.
    int latest = 0;
    for (int i = 1; i < count_; ++i) {
      if (results_[i].start > results_[latest].start) latest = i;
    }
    if (results_[latest].start <= start) return;
    slot = latest;
  } else {
    ++count_;
  }
  IcsOccurrence& o = results_[slot];
  o.start = start;
  o.end = end;
  o.allDay = allDay;
  o.uid = uid_;
  utf8Copy(o.title, sizeof(o.title), summary_[0] ? summary_ : "(Sans titre)");
  utf8Copy(o.location, sizeof(o.location), location_);
  utf8Copy(o.url, sizeof(o.url), url_);
  resultFromSeries_[slot] = fromSeries;
}

void IcsParser::endEvent() {
  inEvent_ = false;
  ++eventsSeen_;
  if (!dtStart_.set) return;

  if (hasRecurrenceId_ && nOverrides_ < kMaxOverrides) {
    overrides_[nOverrides_++] = Override{uid_, recurrenceId_};
  }
  if (cancelled_) return;

  const bool allDay = dtStart_.isDate;
  const int64_t startDay = civ::daysFromCivil(dtStart_.local.y, dtStart_.local.m, dtStart_.local.d);
  int64_t durSec = 0;
  int64_t durDays = 0;
  if (allDay) {
    if (dtEnd_.set && dtEnd_.isDate) {
      durDays = civ::daysFromCivil(dtEnd_.local.y, dtEnd_.local.m, dtEnd_.local.d) - startDay;
    } else if (hasDuration_) {
      durDays = durationSec_ / 86400;
    }
    if (durDays <= 0) durDays = 1;
  } else {
    if (dtEnd_.set) {
      durSec = dtToUtc(dtEnd_) - dtToUtc(dtStart_);
    } else if (hasDuration_) {
      durSec = durationSec_;
    }
    if (durSec < 0) durSec = 0;
  }

  if (hasRRule_ && !hasRecurrenceId_) {
    expandSeries(durSec, durDays);
    return;
  }
  int64_t start = allDay ? localTz_.dayStartUtc(startDay) : dtToUtc(dtStart_);
  int64_t end = allDay ? localTz_.dayStartUtc(startDay + durDays) : start + durSec;
  emit(start, end, allDay, false);
}

int IcsParser::monthCandidates(int y, int m, int32_t* out, int cap) const {
  const RRule& r = rrule_;
  const int64_t first = civ::daysFromCivil(y, m, 1);
  const int dim = civ::daysInMonth(y, m);
  const int firstWd = civ::weekdayFromDays(first);
  uint32_t mdMask = 0, bdMask = 0;
  for (int i = 0; i < r.nByMonthDay; ++i) {
    int md = r.byMonthDay[i];
    int dd = md > 0 ? md : dim + md + 1;
    if (dd >= 1 && dd <= dim) mdMask |= 1u << dd;
  }
  for (int i = 0; i < r.nByDay; ++i) {
    const ByDay& b = r.byDay[i];
    int firstMatch = 1 + ((b.wd - firstWd + 7) % 7);
    if (b.ord == 0) {
      for (int d = firstMatch; d <= dim; d += 7) bdMask |= 1u << d;
    } else if (b.ord > 0) {
      int d = firstMatch + (b.ord - 1) * 7;
      if (d <= dim) bdMask |= 1u << d;
    } else {
      int lastWd = (firstWd + dim - 1) % 7;
      int d = dim - ((lastWd - b.wd + 7) % 7) + (b.ord + 1) * 7;
      if (d >= 1) bdMask |= 1u << d;
    }
  }
  uint32_t mask;
  if (r.nByMonthDay && r.nByDay) {
    mask = mdMask & bdMask;
  } else if (r.nByMonthDay) {
    mask = mdMask;
  } else if (r.nByDay) {
    mask = bdMask;
  } else {
    mask = dtStart_.local.d <= dim ? (1u << dtStart_.local.d) : 0;
  }
  int n = 0;
  for (int d = 1; d <= dim && n < cap; ++d) {
    if (mask & (1u << d)) out[n++] = static_cast<int32_t>(first + d - 1);
  }
  return n;
}

int IcsParser::genCandidates(int64_t p, int32_t* out, int cap) const {
  const RRule& r = rrule_;
  const civ::DateTime& s = dtStart_.local;
  const int64_t startDay = civ::daysFromCivil(s.y, s.m, s.d);
  int n = 0;
  switch (r.freq) {
    case 1: {  // DAILY
      int64_t day = startDay + p * r.interval;
      int y, m, d;
      civ::civilFromDays(day, y, m, d);
      if (r.byMonthMask && !(r.byMonthMask & (1u << (m - 1)))) return 0;
      if (r.nByMonthDay) {
        bool ok = false;
        int dim = civ::daysInMonth(y, m);
        for (int i = 0; i < r.nByMonthDay && !ok; ++i) {
          int md = r.byMonthDay[i];
          ok = (md > 0 ? md : dim + md + 1) == d;
        }
        if (!ok) return 0;
      }
      if (r.nByDay) {
        bool ok = false;
        int wd = civ::weekdayFromDays(day);
        for (int i = 0; i < r.nByDay && !ok; ++i) ok = r.byDay[i].wd == wd;
        if (!ok) return 0;
      }
      out[n++] = static_cast<int32_t>(day);
      return n;
    }
    case 2: {  // WEEKLY
      int64_t ws0 = startDay - ((civ::weekdayFromDays(startDay) - r.wkst + 7) % 7);
      int64_t ws = ws0 + p * 7 * r.interval;
      if (r.nByDay) {
        for (int i = 0; i < r.nByDay && n < cap; ++i) {
          out[n++] = static_cast<int32_t>(ws + ((r.byDay[i].wd - r.wkst + 7) % 7));
        }
      } else {
        out[n++] = static_cast<int32_t>(ws + ((civ::weekdayFromDays(startDay) - r.wkst + 7) % 7));
      }
      if (r.byMonthMask) {
        int w = 0;
        for (int i = 0; i < n; ++i) {
          int y, m, d;
          civ::civilFromDays(out[i], y, m, d);
          if (r.byMonthMask & (1u << (m - 1))) out[w++] = out[i];
        }
        n = w;
      }
      break;
    }
    case 3: {  // MONTHLY
      int64_t mi = static_cast<int64_t>(s.y) * 12 + (s.m - 1) + p * r.interval;
      int y = static_cast<int>(civ::floorDiv(mi, 12));
      int m = static_cast<int>(mi - static_cast<int64_t>(y) * 12) + 1;
      if (r.byMonthMask && !(r.byMonthMask & (1u << (m - 1)))) return 0;
      n = monthCandidates(y, m, out, cap);
      break;
    }
    case 4: {  // YEARLY
      int y = static_cast<int>(s.y + p * r.interval);
      uint16_t months = r.byMonthMask ? r.byMonthMask : static_cast<uint16_t>(1u << (s.m - 1));
      for (int m = 1; m <= 12; ++m) {
        if (!(months & (1u << (m - 1)))) continue;
        if (r.nByDay || r.nByMonthDay) {
          n += monthCandidates(y, m, out + n, cap - n);
        } else if (s.d <= civ::daysInMonth(y, m) && n < cap) {
          out[n++] = static_cast<int32_t>(civ::daysFromCivil(y, m, s.d));
        }
      }
      break;
    }
    default:
      return 0;
  }
  sortUnique(out, n);
  if (r.nBySetPos && n > 0) {
    int32_t picked[8];
    int np = 0;
    for (int i = 0; i < r.nBySetPos; ++i) {
      int pos = r.bySetPos[i];
      int idx = pos > 0 ? pos - 1 : n + pos;
      if (idx >= 0 && idx < n) picked[np++] = out[idx];
    }
    for (int i = 0; i < np; ++i) out[i] = picked[i];
    n = np;
    sortUnique(out, n);
  }
  return n;
}

void IcsParser::expandSeries(int64_t durSec, int64_t durDays) {
  const RRule& r = rrule_;
  const DtValue& s = dtStart_;
  const bool allDay = s.isDate;
  const PosixTz* tz = s.isUtc ? &utcTz_ : (s.tz ? s.tz : &localTz_);
  const int64_t startDay = civ::daysFromCivil(s.local.y, s.local.m, s.local.d);

  bool untilByDay = false;
  int64_t untilDay = 0, untilUtc = 0;
  if (r.hasUntil) {
    DtValue u = r.until;
    if (!u.isUtc && !u.isDate) u.tz = s.tz;
    if (u.isDate) {
      untilByDay = true;
      untilDay = civ::daysFromCivil(u.local.y, u.local.m, u.local.d);
    } else if (allDay) {
      untilByDay = true;
      untilDay = localTz_.localDay(dtToUtc(u));
    } else {
      untilUtc = dtToUtc(u);
    }
  }

  const int64_t winStartDay = tz->localDay(winStart_);
  const int64_t winEndDay = tz->localDay(winEnd_);
  const int64_t spanDays = allDay ? durDays : durSec / 86400 + 1;

  // Skip whole periods that end before the window (only valid without COUNT,
  // which needs every instance since DTSTART to be counted).
  int64_t p0 = 0;
  const int64_t target = winStartDay - spanDays - 1;
  if (r.count == 0 && target > startDay) {
    int ty, tm, td;
    civ::civilFromDays(target, ty, tm, td);
    switch (r.freq) {
      case 1:
        p0 = (target - startDay) / r.interval;
        break;
      case 2: {
        int64_t ws0 = startDay - ((civ::weekdayFromDays(startDay) - r.wkst + 7) % 7);
        p0 = (target - ws0) / (7 * static_cast<int64_t>(r.interval));
        break;
      }
      case 3:
        p0 = ((static_cast<int64_t>(ty) * 12 + tm - 1) - (static_cast<int64_t>(s.local.y) * 12 + s.local.m - 1)) /
             r.interval;
        break;
      case 4:
        p0 = (ty - s.local.y) / r.interval;
        break;
    }
    p0 = p0 > 1 ? p0 - 1 : 0;
  }

  int32_t cand[400];
  int64_t seen = 0;
  for (int64_t p = p0, iter = 0; iter < 6000; ++p, ++iter) {
    int nc = genCandidates(p, cand, 400);
    if (nc == 0) {
      // Empty period (e.g. Feb for BYMONTHDAY=31). Check we're not past the window.
      int64_t probeDay;
      switch (r.freq) {
        case 1: probeDay = startDay + p * r.interval; break;
        case 2: probeDay = startDay + p * 7 * r.interval; break;
        case 3: probeDay = startDay + p * 28 * r.interval; break;
        default: probeDay = startDay + p * 365 * r.interval; break;
      }
      if (probeDay > winEndDay + 1) return;
      continue;
    }
    for (int i = 0; i < nc; ++i) {
      const int64_t day = cand[i];
      if (day < startDay) continue;
      if (untilByDay && day > untilDay) return;
      int64_t occStart;
      if (allDay) {
        occStart = localTz_.dayStartUtc(day);
      } else {
        civ::DateTime dt;
        civ::civilFromDays(day, dt.y, dt.m, dt.d);
        dt.hh = s.local.hh;
        dt.mm = s.local.mm;
        dt.ss = s.local.ss;
        occStart = tz->localToUtc(dt);
      }
      if (r.hasUntil && !untilByDay && occStart > untilUtc) return;
      if (r.count && ++seen > r.count) return;
      if (occStart >= winEnd_) return;
      const int64_t occEnd = allDay ? localTz_.dayStartUtc(day + durDays) : occStart + durSec;
      if (!isExcluded(occStart, day)) emit(occStart, occEnd, allDay, true);
    }
  }
}
