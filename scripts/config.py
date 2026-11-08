#!/usr/bin/env python3
"""Change the monitor's measurement settings over USB serial.

Examples:
    scripts/config.py --get
    scripts/config.py --set interval=1000 --set decimals=3
    scripts/config.py --set average=8 --set osr.t=4 --set filter=2
    scripts/config.py --set mode=forced
    scripts/config.py --ping

The firmware validates every value and answers `ok <key>=<value>` or
`error <reason>`, so this tool reports the device's verdict instead of
guessing. The exit code is non-zero if the device rejected anything.
"""

import argparse
import sys
import time

import serial


def pick_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    from serial.tools import list_ports
    candidates = [p.device for p in list_ports.comports()
                  if "0x2E8A" in p.hwid or "0x0D28" in p.hwid]
    if not candidates:
        print("No Raspberry Pi Pico found. Plug it in or pass --port.", file=sys.stderr)
        sys.exit(2)
    return candidates[0]


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port (default: auto-detect Pico)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--set", action="append", default=[], metavar="KEY=VALUE",
                        help="send a set command (repeatable)")
    parser.add_argument("--get", action="store_true", help="query current settings")
    parser.add_argument("--ping", action="store_true", help="check that the monitor responds")
    parser.add_argument("--wait", type=float, default=4.0,
                        help="seconds to wait for each reply (default: 4)")
    args = parser.parse_args()

    commands = []
    if args.ping:
        commands.append("ping")
    for item in args.set:
        commands.append("set " + item)
    if args.get or not commands:
        commands.append("get")

    port = pick_port(args.port)
    failed = 0
    with serial.Serial(port, args.baud, timeout=1) as ser:
        ser.reset_input_buffer()
        ser.write(b"\r\n")            # resynchronise the line reader
        ser.reset_input_buffer()

        for cmd in commands:
            ser.write((cmd + "\n").encode())
            ser.flush()
            head = cmd.split("=", 1)[0]
            deadline = time.time() + args.wait
            replied = False
            while time.time() < deadline:
                line = ser.readline().decode(errors="replace").strip()
                if not line:
                    continue
                print(line)
                if line.startswith("error"):
                    failed += 1
                    replied = True
                    break
                if line.startswith(("ok", "config", "pong", "monitor")):
                    replied = True
                    break
            if not replied:
                print(f"no reply to {cmd!r}", file=sys.stderr)
                failed += 1
            ser.reset_input_buffer()  # drop the sample stream between commands
            del head

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
