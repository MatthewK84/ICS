"""Terrestrial refraction from a meteorological profile."""

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

MIN_ELEVATION_RAD: float = 0.1
STANDARD_PRESSURE_PA: float = 101325.0
STANDARD_TEMP_K: float = 288.15
REFRACTION_SCALE: float = 1.0e-6


@dataclass(frozen=True)
class MetProfile:
    """Height, temperature, pressure and humidity for refraction."""

    height_m: npt.NDArray[np.float64]
    temp_k: npt.NDArray[np.float64]
    pressure_pa: npt.NDArray[np.float64]
    humidity: npt.NDArray[np.float64]


def refraction_correction(elevation_rad: float, profile: MetProfile, wavelength_m: float = 550e-9) -> float:
    """Approximate angular refraction correction (radians) for a given elevation.

    Uses a simple layered model; returns the correction to add to the observed elevation.
    """
    mean_temp = float(np.mean(profile.temp_k))
    mean_pressure = float(np.mean(profile.pressure_pa))
    base = REFRACTION_SCALE * (mean_pressure / STANDARD_PRESSURE_PA) * (STANDARD_TEMP_K / mean_temp)
    return base * np.tan(max(elevation_rad, MIN_ELEVATION_RAD))
