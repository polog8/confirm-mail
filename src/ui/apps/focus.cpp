#include <stdio.h>

#include "../format.h"
#include "../theme.h"
#include "apps.h"

uint32_t FocusApp::phaseLengthMs() const {
  int minutes = phase_ == Phase::Focus        ? settings_.focusMin
                : phase_ == Phase::ShortBreak ? settings_.shortBreakMin
                                              : settings_.longBreakMin;
  return static_cast<uint32_t>(minutes) * 60000u;
}

uint32_t FocusApp::remainingMs(uint32_t nowMs) const {
  if (!started_) return phaseLengthMs();
  if (!running_) return pausedRemainingMs_;
  int32_t r = static_cast<int32_t>(endAtMs_ - nowMs);
  return r > 0 ? static_cast<uint32_t>(r) : 0;
}

void FocusApp::startPhase(Phase p, uint32_t nowMs, bool run, Actions& a) {
  phase_ = p;
  started_ = run;
  running_ = run;
  pausedRemainingMs_ = phaseLengthMs();
  endAtMs_ = nowMs + pausedRemainingMs_;
  if (settings_.focusControlsMusic && run) {
    a.spotify(p == Phase::Focus ? SpotifyCmd::Play : SpotifyCmd::Pause);
  }
  dirty_ = true;
}

bool FocusApp::consumeDirty() {
  bool d = dirty_;
  dirty_ = false;
  return d;
}

bool FocusApp::consumeAttention() {
  bool d = attention_;
  attention_ = false;
  return d;
}

void FocusApp::tick(const Frame& f, Actions& a) {
  if (f.s->sys.timeValid && day_ != f.lt.day) {
    day_ = f.lt.day;
    sessionsToday_ = 0;
    minutesToday_ = 0;
  }
  if (!running_ || static_cast<int32_t>(endAtMs_ - f.ms) > 0) return;
  if (phase_ == Phase::Focus) {
    ++sessionsToday_;
    minutesToday_ += settings_.focusMin;
    ++cycle_;
    const bool longBreak = cycle_ >= settings_.sessionsBeforeLongBreak;
    if (longBreak) cycle_ = 0;
    a.feedback(Feedback::FocusDone);
    startPhase(longBreak ? Phase::LongBreak : Phase::ShortBreak, f.ms, true, a);
  } else {
    a.feedback(Feedback::BreakDone);
    startPhase(Phase::Focus, f.ms, false, a);
  }
  attention_ = true;
}

void FocusApp::onButton(int idx, bool longPress, const Frame& f, Actions& a) {
  if (idx == 0) {
    if (longPress) {
      cycle_ = 0;
      phase_ = Phase::Focus;
    }
    started_ = running_ = false;
    pausedRemainingMs_ = phaseLengthMs();
  } else if (idx == 1) {
    if (!started_) {
      startPhase(phase_, f.ms, true, a);
    } else if (running_) {
      pausedRemainingMs_ = remainingMs(f.ms);
      running_ = false;
    } else {
      endAtMs_ = f.ms + pausedRemainingMs_;
      running_ = true;
    }
  } else if (idx == 2) {
    if (phase_ == Phase::Focus) {
      const bool longBreak = cycle_ + 1 >= settings_.sessionsBeforeLongBreak;
      startPhase(longBreak ? Phase::LongBreak : Phase::ShortBreak, f.ms, true, a);
    } else {
      startPhase(Phase::Focus, f.ms, false, a);
    }
  }
  dirty_ = true;
}

void FocusApp::render(draw::Canvas& c, const Frame& f) {
  const bool focus = phase_ == Phase::Focus;
  const uint32_t col = focus ? theme::kFocus : theme::kBreak;
  const float cx = 160, cy = 118, r = 72, th = 10;
  const uint32_t len = phaseLengthMs();
  const uint32_t rem = remainingMs(f.ms);

  draw::ringArc(c, cx, cy, r, th, 0, 360, theme::kText, 24);
  if (started_) {
    float frac = len ? 1.f - static_cast<float>(rem) / len : 0.f;
    if (frac > 0.003f) draw::ringArc(c, cx, cy, r, th, 0, 360.f * frac, running_ ? col : draw::mix(col, 0x808080, 0.5f));
  }

  const char* label = focus ? "FOCUS" : phase_ == Phase::ShortBreak ? "PAUSE" : "GRANDE PAUSE";
  draw::spaced(c, Font::Small, label, static_cast<int>(cx), 86, started_ ? col : theme::kText2, 2, draw::Center);

  char buf[32];
  const uint32_t secs = (rem + 999) / 1000;
  snprintf(buf, sizeof(buf), "%02u:%02u", static_cast<unsigned>(secs / 60), static_cast<unsigned>(secs % 60));
  draw::text(c, Font::Timer, buf, static_cast<int>(cx), 138, theme::kText, 0, draw::Center);

  if (!started_) {
    snprintf(buf, sizeof(buf), "Prêt");
  } else if (running_) {
    char t[8];
    fmt::hhmm(t, sizeof(t), *f.tz, f.now + rem / 1000);
    snprintf(buf, sizeof(buf), "Fin à %s", t);
  } else {
    snprintf(buf, sizeof(buf), "En pause");
  }
  draw::text(c, Font::Small, buf, static_cast<int>(cx), 158, theme::kText3, 0, draw::Center);

  // Cycle progress dots
  const int n = settings_.sessionsBeforeLongBreak;
  const float dotGap = 11.f;
  float dx = cx - (n - 1) * dotGap / 2.f;
  for (int i = 0; i < n; ++i) {
    if (i < cycle_) draw::fillCircle(c, dx + i * dotGap, 174, 3.f, theme::kFocus);
    else draw::fillCircle(c, dx + i * dotGap, 174, 3.f, theme::kText, 60);
  }

  // Daily stats
  snprintf(buf, sizeof(buf), "%d", sessionsToday_);
  draw::text(c, Font::Large, buf, 16, 112, theme::kText);
  draw::text(c, Font::Small, sessionsToday_ > 1 ? "sessions" : "session", 16, 130, theme::kText3);
  draw::text(c, Font::Small, "aujourd'hui", 16, 145, theme::kText3);
  snprintf(buf, sizeof(buf), "%d", minutesToday_);
  draw::text(c, Font::Large, buf, 304, 112, theme::kText, 0, draw::Right);
  draw::text(c, Font::Small, "minutes", 304, 130, theme::kText3, 0, draw::Right);
  draw::text(c, Font::Small, "de focus", 304, 145, theme::kText3, 0, draw::Right);
}

Buttons FocusApp::buttons(const Frame& f) const {
  Buttons b;
  b.b[0].icon = Icon::Reset;
  b.b[0].disabled = !started_;
  b.b[1].icon = running_ ? Icon::Pause : Icon::Play;
  b.b[1].accent = true;
  b.b[2].icon = Icon::Skip;
  return b;
}
