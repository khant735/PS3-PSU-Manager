#!/usr/bin/env python3
"""Enrich missing PS2 release dates from an explicitly supplied, reviewed TSV.

Input TSV columns: serial, release_date, region, source_url.
Only exact serial matches are accepted. Never overwrite a dated record.
No web scraping runs in the PS3 build; research/export source data separately.
"""
import argparse
import csv
import datetime
import re
from pathlib import Path

SERIAL = re.compile(r"[A-Z]{4}-[0-9]{5}\Z")
DATE = re.compile(r"([0-9]{4})(?:-([0-9]{2})(?:-([0-9]{2}))?)?\Z")


def precision(value):
    match = DATE.fullmatch(value)
    if not match:
        return None
    year, month, day = match.groups()
    try:
        if day:
            datetime.date(int(year), int(month), int(day))
            return "day"
        if month:
            datetime.date(int(year), int(month), 1)
            return "month"
        datetime.date(int(year), 1, 1)
        return "year"
    except ValueError:
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("database", type=Path)
    parser.add_argument("candidates", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--review", type=Path, help="Write rejected/conflicting rows")
    args = parser.parse_args()
    lines = args.database.read_text(encoding="utf-8").splitlines()
    positions = {}
    rows = []
    for line in lines:
        fields = line.split("\t")
        if len(fields) == 8 and SERIAL.fullmatch(fields[0]):
            positions[fields[0]] = len(rows)
        rows.append(fields)

    rejected = []
    inserted = 0
    with args.candidates.open(encoding="utf-8", newline="") as handle:
        candidates = list(csv.DictReader(handle, delimiter="\t"))
    versions = {}
    for item in candidates:
        key = (item.get("serial") or "").strip().upper()
        value = ((item.get("release_date") or "").strip(),
                 (item.get("region") or "").strip().casefold())
        versions.setdefault(key, set()).add(value)
    ambiguous = {key for key, values in versions.items() if len(values) > 1}
    for item in candidates:
            serial = (item.get("serial") or "").strip().upper()
            date = (item.get("release_date") or "").strip()
            region = (item.get("region") or "").strip()
            url = (item.get("source_url") or "").strip()
            reason = None
            if serial in ambiguous:
                reason = "conflicting candidates for serial"
            elif not SERIAL.fullmatch(serial) or serial not in positions:
                reason = "unknown serial"
            elif not precision(date):
                reason = "invalid date"
            elif not url.startswith(("https://", "http://")) or any(c in url for c in "\t\r\n"):
                reason = "missing/invalid provenance URL"
            elif any(c in region for c in "\t\r\n"):
                reason = "invalid region"
            else:
                row = rows[positions[serial]]
                if row[5]:
                    reason = "existing date protected"
                elif region and row[2] and region.casefold() != row[2].casefold():
                    reason = "region mismatch (manual review required)"
                else:
                    row[5] = date
                    row[6] = precision(date)
                    row[7] = url
                    if region:
                        row[2] = region
                    inserted += 1
            if reason:
                rejected.append((serial, date, region, url, reason))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join("\t".join(row) for row in rows) + "\n", encoding="utf-8")
    if args.review:
        with args.review.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.writer(handle, delimiter="\t")
            writer.writerow(["serial", "release_date", "region", "source_url", "reason"])
            writer.writerows(rejected)
    print(f"Added {inserted} sourced dates; {len(rejected)} candidates require review")


if __name__ == "__main__":
    main()
