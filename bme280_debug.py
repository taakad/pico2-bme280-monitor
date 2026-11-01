#!/usr/bin/env python3
"""BME280 I2C communication debug script for Raspberry Pi Pico 2."""

import machine
from time import sleep_ms

print("Starting BME280 debug...")

# Initialize I2C with explicit pins (GP4=SCL, GP5=SDA are standard)
try:
    i2c = machine.I2C(0, scl=machine.Pin(4), sda=machine.Pin(5))
    print("I2C initialized on bus 0")
except Exception as e:
    print(f"I2C init error: {e}")

# Scan for I2C devices
print("\nScanning I2C bus...")
devices = i2c.scan()
if not devices:
    print("No I2C devices found!")
else:
    print(f"Found {len(devices)} device(s):")
    for addr in devices:
        print(f"  Address: 0x{addr:02X}")

# Try to read from BME280 at both common addresses
for addr in [0x76, 0x77]:
    try:
        # Read chip ID (should be 0x60 for BME280)
        chip_id = i2c.readfrom_mem(addr, 0xD0, 1)[0]
        print(f"\nDevice at 0x{addr:02X}:")
        if chip_id == 0x60:
            print("  ✓ BME280 detected!")
        else:
            print(f"  Chip ID: 0x{chip_id:02X} (not BME280)")

        # Try reading calibration data
        cal_t = i2c.readfrom_mem(addr, 0x88, 6)
        dig_T1 = (cal_t[1] << 8) | cal_t[0]
        print(f"  Calibration T1: {dig_T1}")

    except Exception as e:
        print(f"\nError reading from 0x{addr:02X}: {e}")

print("\nDebug complete.")