// Pulls in the user's include/config.h.
#pragma once

#include <stdint.h>

namespace cfg {

// Settings introduced after the first release. A config.h copied from an older
// config.example.h does not define them and keeps compiling with these values;
// a definition in config.h takes precedence (qualified lookup prefers members
// declared directly in `cfg` over those brought in by the using-directive).
namespace defaults {
constexpr const char* kCalDavUrl = "https://caldav.icloud.com/";
constexpr const char* kCalDavUser = "";
constexpr const char* kCalDavPassword = "";
constexpr const char* kCalDavCalendars = "";
}  // namespace defaults
using namespace defaults;

}  // namespace cfg

// Explicit path: a bare "config.h" could resolve to an unrelated header of the
// framework when the user's file is missing.
#if __has_include("../../include/config.h")
#include "../../include/config.h"
#else
#error "Fichier include/config.h manquant : copiez include/config.example.h en include/config.h puis complétez-le (Wi-Fi, Spotify, calendriers)."
#endif

namespace cfg {

constexpr const char* kVersion = "v1.0.2";

constexpr int kNumCalendars = static_cast<int>(sizeof(cfg::kCalendars) / sizeof(cfg::kCalendars[0]));
constexpr int kNumWifi = static_cast<int>(sizeof(cfg::kWifi) / sizeof(cfg::kWifi[0]));

inline bool spotifyConfigured() {
  return cfg::kSpotifyEnabled && cfg::kSpotifyClientId[0] && cfg::kSpotifyClientSecret[0];
}

inline bool calDavConfigured() { return cfg::kCalDavUrl[0] && cfg::kCalDavUser[0] && cfg::kCalDavPassword[0]; }

inline bool icsConfigured() {
  for (const auto& c : cfg::kCalendars) {
    if (c.url && c.url[0]) return true;
  }
  return false;
}

inline bool calendarsConfigured() { return calDavConfigured() || icsConfigured(); }

}  // namespace cfg
