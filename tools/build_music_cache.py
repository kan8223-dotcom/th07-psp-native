#!/usr/bin/env python3
"""Build a user-local RGB565 Music Room background cache from TH07.DAT."""

import io
import struct
import sys
from pathlib import Path

from PIL import Image

from inspect_ecl_sounds import extract_entry


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} TH07.DAT OUTPUT.rgb565")

    archive = Path(sys.argv[1])
    output = Path(sys.argv[2])
    jpeg = extract_entry(str(archive), "music.jpg")
    image = Image.open(io.BytesIO(jpeg)).convert("RGB")
    if image.size != (640, 480):
        raise ValueError(f"unexpected music.jpg size: {image.size}")

    pixels = bytearray()
    for r, g, b in image.getdata():
        rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        pixels += struct.pack("<H", rgb565)

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(b"TH07M565" + struct.pack("<II", 640, 480) + pixels)
    print(f"wrote {output} ({output.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
