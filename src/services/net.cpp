#include "net.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_sntp.h>

#include "settings.h"
#include "spotify.h"
#include "store.h"

namespace net {

namespace {

WebServer g_server(80);
char g_state[17];  // anti-CSRF token for the Spotify authorization flow

void onTimeSync(struct timeval*) { g_store.rtcNeedsSync = true; }

const char* statusText(SpotifyStatus s) {
  switch (s) {
    case SpotifyStatus::Disabled: return "désactivé";
    case SpotifyStatus::Connecting: return "connexion…";
    case SpotifyStatus::NeedsAuth: return "à autoriser";
    case SpotifyStatus::AuthError: return "autorisation refusée ou expirée";
    case SpotifyStatus::PremiumRequired: return "compte Premium requis";
    case SpotifyStatus::NoDevice: return "connecté (aucun appareil actif)";
    case SpotifyStatus::Ok: return "connecté";
    case SpotifyStatus::Error: return "erreur réseau";
  }
  return "";
}

String htmlEscape(const char* s) {
  String o;
  for (; *s; ++s) {
    switch (*s) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += *s;
    }
  }
  return o;
}

const char kHead[] PROGMEM = R"html(<!doctype html><html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Desk Station</title>
<style>
:root{color-scheme:dark}body{margin:0;font:16px/1.5 -apple-system,system-ui,sans-serif;background:#0d0e12;color:#f2f2f5}
main{max-width:560px;margin:0 auto;padding:28px 20px}h1{font-size:22px;margin:0 0 4px}p.sub{color:#9aa0ad;margin:0 0 24px}
section{background:#181b22;border-radius:14px;padding:18px 18px 8px;margin:0 0 16px}h2{font-size:15px;margin:0 0 10px;color:#c9cdd6}
.row{display:flex;justify-content:space-between;gap:12px;padding:6px 0;border-top:1px solid #23262f}.row:first-of-type{border:0}
.k{color:#8b91a0}.v{text-align:right}ol{padding-left:20px;color:#c9cdd6}li{margin:6px 0}
a.btn,button{display:inline-block;background:#1ed760;color:#06140b;font-weight:600;border:0;border-radius:999px;padding:10px 18px;text-decoration:none;font-size:15px;cursor:pointer}
input{width:100%;box-sizing:border-box;background:#0d0e12;border:1px solid #2c303b;color:#f2f2f5;border-radius:10px;padding:10px;font-size:14px;margin:6px 0 12px}
code{background:#0d0e12;padding:1px 5px;border-radius:5px}
</style></head><body><main>)html";

void handleRoot() {
  // Copy only what the page needs: AppSnapshot is far too big for this stack.
  SpotifyStatus status;
  bool hasItem;
  char title[96], artist[96], ssid[33], calError[64];
  int8_t rssi;
  int calCount;
  {
    Store::Guard g(g_store);
    const AppSnapshot& d = g_store.data();
    status = d.play.status;
    hasItem = d.play.hasItem;
    memcpy(title, d.play.title, sizeof(title));
    memcpy(artist, d.play.artist, sizeof(artist));
    memcpy(ssid, d.sys.ssid, sizeof(ssid));
    memcpy(calError, d.cal.error, sizeof(calError));
    rssi = d.sys.rssi;
    calCount = d.cal.count;
  }
  String html = FPSTR(kHead);
  html += "<h1>Desk Station</h1><p class=sub>M5Stack Core2 · ";
  html += cfg::kVersion;
  html += "</p><section><h2>État</h2>";
  auto row = [&](const char* k, const String& v) {
    html += "<div class=row><span class=k>";
    html += k;
    html += "</span><span class=v>";
    html += v;
    html += "</span></div>";
  };
  row("Spotify", statusText(status));
  if (hasItem) row("Lecture", htmlEscape(title) + " — " + htmlEscape(artist));
  row("Agenda", calError[0] ? htmlEscape(calError) : String(calCount) + " événements");
  row("Wi-Fi", htmlEscape(ssid) + " (" + String(rssi) + " dBm)");
  html += "</section>";

  if (cfg::spotifyConfigured()) {
    char url[512];
    spotify::authorizeUrl(url, sizeof(url), g_state);
    html += "<section><h2>Connecter Spotify</h2><ol>";
    html += "<li>Ouvrez <a class=btn target=_blank rel=noopener href=\"";
    html += htmlEscape(url);
    html += "\">Autoriser Spotify</a> et acceptez.</li>";
    html += "<li>Le navigateur arrive ensuite sur <code>127.0.0.1:8888</code> et affiche une erreur : c'est "
            "normal. Copiez l'adresse complète de cette page.</li>";
    html += "<li>Collez-la ici :</li></ol>";
    html += "<form method=post action=/spotify><input name=url required placeholder=\"http://127.0.0.1:8888/"
            "callback?code=…\"><button type=submit>Valider</button></form>";
    html += "<p class=sub style=\"margin-top:12px\">Si vous lancez <code>tools/spotify_auth.py</code>, collez plutôt "
            "le jeton affiché dans <code>include/config.h</code>.</p></section>";
  } else {
    html += "<section><h2>Spotify</h2><p>Renseignez <code>kSpotifyClientId</code> et <code>kSpotifyClientSecret</code> "
            "dans <code>include/config.h</code>, puis reflashez.</p></section>";
  }
  html += "</main></body></html>";
  g_server.send(200, "text/html; charset=utf-8", html);
}

// Extracts a query parameter value from a pasted URL.
String param(const String& url, const char* key) {
  String k = String(key) + "=";
  int i = url.indexOf("?" + k);
  if (i < 0) i = url.indexOf("&" + k);
  if (i < 0) return url.startsWith(k) ? url.substring(k.length()) : String();
  i += k.length() + 1;
  int end = url.indexOf('&', i);
  return url.substring(i, end < 0 ? url.length() : end);
}

void handleSpotify() {
  String pasted = g_server.arg("url");
  pasted.trim();
  String code = param(pasted, "code");
  String state = param(pasted, "state");
  String message;
  bool ok = false;
  if (pasted.indexOf("error=") >= 0) {
    message = "Spotify a refusé l'autorisation (" + htmlEscape(param(pasted, "error").c_str()) + ").";
  } else if (!code.length()) {
    message = "Aucun code trouvé dans l'adresse collée.";
  } else if (state != g_state) {
    message = "Ce lien d'autorisation ne vient pas de cette page. Recommencez depuis le bouton « Autoriser Spotify ».";
  } else if (code.length() >= sizeof(g_store.pendingAuthCode)) {
    message = "Code trop long.";
  } else {
    strlcpy(g_store.pendingAuthCode, code.c_str(), sizeof(g_store.pendingAuthCode));
    g_store.authCodeReady = true;
    ok = true;
    message = "Code reçu ✓ La station finalise la connexion, regardez l'écran Musique.";
  }
  String html = FPSTR(kHead);
  html += "<h1>";
  html += ok ? "C'est presque fini" : "Oups";
  html += "</h1><section><p>";
  html += message;
  html += "</p><p><a href=/>Retour</a></p></section></main></body></html>";
  g_server.send(ok ? 200 : 400, "text/html; charset=utf-8", html);
}

void startServices() {
  configTzTime(cfg::kTimezone, cfg::kNtpServer, cfg::kNtpServer2);
  if (MDNS.begin(cfg::kHostname)) MDNS.addService("http", "tcp", 80);
  g_server.on("/", HTTP_GET, handleRoot);
  g_server.on("/spotify", HTTP_POST, handleSpotify);
  g_server.onNotFound([] { g_server.send(404, "text/plain", "Not found"); });
  g_server.begin();
  if (cfg::kOtaPassword[0]) {
    ArduinoOTA.setHostname(cfg::kHostname);
    ArduinoOTA.setPassword(cfg::kOtaPassword);
    ArduinoOTA.setMdnsEnabled(false);  // mDNS already running
    ArduinoOTA.begin();
  }
}

void updateSystemInfo() {
  SystemState s;
  {
    Store::Guard g(g_store);
    s = g_store.data().sys;
  }
  SystemState n = s;
  n.wifi = WiFi.status() == WL_CONNECTED;
  n.rssi = static_cast<int8_t>(n.wifi ? WiFi.RSSI() : 0);
  strlcpy(n.ssid, n.wifi ? WiFi.SSID().c_str() : "", sizeof(n.ssid));
  strlcpy(n.ip, n.wifi ? WiFi.localIP().toString().c_str() : "", sizeof(n.ip));
  n.timeValid = time(nullptr) > 1700000000;
  n.freeHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  n.freePsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  n.uptimeS = millis() / 1000;
  n.portalUp = n.wifi;
  // Only signal a change (and a redraw) when something visible moved.
  const bool changed = n.wifi != s.wifi || n.timeValid != s.timeValid || strcmp(n.ip, s.ip) != 0 ||
                       strcmp(n.ssid, s.ssid) != 0 || abs(n.rssi - s.rssi) >= 6;
  Store::Guard g(g_store);
  SystemState& d = g_store.data().sys;
  // Battery fields belong to the UI thread (I2C); keep them.
  n.battery = d.battery;
  n.charging = d.charging;
  n.usbPower = d.usbPower;
  n.batteryVolts = d.batteryVolts;
  if (!changed) n.rssi = d.rssi;
  d = n;
  if (changed) g_store.touch();
}

void taskMain(void*) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg::kHostname);
  WiFi.setAutoReconnect(true);
  WiFiMulti multi;
  for (const auto& n : cfg::kWifi) {
    if (n.ssid && n.ssid[0]) multi.addAP(n.ssid, n.password);
  }
  sntp_set_time_sync_notification_cb(onTimeSync);

  bool started = false;
  uint32_t nextInfo = 0;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      multi.run(15000);
      nextInfo = 0;
    }
    if (WiFi.status() == WL_CONNECTED && !started) {
      startServices();
      started = true;
      log_i("Wi-Fi up: %s", WiFi.localIP().toString().c_str());
    }
    if (static_cast<int32_t>(millis() - nextInfo) >= 0) {
      updateSystemInfo();
      nextInfo = millis() + 5000;
    }
    if (started) {
      g_server.handleClient();
      if (cfg::kOtaPassword[0]) ArduinoOTA.handle();
    }
    delay(WiFi.status() == WL_CONNECTED ? 10 : 1000);
  }
}

}  // namespace

void startTask() {
  snprintf(g_state, sizeof(g_state), "%08lx%08lx", static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  xTaskCreatePinnedToCore(taskMain, "net", 8192, nullptr, 2, nullptr, 0);
}

}  // namespace net
