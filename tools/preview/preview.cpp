// Renders every screen with demo data to PNG files, on a PC.
//   make -C tools/preview M5GFX=/path/to/M5GFX/src   (needs libsdl2-dev)
//   ./tools/preview/preview out_dir
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "ui/chrome.h"
#include "ui/format.h"
#include "ui/fonts.h"
#include "ui/shell.h"

namespace {

const char* kParis = "CET-1CEST,M3.5.0,M10.5.0/3";

struct NullActions : Actions {
  void spotify(SpotifyCmd, int32_t) override {}
  void refreshAll() override {}
  void feedback(Feedback) override {}
  void changeBrightness(int) override {}
  int brightnessPercent() const override { return 70; }
};

int64_t at(const PosixTz& tz, int y, int m, int d, int hh, int mm) {
  civ::DateTime dt{y, m, d, hh, mm, 0};
  return tz.localToUtc(dt);
}

void addEvent(CalendarState& cal, const PosixTz& tz, int day, int h0, int m0, int h1, int m1, const char* title,
              const char* loc, const char* url, int calIdx, bool allDay = false) {
  CalendarEvent& e = cal.events[cal.count++];
  if (allDay) {
    e.start = tz.dayStartUtc(civ::daysFromCivil(2026, 10, day));
    e.end = tz.dayStartUtc(civ::daysFromCivil(2026, 10, day) + 1);
  } else {
    e.start = at(tz, 2026, 10, day, h0, m0);
    e.end = at(tz, 2026, 10, day, h1, m1);
  }
  e.allDay = allDay;
  e.calendar = static_cast<uint8_t>(calIdx);
  e.uid = static_cast<uint32_t>(cal.count * 7919);
  snprintf(e.title, sizeof(e.title), "%s", title);
  snprintf(e.location, sizeof(e.location), "%s", loc);
  snprintf(e.url, sizeof(e.url), "%s", url);
}

std::vector<uint8_t> readFile(const char* path) {
  std::vector<uint8_t> v;
  FILE* f = fopen(path, "rb");
  if (!f) return v;
  fseek(f, 0, SEEK_END);
  v.resize(static_cast<size_t>(ftell(f)));
  fseek(f, 0, SEEK_SET);
  if (fread(v.data(), 1, v.size(), f) != v.size()) v.clear();
  fclose(f);
  return v;
}

void savePng(draw::Canvas& c, const std::string& path) {
  size_t len = 0;
  void* png = c.createPng(&len, 0, 0, c.width(), c.height());
  FILE* f = fopen(path.c_str(), "wb");
  if (f && png) fwrite(png, 1, len, f);
  if (f) fclose(f);
  free(png);
  printf("wrote %s\n", path.c_str());
}

}  // namespace

int main(int argc, char** argv) {
  const std::string out = argc > 1 ? argv[1] : ".";
  const std::string here = argc > 2 ? argv[2] : "tools/preview";
  fonts::begin();
  PosixTz tz(kParis);

  static AppSnapshot snap;
  snap.sys.wifi = true;
  snap.sys.rssi = -58;
  snprintf(snap.sys.ssid, sizeof(snap.sys.ssid), "Freebox-Bureau");
  snprintf(snap.sys.ip, sizeof(snap.sys.ip), "192.168.1.42");
  snap.sys.timeValid = true;
  snap.sys.battery = 82;
  snap.sys.batteryVolts = 4.06f;
  snap.sys.usbPower = true;
  snap.sys.freeHeap = 148 * 1024;
  snap.sys.freePsram = 6900 * 1024;
  snap.sys.uptimeS = 3 * 3600 + 12 * 60;
  snprintf(snap.sys.resetReason, sizeof(snap.sys.resetReason), "mise sous tension");

  PlaybackState& p = snap.play;
  p.status = SpotifyStatus::Ok;
  p.hasItem = true;
  p.isPlaying = true;
  p.liked = true;
  p.supportsVolume = true;
  p.volume = 60;
  snprintf(p.title, sizeof(p.title), "Nightcall (feat. Lovefoxxx)");
  snprintf(p.artist, sizeof(p.artist), "Kavinsky, Lovefoxxx");
  snprintf(p.album, sizeof(p.album), "OutRun");
  snprintf(p.device, sizeof(p.device), "MacBook Pro de Paul");
  p.durationMs = 258000;
  p.progressMs = 83000;
  p.sampledAtMs = 1000;

  CalendarState& cal = snap.cal;
  cal.numCalendars = 2;
  snprintf(cal.calendars[0].name, sizeof(cal.calendars[0].name), "Travail");
  cal.calendars[0].color = 0x5B8DEF;
  snprintf(cal.calendars[1].name, sizeof(cal.calendars[1].name), "Perso");
  cal.calendars[1].color = 0x34D399;
  addEvent(cal, tz, 20, 0, 0, 0, 0, "Anniversaire de Camille", "", "", 1, true);
  addEvent(cal, tz, 20, 9, 30, 9, 45, "Stand-up équipe", "", "https://meet.google.com/abc-defg-hij", 0);
  addEvent(cal, tz, 20, 11, 0, 12, 0, "Revue de sprint", "Salle Eiffel", "", 0);
  addEvent(cal, tz, 20, 14, 45, 15, 30, "Point hebdo avec Léa", "", "https://meet.google.com/xyz-abcd-efg", 0);
  addEvent(cal, tz, 20, 16, 0, 17, 0, "Revue budget Q4", "Bât. A, 3e étage", "", 0);
  addEvent(cal, tz, 20, 18, 30, 19, 30, "Yoga", "Studio Bastille", "", 1);
  addEvent(cal, tz, 21, 9, 0, 10, 0, "Entretien candidat", "", "https://zoom.us/j/123456789", 0);
  cal.updatedAt = at(tz, 2026, 10, 20, 14, 20);

  WeatherState& w = snap.weather;
  w.valid = true;
  w.temp = 14.2f;
  w.code = 2;
  w.isDay = true;
  w.tmax = 17;
  w.tmin = 9;
  w.hourStart = at(tz, 2026, 10, 20, 14, 0);
  for (int i = 0; i < 12; ++i) w.precip[i] = i == 3 ? 60 : 10;
  w.updatedAt = at(tz, 2026, 10, 20, 14, 10);
  snprintf(w.place, sizeof(w.place), "Paris");

  UiSettings settings;
  HomeApp home(settings);
  MusicApp music;
  AgendaApp agenda;
  FocusApp focus(settings);
  SystemApp system;
  AlertApp alert;
  system.setVersion("v1.0.0");
  system.setBrightnessPercent(70);
  Shell shell(settings);
  shell.add(&home);
  shell.add(&music);
  shell.add(&agenda);
  shell.add(&focus);
  shell.add(&system);
  shell.setRoles(&home, &music, &focus);
  shell.setAlert(&alert);

  std::vector<uint8_t> art = readFile((here + "/sample_art.jpg").c_str());
  music.setArt(art.data(), art.size());

  draw::Canvas canvas;
  chrome::createScreenCanvas(canvas);
  NullActions actions;

  Frame f;
  f.s = &snap;
  f.tz = &tz;
  f.now = at(tz, 2026, 10, 20, 14, 32);
  f.ms = 1000;
  f.lt = fmt::localTime(tz, f.now);

  const char* names[] = {"home", "music", "agenda", "focus", "system"};
  for (int i = 0; i < shell.count(); ++i) {
    shell.go(i, 1, f);
    if (i == 3) {
      // Focus: running for 6 minutes, one session already done.
      focus.onButton(1, false, f, actions);
      Frame later = f;
      later.ms += 6 * 60 * 1000 + 47000;
      later.now += 6 * 60 + 47;
      later.lt = fmt::localTime(tz, later.now);
      shell.render(canvas, later);
    } else {
      shell.render(canvas, f);
    }
    savePng(canvas, out + "/" + names[i] + ".png");
  }

  // Meeting reminder
  {
    Frame g = f;
    g.now = at(tz, 2026, 10, 20, 14, 40);
    g.lt = fmt::localTime(tz, g.now);
    shell.go(0, -1, g);
    shell.tick(g, actions);
    shell.render(canvas, g);
    savePng(canvas, out + "/alert.png");
    alert.dismiss();
  }

  // Music: authorization needed
  {
    snap.play.status = SpotifyStatus::NeedsAuth;
    shell.go(1, 1, f);
    shell.render(canvas, f);
    savePng(canvas, out + "/music_auth.png");
    snap.play.status = SpotifyStatus::Ok;
  }

  // Agenda: tomorrow
  {
    shell.go(2, 1, f);
    agenda.onButton(2, false, f, actions);
    shell.render(canvas, f);
    savePng(canvas, out + "/agenda_tomorrow.png");
  }
  return 0;
}
