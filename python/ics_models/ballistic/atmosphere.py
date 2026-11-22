"""ISA troposphere air density (ICS-044).

The ICAO standard atmosphere below 11 km: temperature falls linearly from
288.15 K at mean sea level, and pressure follows the hydrostatic equation for
that lapse rate. Heights are geopotential metres above mean sea level; at
range heights they differ from geometric ones by under a metre per 6 km.
"""

import math

SEA_LEVEL_TEMPERATURE_K: float = 288.15
SEA_LEVEL_PRESSURE_PA: float = 101_325.0
LAPSE_RATE_K_M: float = 0.0065
STANDARD_GRAVITY_M_S2: float = 9.80665
MOLAR_MASS_KG_MOL: float = 0.0289644
GAS_CONSTANT_J_MOL_K: float = 8.3144598
LOWEST_HEIGHT_M: float = -500.0
TROPOPAUSE_HEIGHT_M: float = 11_000.0
PRESSURE_EXPONENT: float = STANDARD_GRAVITY_M_S2 * MOLAR_MASS_KG_MOL / (GAS_CONSTANT_J_MOL_K * LAPSE_RATE_K_M)


def density(height_m: float) -> float:
    """Air density in kg/m³ at a height above mean sea level, from -500 m to 11 km."""
    if not LOWEST_HEIGHT_M <= height_m <= TROPOPAUSE_HEIGHT_M:
        msg = f"height {height_m} m is outside the ISA troposphere"
        raise ValueError(msg)
    temperature = SEA_LEVEL_TEMPERATURE_K - LAPSE_RATE_K_M * height_m
    pressure = SEA_LEVEL_PRESSURE_PA * math.pow(temperature / SEA_LEVEL_TEMPERATURE_K, PRESSURE_EXPONENT)
    return pressure * MOLAR_MASS_KG_MOL / (GAS_CONSTANT_J_MOL_K * temperature)
