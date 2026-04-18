"""Push a firmware binary to the STM32 over the ESP32 UART link.

Frame: AA | CMD | LEN | PAYLOAD[LEN] | XOR
Commands:
  0xF1 OTA_START  payload = size(4B LE) + version(2B LE)
  0xF2 OTA_DATA   payload = offset(4B LE) + chunk[<=128B]
  0xF3 OTA_END    payload = expected_crc32(4B LE)
  0xF4 OTA_ABORT  payload = 0
ACKs arrive as AA | (cmd|0x80) | 01 | status | XOR.

Usage:
  python ota_push.py <firmware.bin> [--port COMx] [--baud 115200] [--version N]
"""

import argparse
import os
import sys
import time
import zlib

import serial

HEADER = 0xAA
CMD_START = 0xF1
CMD_DATA  = 0xF2
CMD_END   = 0xF3
CMD_ABORT = 0xF4
ACK_BIT   = 0x80

STATUS_OK         = 0x00
STATUS_BAD_STATE  = 0x01
STATUS_BAD_PARAM  = 0x02
STATUS_FLASH_ERR  = 0x03
STATUS_BAD_CRC    = 0x04

STATUS_NAMES = {
    STATUS_OK:         "OK",
    STATUS_BAD_STATE:  "BAD_STATE",
    STATUS_BAD_PARAM:  "BAD_PARAM",
    STATUS_FLASH_ERR:  "FLASH_ERR",
    STATUS_BAD_CRC:    "BAD_CRC",
}

CHUNK_DATA_MAX = 128   # must match STM32 ESP_MAX_PAYLOAD - 4


def build_frame(cmd, payload):
    if len(payload) > 255:
        raise ValueError("payload too long")
    frame = bytearray([HEADER, cmd, len(payload)]) + bytearray(payload)
    x = cmd ^ len(payload)
    for b in payload:
        x ^= b
    frame.append(x)
    return bytes(frame)


def wait_ack(ser, expected_cmd, timeout=2.0):
    """Read bytes until a valid ACK frame for `expected_cmd` arrives or timeout."""
    deadline = time.monotonic() + timeout
    expected_ack = expected_cmd | ACK_BIT
    buf = bytearray()
    while time.monotonic() < deadline:
        b = ser.read(1)
        if not b:
            continue
        buf += b
        # Scan for header then parse
        while len(buf) >= 5:
            if buf[0] != HEADER:
                buf.pop(0)
                continue
            cmd = buf[1]
            ln  = buf[2]
            if ln != 1 or cmd != expected_ack:
                # Not our ack — drop header byte and keep scanning
                buf.pop(0)
                continue
            if len(buf) < 5:
                break   # wait for more bytes
            status = buf[3]
            xor    = buf[4]
            calc   = cmd ^ ln ^ status
            if calc != xor:
                buf.pop(0)
                continue
            return status
    return None


def do_push(ser, blob, version):
    size = len(blob)
    if size % 4 != 0:
        pad = 4 - (size % 4)
        blob += b"\xFF" * pad
        size = len(blob)
    crc = zlib.crc32(blob) & 0xFFFFFFFF

    print(f"firmware size = {size} bytes, crc32 = 0x{crc:08X}, version = {version}")

    # START
    payload = size.to_bytes(4, "little") + version.to_bytes(2, "little")
    ser.write(build_frame(CMD_START, payload))
    status = wait_ack(ser, CMD_START, timeout=3.0)
    if status != STATUS_OK:
        print(f"OTA_START failed: {STATUS_NAMES.get(status, status)}")
        return 1
    print("OTA_START ok, target erased")

    # DATA chunks
    total_chunks = (size + CHUNK_DATA_MAX - 1) // CHUNK_DATA_MAX
    offset = 0
    for i in range(total_chunks):
        chunk = blob[offset:offset + CHUNK_DATA_MAX]
        payload = offset.to_bytes(4, "little") + chunk
        ser.write(build_frame(CMD_DATA, payload))
        status = wait_ack(ser, CMD_DATA, timeout=1.5)
        if status != STATUS_OK:
            print(f"\nchunk {i} @ 0x{offset:X} failed: {STATUS_NAMES.get(status, status)}")
            return 2
        offset += len(chunk)
        if (i & 0x1F) == 0 or i == total_chunks - 1:
            pct = 100.0 * (i + 1) / total_chunks
            print(f"  {i + 1}/{total_chunks} ({pct:5.1f}%) offset=0x{offset:05X}", flush=True)

    # END
    payload = crc.to_bytes(4, "little")
    ser.write(build_frame(CMD_END, payload))
    status = wait_ack(ser, CMD_END, timeout=3.0)
    if status != STATUS_OK:
        print(f"OTA_END failed: {STATUS_NAMES.get(status, status)}")
        return 3
    print("OTA_END ok — device will reset into the new slot shortly")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("firmware", help="path to raw .bin file")
    ap.add_argument("--port", default="COM10")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--version", type=int, default=2)
    args = ap.parse_args()

    if not os.path.isfile(args.firmware):
        print(f"firmware not found: {args.firmware}")
        return 1
    with open(args.firmware, "rb") as f:
        blob = f.read()

    with serial.Serial(args.port, args.baud, timeout=0.05) as ser:
        # Drain any junk already in the buffer
        ser.reset_input_buffer()
        return do_push(ser, blob, args.version)


if __name__ == "__main__":
    sys.exit(main())
