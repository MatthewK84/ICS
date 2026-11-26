"""Terrestrial refraction from a meteorological profile."""

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt


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
    # Placeholder layered integral; real implementation would ray-trace the profile.
    mean_temp = float(np.mean(profile.temp_k))
    mean_pressure = float(np.mean(profile.pressure_pa))
    # Rough formula scaled for typical conditions
    base = 1.0e-6 * (mean_pressure / 101325.0) * (288.15 / mean_temp)
    return base * np.tan(max(elevation_rad, 0.1))
