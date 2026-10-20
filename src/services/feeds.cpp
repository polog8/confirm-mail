#include "feeds.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <new>

#include "../core/caldav.h"
#include "../core/ics.h"
#include "caldav_http.h"
#include "settings.h"
#include "store.h"
#include "tls.h"

namespace feeds {

namespace {

PosixTz g_tz;

// One displayed calendar: either an iCal link from config.h or a calendar
// discovered on the CalDAV account.
struct Slot {
  bool caldav = false;
  int icsIndex = -1;
  char url[200] = {0};
  char name[24] = {0};
  uint32_t color = 0;
  CalendarEvent* events = nullptr;
  int count = 0;
  bool ok = false;
};

Slot g_slots[kMaxCalendars];
int g_numSlots = 0;
int g_numIcs = 0;
bool g_discovered = false;
uint32_t g_nextDiscovery = 0;
char g_discoveryError[64] = {0};

const uint32_t kPalette[] = {0x5B8DEF, 0xF59E0B, 0x34D399, 0xF472B6, 0xA78BFA, 0x22D3EE, 0xF87171, 0xFACC15};

const char* accountLabel() { return strstr(cfg::kCalDavUrl, "icloud") ? "iCloud" : "CalDAV"; }

void setError(char* err, size_t n, const char* name, const char* what) { snprintf(err, n, "%s : %s", name, what); }

CalendarEvent* allocEvents() {
  return static_cast<CalendarEvent*>(
      heap_caps_malloc(sizeof(CalendarEvent) * kMaxEvents, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

IcsParser* newParser(int64_t ws, int64_t we) {
  void* mem = heap_caps_malloc(sizeof(IcsParser), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return mem ? new (mem) IcsParser(g_tz, ws, we) : nullptr;
}

void deleteParser(IcsParser* p) {
  p->~IcsParser();
  free(p);
}

void storeResults(Slot& slot, const IcsParser& parser) {
  slot.count = 0;
  for (int i = 0; i < parser.count() && slot.count < kMaxEvents; ++i) {
    const IcsOccurrence& o = parser.at(i);
    CalendarEvent& e = slot.events[slot.count++];
    e.start = o.start;
    e.end = o.end;
    e.allDay = o.allDay;
    e.uid = o.uid;
    memcpy(e.title, o.title, sizeof(e.title));
    memcpy(e.location, o.location, sizeof(e.location));
    memcpy(e.url, o.url, sizeof(e.url));
  }
}

// Pushes names and colors to the UI.
void publishCalendars() {
  Store::Guard g(g_store);
  CalendarState& cal = g_store.data().cal;
  cal.numCalendars = g_numSlots;
  for (int s = 0; s < g_numSlots; ++s) {
    strlcpy(cal.calendars[s].name, g_slots[s].name, sizeof(cal.calendars[s].name));
    cal.calendars[s].color = g_slots[s].color;
  }
  g_store.touch();
}

// "Maison, Travail" -> does `name` appear in the list? Empty list = all.
bool wanted(const char* name) {
  const char* list = cfg::kCalDavCalendars;
  if (!list || !*list) return true;
  const size_t len = strlen(name);
  const char* p = list;
  while (*p) {
    while (*p == ' ' || *p == ',') ++p;
    const char* b = p;
    while (*p && *p != ',') ++p;
    const char* e = p;
    while (e > b && e[-1] == ' ') --e;
    if (static_cast<size_t>(e - b) == len && strncasecmp(b, name, len) == 0) return true;
  }
  return false;
}

// Lists the CalDAV account's calendars and maps them to slots, keeping the
// cached events of calendars that were already known.
bool discoverCalDav() {
  HttpsCalDavTransport transport(cfg::kCalDavUser, cfg::kCalDavPassword);
  CalDavClient client(transport);
  static CalDavCalendar found[16];
  const int n = client.discover(cfg::kCalDavUrl, found, 16);
  if (n < 0) {
    setError(g_discoveryError, sizeof(g_discoveryError), accountLabel(), client.error());
    log_w("CalDAV discovery failed: %s (HTTP %d)", client.error(), client.lastStatus());
    return false;
  }
  g_discoveryError[0] = '\0';

  Slot old[kMaxCalendars];
  const int oldCount = g_numSlots;
  for (int s = g_numIcs; s < oldCount; ++s) old[s] = g_slots[s];
  int next = g_numIcs;
  int matched = 0;
  for (int i = 0; i < n && next < kMaxCalendars; ++i) {
    if (!wanted(found[i].name)) continue;
    ++matched;
    Slot slot;
    slot.caldav = true;
    strlcpy(slot.url, found[i].url, sizeof(slot.url));
    strlcpy(slot.name, found[i].name, sizeof(slot.name));
    slot.color = found[i].hasColor ? found[i].color : kPalette[next % 8];
    // Reuse the event buffer (and cache) of the same calendar if we had it.
    for (int s = g_numIcs; s < oldCount; ++s) {
      if (old[s].events && strcmp(old[s].url, slot.url) == 0) {
        slot.events = old[s].events;
        slot.count = old[s].count;
        slot.ok = old[s].ok;
        old[s].events = nullptr;
        break;
      }
    }
    g_slots[next++] = slot;
  }
  // Recycle buffers of calendars that disappeared, allocate the missing ones.
  for (int s = g_numIcs; s < oldCount; ++s) {
    if (!old[s].events) continue;
    bool reused = false;
    for (int k = g_numIcs; k < next && !reused; ++k) {
      if (!g_slots[k].events) {
        g_slots[k].events = old[s].events;
        reused = true;
      }
    }
    if (!reused) free(old[s].events);
  }
  for (int k = g_numIcs; k < next; ++k) {
    if (!g_slots[k].events) g_slots[k].events = allocEvents();
  }
  for (int k = next; k < oldCount; ++k) g_slots[k] = Slot();  // buffers handled above
  g_numSlots = next;
  if (matched == 0) {
    setError(g_discoveryError, sizeof(g_discoveryError), accountLabel(),
             n == 0 ? "aucun calendrier" : "calendriers choisis introuvables");
  }
  log_i("CalDAV: %d calendars found, %d shown", n, matched);
  publishCalendars();
  return true;
}

bool fetchCalDav(Slot& slot, int64_t ws, int64_t we, char* err, size_t errLen) {
  IcsParser* parser = newParser(ws, we);
  if (!parser) {
    setError(err, errLen, slot.name, "mémoire");
    return false;
  }
  HttpsCalDavTransport transport(cfg::kCalDavUser, cfg::kCalDavPassword);
  CalDavClient client(transport);
  const bool ok = client.fetchEvents(slot.url, ws, we, *parser);
  parser->finish();
  if (ok) {
    storeResults(slot, *parser);
    log_i("CalDAV %s: %d events (%d VEVENTs)", slot.name, slot.count, parser->eventsSeen());
  } else {
    setError(err, errLen, slot.name, client.error());
    if (client.lastStatus() == 404 || client.lastStatus() == 410) g_nextDiscovery = millis();  // calendar moved
  }
  deleteParser(parser);
  return ok;
}

// Streams one iCal feed through the parser.
bool fetchIcs(Slot& slot, int64_t ws, int64_t we, char* err, size_t errLen) {
  String url = cfg::kCalendars[slot.icsIndex].url;
  url.trim();
  if (url.startsWith("webcal://")) url = "https://" + url.substring(9);

  tls::Client secure;
  WiFiClient plain;
  const bool https = url.startsWith("https://");
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding: the body can be streamed as is
  http.setReuse(false);
  http.setTimeout(15000);
  http.setConnectTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("M5DeskStation/1.0");
  if (!(https ? http.begin(secure, url) : http.begin(plain, url))) {
    setError(err, errLen, slot.name, "URL invalide");
    return false;
  }
  const int code = http.GET();
  if (code != 200) {
    char what[32];
    if (code > 0) snprintf(what, sizeof(what), "HTTP %d", code);
    else snprintf(what, sizeof(what), "réseau (%d)", code);
    setError(err, errLen, slot.name, what);
    http.end();
    return false;
  }

  IcsParser* parser = newParser(ws, we);
  if (!parser) {
    setError(err, errLen, slot.name, "mémoire");
    http.end();
    return false;
  }
  WiFiClient* stream = http.getStreamPtr();
  int remaining = http.getSize();
  char buf[1024];
  uint32_t lastData = millis();
  while ((http.connected() || stream->available()) && (remaining > 0 || remaining == -1)) {
    const int avail = stream->available();
    if (avail > 0) {
      int n = stream->readBytes(buf, std::min<int>(avail, sizeof(buf)));
      if (remaining > 0) remaining -= n;
      parser->feed(buf, static_cast<size_t>(n));
      lastData = millis();
    } else {
      if (millis() - lastData > 20000) break;
      delay(2);
    }
  }
  http.end();
  parser->finish();

  bool ok = parser->sawCalendar() && remaining <= 0;
  if (!parser->sawCalendar()) setError(err, errLen, slot.name, "pas un fichier iCal");
  else if (!ok) setError(err, errLen, slot.name, "téléchargement incomplet");
  if (ok) {
    storeResults(slot, *parser);
    log_i("Calendar %s: %d events (%d VEVENTs)", slot.name, slot.count, parser->eventsSeen());
  }
  deleteParser(parser);
  return ok;
}

void refreshCalendars() {
  if (cfg::calDavConfigured() && (!g_discovered || static_cast<int32_t>(millis() - g_nextDiscovery) >= 0)) {
    const bool ok = discoverCalDav();
    g_discovered |= ok;
    g_nextDiscovery = millis() + (ok ? 6u * 3600u * 1000u : 120000u);
  }

  const int64_t now = time(nullptr);
  const int64_t today = g_tz.localDay(now);
  const int64_t ws = g_tz.dayStartUtc(today);
  const int64_t we = g_tz.dayStartUtc(today + 8);
  char err[64];
  memcpy(err, g_discoveryError, sizeof(err));
  for (int s = 0; s < g_numSlots; ++s) {
    Slot& slot = g_slots[s];
    if (!slot.events) continue;
    char e[64] = {0};
    slot.ok = slot.caldav ? fetchCalDav(slot, ws, we, e, sizeof(e)) : fetchIcs(slot, ws, we, e, sizeof(e));
    if (!slot.ok && !err[0]) memcpy(err, e, sizeof(err));
  }

  // Merge (keeping the last good copy of calendars that failed).
  static CalendarEvent* merged = nullptr;
  if (!merged) {
    merged = static_cast<CalendarEvent*>(
        heap_caps_malloc(sizeof(CalendarEvent) * kMaxEvents * kMaxCalendars, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!merged) return;
  }
  int n = 0;
  for (int s = 0; s < g_numSlots; ++s) {
    for (int i = 0; i < g_slots[s].count; ++i) {
      merged[n] = g_slots[s].events[i];
      merged[n].calendar = static_cast<uint8_t>(s);
      ++n;
    }
  }
  std::sort(merged, merged + n, [](const CalendarEvent& a, const CalendarEvent& b) {
    if (a.start != b.start) return a.start < b.start;
    return a.allDay > b.allDay;
  });
  if (n > kMaxEvents) n = kMaxEvents;

  Store::Guard g(g_store);
  CalendarState& cal = g_store.data().cal;
  cal.count = n;
  memcpy(cal.events, merged, sizeof(CalendarEvent) * n);
  cal.numCalendars = g_numSlots;
  for (int s = 0; s < g_numSlots; ++s) {
    cal.calendars[s].ok = g_slots[s].ok;
    cal.calendars[s].lastCount = static_cast<int16_t>(g_slots[s].count);
  }
  memcpy(cal.error, err, sizeof(cal.error));
  cal.updatedAt = now;
  ++cal.serial;
  g_store.touch();
}

bool calendarsDone() {
  if (g_discoveryError[0]) return false;
  for (int s = 0; s < g_numSlots; ++s) {
    if (!g_slots[s].ok) return false;
  }
  return true;
}

bool refreshWeather() {
  char url[400];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,apparent_temperature,weather_code,is_day"
           "&hourly=precipitation_probability,temperature_2m"
           "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset"
           "&timezone=auto&forecast_days=2&timeformat=unixtime",
           cfg::kLatitude, cfg::kLongitude);
  tls::Client client;
  String body;
  const int code = tls::get(client, url, body);
  if (code != 200) {
    log_w("Weather HTTP %d", code);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;

  const int64_t now = time(nullptr);
  Store::Guard g(g_store);
  WeatherState& w = g_store.data().weather;
  w.temp = doc["current"]["temperature_2m"] | 0.f;
  w.apparent = doc["current"]["apparent_temperature"] | 0.f;
  w.code = doc["current"]["weather_code"] | 0;
  w.isDay = (doc["current"]["is_day"] | 1) != 0;
  w.tmax = doc["daily"]["temperature_2m_max"][0] | 0.f;
  w.tmin = doc["daily"]["temperature_2m_min"][0] | 0.f;
  w.precipMax = doc["daily"]["precipitation_probability_max"][0] | 0;
  w.sunrise = doc["daily"]["sunrise"][0] | 0LL;
  w.sunset = doc["daily"]["sunset"][0] | 0LL;
  JsonArrayConst times = doc["hourly"]["time"];
  JsonArrayConst precip = doc["hourly"]["precipitation_probability"];
  JsonArrayConst temps = doc["hourly"]["temperature_2m"];
  size_t first = 0;
  while (first < times.size() && times[first].as<int64_t>() + 3600 <= now) ++first;
  w.hourStart = first < times.size() ? times[first].as<int64_t>() : 0;
  for (size_t i = 0; i < 12; ++i) {
    const size_t k = first + i;
    w.precip[i] = k < precip.size() ? static_cast<int8_t>(precip[k].as<int>()) : 0;
    w.hourTemp[i] = k < temps.size() ? static_cast<int8_t>(lroundf(temps[k].as<float>())) : 0;
  }
  strlcpy(w.place, cfg::kPlaceName, sizeof(w.place));
  w.updatedAt = now;
  w.valid = true;
  g_store.touch();
  return true;
}

void taskMain(void*) {
  uint32_t nextWeather = 0, nextCalendar = 0;
  int64_t lastDay = -1;
  const bool calendars = cfg::calendarsConfigured();
  bool online = false;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED || time(nullptr) < 1700000000) {
      online = false;
      delay(1000);
      continue;
    }
    if (!online) {
      online = true;
      delay(3000);  // spread the TLS handshakes that follow a (re)connection
    }
    const bool force = g_store.refreshRequested;
    if (force) g_store.refreshRequested = false;
    const int64_t day = g_tz.localDay(time(nullptr));
    const bool newDay = day != lastDay;
    lastDay = day;

    if (cfg::kWeatherEnabled && (force || static_cast<int32_t>(millis() - nextWeather) >= 0)) {
      const bool ok = refreshWeather();
      nextWeather = millis() + (ok ? cfg::kWeatherRefreshMin * 60000u : 60000u);
    }
    if (calendars && (force || newDay || static_cast<int32_t>(millis() - nextCalendar) >= 0)) {
      refreshCalendars();
      nextCalendar = millis() + (calendarsDone() ? cfg::kCalendarRefreshMin * 60000u : 120000u);
    }
    delay(250);
  }
}

}  // namespace

void startTask() {
  g_tz.parse(cfg::kTimezone);
  for (int i = 0; i < cfg::kNumCalendars && g_numSlots < kMaxCalendars; ++i) {
    const cfg::CalendarSource& src = cfg::kCalendars[i];
    if (!src.url || !src.url[0]) continue;
    Slot& slot = g_slots[g_numSlots++];
    slot.icsIndex = i;
    strlcpy(slot.name, src.name, sizeof(slot.name));
    slot.color = src.color;
    slot.events = allocEvents();
  }
  g_numIcs = g_numSlots;
  publishCalendars();
  {
    Store::Guard g(g_store);
    strlcpy(g_store.data().weather.place, cfg::kPlaceName, sizeof(g_store.data().weather.place));
  }
  if (!cfg::calendarsConfigured() && !cfg::kWeatherEnabled) return;
  // Priority 0, like core 0's idle task: FreeRTOS time-slices between them, so
  // long downloads, parsing and TLS handshakes can never starve the idle task
  // (whose watchdog would otherwise reboot the station).
  xTaskCreatePinnedToCore(taskMain, "feeds", 16384, nullptr, tskIDLE_PRIORITY, nullptr, 0);
}

}  // namespace feeds
