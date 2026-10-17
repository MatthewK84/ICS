"""Synthetic calibration nights, to prove the fit (ICS-035).

Each star is placed at a random observed azimuth and elevation at a random
moment of the night, and given the motionless catalog place that ERFA says
would be seen there. ERFA's two directions agree only to a few nanoradians, so
the star's observed place is then worked out again, as the reduction will.
Its encoder angles are where a mount with the true model must point to put it
on the boresight, plus the capture side's centroid noise on the sky, so the
stars go through the same reduction as a real night's.
"""

import math
from dataclasses import dataclass
from datetime import datetime, timedelta

import numpy as np

from ics_models.pointing.astrometry import Conditions, catalog_place, observed
from ics_models.pointing.model import AltAz, PointingModel, to_encoder, wrap_turn
from ics_models.pointing.observations import Observation

MIN_ELEVATION_DEG: float = 20.0
MAX_ELEVATION_DEG: float = 85.0


@dataclass(frozen=True)
class Night:
    """Where and when a synthetic night is, and its centroid noise per axis on the sky, in radians."""

    conditions: Conditions
    start: datetime
    hours: float
    noise_rad: float


def synthetic_night(rng: np.random.Generator, night: Night, truth: PointingModel, count: int) -> list[Observation]:
    """count stars, observed through a mount whose true pointing model is truth."""
    observations: list[Observation] = []
    for index in range(count):
        when = night.start + timedelta(hours=float(rng.uniform(0.0, night.hours)))
        placed = AltAz(
            azimuth=float(rng.uniform(0.0, 2.0 * math.pi)),
            elevation=math.radians(float(rng.uniform(MIN_ELEVATION_DEG, MAX_ELEVATION_DEG))),
        )
        star = catalog_place(placed, f"synthetic-{index:03d}", when, night.conditions)
        aimed = to_encoder(truth, observed(star, when, night.conditions))
        across, up = rng.normal(0.0, night.noise_rad, 2)
        encoder = AltAz(
            azimuth=wrap_turn(aimed.azimuth + float(across) / math.cos(aimed.elevation)),
            elevation=aimed.elevation + float(up),
        )
        observations.append(Observation(utc=when, star=star, encoder=encoder, conditions=night.conditions))
    return observations
