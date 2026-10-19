#!/usr/bin/env python3
import os
import pathlib
import stat
import subprocess
import sys
import zipfile
import io


driver = pathlib.Path(sys.argv[1])
root = pathlib.Path(sys.argv[2])


def run(mode, archive, destination, expected):
    result = subprocess.run(
        [str(driver), mode, str(archive), str(destination)],
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == expected, (result.returncode, result.stdout, result.stderr)
    return result.stdout


def reset(path):
    if path.exists():
        import shutil
        shutil.rmtree(path)
    path.mkdir(parents=True)


case = root / "zip-good"
reset(case)
archive = case / "assets.zip"
destination = case / "SAVEDATA"
destination.mkdir()
with zipfile.ZipFile(archive, "w") as zf:
    zf.writestr("models/", "")
    zf.writestr("models/readme.txt", "asset pack\n", compress_type=zipfile.ZIP_DEFLATED)
    zf.writestr("models/raw.bin", bytes(range(256)), compress_type=zipfile.ZIP_STORED)

# Preflight is read-only; extraction keeps the ZIP and its internal folders.
assert "files=2" in run("check", archive, destination, 0)
assert not (destination / "models").exists()
run("extract", archive, destination, 0)
assert (destination / "models/readme.txt").read_text() == "asset pack\n"
assert (destination / "models/raw.bin").read_bytes() == bytes(range(256))
assert archive.is_file()

class ForwardOnly(io.BytesIO):
    def seekable(self):
        return False

    def seek(self, *args, **kwargs):
        raise io.UnsupportedOperation("stream is not seekable")


streamed = case / "streamed.zip"
sink = ForwardOnly()
with zipfile.ZipFile(sink, "w", compression=zipfile.ZIP_DEFLATED) as zf:
    zf.writestr("streamed/chunk.bin", b"streaming descriptor" * 400)
streamed.write_bytes(sink.getvalue())
stream_out = case / "streamed-out"
stream_out.mkdir()
run("extract", streamed, stream_out, 0)
assert (stream_out / "streamed/chunk.bin").read_bytes() == b"streaming descriptor" * 400

# Existing files require an explicit overwrite choice and remain untouched until then.
(destination / "models/readme.txt").write_text("keep me")
assert "conflicts=2" in run("check", archive, destination, 2)
assert (destination / "models/readme.txt").read_text() == "keep me"
run("overwrite", archive, destination, 0)
assert (destination / "models/readme.txt").read_text() == "asset pack\n"

# Traversal and absolute paths must fail without creating files outside the target.
for i, unsafe_name in enumerate(("../escaped.txt", "/absolute.txt", "..\\escaped-backslash.txt", "C:/drive.txt")):
    unsafe_zip = case / f"unsafe-{i}.zip"
    with zipfile.ZipFile(unsafe_zip, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr(unsafe_name, "bad")
    out = case / f"out-{i}"
    assert run("extract", unsafe_zip, out, 1)
    assert not (case / "escaped.txt").exists()
    assert not (case / "escaped-backslash.txt").exists()
    assert not (case / "absolute.txt").exists()

# Do not materialize links or methods the PS3 extractor does not support.
link_zip = case / "symlink.zip"
link = zipfile.ZipInfo("link")
link.external_attr = (stat.S_IFLNK | 0o777) << 16
with zipfile.ZipFile(link_zip, "w") as zf:
    zf.writestr(link, "target.txt")
assert run("extract", link_zip, case / "symlink-out", 1)

bzip_zip = case / "bzip.zip"
with zipfile.ZipFile(bzip_zip, "w") as zf:
    zf.writestr("unsupported.txt", "data", compress_type=zipfile.ZIP_BZIP2)
assert run("extract", bzip_zip, case / "bzip-out", 1)

# Files must not collide with an ancestor path or replace the downloaded ZIP itself.
conflict_zip = case / "file-folder-conflict.zip"
with zipfile.ZipFile(conflict_zip, "w") as zf:
    zf.writestr("same", "file")
    zf.writestr("same/child.txt", "child")
assert run("extract", conflict_zip, case / "path-conflict-out", 1)

self_zip = case / "self.zip"
with zipfile.ZipFile(self_zip, "w") as zf:
    zf.writestr("self.zip", "must not replace source")
before = self_zip.read_bytes()
assert run("extract", self_zip, case, 1)
assert self_zip.read_bytes() == before

# Corrupt compressed data/CRC cannot leave a partial destination file behind.
bad_zip = case / "bad-crc.zip"
with zipfile.ZipFile(bad_zip, "w", zipfile.ZIP_DEFLATED) as zf:
    zf.writestr("nested/corrupt.bin", b"payload" * 100)
data = bytearray(bad_zip.read_bytes())
with zipfile.ZipFile(bad_zip) as zf:
    info = zf.getinfo("nested/corrupt.bin")
    name_len = int.from_bytes(data[info.header_offset + 26:info.header_offset + 28], "little")
    extra_len = int.from_bytes(data[info.header_offset + 28:info.header_offset + 30], "little")
    payload = info.header_offset + 30 + name_len + extra_len
data[payload + info.compress_size // 2] ^= 0x40
bad_zip.write_bytes(data)
bad_out = case / "bad-out"
assert run("extract", bad_zip, bad_out, 1)
assert not (bad_out / "nested/corrupt.bin").exists()

print("ZIP archive tests passed")
