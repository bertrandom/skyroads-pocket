#!/usr/bin/env python3
"""Pack the SkyRoads data files into one blob for the Pocket (data slot 1).

    mkpack.py <data dir> <out.bin>

Format (read by firmware/pocket.c): "SKYPACK1", u32 count, then count
entries of { char name[16]; u32 offset; u32 size; } (little endian), then
the files, each 4-byte aligned.  Offsets are from the start of the pack.
Only the files the game reads go in (as in web/Makefile)."""
import pathlib, struct, sys

src, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
files = sorted(p for p in src.iterdir()
               if p.suffix.upper() in (".LZS", ".DAT", ".SND") or p.name.upper() == "DEMO.REC")
head = 12 + 24 * len(files)
entries, blobs, off = [], [], (head + 3) & ~3
for p in files:
    data = p.read_bytes()
    entries.append(struct.pack("<16sII", p.name.upper().encode()[:15], off, len(data)))
    blobs.append(data + b"\0" * (-len(data) % 4))
    off += len(blobs[-1])
pack = b"SKYPACK1" + struct.pack("<I", len(files)) + b"".join(entries)
pack += b"\0" * (-len(pack) % 4) + b"".join(blobs)
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(pack)
print(f"{out}: {len(files)} files, {len(pack)} bytes")
