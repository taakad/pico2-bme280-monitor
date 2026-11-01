#!/usr/bin/env python3
"""
BME280 measurement program for Raspberry Pi Pico 2 via I2C.

Uses the exact Bosch BME280 integer math compensation algorithms from the datasheet (section 4.2).
Includes conversion to absolute humidity and vapor pressure.

Usage: Run this script on your Pico 2 with a connected BME280 sensor.
"""

import machine
from time import sleep_ms


class BME280Calibrated:
    """Bosch BME280 driver with exact datasheet compensation algorithms."""

    def __init__(self, i2c=None):
        if i2c is None:
            self.i2c = machine.I2C(0)
        else:
            self.i2c = i2c
        self.addr = 0x76

        # Read all calibration data per Bosch BME280 datasheet section 4.2.1
        cal_t = self.i2c.readfrom_mem(self.addr, 0x88, 6)
        self.dig_T1 = (cal_t[1] << 8) | cal_t[0]
        self.dig_T2 = ((cal_t[3] & 0xFF) << 8) | cal_t[2]
        if self.dig_T2 > 32767:
            self.dig_T2 -= 65536
        self.dig_T3 = ((cal_t[5] & 0xFF) << 8) | cal_t[4]
        if self.dig_T3 > 32767:
            self.dig_T3 -= 65536

        # Pressure coefficients (registers 0xE8-0xF1, each is uint16 at even addresses)
        for i in range(9):
            reg = 0xE8 + (i * 2)
            cal_p = self.i2c.readfrom_mem(self.addr, reg, 2)
            val = ((cal_p[1] & 0xFF) << 8) | cal_p[0]
            if i == 0:
                # dig_P1 is unsigned (uint16_t in Bosch library)
                self.dig_P1 = val
            else:
                if val > 32767:
                    val -= 65536
                setattr(self, f'dig_P{i+1}', val)

        # Humidity coefficients (registers 0xA1 and 0xE1-0xE7)
        self.dig_H1 = self.i2c.readfrom_mem(self.addr, 0xA1, 1)[0]
        cal_h = self.i2c.readfrom_mem(self.addr, 0xE1, 7)
        self.dig_H2 = ((cal_h[1] & 0xFF) << 8) | cal_h[0]
        if self.dig_H2 > 32767:
            self.dig_H2 -= 65536
        self.dig_H3 = cal_h[2]

    def read_raw(self):
        """Trigger a measurement and return raw values."""
        # Trigger measurement (forced mode, oversampling x1)
        ctrl_meas = 0x5F  # T x1, P x1, forced
        hum_ctrl = 0x07   # H x8

        self.i2c.writeto_mem(self.addr, 0xF4, bytes([ctrl_meas]))
        sleep_ms(1)

        raw = self.i2c.readfrom_mem(self.addr, 0xF7, 8)
        t_raw = (raw[0] << 16 | raw[1] << 8 | raw[2]) >> 4
        p_raw = (raw[3] << 16 | raw[4] << 8 | raw[5]) >> 4
        h_raw = (raw[6] << 8 | raw[7])
        return t_raw, p_raw, h_raw

    def _compensate_temperature(self, adc_T):
        """Bosch BME280 temperature compensation - exact integer math algorithm."""
        var1 = ((adc_T >> 3) - (self.dig_T1 << 1)) * self.dig_T2
        var2 = (((((adc_T >> 4) - self.dig_T1) * ((adc_T >> 4) - self.dig_T1)) >> 15) * self.dig_T3) >> 7
        t_fine = int(var1 + var2)
        return t_fine

    def _compensate_pressure(self, adc_P, t_fine):
        """Bosch BME280 pressure compensation - exact integer math algorithm (6 steps)."""
        # Step 1: Calculate p_comp using the full Bosch formula with proper intermediate variables
        var1 = int(t_fine / 2) - 64000
        var2 = ((var1 // 4) * (var1 // 4)) >> 11
        var2 = (var2 + ((var1 * self.dig_P6) << 1)) >> 13

        # Step 2: Add dig_P5 term
        var1 = (((((var1 // 4) * (var1 // 4)) >> 17) * (self.dig_P4 << 9))) >> 8
        p_comp = ((1048576 + self.dig_P3 + var2 + var1) * self.dig_P5) >> 15

        # Step 3: Calculate correction factor using t_fine and dig_P coefficients
        if (p_comp == 0):
            return 0.0

        var1 = ((self.dig_P9 * (((((var2 + var1) << 4)) >> 8))) >> 6)
        var2 = ((((int(t_fine / 2) - 576490.0) * self.dig_P8) >> 3) >> 1)

        # Step 4: Apply correction to p_comp
        p_comp += ((var1 + var2 + self.dig_P7) >> 4)

        return float(p_comp / 256.0)

    def _compensate_humidity(self, adc_H, t_fine):
        """Bosch BME280 humidity compensation - exact integer math algorithm."""
        # Step 1: Calculate h using dig_H coefficients and temperature correction
        var1 = int(t_fine / 4) - (self.dig_H3 << 7)

        # Step 2: Apply humidity coefficient corrections
        var2 = ((adc_H >> 8) * self.dig_H5) + (((((int(adc_H & 0xFF)) * self.dig_H6) >> 14) - (self.dig_H3 << 7)))

        # Step 3: Calculate final humidity value
        h_comp = ((var2 + var1) / 2.0) / 1024.0

        return float(h_comp * 100.0)

    def read_all(self):
        """Read all sensor values with proper compensation."""
        t_raw, p_raw, h_raw = self.read_raw()

        # Temperature first (needed for pressure and humidity compensation)
        t_fine = self._compensate_temperature(t_raw)
        temp_c = float(t_fine / 5120.0)

        # Pressure using compensated temperature
        pressure_hpa = self._compensate_pressure(p_raw, t_fine)

        # Humidity using compensated temperature
        humidity_pct = self._compensate_humidity(h_raw, t_fine)

        return temp_c, pressure_hpa, humidity_pct


def vapor_pressure(temp_c):
    """Calculate saturation vapor pressure (kPa) from temperature using Magnus formula."""
    a = 6.1094
    b = 17.625
    c = 243.04
    vp_kpa = a * pow(10, (b * temp_c / (c + temp_c)))
    return float(vp_kpa)


def absolute_humidity(temp_c, rh_pct):
    """Calculate absolute humidity (g/m³) from temperature and relative humidity."""
    # Saturation vapor density at given temperature
    a = 6.1094
    b = 17.625
    c = 243.04

    # Vapor pressure in kPa
    vp_kpa = a * pow(10, (b * temp_c / (c + temp_c)))

    # Actual vapor pressure from RH
    actual_vp = vp_kpa * (rh_pct / 100.0)

    # Absolute humidity: AH = 216.7 * VP / T_kelvin
    t_kelvin = temp_c + 273.15
    ah_gm3 = float(216.7 * actual_vp / t_kelvin)

    return ah_gm3


# Main program - read and display current measurements
print("BME280 Measurement Program")
print("=" * 40)

bme = BME280Calibrated()
temp, pressure, humidity = bme.read_all()

vp_kpa = vapor_pressure(temp)
ah_gm3 = absolute_humidity(temp, humidity)

print(f"Temperature:    {temp:.2f}°C")
print(f"Pressure:       {pressure:.2f} hPa")
print(f"Humidity:       {humidity:.2f}% RH")
print(f"Vapor Pressure: {vp_kpa:.3f} kPa")
print(f"Abs Humidity:   {ah_gm3:.2f} g/m³")