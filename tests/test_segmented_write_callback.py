#!/usr/bin/env python3
"""Host regression test for the production segmented libcurl write callback.

Extracts the actual callback body from source/main.cpp, compiles it with
minimal stand-ins, and verifies that it returns *bytes* for size > 1.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "source/main.cpp").read_text(encoding="utf-8")
start = source.index("static size_t segment_write_cb(")
opening = source.index("{", start)
depth = 0
end = None
for i in range(opening, len(source)):
    if source[i] == "{":
        depth += 1
    elif source[i] == "}":
        depth -= 1
        if depth == 0:
            end = i + 1
            break
assert end is not None
callback = source[start:end]
harness = r"""
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
typedef long long curl_off_t;
typedef struct { FILE *f; curl_off_t got,start,end,file_offset; int owner,slot; char path[1024]; void *easy; curl_off_t *aggregate; } dl_segment_t;
""" + callback + r"""
int main() {
    FILE *f = tmpfile();
    if (!f) return 10;
    curl_off_t aggregate = 0;
    dl_segment_t segment = {};
    segment.f = f;
    segment.aggregate = &aggregate;
    char payload[24];
    memset(payload, 'A', sizeof(payload));
    size_t n = segment_write_cb(payload, 4, 6, &segment);
    if (n != 24 || segment.got != 24 || aggregate != 24) return 11;
    if (ftell(f) != 24) return 12;
    if (segment_write_cb(payload, 0, 6, &segment) != 0) return 13;
    fclose(f);
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="ps3psu-curltest-") as td:
    cpp = Path(td) / "test.cpp"
    exe = Path(td) / "test"
    cpp.write_text(harness, encoding="utf-8")
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: production segmented callback returns byte count and updates totals")
