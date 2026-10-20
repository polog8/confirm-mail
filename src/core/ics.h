// Streaming iCalendar (RFC 5545) parser that expands VEVENTs into concrete
// occurrences inside a time window. Designed for microcontrollers: constant
// memory, bytes are fed as they arrive from the network.
//
// Supported: DTSTART/DTEND/DURATION (UTC, TZID, floating, all-day), SUMMARY,
// LOCATION, STATUS:CANCELLED, EXDATE, RECURRENCE-ID overrides and RRULE with
// FREQ=DAILY|WEEKLY|MONTHLY|YEARLY, INTERVAL, COUNT, UNTIL, BYDAY (with
// ordinals), BYMONTHDAY, BYMONTH, BYSETPOS, WKST.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "civil.h"
#include "tz.h"

struct IcsOccurrence {
  int64_t start = 0;  // UTC
  int64_t end = 0;    // UTC, exclusive
  bool allDay = false;
  uint32_t uid = 0;
  char title[72] = {0};
  char location[56] = {0};
  char url[96] = {0};  // first video-conference link found (Meet/Zoom/Teams/Webex)
};

class IcsParser {
 public:
  static constexpr int kMaxResults = 128;
  static constexpr int kMaxOverrides = 64;
  static constexpr int kMaxExdates = 48;

  // localTz is used for floating times, all-day events and unknown TZIDs.
  IcsParser(const PosixTz& localTz, int64_t windowStart, int64_t windowEnd);

  void feed(const char* data, size_t len);
  void finish();

  int count() const { return count_; }
  const IcsOccurrence& at(int i) const { return results_[i]; }
  int eventsSeen() const { return eventsSeen_; }
  bool sawCalendar() const { return sawCalendar_; }

 private:
  struct DtValue {
    bool set = false;
    bool isDate = false;
    bool isUtc = false;
    civ::DateTime local;
    const PosixTz* tz = nullptr;  // nullptr => UTC when isUtc, else local
  };

  struct ByDay {
    int8_t ord;  // 0 = every
    uint8_t wd;  // 0 = Monday
  };

  struct RRule {
    uint8_t freq = 0;  // 0 none, 1 daily, 2 weekly, 3 monthly, 4 yearly
    uint16_t interval = 1;
    int32_t count = 0;
    bool hasUntil = false;
    DtValue until;
    uint8_t nByDay = 0;
    ByDay byDay[14];
    uint8_t nByMonthDay = 0;
    int8_t byMonthDay[16];
    uint16_t byMonthMask = 0;  // bit (m-1)
    uint8_t nBySetPos = 0;
    int16_t bySetPos[8];
    uint8_t wkst = 0;
  };

  struct Exdate {
    bool isDate;
    int64_t day;  // local day number when isDate
    int64_t utc;
  };

  struct Override {
    uint32_t uid;
    Exdate when;
  };

  void processLine(char* line, size_t len);
  void handleProperty(const char* name, char* params, char* value);
  void beginEvent();
  void endEvent();

  bool parseDt(const char* params, const char* value, DtValue& out);
  bool parseDateTime(const char* s, DtValue& out);
  static bool parseDuration(const char* s, int64_t& seconds);
  void parseRRule(char* value);
  int64_t dtToUtc(const DtValue& v) const;
  const PosixTz* resolveTz(const char* tzid);

  void emit(int64_t start, int64_t end, bool allDay, bool fromSeries);
  void expandSeries(int64_t durationSec, int64_t durationDays);
  int genCandidates(int64_t periodIdx, int32_t* out, int cap) const;
  int monthCandidates(int y, int m, int32_t* out, int cap) const;
  bool isExcluded(int64_t startUtc, int64_t day) const;

  // configuration
  const PosixTz& localTz_;
  int64_t winStart_, winEnd_;

  // line assembly
  char line_[1024];
  size_t lineLen_ = 0;
  bool atLineStart_ = true;
  bool haveLine_ = false;

  // component state
  bool inEvent_ = false;
  int nested_ = 0;
  bool sawCalendar_ = false;
  int eventsSeen_ = 0;

  // current VEVENT
  DtValue dtStart_, dtEnd_;
  bool hasDuration_ = false;
  int64_t durationSec_ = 0;
  bool cancelled_ = false;
  bool hasRRule_ = false;
  RRule rrule_;
  bool hasRecurrenceId_ = false;
  Exdate recurrenceId_{};
  int nExdates_ = 0;
  Exdate exdates_[kMaxExdates];
  uint32_t uid_ = 0;
  char summary_[72];
  char location_[56];
  char url_[96];

  // results
  IcsOccurrence results_[kMaxResults];
  bool resultFromSeries_[kMaxResults];
  int count_ = 0;
  Override overrides_[kMaxOverrides];
  int nOverrides_ = 0;

  // tz cache
  struct TzSlot {
    const char* posix;
    PosixTz tz;
  };
  TzSlot tzCache_[6];
  int nTz_ = 0;
  PosixTz utcTz_;
};

// Copies src into dst (capacity cap incl. NUL) without splitting a UTF-8
// sequence.
void utf8Copy(char* dst, size_t cap, const char* src);
