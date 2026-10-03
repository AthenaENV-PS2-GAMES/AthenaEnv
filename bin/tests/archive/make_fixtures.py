#!/usr/bin/env python3
"""Regenerates the fixtures used by bin/tests/archive_test.js."""
import gzip
import io
import os
import tarfile
import zipfile

os.chdir(os.path.dirname(os.path.abspath(__file__)))

HELLO = b"Hello from AthenaEnv Archive!\n"
NESTED = b"nested file content\n" * 3
FIXED_TIME = 1700000000
ZIP_TIME = (2023, 11, 14, 22, 13, 20)
# Longer than the 100-byte tar name field and than the USTAR prefix split.
LONG_NAME = "long/" + "x" * 120 + ".txt"
# Compresses to a few KiB but expands past the 16 MiB default read limit.
BOMB_SIZE = 20 * 1024 * 1024


def zip_entry(z, name, data):
    info = zipfile.ZipInfo(name, date_time=ZIP_TIME)
    info.compress_type = zipfile.ZIP_DEFLATED
    z.writestr(info, data)


def write_gzip(path, inner_name, data):
    with open(path, "wb") as raw:
        with gzip.GzipFile(filename=inner_name, fileobj=raw, mode="wb", mtime=FIXED_TIME) as g:
            g.write(data)


def make_tar(entries, fmt=tarfile.USTAR_FORMAT, dirs=()):
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w", format=fmt) as t:
        for name, data in entries:
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mtime = FIXED_TIME
            t.addfile(info, io.BytesIO(data))
        for name in dirs:
            info = tarfile.TarInfo(name)
            info.type = tarfile.DIRTYPE
            info.mtime = FIXED_TIME
            t.addfile(info)
    return buffer.getvalue()


# --- zip ---------------------------------------------------------------------
with zipfile.ZipFile("sample.zip", "w") as z:
    zip_entry(z, "readme.txt", HELLO)
    zip_entry(z, "data/", b"")
    zip_entry(z, "data/nested.txt", NESTED)

with zipfile.ZipFile("evil.zip", "w") as z:
    zip_entry(z, "ok.txt", b"ok")
    zip_entry(z, "../escape.txt", b"bad")

with zipfile.ZipFile("bomb.zip", "w") as z:
    zip_entry(z, "zeros.bin", bytes(BOMB_SIZE))

# Only the "encrypted" flag is set: enough for the reader to refuse the entry.
# zipfile clears that bit on write, so it is patched into both headers.
with zipfile.ZipFile("encrypted.zip", "w") as z:
    zip_entry(z, "secret.txt", b"secret")
with open("encrypted.zip", "r+b") as f:
    raw = bytearray(f.read())
    raw[6] |= 0x1                                   # local file header flags
    central = raw.index(b"PK\x01\x02")
    raw[central + 8] |= 0x1                         # central directory flags
    f.seek(0)
    f.write(raw)

# bzip2 (method 12): valid zip, but not a method zlib can decompress.
with zipfile.ZipFile("bzip2.zip", "w") as z:
    info = zipfile.ZipInfo("packed.txt", date_time=ZIP_TIME)
    info.compress_type = zipfile.ZIP_BZIP2
    z.writestr(info, HELLO)

# --- gzip --------------------------------------------------------------------
write_gzip("sample.txt.gz", "sample.txt", HELLO)
write_gzip("bomb.bin.gz", "bomb.bin", bytes(BOMB_SIZE))

# --- tar ---------------------------------------------------------------------
sample_tar = make_tar(
    [("readme.txt", HELLO), ("data/nested.txt", NESTED)], dirs=("empty_dir",))
with open("sample.tar", "wb") as f:
    f.write(sample_tar)
write_gzip("sample.tar.gz", "sample.tar", sample_tar)

# First entry is fine, the second escapes: nothing may be written.
with open("evil.tar", "wb") as f:
    f.write(make_tar([("ok.txt", b"ok"), ("../escape.txt", b"bad")]))

# Long names in the three encodings: USTAR prefix, GNU 'L' records, pax 'path'.
USTAR_NAME = "a" * 60 + "/" + "b" * 60 + ".txt"
with open("longnames.tar", "wb") as f:
    f.write(make_tar([(USTAR_NAME, b"ustar")]))
with open("longnames_gnu.tar", "wb") as f:
    f.write(make_tar([(LONG_NAME, b"gnu")], fmt=tarfile.GNU_FORMAT))
with open("longnames_pax.tar", "wb") as f:
    f.write(make_tar([(LONG_NAME, b"pax")], fmt=tarfile.PAX_FORMAT))

with open("plain.txt", "wb") as f:
    f.write(b"not an archive\n")
