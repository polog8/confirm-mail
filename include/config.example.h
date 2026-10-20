// =============================================================================
//  M5Stack Core2 desk station — configuration
//  Copiez ce fichier en include/config.h puis complétez-le.
//  include/config.h est ignoré par git : vos mots de passe restent locaux.
// =============================================================================
#pragma once

#include <stdint.h>

namespace cfg {

// ---------------------------------------------------------------- Wi-Fi ----
// Réseaux 2,4 GHz essayés dans l'ordre.
struct WifiNetwork {
  const char* ssid;
  const char* password;
};
constexpr WifiNetwork kWifi[] = {
    {"MonWiFi", "motdepasse"},
    // {"Partage iPhone", "motdepasse"},
};

// ---------------------------------------------------------------- Heure ----
// Fuseau au format POSIX. Paris : "CET-1CEST,M3.5.0,M10.5.0/3"
constexpr const char* kTimezone = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr const char* kNtpServer = "pool.ntp.org";
constexpr const char* kNtpServer2 = "time.apple.com";

// -------------------------------------------------------------- Spotify ----
// 1. https://developer.spotify.com/dashboard -> Create app (Web API)
// 2. Redirect URI : http://127.0.0.1:8888/callback
// 3. Copiez Client ID / Client Secret ci-dessous.
// 4. Jeton : laissez vide et utilisez la page web de la station (QR code à
//    l'écran), ou lancez tools/spotify_auth.py et collez le résultat ici.
constexpr bool kSpotifyEnabled = true;
constexpr const char* kSpotifyClientId = "";
constexpr const char* kSpotifyClientSecret = "";
constexpr const char* kSpotifyRefreshToken = "";
// Appareil sur lequel relancer la lecture quand rien n'est actif
// (morceau de son nom, ex. "MacBook" ; vide = le premier disponible).
constexpr const char* kSpotifyPreferredDevice = "";

// ---------------------------------------------------- iCloud (CalDAV) -----
// Recommandé pour le calendrier Apple : lecture directe de vos calendriers,
// sans les rendre publics. Il faut un « mot de passe pour app » (pas votre
// mot de passe Apple) : https://account.apple.com > Connexion et sécurité >
// Mots de passe pour app > « + ». Révocable à tout moment.
// Fonctionne aussi avec d'autres serveurs CalDAV (changez l'URL) :
//   Fastmail  https://caldav.fastmail.com/
//   Nextcloud https://mon-cloud.fr/remote.php/dav/
constexpr const char* kCalDavUrl = "https://caldav.icloud.com/";
constexpr const char* kCalDavUser = "";      // e-mail du compte Apple
constexpr const char* kCalDavPassword = "";  // xxxx-xxxx-xxxx-xxxx
// Calendriers à afficher, séparés par des virgules, ex. "Maison, Travail".
// Vide = tous les calendriers d'événements du compte (les couleurs d'iCloud
// sont reprises).
constexpr const char* kCalDavCalendars = "";

// ------------------------------------------------- Autres calendriers ------
// Liens iCal (.ics / webcal://) : Google (« Adresse secrète au format iCal »),
// Outlook (« Publier un calendrier »), calendrier iCloud public…
// Laisser l'URL vide désactive l'entrée. 8 calendriers au total (CalDAV + iCal).
struct CalendarSource {
  const char* name;
  const char* url;
  uint32_t color;  // 0xRRGGBB
};
constexpr CalendarSource kCalendars[] = {
    {"Google", "", 0xF59E0B},
};
constexpr int kCalendarRefreshMin = 10;
constexpr int kMeetingAlertMin = 5;  // rappel N minutes avant (0 = jamais)

// ---------------------------------------------------------------- Météo ----
// Open-Meteo, sans clé d'API.
constexpr bool kWeatherEnabled = true;
constexpr float kLatitude = 48.8566f;
constexpr float kLongitude = 2.3522f;
constexpr const char* kPlaceName = "Paris";
constexpr int kWeatherRefreshMin = 20;

// ---------------------------------------------------------------- Focus ----
constexpr int kFocusMin = 25;
constexpr int kShortBreakMin = 5;
constexpr int kLongBreakMin = 15;
constexpr int kSessionsBeforeLongBreak = 4;
// Lance la musique au début d'une session et la met en pause pendant les pauses.
constexpr bool kFocusControlsMusic = false;

// ------------------------------------------------------- Écran et sons -----
constexpr int kBrightness = 70;         // % (modifiable depuis l'écran Système)
constexpr int kDimBrightness = 12;      // % après inactivité
constexpr int kDimAfterSec = 120;
constexpr int kNightStartHour = 22;     // la nuit : luminosité réduite
constexpr int kNightEndHour = 7;
constexpr int kNightOffAfterSec = 60;   // écran éteint la nuit après N s (0 = jamais)
constexpr int kAutoReturnSec = 45;      // retour à l'écran le plus utile (0 = jamais)
constexpr bool kSound = true;
constexpr bool kHaptics = true;

// ------------------------------------------------------------- Réseau ------
constexpr const char* kHostname = "deskstation";  // http://deskstation.local/
// Mot de passe des mises à jour Wi-Fi (OTA). Vide = OTA désactivé.
constexpr const char* kOtaPassword = "";

}  // namespace cfg
