#include "shell.h"

#include <stdlib.h>

#include "chrome.h"
#include "theme.h"

namespace {
uint32_t alertKey(const CalendarEvent& e) { return e.uid ^ static_cast<uint32_t>(e.start) ^ 0x9E3779B9u; }
constexpr uint32_t kPressedMs = 180;
}  // namespace

void Shell::add(App* app) {
  if (n_ < static_cast<int>(sizeof(apps_) / sizeof(apps_[0]))) apps_[n_++] = app;
}

int Shell::indexOf(const App* app) const {
  for (int i = 0; i < n_; ++i) {
    if (apps_[i] == app) return i;
  }
  return -1;
}

void Shell::render(draw::Canvas& c, const Frame& f) {
  App* app = current();
  if (!app->drawsOwnBackground()) chrome::background(c);
  app->render(c, f);
  if (alertActive()) chrome::statusBar(c, f, app->title(), 0, 0, true);
  else chrome::statusBar(c, f, app->title(), cur_, n_, app->showsClock());
  chrome::buttonBar(c, app->buttons(f), f.ms - pressedAt_ < kPressedMs ? pressed_ : -1);
  lastDraw_ = f.ms;
  lastMinute_ = f.lt.mm;
  force_ = false;
}

bool Shell::needsRedraw(const Frame& f) {
  App* app = current();
  bool dirty = app->consumeDirty();
  if (force_ || dirty) return true;
  if (f.lt.mm != lastMinute_) return true;
  if (pressed_ >= 0 && f.ms - pressedAt_ >= kPressedMs) {
    pressed_ = -1;
    return true;
  }
  return f.ms - lastDraw_ >= app->refreshMs(f);
}

void Shell::onButton(int idx, bool longPress, const Frame& f, Actions& a) {
  pressed_ = idx;
  pressedAt_ = f.ms;
  force_ = true;
  if (alertActive()) {
    alert_->onButton(idx, longPress, f, a);
    if (!alert_->active() && alert_->snoozed()) {
      snoozeKey_ = alertKey(alert_->event());
      snoozeAt_ = alert_->event().start;
    }
    return;
  }
  apps_[cur_]->onButton(idx, longPress, f, a);
}

void Shell::onTap(int x, int y, const Frame& f, Actions& a) {
  if (alertActive()) return;
  // Tapping the page dots cycles pages, as a discoverable alternative to swiping.
  if (y < theme::kStatusH && x > 110 && x < 210) {
    go((cur_ + 1) % n_, +1, f);
    return;
  }
  apps_[cur_]->onTap(x, y, f, a);
  force_ = true;
}

void Shell::onSwipe(int dx, int dy, const Frame& f, Actions& a) {
  if (alertActive()) return;
  if (abs(dx) > abs(dy)) {
    if (dx < 0) go((cur_ + 1) % n_, +1, f);
    else go((cur_ + n_ - 1) % n_, -1, f);
  } else {
    apps_[cur_]->onVSwipe(dy, f, a);
    force_ = true;
  }
}

void Shell::onDrag(int dy, const Frame& f) {
  if (alertActive()) return;
  apps_[cur_]->onDrag(dy, f);
}

void Shell::go(int idx, int dir, const Frame& f) {
  if (idx < 0 || idx >= n_ || idx == cur_) return;
  cur_ = idx;
  pendingDir_ = dir;
  apps_[cur_]->onShow(f);
  force_ = true;
}

bool Shell::takeTransition(int* dir) {
  if (!pendingDir_) return false;
  *dir = pendingDir_;
  pendingDir_ = 0;
  return true;
}

bool Shell::takeWake() {
  bool w = wake_;
  wake_ = false;
  return w;
}

bool Shell::alreadyAlerted(uint32_t key) const {
  for (uint32_t k : alerted_) {
    if (k == key) return true;
  }
  return false;
}

void Shell::tick(const Frame& f, Actions& a) {
  for (int i = 0; i < n_; ++i) {
    apps_[i]->tick(f, a);
    if (apps_[i]->consumeAttention()) {
      if (!alertActive()) go(i, i > cur_ ? +1 : -1, f);
      wake_ = true;
      force_ = true;
      lastInput_ = f.ms;
    }
  }

  // Meeting reminders
  if (alert_ && settings_.alertLeadMin > 0 && f.s->sys.timeValid) {
    const CalendarState& cal = f.s->cal;
    for (int i = 0; i < cal.count; ++i) {
      const CalendarEvent& e = cal.events[i];
      if (e.allDay) continue;
      const int64_t lead = e.start - f.now;
      const uint32_t key = alertKey(e);
      const bool due = lead > 0 && lead <= settings_.alertLeadMin * 60;
      const bool snoozeDue = snoozeKey_ == key && f.now >= snoozeAt_ && f.now < e.start + 60;
      if ((due && !alreadyAlerted(key)) || snoozeDue) {
        if (snoozeDue) snoozeKey_ = 0;
        alerted_[alertedPos_] = key;
        alertedPos_ = (alertedPos_ + 1) % static_cast<int>(sizeof(alerted_) / sizeof(alerted_[0]));
        alert_->show(e, evt::color(cal, e));
        a.feedback(Feedback::Alert);
        wake_ = true;
        force_ = true;
        lastInput_ = f.ms;
        break;
      }
    }
    if (alert_->active() && alert_->expired(f.now)) {
      alert_->dismiss();
      force_ = true;
    }
  }

  // Bring the most relevant page back after a while without interaction.
  if (!alertActive() && settings_.autoReturnSec > 0 && idleMs(f.ms) > settings_.autoReturnSec * 1000u) {
    App* target = home_;
    if (focus_ && focus_->isRunning()) target = focus_;
    else if (music_ && f.s->play.isPlaying) target = music_;
    int idx = indexOf(target);
    if (idx >= 0 && idx != cur_) go(idx, idx > cur_ ? +1 : -1, f);
  }
}
