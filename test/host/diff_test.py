#!/usr/bin/env python3
"""Differential test: compares IcsParser (via ./ics_dump) with the python
reference implementation `recurring-ical-events` on randomly generated
recurring events.

    pip install recurring-ical-events
    make -C test/host ics_dump && python3 test/host/diff_test.py [iterations] [seed]
"""
import datetime as dt
import os
import random
import subprocess
import sys
import tempfile
from zoneinfo import ZoneInfo

import icalendar
import recurring_ical_events
from dateutil.rrule import rrulestr

HERE = os.path.dirname(os.path.abspath(__file__))
DUMP = os.path.join(HERE, "ics_dump")
PARIS = ZoneInfo("Europe/Paris")
UTC = dt.timezone.utc
DAYS = ["MO", "TU", "WE", "TH", "FR", "SA", "SU"]


def rand_rule(rng, all_day):
    freq = rng.choice(["DAILY", "WEEKLY", "MONTHLY", "YEARLY"])
    parts = [f"FREQ={freq}"]
    if rng.random() < 0.5:
        parts.append(f"INTERVAL={rng.randint(2, 4)}")
    if freq == "DAILY" and rng.random() < 0.3:
        parts.append("BYDAY=MO,TU,WE,TH,FR")
    if freq == "WEEKLY":
        if rng.random() < 0.7:
            parts.append("BYDAY=" + ",".join(sorted(rng.sample(DAYS, rng.randint(1, 3)), key=DAYS.index)))
        if rng.random() < 0.3:
            parts.append("WKST=" + rng.choice(["SU", "MO", "SA"]))
    if freq == "MONTHLY":
        k = rng.random()
        if k < 0.3:
            parts.append("BYMONTHDAY=" + ",".join(str(x) for x in rng.sample([1, 5, 15, 28, 29, 30, 31, -1, -2], rng.randint(1, 2))))
        elif k < 0.6:
            parts.append(f"BYDAY={rng.choice([1, 2, 3, 4, -1, -2])}{rng.choice(DAYS)}")
        elif k < 0.8:
            parts.append("BYDAY=MO,TU,WE,TH,FR;BYSETPOS=" + rng.choice(["1", "-1", "2"]))
    if freq == "YEARLY":
        k = rng.random()
        if k < 0.3:
            parts.append(f"BYMONTH={rng.randint(1, 12)};BYDAY={rng.choice([1, 2, 4, -1])}{rng.choice(DAYS)}")
        elif k < 0.5:
            parts.append(f"BYMONTH={rng.randint(1, 12)},{rng.randint(1, 12)};BYMONTHDAY={rng.randint(1, 28)}")
    t = rng.random()
    if t < 0.3:
        parts.append(f"COUNT={rng.randint(1, 300)}")
    elif t < 0.55:
        until = dt.datetime(2026, 1, 1, tzinfo=UTC) + dt.timedelta(days=rng.randint(0, 700), hours=rng.randint(0, 23))
        parts.append("UNTIL=" + (until.strftime("%Y%m%d") if all_day else until.strftime("%Y%m%dT%H%M%SZ")))
    return ";".join(parts)


def make_event(rng, idx):
    kind = rng.choice(["utc", "paris", "ny", "floating", "date"])
    all_day = kind == "date"
    base = dt.datetime(2024, 1, 1) + dt.timedelta(days=rng.randint(0, 1050))
    base = base.replace(hour=rng.randint(0, 23), minute=rng.choice([0, 15, 30, 45]))
    rule = rand_rule(rng, all_day)
    # Synchronise DTSTART with the rule (first real occurrence).
    probe = rrulestr("RRULE:" + rule, dtstart=base, ignoretz=True)
    first = next(iter(probe), None) if "COUNT" not in rule and "UNTIL" not in rule else probe.after(base, inc=True)
    if first is None:
        first = base
    lines = ["BEGIN:VEVENT", f"UID:ev{idx}@diff", f"SUMMARY:ev{idx}"]
    if all_day:
        days = rng.randint(1, 3)
        lines.append(f"DTSTART;VALUE=DATE:{first:%Y%m%d}")
        lines.append(f"DTEND;VALUE=DATE:{(first + dt.timedelta(days=days)):%Y%m%d}")
    else:
        dur = rng.choice([15, 30, 45, 60, 90, 180])
        fmt = "%Y%m%dT%H%M%S"
        if kind == "utc":
            lines.append(f"DTSTART:{first.strftime(fmt)}Z")
        elif kind == "paris":
            lines.append(f"DTSTART;TZID=Europe/Paris:{first.strftime(fmt)}")
        elif kind == "ny":
            lines.append(f"DTSTART;TZID=America/New_York:{first.strftime(fmt)}")
        else:
            lines.append(f"DTSTART:{first.strftime(fmt)}")
        lines.append(f"DURATION:PT{dur}M")
    lines.append("RRULE:" + rule)
    if rng.random() < 0.3:
        # Exclude one real instance.
        occ = list(rrulestr("RRULE:" + rule, dtstart=first, ignoretz=True).between(
            dt.datetime(2026, 1, 1), dt.datetime(2027, 12, 31)))[:40]
        if occ:
            ex = rng.choice(occ)
            if all_day:
                lines.append(f"EXDATE;VALUE=DATE:{ex:%Y%m%d}")
            elif kind == "utc":
                lines.append(f"EXDATE:{ex:%Y%m%dT%H%M%S}Z")
            elif kind == "paris":
                lines.append(f"EXDATE;TZID=Europe/Paris:{ex:%Y%m%dT%H%M%S}")
            elif kind == "ny":
                lines.append(f"EXDATE;TZID=America/New_York:{ex:%Y%m%dT%H%M%S}")
            else:
                lines.append(f"EXDATE:{ex:%Y%m%dT%H%M%S}")
    lines.append("END:VEVENT")
    return "\r\n".join(lines)


def to_utc(v):
    if isinstance(v, dt.datetime):
        if v.tzinfo is None:
            v = v.replace(tzinfo=PARIS)
        return int(v.timestamp())
    return int(dt.datetime(v.year, v.month, v.day, tzinfo=PARIS).timestamp())


def reference(ics_text, ws, we):
    cal = icalendar.Calendar.from_ical(ics_text)
    out = set()
    for ev in recurring_ical_events.of(cal).between(ws, we):
        s = ev["DTSTART"].dt
        e = ev["DTEND"].dt if "DTEND" in ev else (s + ev["DURATION"].dt if "DURATION" in ev else s)
        all_day = not isinstance(s, dt.datetime)
        out.add((to_utc(s), to_utc(e), int(all_day), str(ev["SUMMARY"])))
    return out


def ours(path, ws, we):
    res = subprocess.run([DUMP, path, str(int(ws.timestamp())), str(int(we.timestamp()))],
                         capture_output=True, text=True, check=True)
    out = set()
    for line in res.stdout.splitlines():
        s, e, a, title = line.split(" ", 3)
        out.add((int(s), int(e), int(a), title))
    return out


def main():
    iterations = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    rng = random.Random(int(sys.argv[2]) if len(sys.argv) > 2 else 1234)
    failures = 0
    total = 0
    for it in range(iterations):
        events = [make_event(rng, i) for i in range(6)]
        text = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:diff\r\n" + "\r\n".join(events) + "\r\nEND:VCALENDAR\r\n"
        start = dt.datetime(2026, 1, 1, 0, 7, tzinfo=PARIS) + dt.timedelta(days=rng.randint(0, 600))
        ws, we = start, start + dt.timedelta(days=9)
        with tempfile.NamedTemporaryFile("w", suffix=".ics", delete=False) as f:
            f.write(text)
            path = f.name
        try:
            ref = reference(text, ws, we)
            got = ours(path, ws, we)
        finally:
            os.unlink(path)
        total += len(ref)
        if ref != got:
            failures += 1
            if failures <= 5:
                print(f"--- mismatch in iteration {it} window {ws} .. {we}")
                for title in sorted({x[3] for x in ref ^ got}):
                    print("event:", next(e for e in events if f"SUMMARY:{title}\r" in e).replace("\r\n", " | "))
                    print("  ref :", sorted(x[:3] for x in ref if x[3] == title))
                    print("  ours:", sorted(x[:3] for x in got if x[3] == title))
    print(f"{iterations} calendars, {total} reference occurrences, {failures} mismatching calendars")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
