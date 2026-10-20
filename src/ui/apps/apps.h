// The station's screens.
#pragma once

#include "../app.h"

// Helpers on the calendar snapshot.
namespace evt {
int current(const CalendarState& cal, int64_t now);  // ongoing timed event, -1 if none
int next(const CalendarState& cal, int64_t now);     // next timed event, -1 if none
uint32_t color(const CalendarState& cal, const CalendarEvent& e);
// Formats "15:00 – 16:00"
void range(char* out, size_t n, const PosixTz& tz, const CalendarEvent& e);
}  // namespace evt

class HomeApp : public App {
 public:
  explicit HomeApp(const UiSettings& s) : settings_(s) {}
  const char* title() const override { return ""; }
  void render(draw::Canvas& c, const Frame& f) override;
  Buttons buttons(const Frame& f) const override;
  void onButton(int idx, bool longPress, const Frame& f, Actions& a) override;
  uint32_t refreshMs(const Frame& f) const override;
  bool showsClock() const override { return false; }

 private:
  void renderWeather(draw::Canvas& c, const Frame& f);
  void renderEventCard(draw::Canvas& c, const Frame& f, int x, int y, int w, int h);
  void renderFooter(draw::Canvas& c, const Frame& f);
  const UiSettings& settings_;
};

class MusicApp : public App {
 public:
  MusicApp();
  const char* title() const override { return "MUSIQUE"; }
  void render(draw::Canvas& c, const Frame& f) override;
  Buttons buttons(const Frame& f) const override;
  void onButton(int idx, bool longPress, const Frame& f, Actions& a) override;
  void onTap(int x, int y, const Frame& f, Actions& a) override;
  void onVSwipe(int dy, const Frame& f, Actions& a) override;
  uint32_t refreshMs(const Frame& f) const override;
  bool drawsOwnBackground() const override { return true; }
  bool consumeDirty() override;

  // New cover art (JPEG bytes). nullptr/0 shows a placeholder.
  void setArt(const uint8_t* jpg, size_t len);
  uint32_t accent() const { return accent_; }

 private:
  void rebuildBackdrop();
  void renderMessage(draw::Canvas& c, const Frame& f, const char* title, const char* body, bool qr);
  draw::Canvas art_;
  draw::Canvas backdrop_;
  bool artOk_ = false;
  bool backdropOk_ = false;
  uint32_t accent_;
  uint32_t barColor_;
  uint32_t likeAnimAt_ = 0;
  bool likeAnimOn_ = false;
  uint32_t volumeShownAt_ = 0;
  int volumeTarget_ = -1;
  bool dirty_ = false;
};

class AgendaApp : public App {
 public:
  const char* title() const override { return "AGENDA"; }
  void render(draw::Canvas& c, const Frame& f) override;
  Buttons buttons(const Frame& f) const override;
  void onButton(int idx, bool longPress, const Frame& f, Actions& a) override;
  void onDrag(int dy, const Frame& f) override;
  void onShow(const Frame& f) override;
  uint32_t refreshMs(const Frame& f) const override { return 5000; }
  bool consumeDirty() override;

 private:
  int collect(const Frame& f, int64_t day, int* timed, int maxTimed, int* allDay, int maxAllDay, int* nAllDay) const;
  int dayOffset_ = 0;
  int scroll_ = 0;
  int maxScroll_ = 0;
  bool autoScroll_ = true;
  bool dirty_ = false;
};

class FocusApp : public App {
 public:
  explicit FocusApp(const UiSettings& s) : settings_(s) {}
  const char* title() const override { return "FOCUS"; }
  void render(draw::Canvas& c, const Frame& f) override;
  Buttons buttons(const Frame& f) const override;
  void onButton(int idx, bool longPress, const Frame& f, Actions& a) override;
  void tick(const Frame& f, Actions& a) override;
  uint32_t refreshMs(const Frame& f) const override { return running_ ? 500 : 1000; }
  bool consumeDirty() override;
  bool consumeAttention() override;
  bool isRunning() const { return running_; }

  enum class Phase : uint8_t { Focus, ShortBreak, LongBreak };

 private:
  uint32_t phaseLengthMs() const;
  uint32_t remainingMs(uint32_t nowMs) const;
  void startPhase(Phase p, uint32_t nowMs, bool run, Actions& a);
  const UiSettings& settings_;
  Phase phase_ = Phase::Focus;
  bool running_ = false;
  bool started_ = false;  // phase has begun (paused or running)
  uint32_t endAtMs_ = 0;
  uint32_t pausedRemainingMs_ = 0;
  int cycle_ = 0;          // focus sessions completed in the current cycle
  int sessionsToday_ = 0;
  int minutesToday_ = 0;
  int64_t day_ = -1;
  bool dirty_ = false;
  bool attention_ = false;
};

class SystemApp : public App {
 public:
  const char* title() const override { return "SYSTÈME"; }
  void render(draw::Canvas& c, const Frame& f) override;
  Buttons buttons(const Frame& f) const override;
  void onButton(int idx, bool longPress, const Frame& f, Actions& a) override;
  uint32_t refreshMs(const Frame& f) const override { return 2000; }
  bool consumeDirty() override;
  void setBrightnessPercent(int p) { brightness_ = p; }
  void setVersion(const char* v) { version_ = v; }

 private:
  int brightness_ = 60;
  const char* version_ = "";
  bool dirty_ = false;
};

// Full-screen reminder shown a few minutes before a meeting.
class AlertApp : public App {
 public:
  const char* title() const override { return "RAPPEL"; }
  void render(draw::Canvas& c, const Frame& f) override;
  Buttons buttons(const Frame& f) const override;
  void onButton(int idx, bool longPress, const Frame& f, Actions& a) override;
  bool drawsOwnBackground() const override { return true; }

  void show(const CalendarEvent& e, uint32_t color);
  bool active() const { return active_; }
  void dismiss() { active_ = false; }
  // true when the alert should close on its own
  bool expired(int64_t now) const { return now > event_.start + 120; }
  // Re-alert at the start time instead ("plus tard").
  bool snoozed() const { return snoozed_; }
  const CalendarEvent& event() const { return event_; }

 private:
  CalendarEvent event_;
  uint32_t color_ = 0;
  bool active_ = false;
  bool snoozed_ = false;
};
