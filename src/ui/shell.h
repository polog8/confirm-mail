// Owns the screens: navigation, chrome, meeting alerts, auto-return.
// Platform independent: the firmware feeds it input events and pushes the
// canvas it renders to the display.
#pragma once

#include "app.h"
#include "apps/apps.h"

class Shell {
 public:
  explicit Shell(const UiSettings& settings) : settings_(settings) {}

  void add(App* app);
  void setRoles(App* home, App* music, FocusApp* focus) {
    home_ = home;
    music_ = music;
    focus_ = focus;
  }
  void setAlert(AlertApp* alert) { alert_ = alert; }

  int count() const { return n_; }
  int currentIndex() const { return cur_; }
  App* current() const { return alertActive() ? static_cast<App*>(alert_) : apps_[cur_]; }
  bool alertActive() const { return alert_ && alert_->active(); }
  bool musicVisible() const { return !alertActive() && apps_[cur_] == music_; }

  void render(draw::Canvas& c, const Frame& f);
  bool needsRedraw(const Frame& f);
  void invalidate() { force_ = true; }

  // Input
  void onButton(int idx, bool longPress, const Frame& f, Actions& a);
  void onTap(int x, int y, const Frame& f, Actions& a);
  void onSwipe(int dx, int dy, const Frame& f, Actions& a);
  void onDrag(int dy, const Frame& f);
  void noteInput(uint32_t ms) { lastInput_ = ms; }
  uint32_t idleMs(uint32_t ms) const { return ms - lastInput_; }

  void tick(const Frame& f, Actions& a);

  void go(int idx, int dir, const Frame& f);
  // One-shot: a page change happened, animate it (dir +1 = from the right).
  bool takeTransition(int* dir);
  // One-shot: something important happened, wake the screen.
  bool takeWake();

 private:
  int indexOf(const App* app) const;
  bool alreadyAlerted(uint32_t key) const;

  const UiSettings& settings_;
  App* apps_[8] = {};
  int n_ = 0;
  int cur_ = 0;
  App* home_ = nullptr;
  App* music_ = nullptr;
  FocusApp* focus_ = nullptr;
  AlertApp* alert_ = nullptr;

  bool force_ = true;
  uint32_t lastDraw_ = 0;
  int lastMinute_ = -1;
  int pressed_ = -1;
  uint32_t pressedAt_ = 0;
  uint32_t lastInput_ = 0;
  int pendingDir_ = 0;
  bool wake_ = false;

  uint32_t alerted_[24] = {};
  int alertedPos_ = 0;
  uint32_t snoozeKey_ = 0;
  int64_t snoozeAt_ = 0;
};
