#include "feeds.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <new>

#include "../core/ics.h"
#include "settings.h"
#include "store.h"
#include "tls.h"

namespace feeds {

namespace {

PosixTz g_tz;

struct CalendarCache {
  CalendarEvent* events = nullptr;
  int count = 0;
  bool ok = false;
};

CalendarCache g_cache[kMaxCalendars];
int g_active[kMaxCalendars];  // index into cfg::kCalendars
int g_numActive = 0;

void setError(char* err, size_t n, const char* name, const char* what) { snprintf(err, n, "%s : %s", name, what); }

// Streams one iCal feed through the parser. Returns false on failure.
bool fetchCalendar(int slot, int64_t ws, int64_t we, char* err, size_t errLen) {
  const cfg::CalendarSource& src = cfg::kCalendars[g_active[slot]];
  String url = src.url;
  url.trim();
  if (url.startsWith("webcal://")) url = "https://" + url.substring(9);

  WiFiClientSecure secure;
  WiFiClient plain;
  tls::setup(secure);
  const bool https = url.startsWith("https://");
  HTTPClient http;
  http.useHTTP10(true);  // no chunked encoding: the body can be streamed as is
  http.setReuse(false);
  http.setTimeout(15000);
  http.setConnectTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("M5DeskStation/1.0");
  if (!(https ? http.begin(secure, url) : http.begin(plain, url))) {
    setError(err, errLen, src.name, "URL invalide");
    return false;
  }
  const int code = http.GET();
  if (code != 200) {
    char what[32];
    if (code > 0) snprintf(what, sizeof(what), "HTTP %d", code);
    else snprintf(what, sizeof(what), "réseau (%d)", code);
    setError(err, errLen, src.name, what);
    http.end();
    return false;
  }

  void* mem = heap_caps_malloc(sizeof(IcsParser), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!mem) {
    setError(err, errLen, src.name, "mémoire");
    http.end();
    return false;
  }
  IcsParser* parser = new (mem) IcsParser(g_tz, ws, we);

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
  if (!parser->sawCalendar()) setError(err, errLen, src.name, "pas un fichier iCal");
  else if (!ok) setError(err, errLen, src.name, "téléchargement incomplet");
  if (ok) {
    CalendarCache& cache = g_cache[slot];
    cache.count = 0;
    for (int i = 0; i < parser->count() && cache.count < kMaxEvents; ++i) {
      const IcsOccurrence& o = parser->at(i);
      CalendarEvent& e = cache.events[cache.count++];
      e.start = o.start;
      e.end = o.end;
      e.allDay = o.allDay;
      e.calendar = static_cast<uint8_t>(slot);
      e.uid = o.uid;
      memcpy(e.title, o.title, sizeof(e.title));
      memcpy(e.location, o.location, sizeof(e.location));
      memcpy(e.url, o.url, sizeof(e.url));
    }
    log_i("Calendar %s: %d events (%d VEVENTs)", src.name, cache.count, parser->eventsSeen());
  }
  parser->~IcsParser();
  free(mem);
  return ok;
}

void refreshCalendars() {
  const int64_t now = time(nullptr);
  const int64_t today = g_tz.localDay(now);
  const int64_t ws = g_tz.dayStartUtc(today);
  const int64_t we = g_tz.dayStartUtc(today + 8);
  char err[48] = {0};
  for (int s = 0; s < g_numActive; ++s) {
    char e[48] = {0};
    g_cache[s].ok = fetchCalendar(s, ws, we, e, sizeof(e));
    if (!g_cache[s].ok && !err[0]) memcpy(err, e, sizeof(err));
  }

  // Merge (keeping the last good copy of calendars that failed).
  static CalendarEvent* merged = nullptr;
  if (!merged) {
    merged = static_cast<CalendarEvent*>(
        heap_caps_malloc(sizeof(CalendarEvent) * kMaxEvents * kMaxCalendars, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!merged) return;
  }
  int n = 0;
  for (int s = 0; s < g_numActive; ++s) {
    for (int i = 0; i < g_cache[s].count; ++i) {
      const CalendarEvent& e = g_cache[s].events[i];
      if (e.end < now - 86400 * 2 && e.end != e.start) continue;
      merged[n++] = e;
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
  for (int s = 0; s < g_numActive; ++s) {
    cal.calendars[s].ok = g_cache[s].ok;
    cal.calendars[s].lastCount = static_cast<int16_t>(g_cache[s].count);
  }
  memcpy(cal.error, err, sizeof(cal.error));
  cal.updatedAt = now;
  ++cal.serial;
  g_store.touch();
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
  WiFiClientSecure client;
  tls::setup(client);
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
  for (;;) {
    if (WiFi.status() != WL_CONNECTED || time(nullptr) < 1700000000) {
      delay(1000);
      continue;
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
    if (g_numActive > 0 && (force || newDay || static_cast<int32_t>(millis() - nextCalendar) >= 0)) {
      refreshCalendars();
      bool allOk = true;
      for (int s = 0; s < g_numActive; ++s) allOk &= g_cache[s].ok;
      nextCalendar = millis() + (allOk ? cfg::kCalendarRefreshMin * 60000u : 120000u);
    }
    delay(250);
  }
}

}  // namespace

void startTask() {
  g_tz.parse(cfg::kTimezone);
  {
    Store::Guard g(g_store);
    CalendarState& cal = g_store.data().cal;
    for (int i = 0; i < cfg::kNumCalendars && g_numActive < kMaxCalendars; ++i) {
      const cfg::CalendarSource& src = cfg::kCalendars[i];
      if (!src.url || !src.url[0]) continue;
      const int s = g_numActive++;
      g_active[s] = i;
      g_cache[s].events = static_cast<CalendarEvent*>(
          heap_caps_malloc(sizeof(CalendarEvent) * kMaxEvents, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      strlcpy(cal.calendars[s].name, src.name, sizeof(cal.calendars[s].name));
      cal.calendars[s].color = src.color;
    }
    cal.numCalendars = g_numActive;
    strlcpy(g_store.data().weather.place, cfg::kPlaceName, sizeof(g_store.data().weather.place));
  }
  if (g_numActive == 0 && !cfg::kWeatherEnabled) return;
  xTaskCreatePinnedToCore(taskMain, "sync", 16384, nullptr, 1, nullptr, 0);
}

}  // namespace feeds
