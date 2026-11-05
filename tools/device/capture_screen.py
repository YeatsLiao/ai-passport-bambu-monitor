"""Request a device screenshot via its USB console; save a validated PNG.

Run in the ESP-IDF Python environment (already contains pyserial).
No device reset, flashing, Pillow, or automatic dependency installation.
Ported from ai-passport-sheikah-slate tools/device/capture_screen.py;
talks to main/device/screenshot.c (@SLATE text protocol, per-row CRC validated).
"""
import argparse
from datetime import datetime
from pathlib import Path
import struct
import sys
import time
import zlib


def wait_ready(port):
    """Identify the screenshot firmware before attempting a capture."""
    deadline = time.monotonic() + 5
    pending = bytearray()
    while time.monotonic() < deadline:
        pending.extend(port.read(max(1, min(port.in_waiting, 4096))))
        if len(pending) > 16384:
            raise ValueError("No valid screenshot readiness reply")
        while b"\n" in pending:
            line, _, rest = pending.partition(b"\n")
            pending = bytearray(rest)
            if line.rstrip(b"\r") == b"@SLATE_READY 2 FAP_SCREENSHOT_V1 screenshot":
                return
    raise TimeoutError(
        "USB command sent, but screenshot firmware did not reply. "
        "Rebuild and flash; its boot log must contain "
        "'USB ready: FAP_SCREENSHOT_V1 + screenshot, stack=8192'."
    )


def send_command(port, serial, command):
    try:
        if port.write(command) != len(command):
            raise serial.SerialTimeoutException("Incomplete command write")
    except serial.SerialTimeoutException as error:
        port.reset_output_buffer()
        raise TimeoutError(
            f"{port.port} opened, but USB command write timed out. "
            "The device USB receive path is not accepting the command. "
            "Rebuild and flash the screenshot firmware first. If already updated, "
            "check the boot log for 'USB ready: FAP_SCREENSHOT_V1 + screenshot, stack=8192'."
        ) from error


def receive(port, timeout):
    deadline = time.monotonic() + timeout
    frame = coverage = None
    width = height = 0
    crc = pixels = 0
    pending = bytearray()
    while time.monotonic() < deadline:
        pending.extend(port.read(max(1, min(port.in_waiting, 4096))))
        if len(pending) > 16384:
            raise ValueError("Serial record too long; no valid screenshot protocol")
        while b"\n" in pending:
            raw, _, rest = pending.partition(b"\n")
            pending = bytearray(rest)
            line = raw.rstrip(b"\r")
            if line.startswith(b"@SLATE_ERROR "):
                raise ValueError(line.decode("ascii", errors="replace"))
            if line.startswith(b"@SLATE_BEGIN "):
                header = line.split()
                if len(header) != 5 or header[1] != b"1" or header[4] != b"RGB565LE":
                    raise ValueError("Unsupported screenshot format")
                width, height = int(header[2]), int(header[3])
                if (width, height) not in ((320, 240), (240, 320)):
                    raise ValueError("Unsupported screenshot dimensions")
                frame = bytearray(width * height * 2)
                coverage = bytearray(width * height)
                crc = pixels = 0
            elif line.startswith(b"@SLATE_ROW "):
                if frame is None:
                    continue
                _, x, y, count, encoded = line.split()
                x, y, count = int(x), int(y), int(count)
                if not (0 <= y < height and 0 <= x < width and 0 < count <= width - x):
                    raise ValueError("Screenshot row outside screen bounds")
                if len(encoded) != count * 4:
                    raise ValueError("Incomplete screenshot row")
                data = bytes.fromhex(encoded.decode("ascii"))
                start = y * width + x
                if any(coverage[start:start + count]):
                    raise ValueError("Duplicate screenshot pixels")
                frame[start * 2:(start + count) * 2] = data
                coverage[start:start + count] = b"\x01" * count
                crc = zlib.crc32(data, crc)
                pixels += count
            elif line.startswith(b"@SLATE_END ") and frame is not None:
                _, total, expected_crc = line.split()
                if pixels != width * height or pixels != int(total) or not all(coverage):
                    raise ValueError("Screenshot is missing pixels; no PNG saved")
                if crc != int(expected_crc, 16):
                    raise ValueError("Screenshot CRC mismatch; no PNG saved")
                return frame, width, height
            # Other console logs may appear between protocol records.
    raise TimeoutError("No complete screenshot received. Check firmware, COM port, and close monitor.")


def png_bytes(frame, width=240, height=320):
    """Expand RGB565 little-endian into a standard RGB PNG in the captured orientation."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # PNG filter: None
        for x in range(width):
            offset = (y * width + x) * 2
            value = frame[offset] | (frame[offset + 1] << 8)
            r, g, b = value >> 11, (value >> 5) & 63, value & 31
            rows.extend(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="USB console port, e.g. COM4")
    parser.add_argument("--output", type=Path, default=None, help="New PNG file (never overwrites)")
    parser.add_argument("--timeout", type=float, default=45, help="Receive timeout in seconds")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    output = args.output or Path(f"bambu-monitor-{datetime.now():%Y%m%d-%H%M%S-%f}.png")
    if output.suffix.lower() != ".png":
        parser.error("--output must end in .png")
    if output.exists():
        parser.error(f"Output already exists: {output}")
    try:
        import serial
    except ImportError:
        parser.exit(1, "pyserial is missing. Run this script in the ESP-IDF Python environment.\n")
    try:
        # Set control lines before opening; don't deliberately reset the board.
        with serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=5,
                          xonxoff=False, rtscts=False, dsrdtr=False) as port:
            port.dtr = False
            port.rts = False
            port.port = args.port
            port.open()
            # Reapply idle control levels after open (also used by IDF monitor).
            # Do not pulse RTS or reset the board to recover a failed capture.
            port.rts = False
            port.dtr = False
            time.sleep(0.5)
            port.reset_input_buffer()
            print(f"Checking screenshot firmware on {args.port}...", flush=True)
            # Windows write() waits for completion; don't add an unbounded flush().
            send_command(port, serial, b"\nSLATE_SCREENSHOT_PING\n")
            wait_ready(port)
            print("Firmware ready; requesting screenshot...", flush=True)
            send_command(port, serial, b"\nscreenshot\n")
            print("Receiving screenshot; keep the device connected...", flush=True)
            frame, width, height = receive(port, args.timeout)
        image = png_bytes(frame, width, height)
        with output.open("xb") as destination:
            destination.write(image)
        print(f"Saved: {output.resolve()}")
    except (serial.SerialException, OSError, ValueError, TimeoutError) as error:
        print(f"Screenshot failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
