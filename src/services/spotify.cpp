#include "spotify.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include "settings.h"
#include "store.h"
#include "tls.h"

namespace spotify {

namespace {

constexpr const char* kApi = "https://api.spotify.com/v1";
constexpr const char* kTokenUrl = "https://accounts.spotify.com/api/token";
constexpr const char* kScopes =
    "user-read-playback-state user-modify-playback-state user-read-currently-playing "
    "user-library-read user-library-modify";

String urlEncode(const char* s) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  for (; *s; ++s) {
    const unsigned char c = static_cast<unsigned char>(*s);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

void copyStr(char* dst, size_t n, const char* src) {
  if (!src) src = "";
  strlcpy(dst, src, n);
  // Do not leave a truncated UTF-8 sequence at the end.
  size_t len = strlen(dst);
  if (len == n - 1) {
    size_t i = len;
    while (i > 0 && (static_cast<unsigned char>(dst[i - 1]) & 0xC0) == 0x80) --i;
    if (i > 0 && (static_cast<unsigned char>(dst[i - 1]) & 0xC0) == 0xC0) {
      size_t need = (static_cast<unsigned char>(dst[i - 1]) >= 0xF0)   ? 4
                    : (static_cast<unsigned char>(dst[i - 1]) >= 0xE0) ? 3
                                                                         : 2;
      if (len - (i - 1) < need) dst[i - 1] = '\0';
    }
  }
}

class Worker {
 public:
  void run();

 private:
  bool ensureToken();
  bool tokenRequest(const String& body);
  int call(const char* method, const String& path, const char* body, String* out, bool retry = true);
  void poll();
  void execute(const SpotifyRequest& r);
  bool startOnPreferredDevice();
  void fetchLiked(const String& uri);
  void setLiked(const String& uri, bool like);
  void downloadArt(const String& url);
  void setStatus(SpotifyStatus s);
  uint32_t pollInterval();

  tls::Client apiTls_;
  tls::Client authTls_;
  tls::Client imgTls_;
  HTTPClient api_;
  Preferences prefs_;
  String access_;
  String refresh_;
  bool refreshFromNvs_ = false;
  uint32_t tokenExpiry_ = 0;
  uint32_t nextPoll_ = 0;
  uint32_t backoffUntil_ = 0;
  uint32_t errorDelay_ = 2000;
  String uri_;
  String artUrl_;
  bool likedFetched_ = false;
};

void Worker::setStatus(SpotifyStatus s) {
  Store::Guard g(g_store);
  if (g_store.data().play.status != s) {
    g_store.data().play.status = s;
    g_store.touch();
  }
}

bool Worker::tokenRequest(const String& body) {
  HTTPClient http;
  http.setTimeout(10000);
  http.setReuse(false);
  if (!http.begin(authTls_, kTokenUrl)) return false;
  http.setAuthorization(cfg::kSpotifyClientId, cfg::kSpotifyClientSecret);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  const int code = http.POST(body);
  const String resp = code > 0 ? http.getString() : String();
  http.end();

  if (code == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, resp)) return false;
    access_ = doc["access_token"].as<const char*>();
    const uint32_t expires = doc["expires_in"] | 3600;
    tokenExpiry_ = millis() + (expires > 120 ? expires - 60 : expires) * 1000;
    const char* newRefresh = doc["refresh_token"] | "";
    if (newRefresh[0] && refresh_ != newRefresh) {
      refresh_ = newRefresh;
      prefs_.putString("refresh", refresh_);
      refreshFromNvs_ = true;
    }
    log_i("Spotify token OK");
    return access_.length() > 0;
  }
  log_w("Spotify token request failed: %d %s", code, resp.c_str());
  if (code == 400 || code == 401) {
    // Stored token revoked: fall back to the one from config.h, if different.
    if (refreshFromNvs_ && cfg::kSpotifyRefreshToken[0] && refresh_ != cfg::kSpotifyRefreshToken) {
      prefs_.remove("refresh");
      refresh_ = cfg::kSpotifyRefreshToken;
      refreshFromNvs_ = false;
      return false;
    }
    setStatus(SpotifyStatus::AuthError);
    refresh_ = "";
    backoffUntil_ = millis() + 60000;
  } else {
    setStatus(SpotifyStatus::Error);
    backoffUntil_ = millis() + errorDelay_;
    errorDelay_ = min<uint32_t>(errorDelay_ * 2, 60000);
  }
  return false;
}

bool Worker::ensureToken() {
  if (access_.length() && static_cast<int32_t>(tokenExpiry_ - millis()) > 0) return true;
  if (!refresh_.length()) return false;
  return tokenRequest("grant_type=refresh_token&refresh_token=" + urlEncode(refresh_.c_str()));
}

int Worker::call(const char* method, const String& path, const char* body, String* out, bool retry) {
  if (!ensureToken()) return -1;
  const String url = path.startsWith("https://") ? path : String(kApi) + path;
  api_.setReuse(true);
  api_.setTimeout(8000);
  api_.setConnectTimeout(8000);
  if (!api_.begin(apiTls_, url)) return -1;
  static const char* kHeaders[] = {"Retry-After"};
  api_.collectHeaders(kHeaders, 1);
  api_.addHeader("Authorization", "Bearer " + access_);
  int code;
  if (strcmp(method, "GET") == 0) {
    code = api_.GET();
  } else if (body) {
    api_.addHeader("Content-Type", "application/json");
    code = api_.sendRequest(method, reinterpret_cast<uint8_t*>(const_cast<char*>(body)), strlen(body));
  } else {
    api_.addHeader("Content-Length", "0");
    code = api_.sendRequest(method, static_cast<uint8_t*>(nullptr), 0);
  }
  String resp;
  // 204 has no body; reading it on a keep-alive connection would block.
  if (code > 0 && code != 204) resp = api_.getString();
  if (code == 429) {
    long wait = api_.header("Retry-After").toInt();
    backoffUntil_ = millis() + static_cast<uint32_t>(wait > 0 ? wait : 2) * 1000;
  }
  api_.end();
  if (code <= 0) {
    // Transport failure: drop the connection so the next call reconnects.
    apiTls_.stop();
  }
  if (code == 401 && retry) {
    access_ = "";
    return call(method, path, body, out, false);
  }
  if (code == 403 && resp.indexOf("PREMIUM_REQUIRED") >= 0) setStatus(SpotifyStatus::PremiumRequired);
  if (out) *out = resp;
  return code;
}

uint32_t Worker::pollInterval() {
  bool playing;
  {
    Store::Guard g(g_store);
    playing = g_store.data().play.isPlaying;
  }
  if (g_store.musicVisible) return 1000;
  return playing ? 3000 : 8000;
}

void Worker::poll() {
  String body;
  const uint32_t started = millis();
  const int code = call("GET", "/me/player?additional_types=episode", nullptr, &body);
  if (code <= 0 || code >= 500) {
    setStatus(SpotifyStatus::Error);
    nextPoll_ = millis() + errorDelay_;
    errorDelay_ = min<uint32_t>(errorDelay_ * 2, 30000);
    return;
  }
  errorDelay_ = 2000;
  if (code == 204 || body.length() == 0) {
    Store::Guard g(g_store);
    PlaybackState& p = g_store.data().play;
    p.status = SpotifyStatus::NoDevice;
    p.isPlaying = false;
    p.lastUpdateMs = millis();
    g_store.touch();
    return;
  }
  if (code != 200) {
    if (code != 429) setStatus(SpotifyStatus::Error);
    return;
  }

  JsonDocument filter;
  filter["is_playing"] = true;
  filter["progress_ms"] = true;
  filter["shuffle_state"] = true;
  filter["repeat_state"] = true;
  filter["currently_playing_type"] = true;
  filter["device"]["name"] = true;
  filter["device"]["volume_percent"] = true;
  filter["device"]["supports_volume"] = true;
  JsonObject item = filter["item"].to<JsonObject>();
  item["uri"] = true;
  item["name"] = true;
  item["duration_ms"] = true;
  item["artists"][0]["name"] = true;
  item["album"]["name"] = true;
  item["album"]["images"][0]["url"] = true;
  item["album"]["images"][0]["width"] = true;
  item["show"]["name"] = true;
  item["images"][0]["url"] = true;
  item["images"][0]["width"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    setStatus(SpotifyStatus::Error);
    return;
  }

  JsonObjectConst it = doc["item"];
  const bool hasItem = !it.isNull();
  const bool episode = strcmp(doc["currently_playing_type"] | "", "episode") == 0;
  String newUri = hasItem ? String(it["uri"] | "") : String();

  // Pick the smallest cover that is still >= 150 px wide.
  JsonArrayConst images = episode ? it["images"].as<JsonArrayConst>() : it["album"]["images"].as<JsonArrayConst>();
  if (episode && images.size() == 0) images = it["show"]["images"].as<JsonArrayConst>();
  String bestUrl;
  int bestW = 1 << 30;
  for (JsonObjectConst img : images) {
    int w = img["width"] | 0;
    if (w >= 150 && w < bestW) {
      bestW = w;
      bestUrl = img["url"] | "";
    }
  }
  if (!bestUrl.length() && images.size() > 0) bestUrl = images[0]["url"] | "";

  bool trackChanged = false;
  {
    Store::Guard g(g_store);
    PlaybackState& p = g_store.data().play;
    const bool hold = static_cast<int32_t>(g_store.optimisticUntilMs - started) > 0;
    p.status = SpotifyStatus::Ok;
    p.hasItem = hasItem;
    p.isEpisode = episode;
    if (!hold) {
      p.isPlaying = doc["is_playing"] | false;
      p.progressMs = doc["progress_ms"] | 0;
      p.sampledAtMs = started + (millis() - started) / 2;  // mid-request estimate
      if (!doc["device"]["volume_percent"].isNull()) p.volume = static_cast<int8_t>(doc["device"]["volume_percent"].as<int>());
    }
    p.shuffle = doc["shuffle_state"] | false;
    const char* rep = doc["repeat_state"] | "off";
    p.repeat = strcmp(rep, "track") == 0 ? 2 : strcmp(rep, "context") == 0 ? 1 : 0;
    p.supportsVolume = doc["device"]["supports_volume"] | false;
    copyStr(p.device, sizeof(p.device), doc["device"]["name"] | "");
    if (hasItem) {
      copyStr(p.title, sizeof(p.title), it["name"] | "");
      p.durationMs = it["duration_ms"] | 0;
      String artists;
      if (episode) {
        artists = it["show"]["name"] | "";
      } else {
        for (JsonObjectConst a : it["artists"].as<JsonArrayConst>()) {
          if (artists.length()) artists += ", ";
          artists += a["name"] | "";
        }
      }
      copyStr(p.artist, sizeof(p.artist), artists.c_str());
      copyStr(p.album, sizeof(p.album), episode ? "Podcast" : (it["album"]["name"] | ""));
      copyStr(p.artUrl, sizeof(p.artUrl), bestUrl.c_str());
    }
    if (newUri != uri_) {
      trackChanged = true;
      copyStr(p.uri, sizeof(p.uri), newUri.c_str());
      ++p.trackSerial;
      p.likedKnown = false;
      p.liked = false;
    }
    p.lastUpdateMs = millis();
    g_store.touch();
  }
  if (trackChanged) {
    uri_ = newUri;
    likedFetched_ = false;
  }
  if (bestUrl.length() && bestUrl != artUrl_) downloadArt(bestUrl);
  if (!bestUrl.length() && artUrl_.length()) {
    artUrl_ = "";
    g_store.setArt(nullptr, 0);
  }
  if (!likedFetched_ && uri_.startsWith("spotify:track:")) fetchLiked(uri_);
}

void Worker::fetchLiked(const String& uri) {
  likedFetched_ = true;
  String body;
  int code = call("GET", "/me/library/contains?uris=" + urlEncode(uri.c_str()), nullptr, &body);
  if (code == 404 || code == 403) {
    // Older endpoint, still served to some apps.
    code = call("GET", "/me/tracks/contains?ids=" + uri.substring(14), nullptr, &body);
  }
  if (code != 200) return;
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc.is<JsonArray>()) return;
  Store::Guard g(g_store);
  PlaybackState& p = g_store.data().play;
  if (uri != p.uri) return;
  p.liked = doc[0] | false;
  p.likedKnown = true;
  g_store.touch();
}

void Worker::setLiked(const String& uri, bool like) {
  int code = 0;
  if (uri.startsWith("spotify:track:") || uri.startsWith("spotify:episode:")) {
    const char* method = like ? "PUT" : "DELETE";
    code = call(method, "/me/library?uris=" + urlEncode(uri.c_str()), nullptr, nullptr);
    if ((code == 404 || code == 403) && uri.startsWith("spotify:track:")) {
      code = call(method, "/me/tracks?ids=" + uri.substring(14), nullptr, nullptr);
    }
  }
  if (code < 200 || code >= 300) {
    // Revert the optimistic toggle.
    Store::Guard g(g_store);
    g_store.data().play.liked = !like;
    g_store.touch();
  }
}

bool Worker::startOnPreferredDevice() {
  String body;
  if (call("GET", "/me/player/devices", nullptr, &body) != 200) return false;
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  String id;
  for (JsonObjectConst d : doc["devices"].as<JsonArrayConst>()) {
    const char* name = d["name"] | "";
    if (d["is_restricted"] | false) continue;
    if (cfg::kSpotifyPreferredDevice[0] && strcasestr(name, cfg::kSpotifyPreferredDevice)) {
      id = d["id"] | "";
      break;
    }
    if (!id.length()) id = d["id"] | "";
  }
  if (!id.length()) return false;
  String req = "{\"device_ids\":[\"" + id + "\"],\"play\":true}";
  int code = call("PUT", "/me/player", req.c_str(), nullptr);
  return code >= 200 && code < 300;
}

void Worker::execute(const SpotifyRequest& r) {
  int code = 0;
  switch (r.cmd) {
    case SpotifyCmd::Play:
      code = call("PUT", "/me/player/play", nullptr, nullptr);
      if (code == 404) startOnPreferredDevice();
      break;
    case SpotifyCmd::Pause:
      code = call("PUT", "/me/player/pause", nullptr, nullptr);
      break;
    case SpotifyCmd::PlayPause: {
      bool playing;
      {
        Store::Guard g(g_store);
        playing = g_store.data().play.isPlaying;
      }
      code = call("PUT", playing ? "/me/player/pause" : "/me/player/play", nullptr, nullptr);
      if (!playing && code == 404) startOnPreferredDevice();
      break;
    }
    case SpotifyCmd::Next:
      code = call("POST", "/me/player/next", nullptr, nullptr);
      break;
    case SpotifyCmd::Previous:
      code = call("POST", "/me/player/previous", nullptr, nullptr);
      break;
    case SpotifyCmd::Seek:
      code = call("PUT", "/me/player/seek?position_ms=" + String(r.arg), nullptr, nullptr);
      break;
    case SpotifyCmd::Volume:
      code = call("PUT", "/me/player/volume?volume_percent=" + String(r.arg), nullptr, nullptr);
      break;
    case SpotifyCmd::ToggleLike:
      setLiked(uri_, r.arg != 0);
      break;
    case SpotifyCmd::Refresh:
      break;
  }
  if (code >= 400) {
    log_w("Spotify command %d failed: %d", static_cast<int>(r.cmd), code);
    g_store.optimisticUntilMs = millis();  // let the next poll restore the truth
  }
  // Spotify needs a moment to reflect the change.
  nextPoll_ = millis() + 350;
}

void Worker::downloadArt(const String& url) {
  artUrl_ = url;
  HTTPClient http;
  http.setTimeout(10000);
  http.setReuse(false);
  if (!http.begin(imgTls_, url)) return;
  const int code = http.GET();
  const int len = http.getSize();
  if (code != 200 || len <= 0 || len > 400 * 1024) {
    http.end();
    return;
  }
  uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    http.end();
    return;
  }
  WiFiClient* stream = http.getStreamPtr();
  int got = 0;
  const uint32_t deadline = millis() + 10000;
  while (got < len && http.connected() && static_cast<int32_t>(deadline - millis()) > 0) {
    const int avail = stream->available();
    if (avail > 0) {
      got += stream->readBytes(buf + got, min(avail, len - got));
    } else {
      delay(5);
    }
  }
  http.end();
  if (got == len) {
    g_store.setArt(buf, len);
  } else {
    free(buf);
    artUrl_ = "";  // retry on the next poll
  }
}

void Worker::run() {
  prefs_.begin("spotify", false);
  refresh_ = prefs_.isKey("refresh") ? prefs_.getString("refresh", "") : String();
  refreshFromNvs_ = refresh_.length() > 0;
  if (!refresh_.length()) refresh_ = cfg::kSpotifyRefreshToken;
  setStatus(refresh_.length() ? SpotifyStatus::Connecting : SpotifyStatus::NeedsAuth);

  for (;;) {
    // Authorization code pasted in the web portal.
    if (g_store.authCodeReady) {
      String code = g_store.pendingAuthCode;
      g_store.authCodeReady = false;
      String saved = refresh_;
      refresh_ = "";
      access_ = "";
      setStatus(SpotifyStatus::Connecting);
      if (tokenRequest("grant_type=authorization_code&code=" + urlEncode(code.c_str()) +
                       "&redirect_uri=" + urlEncode(kRedirectUri))) {
        backoffUntil_ = 0;
        nextPoll_ = 0;
      } else if (saved.length()) {
        refresh_ = saved;
      }
      if (!refresh_.length()) setStatus(SpotifyStatus::NeedsAuth);
    }

    if (WiFi.status() != WL_CONNECTED || time(nullptr) < 1700000000) {
      delay(500);
      continue;
    }
    if (!refresh_.length()) {
      SpotifyRequest dropped;
      g_store.popCommand(&dropped, 500);  // keep the queue drained
      continue;
    }

    // Wait for a command until the next poll is due.
    int32_t wait = static_cast<int32_t>(nextPoll_ - millis());
    if (static_cast<int32_t>(backoffUntil_ - millis()) > wait) wait = static_cast<int32_t>(backoffUntil_ - millis());
    SpotifyRequest req;
    if (g_store.popCommand(&req, wait > 0 ? static_cast<uint32_t>(wait) : 0)) {
      // Coalesce bursts of volume/seek changes: only the last one matters.
      SpotifyRequest more;
      while ((req.cmd == SpotifyCmd::Volume || req.cmd == SpotifyCmd::Seek) && g_store.popCommand(&more, 0)) {
        if (more.cmd != req.cmd) {
          execute(req);
        }
        req = more;
      }
      if (static_cast<int32_t>(backoffUntil_ - millis()) > 0) continue;
      execute(req);
      continue;
    }
    if (static_cast<int32_t>(backoffUntil_ - millis()) > 0) continue;
    if (static_cast<int32_t>(nextPoll_ - millis()) <= 0) {
      poll();
      if (static_cast<int32_t>(nextPoll_ - millis()) <= 0) nextPoll_ = millis() + pollInterval();
    }
  }
}

void taskMain(void*) {
  static Worker worker;
  worker.run();
}

}  // namespace

void startTask() {
  if (!cfg::spotifyConfigured()) return;
  xTaskCreatePinnedToCore(taskMain, "spotify", 16384, nullptr, 1, nullptr, 0);
}

void authorizeUrl(char* out, size_t n, const char* state) {
  snprintf(out, n,
           "https://accounts.spotify.com/authorize?response_type=code&client_id=%s&scope=%s&redirect_uri=%s"
           "&state=%s&show_dialog=true",
           cfg::kSpotifyClientId, urlEncode(kScopes).c_str(), urlEncode(kRedirectUri).c_str(), state);
}

}  // namespace spotify
