"""Basic tests for the refraction reference."""

import numpy as np

from ics_models.refraction.refraction import MetProfile, refraction_correction
from ics_models.refraction.turbulence import turbulence_from_jitter

ELEVATION_RAD: float = 0.5


def test_refraction_positive() -> None:
    profile = MetProfile(
        height_m=np.array([0.0, 1000.0]),
        temp_k=np.array([288.0, 280.0]),
        pressure_pa=np.array([101325.0, 90000.0]),
        humidity=np.array([0.5, 0.4]),
    )
    corr = refraction_correction(ELEVATION_RAD, profile)
    assert corr > 0.0


def test_turbulence_zero_on_bad_input() -> None:
    assert turbulence_from_jitter(0.0, 1000.0) == 0.0
    assert turbulence_from_jitter(1e-5, 0.0) == 0.0
