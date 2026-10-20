// Data shared between the network services (writers) and the UI (reader).
// Plain structs only: no Arduino dependency so the UI can be rendered on a PC.
#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr int kMaxEvents = 96;
constexpr int kMaxCalendars = 4;

enum class SpotifyStatus : uint8_t {
  Disabled,         // not configured
  Connecting,       // waiting for Wi-Fi / first token
  NeedsAuth,        // no refresh token yet: use the web portal
  AuthError,        // refresh token rejected
  PremiumRequired,  // 403 on player endpoints
  NoDevice,         // nothing active on Spotify Connect
  Ok,
  Error,            // transient network/API error
};

struct PlaybackState {
  SpotifyStatus status = SpotifyStatus::Connecting;
  bool hasItem = false;
  bool isPlaying = false;
  bool isEpisode = false;
  bool liked = false;
  bool likedKnown = false;
  bool shuffle = false;
  uint8_t repeat = 0;  // 0 off, 1 context, 2 track
  bool supportsVolume = false;
  int8_t volume = -1;
  char uri[48] = {0};
  char title[96] = {0};
  char artist[96] = {0};
  char album[80] = {0};
  char device[40] = {0};
  char artUrl[128] = {0};
  uint32_t progressMs = 0;
  uint32_t durationMs = 0;
  uint32_t sampledAtMs = 0;   // monotonic ms when progressMs was valid
  uint32_t trackSerial = 0;   // bumps when the item changes
  uint32_t lastUpdateMs = 0;  // last successful poll

  uint32_t positionAt(uint32_t nowMs) const {
    uint32_t p = progressMs;
    if (isPlaying) p += nowMs - sampledAtMs;
    return p > durationMs ? durationMs : p;
  }
};

struct CalendarEvent {
  int64_t start = 0;  // UTC seconds
  int64_t end = 0;
  bool allDay = false;
  uint8_t calendar = 0;
  uint32_t uid = 0;
  char title[72] = {0};
  char location[56] = {0};
  char url[96] = {0};
};

struct CalendarInfo {
  char name[24] = {0};
  uint32_t color = 0x5B8DEF;
  bool ok = false;
  int16_t lastCount = -1;
};

struct CalendarState {
  int count = 0;
  CalendarEvent events[kMaxEvents];  // sorted by start
  int numCalendars = 0;
  CalendarInfo calendars[kMaxCalendars];
  uint32_t serial = 0;
  int64_t updatedAt = 0;  // UTC, 0 = never
  char error[48] = {0};
};

struct WeatherState {
  bool valid = false;
  float temp = 0;
  float apparent = 0;
  int code = 0;  // WMO weather code
  bool isDay = true;
  float tmin = 0;
  float tmax = 0;
  int precipMax = 0;           // today's max precipitation probability (%)
  int64_t hourStart = 0;       // UTC of hourly[0]
  int8_t precip[12] = {0};     // next 12 hours precipitation probability
  int8_t hourTemp[12] = {0};   // next 12 hours temperature (°C, rounded)
  int64_t sunrise = 0;
  int64_t sunset = 0;
  int64_t updatedAt = 0;
  char place[24] = {0};
};

struct SystemState {
  bool wifi = false;
  int8_t rssi = 0;
  char ssid[33] = {0};
  char ip[16] = {0};
  bool timeValid = false;
  int battery = -1;  // %
  bool charging = false;
  bool usbPower = false;
  float batteryVolts = 0;
  uint32_t freeHeap = 0;
  uint32_t freePsram = 0;
  uint32_t uptimeS = 0;
  bool portalUp = false;
};

struct AppSnapshot {
  PlaybackState play;
  CalendarState cal;
  WeatherState weather;
  SystemState sys;
};

// Commands the UI sends to the Spotify worker.
enum class SpotifyCmd : uint8_t {
  PlayPause,
  Play,
  Pause,
  Next,
  Previous,
  Seek,       // arg = position ms
  Volume,     // arg = percent
  ToggleLike,
  Refresh,
};
