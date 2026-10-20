// M5Stack Core2 desk station.
//
// Core 1 (Arduino loop): input, rendering, haptics/sound, power management.
// Core 0: network tasks (Wi-Fi + web page, Spotify, calendar/weather sync).
#include <M5Unified.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <sys/time.h>

#include <new>
#include <utility>

#include "services/net.h"
#include "services/settings.h"
#include "services/spotify.h"
#include "services/feeds.h"
#include "services/store.h"
#include "services/tls.h"
#include "ui/chrome.h"
#include "ui/fonts.h"
#include "ui/format.h"
#include "ui/shell.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

namespace {

// ---------------------------------------------------------------------------
// Settings & UI objects

UiSettings makeSettings() {
  UiSettings s;
  s.focusMin = cfg::kFocusMin;
  s.shortBreakMin = cfg::kShortBreakMin;
  s.longBreakMin = cfg::kLongBreakMin;
  s.sessionsBeforeLongBreak = cfg::kSessionsBeforeLongBreak;
  s.focusControlsMusic = cfg::kFocusControlsMusic && cfg::spotifyConfigured();
  s.alertLeadMin = cfg::kMeetingAlertMin;
  s.autoReturnSec = cfg::kAutoReturnSec;
  s.musicEnabled = cfg::spotifyConfigured();
  s.calendarEnabled = cfg::calendarsConfigured();
  s.weatherEnabled = cfg::kWeatherEnabled;
  return s;
}

const UiSettings g_settings = makeSettings();
PosixTz g_tz;
HomeApp g_home(g_settings);
MusicApp g_music;
AgendaApp g_agenda;
FocusApp g_focus(g_settings);
SystemApp g_system;
AlertApp g_alert;
Shell g_shell(g_settings);

AppSnapshot* g_ui = nullptr;  // UI-side copy of the shared state
uint32_t g_seenVersion = 0;
uint32_t g_seenCalSerial = 0;
uint32_t g_seenArt = 0;
uint8_t* g_artBuf = nullptr;
size_t g_artCap = 0;

M5Canvas g_canvasA(&M5.Display);
M5Canvas g_canvasB(&M5.Display);
M5Canvas* g_front = &g_canvasA;
M5Canvas* g_back = &g_canvasB;

Preferences g_prefs;
int g_brightnessPct = cfg::kBrightness;
int g_appliedLevel = -1;
bool g_screenOff = false;

// ---------------------------------------------------------------------------
// Haptics, sound and LED patterns

struct Step {
  uint8_t vibration;  // 0..255
  uint16_t hz;        // 0 = silence
  uint16_t ms;
  bool led;
};

class Notifier {
 public:
  void play(Feedback f) {
    n_ = 0;
    switch (f) {
      case Feedback::Tap:
        add({110, 0, 22, false});
        break;
      case Feedback::Success:
        add({0, 1319, 70, false});
        add({0, 1760, 110, false});
        break;
      case Feedback::Alert:
        for (int i = 0; i < 2; ++i) {
          add({200, 880, 140, true});
          add({0, 0, 90, false});
        }
        add({200, 1175, 320, true});
        break;
      case Feedback::FocusDone:
        add({180, 784, 170, true});
        add({0, 0, 60, false});
        add({180, 988, 170, true});
        add({0, 0, 60, false});
        add({180, 1175, 360, true});
        break;
      case Feedback::BreakDone:
        add({160, 1175, 150, true});
        add({0, 0, 60, false});
        add({160, 988, 260, true});
        break;
      case Feedback::Error:
        add({200, 220, 220, false});
        break;
    }
    idx_ = -1;
    stepEnd_ = millis();
  }

  void loop() {
    if (idx_ >= n_ || static_cast<int32_t>(millis() - stepEnd_) < 0) return;
    ++idx_;
    if (idx_ >= n_) {
      M5.Power.setVibration(0);
      M5.Power.setLed(0);
      return;
    }
    const Step& s = steps_[idx_];
    M5.Power.setVibration(cfg::kHaptics ? s.vibration : 0);
    M5.Power.setLed(s.led ? 255 : 0);
    if (cfg::kSound && s.hz) M5.Speaker.tone(s.hz, s.ms);
    stepEnd_ = millis() + s.ms;
  }

 private:
  void add(const Step& s) {
    if (n_ < static_cast<int>(sizeof(steps_) / sizeof(steps_[0]))) steps_[n_++] = s;
  }
  Step steps_[10];
  int n_ = 0;
  int idx_ = 0;
  uint32_t stepEnd_ = 0;
};

Notifier g_notifier;

// ---------------------------------------------------------------------------
// Actions requested by the UI

class DeviceActions : public Actions {
 public:
  void spotify(SpotifyCmd cmd, int32_t arg) override {
    const uint32_t now = millis();
    {
      Store::Guard g(g_store);
      PlaybackState& p = g_store.data().play;
      if (p.status == SpotifyStatus::Disabled || p.status == SpotifyStatus::NeedsAuth) return;
      // Resolve toggles here so the worker receives explicit, idempotent commands.
      if (cmd == SpotifyCmd::PlayPause) cmd = p.isPlaying ? SpotifyCmd::Pause : SpotifyCmd::Play;
      if (cmd == SpotifyCmd::Previous && p.positionAt(now) > 5000) {
        cmd = SpotifyCmd::Seek;  // like any player: restart the track first
        arg = 0;
      }
      // Optimistic update: the screen reacts before Spotify confirms.
      switch (cmd) {
        case SpotifyCmd::Play:
        case SpotifyCmd::Pause:
          p.progressMs = p.positionAt(now);
          p.sampledAtMs = now;
          p.isPlaying = cmd == SpotifyCmd::Play;
          break;
        case SpotifyCmd::Next:
        case SpotifyCmd::Previous:
          p.progressMs = 0;
          p.sampledAtMs = now;
          break;
        case SpotifyCmd::Seek:
          p.progressMs = static_cast<uint32_t>(arg);
          p.sampledAtMs = now;
          break;
        case SpotifyCmd::Volume:
          p.volume = static_cast<int8_t>(arg);
          break;
        case SpotifyCmd::ToggleLike:
          p.liked = !p.liked;
          arg = p.liked ? 1 : 0;
          break;
        default:
          break;
      }
      g_store.optimisticUntilMs = now + 1500;
      g_store.touch();
    }
    if (!g_store.pushCommand(cmd, arg)) g_notifier.play(Feedback::Error);
  }

  void refreshAll() override { g_store.refreshRequested = true; }
  void feedback(Feedback f) override { g_notifier.play(f); }

  void changeBrightness(int delta) override {
    g_brightnessPct = constrain(g_brightnessPct + delta, 10, 100);
    g_prefs.putInt("bright", g_brightnessPct);
  }
  int brightnessPercent() const override { return g_brightnessPct; }
};

DeviceActions g_actions;

// ---------------------------------------------------------------------------

Frame makeFrame() {
  Frame f;
  f.s = g_ui;
  f.tz = &g_tz;
  f.ms = millis();
  f.now = time(nullptr);
  f.lt = fmt::localTime(g_tz, f.now);
  // The clock may already be right (RTC) before the network task reports it.
  g_ui->sys.timeValid = f.now > 1700000000;
  return f;
}

// Why the previous run ended, shown on the System page.
const char* resetReasonText(esp_reset_reason_t r, bool* abnormal) {
  *abnormal = true;
  switch (r) {
    case ESP_RST_PANIC: return "plantage";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "chute de tension";
    default: break;
  }
  *abnormal = false;
  switch (r) {
    case ESP_RST_POWERON: return "mise sous tension";
    case ESP_RST_SW: return "redémarrage";
    case ESP_RST_EXT: return "bouton reset";
    case ESP_RST_DEEPSLEEP: return "réveil";
    default: return "autre";
  }
}

void restoreClockFromRtc() {
  if (!M5.Rtc.isEnabled()) return;
  m5::rtc_datetime_t dt;
  if (!M5.Rtc.getDateTime(&dt) || dt.date.year < 2024) return;
  civ::DateTime c{dt.date.year, dt.date.month, dt.date.date, dt.time.hours, dt.time.minutes, dt.time.seconds};
  timeval tv{static_cast<time_t>(civ::toEpoch(c)), 0};  // the RTC keeps UTC
  settimeofday(&tv, nullptr);
}

void saveClockToRtc() {
  time_t now = time(nullptr);
  if (now < 1700000000 || !M5.Rtc.isEnabled()) return;
  tm utc;
  gmtime_r(&now, &utc);
  M5.Rtc.setDateTime(&utc);
}

void pullState() {
  if (g_store.artSerial() != g_seenArt) {
    g_seenArt = g_store.artSerial();
    size_t len = g_store.copyArt(&g_artBuf, &g_artCap);
    g_music.setArt(len ? g_artBuf : nullptr, len);
    g_shell.invalidate();
  }
  if (g_store.version() == g_seenVersion) return;
  Store::Guard g(g_store);
  g_seenVersion = g_store.version();
  const AppSnapshot& s = g_store.data();
  g_ui->play = s.play;
  g_ui->weather = s.weather;
  SystemState local = g_ui->sys;
  g_ui->sys = s.sys;
  g_ui->sys.battery = local.battery;
  g_ui->sys.charging = local.charging;
  g_ui->sys.usbPower = local.usbPower;
  g_ui->sys.batteryVolts = local.batteryVolts;
  if (s.cal.serial != g_seenCalSerial) {
    g_seenCalSerial = s.cal.serial;
    g_ui->cal = s.cal;
  }
  g_shell.invalidate();
}

void readPower() {
  static uint32_t next = 0;
  if (static_cast<int32_t>(millis() - next) < 0) return;
  next = millis() + 5000;
  SystemState& s = g_ui->sys;
  const int level = M5.Power.getBatteryLevel();
  const bool charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
  const bool usb = M5.Power.getVBUSVoltage() > 4000;
  const float volts = M5.Power.getBatteryVoltage() / 1000.f;
  if (level != s.battery || charging != s.charging || usb != s.usbPower) g_shell.invalidate();
  s.battery = level;
  s.charging = charging;
  s.usbPower = usb;
  s.batteryVolts = volts;
}

bool isNight(int hour) {
  if (cfg::kNightStartHour == cfg::kNightEndHour) return false;
  if (cfg::kNightStartHour < cfg::kNightEndHour) return hour >= cfg::kNightStartHour && hour < cfg::kNightEndHour;
  return hour >= cfg::kNightStartHour || hour < cfg::kNightEndHour;
}

void applyBrightness(const Frame& f) {
  const uint32_t idle = g_shell.idleMs(f.ms);
  const bool night = g_ui->sys.timeValid && isNight(f.lt.hh);
  int pct = g_brightnessPct;
  if (night) pct = max(8, pct / 3);
  if (!g_shell.alertActive()) {
    if (idle > cfg::kDimAfterSec * 1000u) pct = min(pct, cfg::kDimBrightness);
    if (night && cfg::kNightOffAfterSec > 0 && idle > cfg::kNightOffAfterSec * 1000u && !g_focus.isRunning()) pct = 0;
  }
  const int target = pct * 255 / 100;
  if (g_appliedLevel < 0) g_appliedLevel = target;
  // Gentle fade.
  if (g_appliedLevel != target) {
    const int step = max(2, abs(target - g_appliedLevel) / 6);
    g_appliedLevel += target > g_appliedLevel ? min(step, target - g_appliedLevel) : -min(step, g_appliedLevel - target);
  }
  M5.Display.setBrightness(static_cast<uint8_t>(g_appliedLevel));
  const bool off = g_appliedLevel == 0;
  if (g_screenOff && !off) g_shell.invalidate();
  g_screenOff = off;
}

void slide(M5Canvas& from, M5Canvas& to, int dir) {
  constexpr int kSteps = 7;
  M5.Display.startWrite();
  for (int i = 1; i <= kSteps; ++i) {
    const float t = static_cast<float>(i) / kSteps;
    const float e = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
    const int off = static_cast<int>(e * 320.f + 0.5f);
    if (dir > 0) {
      from.pushSprite(-off, 0);
      to.pushSprite(320 - off, 0);
    } else {
      from.pushSprite(off, 0);
      to.pushSprite(off - 320, 0);
    }
  }
  M5.Display.endWrite();
}

// Touch screen + the three touch buttons below it.
void handleInput(const Frame& f) {
  bool any = false;
  const bool wasAsleep = g_screenOff || g_appliedLevel < 20;
  m5::Button_Class* buttons[3] = {&M5.BtnA, &M5.BtnB, &M5.BtnC};
  for (int i = 0; i < 3; ++i) {
    m5::Button_Class& b = *buttons[i];
    if (b.wasPressed()) {
      any = true;
      if (!wasAsleep) g_notifier.play(Feedback::Tap);
    }
    if (wasAsleep) continue;  // first touch only wakes the screen
    if (b.wasHold()) g_shell.onButton(i, true, f, g_actions);
    else if (b.wasClicked()) g_shell.onButton(i, false, f, g_actions);
  }

  const auto t = M5.Touch.getDetail();
  if (t.base_y < 236 && (t.isPressed() || t.wasReleased())) {
    any = true;
    if (!wasAsleep) {
      if (t.wasClicked()) {
        g_shell.onTap(t.x, t.y, f, g_actions);
      } else if (t.wasFlicked() || t.wasDragged()) {
        const int dx = t.distanceX(), dy = t.distanceY();
        if (abs(dx) > 40 || abs(dy) > 30) g_shell.onSwipe(dx, dy, f, g_actions);
      } else if ((t.isFlicking() || t.isDragging()) && abs(t.distanceY()) > abs(t.distanceX())) {
        g_shell.onDrag(t.deltaY(), f);
      }
    }
  }
  if (any) {
    g_shell.noteInput(f.ms);
    if (wasAsleep) g_shell.invalidate();
  }
}

}  // namespace

void setup() {
  // TLS handshakes and HTTPClient's busy waits can keep core 0 busy for a few
  // seconds; keep the watchdog for real hangs but don't let it fire on those.
  esp_task_wdt_init(30, true);

  auto config = M5.config();
  config.serial_baudrate = 115200;
  config.internal_mic = false;
  M5.begin(config);
  M5.Display.setBrightness(0);
  M5.Speaker.setVolume(110);
  M5.Touch.setFlickThresh(12);

  g_tz.parse(cfg::kTimezone);
  setenv("TZ", cfg::kTimezone, 1);
  tzset();
  restoreClockFromRtc();

  g_prefs.begin("ui", false);
  g_brightnessPct = g_prefs.getInt("bright", cfg::kBrightness);

  fonts::begin();
  chrome::createScreenCanvas(g_canvasA);
  chrome::createScreenCanvas(g_canvasB);
  g_ui = new (heap_caps_malloc(sizeof(AppSnapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) AppSnapshot();

  g_store.begin();
  {
    Store::Guard g(g_store);
    g_store.data().play.status = cfg::spotifyConfigured() ? SpotifyStatus::Connecting : SpotifyStatus::Disabled;
    SystemState& sys = g_store.data().sys;
    const esp_reset_reason_t reason = esp_reset_reason();
    strlcpy(sys.resetReason, resetReasonText(reason, &sys.abnormalReset), sizeof(sys.resetReason));
    g_store.touch();
    log_i("Reset reason: %d (%s)", static_cast<int>(reason), sys.resetReason);
  }

  g_shell.add(&g_home);
  if (cfg::kSpotifyEnabled) g_shell.add(&g_music);
  if (cfg::calendarsConfigured()) g_shell.add(&g_agenda);
  g_shell.add(&g_focus);
  g_shell.add(&g_system);
  g_shell.setRoles(&g_home, cfg::kSpotifyEnabled ? &g_music : nullptr, &g_focus);
  g_shell.setAlert(&g_alert);
  g_system.setVersion(cfg::kVersion);
  g_system.setBrightnessPercent(g_brightnessPct);
  g_music.setArt(nullptr, 0);

  tls::begin();  // before any task opens a TLS connection
  net::startTask();
  spotify::startTask();
  feeds::startTask();
  g_shell.noteInput(millis());
}

void loop() {
  M5.update();
  g_notifier.loop();
  pullState();
  readPower();
  if (g_store.rtcNeedsSync) {
    g_store.rtcNeedsSync = false;
    saveClockToRtc();
  }

  Frame f = makeFrame();
  handleInput(f);
  g_shell.tick(f, g_actions);
  if (g_shell.takeWake()) g_shell.noteInput(f.ms);
  const bool musicVisible = g_shell.musicVisible();
  if (musicVisible && !g_store.musicVisible) {
    g_store.pushCommand(SpotifyCmd::Refresh, 0);  // fresh state right away
  }
  g_store.musicVisible = musicVisible;
  applyBrightness(f);

  if (!g_screenOff && g_shell.needsRedraw(f)) {
    int dir = 0;
    const bool animate = g_shell.takeTransition(&dir);
    g_shell.render(*g_back, f);
    if (animate) slide(*g_front, *g_back, dir);
    else g_back->pushSprite(0, 0);
    std::swap(g_front, g_back);
  } else if (g_screenOff) {
    int dir;
    g_shell.takeTransition(&dir);  // nothing to animate while dark
  }
  delay(5);
}
