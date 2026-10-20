// Screen ("app") interface shared by every page of the station.
#pragma once

#include "../core/tz.h"
#include "../model/state.h"
#include "draw.h"
#include "icons.h"

struct LocalTime {
  int y = 1970, m = 1, d = 1, hh = 0, mm = 0, ss = 0;
  int wday = 3;     // 0 = Monday
  int64_t day = 0;  // days since 1970-01-01 (local)
};

struct Frame {
  const AppSnapshot* s = nullptr;
  int64_t now = 0;  // UTC seconds
  uint32_t ms = 0;  // monotonic milliseconds
  LocalTime lt;
  const PosixTz* tz = nullptr;
};

struct UiSettings {
  int focusMin = 25;
  int shortBreakMin = 5;
  int longBreakMin = 15;
  int sessionsBeforeLongBreak = 4;
  bool focusControlsMusic = false;
  int alertLeadMin = 5;
  int autoReturnSec = 45;
  bool musicEnabled = true;
  bool calendarEnabled = true;
  bool weatherEnabled = true;
};

enum class Feedback : uint8_t { Tap, Success, Alert, FocusDone, BreakDone, Error };

// Side effects requested by the UI; implemented by the firmware (no-op on PC).
class Actions {
 public:
  virtual ~Actions() = default;
  virtual void spotify(SpotifyCmd cmd, int32_t arg = 0) = 0;
  virtual void refreshAll() = 0;
  virtual void feedback(Feedback f) = 0;
  virtual void changeBrightness(int delta) = 0;
  virtual int brightnessPercent() const = 0;
};

struct ButtonSpec {
  Icon icon = Icon::None;
  const char* label = nullptr;
  bool accent = false;
  bool disabled = false;
};

struct Buttons {
  ButtonSpec b[3];
};

class App {
 public:
  virtual ~App() = default;
  virtual const char* title() const = 0;
  virtual void render(draw::Canvas& c, const Frame& f) = 0;
  virtual Buttons buttons(const Frame& f) const = 0;

  virtual void onButton(int idx, bool longPress, const Frame& f, Actions& a) {}
  virtual void onTap(int x, int y, const Frame& f, Actions& a) {}
  virtual void onDrag(int dy, const Frame& f) {}
  virtual void onVSwipe(int dy, const Frame& f, Actions& a) {}
  virtual void onShow(const Frame& f) {}
  // Called every loop iteration, even when the app is not visible.
  virtual void tick(const Frame& f, Actions& a) {}
  // Desired redraw period while visible.
  virtual uint32_t refreshMs(const Frame& f) const { return 1000; }
  // Set when state changed and an immediate redraw is wanted.
  virtual bool consumeDirty() { return false; }
  // The app wants to be brought to the front (e.g. timer finished).
  virtual bool consumeAttention() { return false; }
  virtual bool drawsOwnBackground() const { return false; }
  virtual bool showsClock() const { return true; }
};
