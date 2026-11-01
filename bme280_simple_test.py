#!/usr/bin/env python3
"""Simple BME280 connectivity test."""

import machine
from time import sleep_ms

print("Testing I2C connection...")

# Initialize I2C (Pico default: GP4=SCL, GP5=SDA)
i2c = machine.I2C(0)

# Scan for devices
devices = i2c.scan()
print(f"Found {len(devices)} device(s): {[hex(d) for d in devices]}")

if 0x76 not in devices and 0x77 not in devices:
    print("BME280 not found at expected addresses!")
else:
    addr = 0x76 if 0x76 in devices else 0x77
    print(f"Trying address 0x{addr:02X}")

    # Read chip ID (should be 0x60 for BME280)
    try:
        chip_id = i2c.readfrom_mem(addr, 0xD0, 1)[0]
        print(f"Chip ID: 0x{chip_id:02X}")

        if chip_id == 0x60:
            print("BME280 detected successfully!")
        else:
            print("Not a BME280 sensor")
    except Exception as e:
        print(f"Error reading chip ID: {e}")

print("Test complete.")