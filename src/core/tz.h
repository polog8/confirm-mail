// Minimal POSIX TZ string evaluator ("CET-1CEST,M3.5.0,M10.5.0/3").
// Used by the calendar parser so that converting event times never touches
// the process-wide TZ environment (which the UI thread relies on).
#pragma once

#include <stdint.h>

#include "civil.h"

class PosixTz {
 public:
  PosixTz() = default;
  explicit PosixTz(const char* spec) { parse(spec); }

  // Returns false (and falls back to UTC) on malformed input.
  bool parse(const char* spec);

  // Seconds east of UTC in effect at the given instant (local = utc + offset).
  int32_t offsetAt(int64_t utc) const;

  // Converts a local wall-clock time to an instant. Ambiguous times (DST
  // fall-back) resolve to the earlier instant; non-existent times (spring
  // forward gap) are interpreted with the standard offset.
  int64_t localToUtc(const civ::DateTime& local) const;

  // Local midnight of the given day number (days since 1970-01-01).
  int64_t dayStartUtc(int64_t localDay) const;

  // Local day number containing the instant.
  int64_t localDay(int64_t utc) const { return civ::floorDiv(utc + offsetAt(utc), 86400); }

  bool hasDst() const { return hasDst_; }

 private:
  struct Rule {
    char kind = 'M';  // 'M' month/week/day, 'J' julian 1..365 (no leap), 'D' 0..365
    int month = 0, week = 0, wday = 0, day = 0;
    int32_t time = 7200;  // seconds after local midnight
  };

  // Local (pre-transition) time of the rule in a given year, as "local epoch".
  static int64_t ruleLocal(const Rule& r, int year);

  int32_t stdOffset_ = 0;  // seconds east of UTC
  int32_t dstOffset_ = 0;
  bool hasDst_ = false;
  Rule start_, end_;
};

// Maps an iCalendar TZID (IANA, Windows or Mozilla-prefixed) to a POSIX TZ
// string. Returns nullptr when unknown.
const char* posixForTzid(const char* tzid);
