// Host-side unit tests for the portable calendar/time code.
//   make -C test/host
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "../../src/core/civil.h"
#include "../../src/core/ics.h"
#include "../../src/core/tz.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                    \
  do {                                                                 \
    ++g_checks;                                                        \
    if (!(cond)) {                                                     \
      ++g_failures;                                                    \
      printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
    }                                                                  \
  } while (0)

#define CHECK_EQ(a, b)                                                                     \
  do {                                                                                     \
    ++g_checks;                                                                            \
    long long _va = (long long)(a), _vb = (long long)(b);                                  \
    if (_va != _vb) {                                                                      \
      ++g_failures;                                                                        \
      printf("  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _va, _vb); \
    }                                                                                      \
  } while (0)

static const char* kParis = "CET-1CEST,M3.5.0,M10.5.0/3";

static int64_t utc(int y, int m, int d, int hh = 0, int mm = 0, int ss = 0) {
  civ::DateTime dt{y, m, d, hh, mm, ss};
  return civ::toEpoch(dt);
}

static std::string fmt(int64_t t) {
  civ::DateTime d = civ::fromEpoch(t);
  char buf[32];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", d.y, d.m, d.d, d.hh, d.mm);
  return buf;
}

struct Parsed {
  std::vector<IcsOccurrence> occ;
};

static Parsed parse(const char* ics, int64_t ws, int64_t we, size_t chunk = 7) {
  static PosixTz paris(kParis);
  IcsParser* p = new IcsParser(paris, ws, we);
  // Feed in small chunks to exercise line reassembly.
  size_t len = strlen(ics);
  for (size_t i = 0; i < len; i += chunk) p->feed(ics + i, (len - i) < chunk ? (len - i) : chunk);
  p->finish();
  Parsed r;
  for (int i = 0; i < p->count(); ++i) r.occ.push_back(p->at(i));
  std::sort(r.occ.begin(), r.occ.end(),
            [](const IcsOccurrence& a, const IcsOccurrence& b) { return a.start < b.start; });
  delete p;
  return r;
}

static std::string wrap(const char* body) {
  std::string s = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:test\r\n";
  s += body;
  s += "END:VCALENDAR\r\n";
  return s;
}

static void testCivil() {
  printf("civil\n");
  CHECK_EQ(civ::daysFromCivil(1970, 1, 1), 0);
  CHECK_EQ(civ::daysFromCivil(2000, 3, 1), 11017);
  int y, m, d;
  civ::civilFromDays(20746, y, m, d);  // 2026-10-20
  CHECK_EQ(y, 2026);
  CHECK_EQ(m, 10);
  CHECK_EQ(d, 20);
  CHECK_EQ(civ::weekdayFromDays(civ::daysFromCivil(2026, 10, 20)), 1);  // Tuesday
  CHECK_EQ(civ::weekdayFromDays(civ::daysFromCivil(1969, 12, 31)), 2);  // Wednesday
  CHECK_EQ(civ::daysInMonth(2028, 2), 29);
  CHECK_EQ(civ::daysInMonth(2100, 2), 28);
  CHECK_EQ(civ::fromEpoch(-1).y, 1969);
}

static void testTz() {
  printf("tz\n");
  PosixTz paris(kParis);
  CHECK_EQ(paris.offsetAt(utc(2026, 1, 15, 12)), 3600);
  CHECK_EQ(paris.offsetAt(utc(2026, 7, 15, 12)), 7200);
  // 2026 DST: starts Sun 29 March 01:00 UTC, ends Sun 25 October 01:00 UTC.
  CHECK_EQ(paris.offsetAt(utc(2026, 3, 29, 0, 59, 59)), 3600);
  CHECK_EQ(paris.offsetAt(utc(2026, 3, 29, 1, 0, 0)), 7200);
  CHECK_EQ(paris.offsetAt(utc(2026, 10, 25, 0, 59, 59)), 7200);
  CHECK_EQ(paris.offsetAt(utc(2026, 10, 25, 1, 0, 0)), 3600);
  // local -> utc
  CHECK_EQ(paris.localToUtc({2026, 10, 20, 9, 0, 0}), utc(2026, 10, 20, 7));
  CHECK_EQ(paris.localToUtc({2026, 12, 1, 9, 0, 0}), utc(2026, 12, 1, 8));
  // ambiguous 02:30 on fall-back day -> earlier instant (CEST)
  CHECK_EQ(paris.localToUtc({2026, 10, 25, 2, 30, 0}), utc(2026, 10, 25, 0, 30));
  // gap 02:30 on spring-forward day -> standard offset interpretation
  CHECK_EQ(paris.localToUtc({2026, 3, 29, 2, 30, 0}), utc(2026, 3, 29, 1, 30));
  CHECK_EQ(paris.dayStartUtc(civ::daysFromCivil(2026, 10, 20)), utc(2026, 10, 19, 22));

  PosixTz ny("EST5EDT,M3.2.0,M11.1.0");
  CHECK_EQ(ny.offsetAt(utc(2026, 3, 8, 6, 59)), -5 * 3600);
  CHECK_EQ(ny.offsetAt(utc(2026, 3, 8, 7, 0)), -4 * 3600);
  CHECK_EQ(ny.offsetAt(utc(2026, 11, 1, 5, 59)), -4 * 3600);
  CHECK_EQ(ny.offsetAt(utc(2026, 11, 1, 6, 0)), -5 * 3600);

  PosixTz syd("AEST-10AEDT,M10.1.0,M4.1.0/3");
  CHECK_EQ(syd.offsetAt(utc(2026, 1, 10)), 11 * 3600);
  CHECK_EQ(syd.offsetAt(utc(2026, 6, 10)), 10 * 3600);
  CHECK_EQ(syd.offsetAt(utc(2026, 12, 31, 23)), 11 * 3600);

  PosixTz india("IST-5:30");
  CHECK_EQ(india.offsetAt(utc(2026, 6, 1)), 5 * 3600 + 1800);
  PosixTz quoted("<+04>-4");
  CHECK_EQ(quoted.offsetAt(0), 4 * 3600);
  PosixTz utc0("UTC0");
  CHECK_EQ(utc0.offsetAt(123456), 0);

  CHECK(posixForTzid("Europe/Paris") != nullptr);
  CHECK(strcmp(posixForTzid("/mozilla.org/20050126_1/Europe/Paris"), kParis) == 0);
  CHECK(strcmp(posixForTzid("\"Romance Standard Time\""), kParis) == 0);
  CHECK(strcmp(posixForTzid("(UTC+01:00) Brussels, Copenhagen, Madrid, Paris"), kParis) == 0);
  CHECK(posixForTzid("Mars/Olympus_Mons") == nullptr);
}

static void testSimpleEvents() {
  printf("simple events\n");
  std::string ics = wrap(
      "BEGIN:VEVENT\r\nUID:a\r\nDTSTART:20261020T130000Z\r\nDTEND:20261020T140000Z\r\n"
      "SUMMARY:R\\u00e9union \\, \"budget\" \\;\r\n LONG folded\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:b\r\nDTSTART;TZID=Europe/Paris:20261021T090000\r\n"
      "DTEND;TZID=Europe/Paris:20261021T093000\r\nSUMMARY:Stand-up équipe\r\n"
      "LOCATION:Salle 2\\, bât. A\r\n"
      "DESCRIPTION:Rejoindre : https://meet.google.com/abc-defg-hij\\nMerci\r\n"
      "BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:Reminder\r\nTRIGGER:-PT15M\r\nEND:VALARM\r\n"
      "END:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:c\r\nDTSTART;VALUE=DATE:20261022\r\nDTEND;VALUE=DATE:20261024\r\n"
      "SUMMARY:Congés\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:d\r\nDTSTART:20261023T100000\r\nDURATION:PT45M\r\nSUMMARY:Flottant\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:e\r\nDTSTART:20261023T100000Z\r\nSTATUS:CANCELLED\r\nSUMMARY:Annulé\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:f\r\nDTSTART:20261123T100000Z\r\nSUMMARY:Hors fenêtre\r\nEND:VEVENT\r\n");
  Parsed r = parse(ics.c_str(), utc(2026, 10, 19, 22), utc(2026, 10, 27, 23));
  CHECK_EQ(r.occ.size(), 4);
  if (r.occ.size() != 4) return;
  CHECK_EQ(r.occ[0].start, utc(2026, 10, 20, 13));
  CHECK_EQ(r.occ[0].end, utc(2026, 10, 20, 14));
  CHECK(strstr(r.occ[0].title, "LONG folded") != nullptr);
  CHECK(strstr(r.occ[0].title, ", \"budget\" ;") != nullptr);
  CHECK_EQ(r.occ[1].start, utc(2026, 10, 21, 7));
  CHECK_EQ(r.occ[1].end, utc(2026, 10, 21, 7, 30));
  CHECK(strcmp(r.occ[1].title, "Stand-up équipe") == 0);
  CHECK(strcmp(r.occ[1].location, "Salle 2, bât. A") == 0);
  CHECK(strcmp(r.occ[1].url, "https://meet.google.com/abc-defg-hij") == 0);
  CHECK(r.occ[2].allDay);
  CHECK_EQ(r.occ[2].start, utc(2026, 10, 21, 22));
  CHECK_EQ(r.occ[2].end, utc(2026, 10, 23, 22));
  CHECK_EQ(r.occ[3].start, utc(2026, 10, 23, 8));  // floating = local
  CHECK_EQ(r.occ[3].end, utc(2026, 10, 23, 8, 45));
}

static std::vector<std::string> starts(const Parsed& r) {
  std::vector<std::string> v;
  for (auto& o : r.occ) v.push_back(fmt(o.start));
  return v;
}

static void expectStarts(const Parsed& r, const std::vector<std::string>& want) {
  auto got = starts(r);
  ++g_checks;
  if (got != want) {
    ++g_failures;
    printf("  FAIL starts mismatch\n    got :");
    for (auto& s : got) printf(" [%s]", s.c_str());
    printf("\n    want:");
    for (auto& s : want) printf(" [%s]", s.c_str());
    printf("\n");
  }
}

static void testRecurrence() {
  printf("recurrence\n");
  const int64_t ws = utc(2026, 10, 18, 22);  // Mon 19 Oct local midnight
  const int64_t we = utc(2026, 11, 1, 23);   // Mon 2 Nov local midnight

  // Weekly MO,WE,FR at 10:00 Paris; crosses the 25 Oct DST change.
  expectStarts(parse(wrap("BEGIN:VEVENT\r\nUID:w\r\nDTSTART;TZID=Europe/Paris:20260105T100000\r\n"
                          "DTEND;TZID=Europe/Paris:20260105T103000\r\n"
                          "RRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR\r\nSUMMARY:Sync\r\nEND:VEVENT\r\n")
                         .c_str(),
                     ws, we),
               {"2026-10-19 08:00", "2026-10-21 08:00", "2026-10-23 08:00", "2026-10-26 09:00",
                "2026-10-28 09:00", "2026-10-30 09:00"});

  // COUNT limits instances counted from DTSTART.
  expectStarts(parse(wrap("BEGIN:VEVENT\r\nUID:c\r\nDTSTART:20261015T120000Z\r\n"
                          "RRULE:FREQ=DAILY;COUNT=7\r\nSUMMARY:Cure\r\nEND:VEVENT\r\n")
                         .c_str(),
                     ws, we),
               {"2026-10-19 12:00", "2026-10-20 12:00", "2026-10-21 12:00"});

  // UNTIL (UTC) inclusive + INTERVAL.
  expectStarts(parse(wrap("BEGIN:VEVENT\r\nUID:u\r\nDTSTART;TZID=Europe/Paris:20260901T180000\r\n"
                          "RRULE:FREQ=DAILY;INTERVAL=3;UNTIL=20261025T170000Z\r\nSUMMARY:Sport\r\nEND:VEVENT\r\n")
                         .c_str(),
                     ws, we),
               {"2026-10-19 16:00", "2026-10-22 16:00", "2026-10-25 17:00"});

  // Long running daily series with interval: skip-ahead must stay aligned.
  expectStarts(parse(wrap("BEGIN:VEVENT\r\nUID:d\r\nDTSTART:20150101T070000Z\r\n"
                          "RRULE:FREQ=DAILY;INTERVAL=5\r\nSUMMARY:x\r\nEND:VEVENT\r\n")
                         .c_str(),
                     ws, we),
               {"2026-10-20 07:00", "2026-10-25 07:00", "2026-10-30 07:00"});

  // Monthly last Friday.
  expectStarts(parse(wrap("BEGIN:VEVENT\r\nUID:m\r\nDTSTART:20250131T150000Z\r\n"
                          "RRULE:FREQ=MONTHLY;BYDAY=-1FR\r\nSUMMARY:Bilan\r\nEND:VEVENT\r\n")
                         .c_str(),
                     ws, we),
               {"2026-10-30 15:00"});

  // Monthly on the 31st skips short months; window over Oct 31.
  expectStarts(parse(wrap("BEGIN:VEVENT\r\nUID:m31\r\nDTSTART:20260131T090000Z\r\n"
                          "RRULE:FREQ=MONTHLY\r\nSUMMARY:Loyer\r\nEND:VEVENT\r\n")
                         .c_str(),
                     ws, we),
               {"2026-10-31 09:00"});

  // Last weekday of the month via BYSETPOS.
  expectStarts(parse(wrap("BEGIN:VEVENT\r\nUID:sp\r\nDTSTART:20260130T160000Z\r\n"
                          "RRULE:FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1\r\nSUMMARY:Paie\r\nEND:VEVENT\r\n")
                         .c_str(),
                     ws, we),
               {"2026-10-30 16:00"});

  // Yearly all-day birthday since 1990.
  {
    Parsed r = parse(wrap("BEGIN:VEVENT\r\nUID:bd\r\nDTSTART;VALUE=DATE:19901024\r\n"
                          "DTEND;VALUE=DATE:19901025\r\nRRULE:FREQ=YEARLY\r\nSUMMARY:Anniv Léa\r\nEND:VEVENT\r\n")
                         .c_str(),
                     ws, we);
    expectStarts(r, {"2026-10-23 22:00"});
    if (!r.occ.empty()) {
      CHECK(r.occ[0].allDay);
      CHECK_EQ(r.occ[0].end - r.occ[0].start, 86400);
    }
  }

  // Yearly with BYMONTH + ordinal BYDAY (4th Thursday of October).
  expectStarts(parse(wrap("BEGIN:VEVENT\r\nUID:y\r\nDTSTART:20201022T170000Z\r\n"
                          "RRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=4TH\r\nSUMMARY:x\r\nEND:VEVENT\r\n")
                         .c_str(),
                     ws, we),
               {"2026-10-22 17:00"});

  // EXDATE (with TZID) removes an instance; RECURRENCE-ID moves another.
  expectStarts(parse(wrap("BEGIN:VEVENT\r\nUID:series@x\r\nDTSTART;TZID=Europe/Paris:20260901T143000\r\n"
                          "DTEND;TZID=Europe/Paris:20260901T150000\r\nRRULE:FREQ=WEEKLY;BYDAY=TU,TH\r\n"
                          "EXDATE;TZID=Europe/Paris:20261020T143000,20261029T143000\r\n"
                          "SUMMARY:1:1\r\nEND:VEVENT\r\n"
                          "BEGIN:VEVENT\r\nUID:series@x\r\nRECURRENCE-ID;TZID=Europe/Paris:20261022T143000\r\n"
                          "DTSTART;TZID=Europe/Paris:20261022T170000\r\nDTEND;TZID=Europe/Paris:20261022T173000\r\n"
                          "SUMMARY:1:1 (décalé)\r\nEND:VEVENT\r\n"
                          "BEGIN:VEVENT\r\nUID:series@x\r\nRECURRENCE-ID;TZID=Europe/Paris:20261027T143000\r\n"
                          "DTSTART;TZID=Europe/Paris:20261027T143000\r\nSTATUS:CANCELLED\r\nEND:VEVENT\r\n")
                         .c_str(),
                     ws, we),
               {"2026-10-22 15:00"});

  // Override parsed before its master.
  expectStarts(parse(wrap("BEGIN:VEVENT\r\nUID:o\r\nRECURRENCE-ID:20261021T080000Z\r\n"
                          "DTSTART:20261021T090000Z\r\nSUMMARY:moved\r\nEND:VEVENT\r\n"
                          "BEGIN:VEVENT\r\nUID:o\r\nDTSTART:20261019T080000Z\r\nRRULE:FREQ=DAILY;COUNT=3\r\n"
                          "SUMMARY:base\r\nEND:VEVENT\r\n")
                         .c_str(),
                     ws, we),
               {"2026-10-19 08:00", "2026-10-20 08:00", "2026-10-21 09:00"});

  // Biweekly with WKST=SU.
  expectStarts(parse(wrap("BEGIN:VEVENT\r\nUID:bw\r\nDTSTART:20260906T100000Z\r\n"
                          "RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=SU,TU;WKST=SU\r\nSUMMARY:x\r\nEND:VEVENT\r\n")
                         .c_str(),
                     ws, we),
               {"2026-10-20 10:00", "2026-11-01 10:00"});
}

static void testUtf8() {
  printf("utf8\n");
  char buf[6];
  utf8Copy(buf, sizeof(buf), "abcdé");  // é is 2 bytes; would need 7
  CHECK(strcmp(buf, "abcd") == 0);
  utf8Copy(buf, sizeof(buf), "abé");
  CHECK(strcmp(buf, "abé") == 0);
}

int main() {
  testCivil();
  testTz();
  testSimpleEvents();
  testRecurrence();
  testUtf8();
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
