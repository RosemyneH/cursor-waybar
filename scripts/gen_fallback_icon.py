#!/usr/bin/env python3
"""Writes assets/cursor-icon-fallback.png (32x32 RGBA) for Waybar when Cursor code.png is missing."""
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "cursor-icon-fallback.png"


def chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def main() -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    w, h = 32, 32
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    px = bytes([0x24, 0x76, 0xE3, 0xFF])
    raw = b"".join(b"\x00" + px * w for _ in range(h))
    sig = b"\x89PNG\r\n\x1a\n"
    png = (
        sig
        + chunk(b"IHDR", ihdr)
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )
    OUT.write_bytes(png)


if __name__ == "__main__":
    main()
