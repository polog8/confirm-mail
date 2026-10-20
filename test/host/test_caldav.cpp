// Unit tests for the CalDAV client with canned, iCloud-shaped responses.
//   make -C test/host
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "../../src/core/caldav.h"
#include "../../src/core/civil.h"

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                            \
  do {                                                         \
    ++g_checks;                                                \
    if (!(cond)) {                                             \
      ++g_failures;                                            \
      printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                          \
  } while (0)

#define CHECK_STR(a, b)                                                                               \
  do {                                                                                                \
    ++g_checks;                                                                                       \
    std::string _a = (a), _b = (b);                                                                   \
    if (_a != _b) {                                                                                   \
      ++g_failures;                                                                                   \
      printf("  FAIL %s:%d: %s\n    got  [%s]\n    want [%s]\n", __FILE__, __LINE__, #a, _a.c_str(), _b.c_str()); \
    }                                                                                                 \
  } while (0)

static void collect(void* ctx, const char* d, size_t n) { static_cast<std::string*>(ctx)->append(d, n); }

// Feeds a document one byte at a time (worst case for the state machine).
static std::string extract(const std::string& doc, int* blocks = nullptr) {
  std::string out;
  CalendarDataExtractor x(collect, &out);
  for (char c : doc) x.feed(&c, 1);
  x.finish();
  if (blocks) *blocks = x.blocks();
  return out;
}

static void testExtractor() {
  printf("extractor\n");
  int blocks = 0;
  // iCloud style: CDATA, no trailing newline before ]]>.
  std::string icloud =
      "<?xml version='1.0' encoding='UTF-8'?><multistatus xmlns='DAV:'>"
      "<response><href>/1/calendars/home/a.ics</href><propstat><prop>"
      "<calendar-data xmlns='urn:ietf:params:xml:ns:caldav'><![CDATA[BEGIN:VCALENDAR\r\nX:a & <b>\r\n"
      "END:VCALENDAR]]></calendar-data></prop><status>HTTP/1.1 200 OK</status></propstat></response>"
      "<response><href>/1/calendars/home/b.ics</href><propstat><prop>"
      "<calendar-data xmlns='urn:ietf:params:xml:ns:caldav'><![CDATA[BEGIN:VCALENDAR\r\nY:]]]]><![CDATA[>]]>"
      "</calendar-data></prop></propstat></response></multistatus>";
  CHECK_STR(extract(icloud, &blocks), "BEGIN:VCALENDAR\r\nX:a & <b>\r\nEND:VCALENDAR\r\nBEGIN:VCALENDAR\r\nY:]]>\r\n");
  CHECK(blocks == 2);

  // Nextcloud/Radicale style: escaped text, prefixed element, CR as entity.
  std::string escaped =
      "<d:multistatus xmlns:d=\"DAV:\" xmlns:cal=\"urn:ietf:params:xml:ns:caldav\"><d:response>"
      "<d:propstat><d:prop><cal:calendar-data>BEGIN:VCALENDAR&#13;\nSUMMARY:Caf&#233; &amp; th&#xE9; &lt;3&#13;\n"
      "END:VCALENDAR&#13;\n</cal:calendar-data></d:prop></d:propstat></d:response></d:multistatus>";
  CHECK_STR(extract(escaped, &blocks),
            "BEGIN:VCALENDAR\r\nSUMMARY:Café & thé <3\r\nEND:VCALENDAR\r\n\r\n");
  CHECK(blocks == 1);

  // Self-closing and other elements are ignored.
  CHECK_STR(extract("<a><calendar-data/><calendar-data-x>no</calendar-data-x><b>t</b></a>", &blocks), "");
  CHECK(blocks == 0);
}

static void testHelpers() {
  printf("helpers\n");
  CHECK_STR(caldav::resolve("https://caldav.icloud.com/", "/123/principal/"), "https://caldav.icloud.com/123/principal/");
  CHECK_STR(caldav::resolve("https://p52-caldav.icloud.com:443/123/calendars/", "/123/calendars/home/"),
            "https://p52-caldav.icloud.com:443/123/calendars/home/");
  CHECK_STR(caldav::resolve("https://a.example/dav/cal/", "work/"), "https://a.example/dav/cal/work/");
  CHECK_STR(caldav::resolve("https://a.example/x", "https://b.example/y"), "https://b.example/y");
  CHECK_STR(caldav::resolve("https://a.example", "/p"), "https://a.example/p");
  uint32_t c = 0;
  CHECK(caldav::parseColor("#1BADF8FF", &c) && c == 0x1BADF8);
  CHECK(caldav::parseColor("#ff2968", &c) && c == 0xFF2968);
  CHECK(!caldav::parseColor("blue", &c));
  CHECK_STR(caldav::utcStamp(1792454400), "20261020T000000Z");

  const std::string doc = "<a:x><b:href>  /one&amp;two  </b:href><href><![CDATA[/c<d]]></href><n:x>inner</n:x></a:x>";
  const char *ib, *ie, *after;
  CHECK(caldav::findElement(doc.data(), doc.data() + doc.size(), "x", &ib, &ie, &after));
  CHECK_STR(std::string(ib, ie), "<b:href>  /one&amp;two  </b:href><href><![CDATA[/c<d]]></href><n:x>inner</n:x>");
  const char *hb, *he;
  CHECK(caldav::findElement(ib, ie, "href", &hb, &he, &after));
  CHECK_STR(caldav::elementText(hb, he), "/one&two");
  CHECK(caldav::findElement(after, ie, "href", &hb, &he));
  CHECK_STR(caldav::elementText(hb, he), "/c<d");
}

// ---------------------------------------------------------------------------
// Fake server

struct FakeTransport : CalDavTransport {
  struct Reply {
    int code;
    std::string body;
    std::string location;
  };
  std::map<std::string, Reply> routes;  // "METHOD url"
  std::vector<std::string> log;

  int request(const char* method, const std::string& url, const char* depth, const std::string& body, Sink sink,
              void* ctx, std::string* location) override {
    log.push_back(std::string(method) + " " + url + " depth=" + depth);
    auto it = routes.find(std::string(method) + " " + url);
    if (it == routes.end()) return 404;
    if (std::string(method) == "REPORT") {
      // The query must carry a time range.
      if (body.find("<c:time-range start=\"") == std::string::npos) return 400;
    }
    if (location) *location = it->second.location;
    const std::string& b = it->second.body;
    for (size_t i = 0; i < b.size(); i += 7) sink(ctx, b.data() + i, std::min<size_t>(7, b.size() - i));
    return it->second.code;
  }
};

static void testClient() {
  printf("client\n");
  FakeTransport t;
  t.routes["PROPFIND https://caldav.icloud.com/"] = {301, "", "https://caldav.icloud.com:443/"};
  t.routes["PROPFIND https://caldav.icloud.com:443/"] = {
      207,
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?><multistatus xmlns=\"DAV:\"><response><href>/</href><propstat><prop>"
      "<current-user-principal><href>/1234567890/principal/</href></current-user-principal></prop>"
      "<status>HTTP/1.1 200 OK</status></propstat></response></multistatus>",
      ""};
  t.routes["PROPFIND https://caldav.icloud.com:443/1234567890/principal/"] = {
      207,
      "<multistatus xmlns=\"DAV:\"><response><href>/1234567890/principal/</href><propstat><prop>"
      "<calendar-home-set xmlns=\"urn:ietf:params:xml:ns:caldav\"><href xmlns=\"DAV:\">"
      "https://p52-caldav.icloud.com:443/1234567890/calendars/</href></calendar-home-set></prop>"
      "<status>HTTP/1.1 200 OK</status></propstat></response></multistatus>",
      ""};
  t.routes["PROPFIND https://p52-caldav.icloud.com:443/1234567890/calendars/"] = {
      207,
      "<multistatus xmlns=\"DAV:\">"
      "<response><href>/1234567890/calendars/</href><propstat><prop><resourcetype><collection/></resourcetype>"
      "</prop><status>HTTP/1.1 200 OK</status></propstat></response>"
      "<response><href>/1234567890/calendars/home/</href><propstat><prop><displayname>Maison</displayname>"
      "<resourcetype><collection/><calendar xmlns=\"urn:ietf:params:xml:ns:caldav\"/></resourcetype>"
      "<supported-calendar-component-set xmlns=\"urn:ietf:params:xml:ns:caldav\"><comp name=\"VEVENT\"/>"
      "</supported-calendar-component-set><calendar-color xmlns=\"http://apple.com/ns/ical/\" "
      "symbolic-color=\"blue\">#1BADF8FF</calendar-color></prop><status>HTTP/1.1 200 OK</status></propstat>"
      "</response>"
      "<response><href>/1234567890/calendars/work/</href><propstat><prop><displayname>Travail &amp; projets"
      "</displayname><resourcetype><collection/><calendar xmlns=\"urn:ietf:params:xml:ns:caldav\"/></resourcetype>"
      "<calendar-color xmlns=\"http://apple.com/ns/ical/\">#FF2968</calendar-color></prop>"
      "<status>HTTP/1.1 200 OK</status></propstat><propstat><prop><supported-calendar-component-set "
      "xmlns=\"urn:ietf:params:xml:ns:caldav\"/></prop><status>HTTP/1.1 404 Not Found</status></propstat></response>"
      "<response><href>/1234567890/calendars/tasks/</href><propstat><prop><displayname>Rappels</displayname>"
      "<resourcetype><collection/><calendar xmlns=\"urn:ietf:params:xml:ns:caldav\"/></resourcetype>"
      "<supported-calendar-component-set xmlns=\"urn:ietf:params:xml:ns:caldav\"><comp name=\"VTODO\"/>"
      "</supported-calendar-component-set></prop><status>HTTP/1.1 200 OK</status></propstat></response>"
      "<response><href>/1234567890/calendars/inbox/</href><propstat><prop><resourcetype><collection/>"
      "<schedule-inbox xmlns=\"urn:ietf:params:xml:ns:caldav\"/></resourcetype></prop>"
      "<status>HTTP/1.1 200 OK</status></propstat></response>"
      "<response><href>/1234567890/calendars/notification/</href><propstat><prop><resourcetype><collection/>"
      "<notification xmlns=\"http://calendarserver.org/ns/\"/></resourcetype></prop>"
      "<status>HTTP/1.1 200 OK</status></propstat></response>"
      "</multistatus>",
      ""};

  CalDavClient client(t);
  CalDavCalendar cals[8];
  int n = client.discover("https://caldav.icloud.com/", cals, 8);
  CHECK(n == 2);
  if (n == 2) {
    CHECK_STR(cals[0].name, "Maison");
    CHECK_STR(cals[0].url, "https://p52-caldav.icloud.com:443/1234567890/calendars/home/");
    CHECK(cals[0].hasColor && cals[0].color == 0x1BADF8);
    CHECK_STR(cals[1].name, "Travail & projets");
    CHECK(cals[1].hasColor && cals[1].color == 0xFF2968);
  }
  CHECK(t.log.size() == 4);
  if (t.log.size() == 4) CHECK_STR(t.log[3], "PROPFIND https://p52-caldav.icloud.com:443/1234567890/calendars/ depth=1");

  // Events
  t.routes["REPORT https://p52-caldav.icloud.com:443/1234567890/calendars/home/"] = {
      207,
      "<?xml version='1.0' encoding='UTF-8'?><multistatus xmlns='DAV:'><response>"
      "<href>/1234567890/calendars/home/A.ics</href><propstat><prop><calendar-data "
      "xmlns='urn:ietf:params:xml:ns:caldav'><![CDATA[BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
      "BEGIN:VTIMEZONE\r\nTZID:Europe/Paris\r\nBEGIN:DAYLIGHT\r\nDTSTART:19810329T020000\r\nTZOFFSETTO:+0200\r\n"
      "END:DAYLIGHT\r\nEND:VTIMEZONE\r\nBEGIN:VEVENT\r\nUID:a\r\nDTSTART;TZID=Europe/Paris:20261020T100000\r\n"
      "DTEND;TZID=Europe/Paris:20261020T110000\r\nSUMMARY:Café & croissants <3\r\nEND:VEVENT\r\nEND:VCALENDAR"
      "]]></calendar-data></prop><status>HTTP/1.1 200 OK</status></propstat></response><response>"
      "<href>/1234567890/calendars/home/B.ics</href><propstat><prop><calendar-data "
      "xmlns='urn:ietf:params:xml:ns:caldav'><![CDATA[BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:b\r\n"
      "DTSTART:20260105T080000Z\r\nDURATION:PT30M\r\nRRULE:FREQ=WEEKLY;BYDAY=MO,WE\r\nSUMMARY:Sync\r\n"
      "END:VEVENT\r\nBEGIN:VEVENT\r\nUID:b\r\nRECURRENCE-ID:20261021T080000Z\r\nDTSTART:20261021T090000Z\r\n"
      "DURATION:PT30M\r\nSUMMARY:Sync (décalé)\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n]]></calendar-data></prop>"
      "<status>HTTP/1.1 200 OK</status></propstat></response></multistatus>",
      ""};
  PosixTz paris("CET-1CEST,M3.5.0,M10.5.0/3");
  auto utc = [](int y, int m, int d, int hh) { return civ::toEpoch(civ::DateTime{y, m, d, hh, 0, 0}); };
  const int64_t ws = utc(2026, 10, 18, 22);  // Mon 19 Oct, local midnight
  const int64_t we = ws + 7 * 86400;
  IcsParser* parser = new IcsParser(paris, ws, we);
  CHECK(client.fetchEvents(cals[0].url, ws, we, *parser));
  parser->finish();
  std::vector<std::pair<int64_t, std::string>> got;
  for (int i = 0; i < parser->count(); ++i) got.push_back({parser->at(i).start, parser->at(i).title});
  std::sort(got.begin(), got.end());
  CHECK(got.size() == 3);
  if (got.size() == 3) {
    CHECK(got[0].first == utc(2026, 10, 19, 8) && got[0].second == "Sync");
    CHECK(got[1].first == utc(2026, 10, 20, 8) && got[1].second == "Café & croissants <3");
    CHECK(got[2].first == utc(2026, 10, 21, 9) && got[2].second == "Sync (décalé)");
  }
  delete parser;

  // Wrong password
  FakeTransport denied;
  denied.routes["PROPFIND https://caldav.icloud.com/"] = {401, "", ""};
  CalDavClient c2(denied);
  CHECK(c2.discover("https://caldav.icloud.com/", cals, 8) == -1);
  CHECK_STR(c2.error(), "identifiants refusés");
}

int main() {
  testExtractor();
  testHelpers();
  testClient();
  printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
