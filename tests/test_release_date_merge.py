#!/usr/bin/env python3
"""Offline regression tests for the PS2 release-date candidate merger."""
import csv
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as folder:
    p = Path(folder)
    db, candidates, out, review = (p / name for name in ("db.tsv", "candidates.tsv", "out.tsv", "review.tsv"))
    db.write_text(
        "# serial\ttitle\tregion\tedition\trevision\trelease_date\tdate_precision\tsource\n"
        "SLUS-12345\tExample\tNTSC-U\t\t\t\tunknown\tGameDB\n"
        "SLES-54321\tExample EU\tPAL\t\t\t2001-01-01\tday\tbaseline\n"
        "SCES-12345\tExample EU 2\tPAL\t\t\t\tunknown\tGameDB\n",
        encoding="utf-8",
    )
    with candidates.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, delimiter="\t")
        writer.writerow(("serial", "release_date", "region", "source_url"))
        writer.writerows([
            ("SLUS-12345", "2002-01-01", "NTSC-U", "https://example.org/a"),
            ("SLUS-12345", "2003-01-01", "NTSC-U", "https://example.org/b"),
            ("SLES-54321", "2002-01-01", "PAL", "https://example.org/c"),
            ("SCES-12345", "2004-04-04", "PAL", "https://example.org/d"),
        ])
    subprocess.run([
        sys.executable, str(ROOT / "tools/merge_ps2_release_dates.py"),
        str(db), str(candidates), "--output", str(out), "--review", str(review)
    ], check=True)
    merged = out.read_text(encoding="utf-8")
    rejected = review.read_text(encoding="utf-8")
    assert "SLUS-12345\tExample\tNTSC-U\t\t\t\tunknown" in merged, merged
    assert "SLES-54321\tExample EU\tPAL\t\t\t2001-01-01" in merged, merged
    assert "SCES-12345\tExample EU 2\tPAL\t\t\t2004-04-04\tday" in merged, merged
    assert rejected.count("conflicting candidates for serial") == 2, rejected
    assert "existing date protected" in rejected, rejected
print("PS2 release-date merge regression: PASS")
