#!/usr/bin/env python3
"""End-to-end CalDAV test: starts a local Radicale server, creates calendars
the way iCloud exposes them (event calendars with colors, a VTODO-only
"Reminders" list), stores events, then checks that ./caldav_live discovers
the right calendars and returns the same occurrences as the python reference.

    pip install radicale recurring-ical-events
    make -C test/host caldav_live && python3 test/host/caldav_e2e.py
"""
import datetime as dt
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
from zoneinfo import ZoneInfo

import icalendar
import recurring_ical_events

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 5232
BASE = f"http://127.0.0.1:{PORT}"
USER, PASSWORD = "paul", "abcd-efgh-ijkl-mnop"
PARIS = ZoneInfo("Europe/Paris")

VTIMEZONE = """BEGIN:VTIMEZONE
TZID:Europe/Paris
BEGIN:DAYLIGHT
TZOFFSETFROM:+0100
TZOFFSETTO:+0200
TZNAME:CEST
DTSTART:19700329T020000
RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=-1SU
END:DAYLIGHT
BEGIN:STANDARD
TZOFFSETFROM:+0200
TZOFFSETTO:+0100
TZNAME:CET
DTSTART:19701025T030000
RRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU
END:STANDARD
END:VTIMEZONE"""


def vcal(body, tz=True):
    parts = ["BEGIN:VCALENDAR", "VERSION:2.0", "PRODID:-//test//caldav e2e//FR"]
    if tz:
        parts.append(VTIMEZONE)
    parts.append(body)
    parts.append("END:VCALENDAR")
    return "\r\n".join("\n".join(parts).splitlines()) + "\r\n"


RESOURCES = {
    "work/standup.ics": vcal("""BEGIN:VEVENT
UID:standup@test
DTSTAMP:20260101T000000Z
DTSTART;TZID=Europe/Paris:20260105T100000
DTEND;TZID=Europe/Paris:20260105T101500
RRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR
EXDATE;TZID=Europe/Paris:20261023T100000
SUMMARY:Stand-up équipe
END:VEVENT
BEGIN:VEVENT
UID:standup@test
DTSTAMP:20260101T000000Z
RECURRENCE-ID;TZID=Europe/Paris:20261021T100000
DTSTART;TZID=Europe/Paris:20261021T113000
DTEND;TZID=Europe/Paris:20261021T114500
SUMMARY:Stand-up équipe (décalé)
END:VEVENT"""),
    "work/budget.ics": vcal("""BEGIN:VEVENT
UID:budget@test
DTSTAMP:20260101T000000Z
DTSTART;TZID=Europe/Paris:20261020T160000
DTEND;TZID=Europe/Paris:20261020T170000
SUMMARY:Revue <budget> & Q4 — « important »
LOCATION:Bât. A
END:VEVENT"""),
    "work/later.ics": vcal("""BEGIN:VEVENT
UID:later@test
DTSTAMP:20260101T000000Z
DTSTART:20261130T090000Z
DTEND:20261130T100000Z
SUMMARY:Hors fenêtre
END:VEVENT""", tz=False),
    "perso/birthday.ics": vcal("""BEGIN:VEVENT
UID:bday@test
DTSTAMP:20260101T000000Z
DTSTART;VALUE=DATE:19901024
DTEND;VALUE=DATE:19901025
RRULE:FREQ=YEARLY
SUMMARY:Anniversaire de Léa
END:VEVENT""", tz=False),
    "perso/run.ics": vcal("""BEGIN:VEVENT
UID:run@test
DTSTAMP:20260101T000000Z
DTSTART:20260901T050000Z
DURATION:PT45M
RRULE:FREQ=DAILY;INTERVAL=2;COUNT=40
SUMMARY:Course
END:VEVENT""", tz=False),
    "tasks/todo.ics": vcal("""BEGIN:VTODO
UID:todo@test
DTSTAMP:20260101T000000Z
DUE:20261021T100000Z
SUMMARY:Acheter du café
END:VTODO""", tz=False),
}

CALENDARS = {
    "work": ("Travail &amp; projets", "#FF2968FF", "VEVENT"),
    "perso": ("Perso", "#34D399", "VEVENT"),
    "tasks": ("Rappels", "#FFCC00", "VTODO"),
}


def http(method, path, body=b"", headers=None):
    req = urllib.request.Request(BASE + path, data=body or None, method=method)
    import base64
    req.add_header("Authorization", "Basic " + base64.b64encode(f"{USER}:{PASSWORD}".encode()).decode())
    for k, v in (headers or {}).items():
        req.add_header(k, v)
    try:
        with urllib.request.urlopen(req) as r:
            return r.status
    except urllib.error.HTTPError as e:
        return e.code


def wait_port():
    for _ in range(100):
        try:
            socket.create_connection(("127.0.0.1", PORT), timeout=0.2).close()
            return
        except OSError:
            time.sleep(0.1)
    raise SystemExit("Radicale did not start")


def main():
    work = tempfile.mkdtemp()
    with open(os.path.join(work, "users"), "w") as f:
        f.write(f"{USER}:{PASSWORD}\n")
    conf = os.path.join(work, "config")
    with open(conf, "w") as f:
        f.write(f"""[server]
hosts = 127.0.0.1:{PORT}
[auth]
type = htpasswd
htpasswd_filename = {work}/users
htpasswd_encryption = plain
[storage]
filesystem_folder = {work}/collections
[rights]
type = owner_only
[logging]
level = warning
""")
    server = subprocess.Popen([sys.executable, "-m", "radicale", "--config", conf],
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        wait_port()
        http("PROPFIND", f"/{USER}/", headers={"Depth": "0"})  # creates the principal
        for name, (display, color, comp) in CALENDARS.items():
            body = f"""<?xml version="1.0" encoding="utf-8"?>
<C:mkcalendar xmlns:D="DAV:" xmlns:C="urn:ietf:params:xml:ns:caldav" xmlns:A="http://apple.com/ns/ical/">
<D:set><D:prop><D:displayname>{display}</D:displayname><A:calendar-color>{color}</A:calendar-color>
<C:supported-calendar-component-set><C:comp name="{comp}"/></C:supported-calendar-component-set>
</D:prop></D:set></C:mkcalendar>""".encode()
            st = http("MKCALENDAR", f"/{USER}/{name}/", body, {"Content-Type": "application/xml"})
            assert st == 201, (name, st)
        for path, text in RESOURCES.items():
            st = http("PUT", f"/{USER}/{path}", text.encode(), {"Content-Type": "text/calendar; charset=utf-8"})
            assert st in (201, 204), (path, st)

        ws = dt.datetime(2026, 10, 19, tzinfo=PARIS)
        we = dt.datetime(2026, 10, 26, tzinfo=PARIS)
        out = subprocess.run([os.path.join(HERE, "caldav_live"), BASE + "/.well-known/caldav", USER, PASSWORD,
                              str(int(ws.timestamp())), str(int(we.timestamp()))],
                             capture_output=True, text=True, check=False)
        print(out.stdout)
        if out.returncode != 0:
            print(out.stderr)
            return 1

        calendars = {}
        got = set()
        for line in out.stdout.splitlines():
            if line.startswith("CALENDAR "):
                name, color, has, url = line[9:].split("|")
                calendars[name] = (color, has, url)
            else:
                cal, s, e, a, title = line.split("|", 4)
                got.add((cal, int(s), int(e), int(a), title))

        expected_cals = {"Travail & projets": ("FF2968", "1"), "Perso": ("34D399", "1")}
        ok = {k: v[:2] for k, v in calendars.items()} == expected_cals

        want = set()
        names = {"work": "Travail & projets", "perso": "Perso"}
        for path, text in RESOURCES.items():
            folder = path.split("/")[0]
            if folder not in names:
                continue
            for ev in recurring_ical_events.of(icalendar.Calendar.from_ical(text)).between(ws, we):
                s = ev["DTSTART"].dt
                if "DTEND" in ev:
                    e = ev["DTEND"].dt
                else:
                    e = s + ev["DURATION"].dt
                allday = not isinstance(s, dt.datetime)
                conv = (lambda v: int(dt.datetime(v.year, v.month, v.day, tzinfo=PARIS).timestamp())) if allday \
                    else (lambda v: int(v.timestamp()))
                want.add((names[folder], conv(s), conv(e), int(allday), str(ev["SUMMARY"])))

        if got != want:
            ok = False
            print("missing:", sorted(want - got))
            print("unexpected:", sorted(got - want))
        print(f"calendars: {sorted(calendars)}  occurrences: {len(got)} (expected {len(want)})")
        print("OK" if ok else "FAIL")
        return 0 if ok else 1
    finally:
        server.terminate()
        server.wait(timeout=10)


if __name__ == "__main__":
    sys.exit(main())
