#!/usr/bin/env python3
"""Extract conservative serial/date candidates from a saved PCSX2 Wiki games HTML page.

Requires beautifulsoup4: python -m pip install beautifulsoup4
Save https://wiki.pcsx2.net/List_of_games as HTML before running.
Only one-serial/one-date regional rows are emitted. All ambiguous rows are
written to a review TSV, never silently assigned a release date.
"""
import argparse
import csv
import datetime
import re
from pathlib import Path

SERIAL = re.compile(r"\b(?:SCES|SCUS|SCPS|SLUS|SLES|SLPS|SLPM|SCAJ|SCKA|SLKA|SCCS|SLAJ)-\d{5}\b", re.I)
MONTHS = r"January|February|March|April|May|June|July|August|September|October|November|December"
DATE = re.compile(rf"\b({MONTHS})\s+(\d{{1,2}}),\s+(\d{{4}})\b", re.I)
REGION = re.compile(r"^(PAL|NTSC-U|NTSC-J)\b", re.I)
SOURCE = "https://wiki.pcsx2.net/List_of_games"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("html", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--review", type=Path, required=True)
    args = ap.parse_args()
    try:
        from bs4 import BeautifulSoup
    except ImportError as exc:
        raise SystemExit("Install beautifulsoup4 first: python -m pip install beautifulsoup4") from exc
    soup = BeautifulSoup(args.html.read_text(encoding="utf-8"), "html.parser")
    table = next((t for t in soup.find_all("table") if
                  "serial number" in t.get_text(" ", strip=True).lower() and
                  "release date" in t.get_text(" ", strip=True).lower()), None)
    if table is None:
        raise SystemExit("Could not identify PCSX2 game list table; no output generated")
    candidates, review = {}, []
    current_title = ""
    current_region = ""
    for tr in table.find_all("tr"):
        cells = tr.find_all(["th", "td"], recursive=False)
        if len(cells) < 3:
            continue
        values = [c.get_text(" ", strip=True) for c in cells]
        if any("serial number" in v.lower() for v in values):
            continue
        # Main rows have 6 columns; continuation regional rows have 3.
        if len(values) >= 6:
            current_title = values[0]
            region_text, serial_text, date_text = values[-3:]
        elif len(values) == 3:
            region_text, serial_text, date_text = values
        else:
            continue
        if REGION.match(region_text):
            current_region = REGION.match(region_text).group(1).upper()
        else:
            continue
        serials = list(dict.fromkeys(s.upper() for s in SERIAL.findall(serial_text)))
        dates = DATE.findall(date_text)
        reason = ""
        if len(serials) != 1:
            reason = "multiple or missing serials"
        elif len(dates) != 1 or ";;" in date_text:
            reason = "multiple or missing release dates"
        elif re.search(r"\b(demo|trial|prototype|pre-release|cancelled|unconfirmed)\b", serial_text + " " + date_text, re.I):
            reason = "non-retail or unconfirmed"
        if not reason:
            month, day, year = dates[0]
            try:
                parsed = datetime.datetime.strptime(f"{month} {day} {year}", "%B %d %Y").date()
                iso = parsed.isoformat()
            except ValueError:
                reason = "invalid date"
        if reason:
            review.append([current_title, current_region, serial_text, date_text, reason])
            continue
        serial = serials[0]
        candidate = [serial, iso, current_region, SOURCE]
        if serial in candidates and candidates[serial] != candidate:
            review.append([current_title, current_region, serial_text, date_text, "conflicting serial dates"])
            del candidates[serial]
            continue
        candidates[serial] = candidate
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f, delimiter="\t")
        writer.writerow(["serial", "release_date", "region", "source_url"])
        writer.writerows(candidates[k] for k in sorted(candidates))
    with args.review.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f, delimiter="\t")
        writer.writerow(["title", "region", "serials", "dates", "reason"])
        writer.writerows(review)
    print(f"Conservative candidates: {len(candidates)}; rows needing review: {len(review)}")


if __name__ == "__main__":
    main()
