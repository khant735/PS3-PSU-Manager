#!/usr/bin/env python3
"""Offline Stage 6V frame-pacing triage; Python 3 standard library only.
Usage: python3 tools/stage6v_run59_triage.py resultz.zip
Produces run59_frame_triage.csv and run59_frame_triage.txt alongside the ZIP.
Read-only analysis: does not modify PS3 render state or claim a fix.
"""
import csv
import io
import pathlib
import statistics
import sys
import zipfile

SCENES = (
    "p15_shell_no_rows", "p15_background_text", "p15_icons_text",
    "p15_combined_rows", "p15_full_browser", "p15_reference",
)
BUDGET_US = 16667

def percentile(values, fraction):
    if not values:
        return 0
    values = sorted(values)
    return values[min(len(values) - 1, int((len(values) - 1) * fraction))]

def analyze(zf, scene):
    with zf.open(scene + ".csv") as raw:
        rows = list(csv.DictReader(io.TextIOWrapper(raw, encoding="utf-8-sig")))
    # First row is the scene transition and has no valid preceding interval.
    valid = [r for r in rows if int(r["interval_us"]) > 0]
    if not valid:
        raise ValueError(scene + ": no valid frame intervals")
    val = lambda r, k: int(r[k])
    intervals = [val(r, "interval_us") for r in valid]
    draws = [val(r, "draw_us") for r in valid]
    flips = [val(r, "flip_us") for r in valid]
    over = sum(x > BUDGET_US for x in intervals)
    draw_over = sum(x > BUDGET_US for x in draws)
    flip_over = sum(x > BUDGET_US for x in flips)
    fifo = sum(r["pre_get"] != r["pre_put"] for r in valid)
    missed = sum(val(r, "vblank_delta") > 1 for r in valid)
    n = len(valid)
    return {
        "scene": scene, "frames": n,
        "mean_interval_us": round(statistics.mean(intervals)),
        "p95_interval_us": percentile(intervals, .95),
        "p99_interval_us": percentile(intervals, .99),
        "max_interval_us": max(intervals),
        "over_16667": over, "over_pct": round(100 * over / n, 2),
        "draw_over": draw_over, "flip_over": flip_over,
        "mean_draw_us": round(statistics.mean(draws)),
        "mean_flip_us": round(statistics.mean(flips)),
        "fifo_pending": fifo, "multi_vblank": missed,
        "mean_poll_us": round(statistics.mean(val(r, "poll_us") for r in valid)),
        "mean_queue_us": round(statistics.mean(val(r, "queue_us") for r in valid)),
        "mean_list_us": round(statistics.mean(val(r, "avg_list_us") for r in valid)),
        "mean_bg_us": round(statistics.mean(val(r, "avg_bg_us") for r in valid)),
    }

def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: stage6v_run59_triage.py resultz.zip")
    archive = pathlib.Path(sys.argv[1])
    with zipfile.ZipFile(archive) as zf:
        results = [analyze(zf, scene) for scene in SCENES]
    csv_path = archive.with_name("run59_frame_triage.csv")
    txt_path = archive.with_name("run59_frame_triage.txt")
    with csv_path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fieldnames=list(results[0]))
        writer.writeheader()
        writer.writerows(results)
    with txt_path.open("w", encoding="utf-8") as out:
        out.write("Stage 6V Run 59 offline triage of Run 58 hardware logs\n")
        out.write("16,667 us threshold; frame intervals are measured, not inferred from FPS.\n")
        out.write("Draw and flip counters may overlap with waiting and must not be summed.\n\n")
        for r in results:
            out.write(f"{r['scene']}: {r['over_pct']}% over budget; "
                      f"p99={r['p99_interval_us']}us, max={r['max_interval_us']}us; "
                      f"draw-over={r['draw_over']}, flip-over={r['flip_over']}, "
                      f"FIFO-pending={r['fifo_pending']}, multi-vblank={r['multi_vblank']}\n")
        out.write("\nThese observations do not prove the root cause or a 60 FPS fix.\n")
    print(csv_path)
    print(txt_path)

if __name__ == "__main__":
    main()
