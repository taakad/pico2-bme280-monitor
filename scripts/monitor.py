#!/usr/bin/env python3
"""Watch readings from the BME280 monitor firmware over USB serial.

Waits for the board to appear, then prints each sample line. With --json it
emits one JSON object per sample, which is what the planned Angular UI and the
install-as-a-service mode will consume.

    scripts/monitor.py                 # human readable
    scripts/monitor.py --json          # machine readable
    scripts/monitor.py --port /dev/cu.usbmodem1101 --seconds 60
"""
import argparse
import glob
import json
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required: python3 -m pip install pyserial")

# T=  23.77 C  P=  1002.95 hPa  RH= 49.83 %  |  dew= 12.68 C  AH=10.68 g/m3  (adc T=532928 P=276784 H=27471)
SAMPLE_RE = re.compile(
    r"T=\s*(?P<temperature_c>-?\d+\.\d+) C\s+P=\s*(?P<pressure_hpa>-?\d+\.\d+) hPa\s+"
    r"RH=\s*(?P<humidity_rh>-?\d+\.\d+) %\s+\|\s+dew=\s*(?P<dew_point_c>-?\d+\.\d+) C\s+"
    r"AH=\s*(?P<absolute_humidity_gm3>-?\d+\.\d+) g/m3\s+"
    r"\(adc T=(?P<adc_t>\d+) P=(?P<adc_p>\d+) H=(?P<adc_h>\d+)\)"
)


def find_port(explicit):
    if explicit:
        return explicit
    candidates = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
    return candidates[0] if candidates else None


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", help="serial device (auto-detected if omitted)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=0, help="stop after N seconds (0 = forever)")
    ap.add_argument("--wait", type=float, default=30, help="seconds to wait for the board")
    ap.add_argument("--json", action="store_true", help="emit JSON objects, one per sample")
    args = ap.parse_args()

    deadline = time.time() + args.wait
    port = None
    while time.time() < deadline:
        port = find_port(args.port)
        if port:
            break
        time.sleep(0.5)
    if port is None:
        sys.exit("no Pico serial port found - is the board plugged in and flashed?")

    ser = serial.Serial(port, args.baud, timeout=1)
    ser.dtr = True
    ser.rts = True

    stop_at = time.time() + args.seconds if args.seconds else None
    end = time.time() + 120
    while time.time() < end:
        line = ser.readline()
        if not line:
            continue
        text = line.decode(errors="replace").rstrip()
        if not text:
            continue
        match = SAMPLE_RE.search(text)
        if match and args.json:
            record = {"port": port, "ts": time.time()}
            record.update({k: float(v) if "." in v else int(v) for k, v in match.groupdict().items()})
            print(json.dumps(record, ensure_ascii=False), flush=True)
        else:
            print(text, flush=True)
        if stop_at and time.time() >= stop_at:
            break
    ser.close()


if __name__ == "__main__":
    main()
