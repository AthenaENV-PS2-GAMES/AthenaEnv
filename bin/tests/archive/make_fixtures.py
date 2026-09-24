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

with zipfile.ZipFile("sample.zip", "w", zipfile.ZIP_DEFLATED) as z:
    for name, data in (("readme.txt", HELLO), ("data/", b""), ("data/nested.txt", NESTED)):
        info = zipfile.ZipInfo(name, date_time=(2023, 11, 14, 22, 13, 20))
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, data)

with zipfile.ZipFile("evil.zip", "w") as z:
    z.writestr(zipfile.ZipInfo("ok.txt", date_time=(2023, 11, 14, 22, 13, 20)), b"ok")
    z.writestr(zipfile.ZipInfo("../escape.txt", date_time=(2023, 11, 14, 22, 13, 20)), b"bad")

with open("sample.txt.gz", "wb") as raw:
    with gzip.GzipFile(filename="sample.txt", fileobj=raw, mode="wb", mtime=0) as g:
        g.write(HELLO)


def make_tar(path, mode):
    with tarfile.open(path, mode, format=tarfile.USTAR_FORMAT) as t:
        for name, data in (("readme.txt", HELLO), ("data/nested.txt", NESTED)):
            info = tarfile.TarInfo(name)
            info.size = len(data)
            info.mtime = FIXED_TIME
            t.addfile(info, io.BytesIO(data))
        info = tarfile.TarInfo("empty_dir")
        info.type = tarfile.DIRTYPE
        info.mtime = FIXED_TIME
        t.addfile(info)


make_tar("sample.tar", "w")
with open("sample.tar.gz", "wb") as raw:
    with gzip.GzipFile(filename="sample.tar", fileobj=raw, mode="wb", mtime=0) as g:
        with open("sample.tar", "rb") as tar:
            g.write(tar.read())

with open("plain.txt", "wb") as f:
    f.write(b"not an archive\n")
