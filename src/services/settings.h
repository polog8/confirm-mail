// Pulls in the user's include/config.h.
#pragma once

#if __has_include("config.h")
#include "config.h"
#else
#error "include/config.h manquant : copiez include/config.example.h en include/config.h et complétez-le."
#endif

namespace cfg {

constexpr const char* kVersion = "v1.0.1";

constexpr int kNumCalendars = static_cast<int>(sizeof(kCalendars) / sizeof(kCalendars[0]));
constexpr int kNumWifi = static_cast<int>(sizeof(kWifi) / sizeof(kWifi[0]));

inline bool spotifyConfigured() { return kSpotifyEnabled && kSpotifyClientId[0] && kSpotifyClientSecret[0]; }

inline bool calDavConfigured() { return kCalDavUrl[0] && kCalDavUser[0] && kCalDavPassword[0]; }

inline bool icsConfigured() {
  for (const auto& c : kCalendars) {
    if (c.url && c.url[0]) return true;
  }
  return false;
}

inline bool calendarsConfigured() { return calDavConfigured() || icsConfigured(); }

}  // namespace cfg
