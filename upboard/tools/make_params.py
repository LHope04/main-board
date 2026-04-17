"""
Generate a boot_params_t binary blob for manual flashing.

Usage:
  python make_params.py --slot A -o params.bin
  python make_params.py --slot B -o params.bin

Flash with:
  ST-LINK_CLI.exe -c SWD -P params.bin 0x08008000 -V after_programming -Rst

The struct layout must stay in sync with Common/boot_params.h.
"""

import argparse
import struct
import zlib
from pathlib import Path

MAGIC = 0xA5A5A5A5

# Layout: magic, boot_slot, slot_valid[2], slot_size[2], slot_crc32[2],
#         slot_version[2], pending_update, boot_count, reserved[4], self_crc
# 17 x uint32 = 68 bytes; self_crc is the 17th word.
FMT_BODY = "<IIIIIIIIIIIIIIII"  # 16 uint32 before self_crc
FMT_FULL = FMT_BODY + "I"

assert struct.calcsize(FMT_FULL) == 68


def build(slot: str) -> bytes:
    boot_slot = 0 if slot.upper() == "A" else 1

    body = struct.pack(
        FMT_BODY,
        MAGIC,         # magic
        boot_slot,     # boot_slot
        0, 0,          # slot_valid[2]
        0, 0,          # slot_size[2]
        0, 0,          # slot_crc32[2]
        0, 0,          # slot_version[2]
        0,             # pending_update
        0,             # boot_count
        0, 0, 0, 0,    # reserved[4]
    )
    self_crc = zlib.crc32(body) & 0xFFFFFFFF
    return body + struct.pack("<I", self_crc)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--slot", choices=["A", "B", "a", "b"], required=True)
    p.add_argument("-o", "--out", default="params.bin")
    args = p.parse_args()

    blob = build(args.slot)
    Path(args.out).write_bytes(blob)
    print(f"wrote {args.out} ({len(blob)} bytes), slot={args.slot.upper()}")


if __name__ == "__main__":
    main()
