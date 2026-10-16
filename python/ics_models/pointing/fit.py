"""Fitting a pointing model to a night's stars (ICS-035).

Each sighting pairs the encoder angles at which a star sat on the boresight
with the star's observed place. The mount's errors are a few milliradians at
most, so the terms add linearly and the fit is linear least squares on the
sky: across (Δaz·cos el) and up (Δel), one row each per star.

A star whose residual is more than three times the RMS is rejected, and the
fit run again, at most five passes; a misidentified star or a gust is
rejected while honest noise almost never is (a 2-D Gaussian passes three
times its RMS once in ten thousand).

C17 is the RMS of the on-sky residual of stars the fit never saw:
sqrt(mean(across² + up²)).
"""

import math
from collections.abc import Sequence
from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

from ics_models.pointing.model import AltAz, PointingError, PointingModel, Term, basis, sky_offset, to_sky

MAX_PASSES: int = 5
CLIP: float = 3.0

type Matrix = npt.NDArray[np.float64]


@dataclass(frozen=True)
class Sighting:
    """A star on the boresight: the encoder angles, and where the star truly was."""

    encoder: AltAz
    sky: AltAz


@dataclass(frozen=True)
class FitResult:
    """A fitted model, each coefficient's standard deviation, the RMS on the stars used, and the stars rejected."""

    model: PointingModel
    sigmas: tuple[float, ...]
    rms_rad: float
    used: int
    rejected: tuple[int, ...]


def design(sightings: Sequence[Sighting], terms: Sequence[Term]) -> tuple[Matrix, Matrix]:
    """The least-squares rows, across then up for each star, and the offsets the terms must explain."""
    rows = np.zeros((2 * len(sightings), len(terms)))
    offsets = np.zeros(2 * len(sightings))
    for i, sighting in enumerate(sightings):
        cos_e = math.cos(sighting.encoder.elevation)
        for k, (az_per, el_per) in enumerate(basis(terms, sighting.encoder)):
            rows[2 * i, k] = az_per * cos_e
            rows[2 * i + 1, k] = el_per
        across, up = sky_offset(sighting.sky, sighting.encoder)
        offsets[2 * i] = across
        offsets[2 * i + 1] = up
    return rows, offsets


def solve(sightings: Sequence[Sighting], terms: Sequence[Term]) -> tuple[Matrix, Matrix, Matrix]:
    """The coefficients, their standard deviations, and each star's residual on the sky.

    PointingError when there are no more rows than terms, or the stars
    cannot tell two terms apart.
    """
    rows, offsets = design(sightings, terms)
    spare = rows.shape[0] - rows.shape[1]
    if spare <= 0:
        raise PointingError("fit", f"{len(sightings)} stars are too few for {len(terms)} terms")
    coefficients, _, rank, _ = np.linalg.lstsq(rows, offsets, rcond=None)
    if int(rank) < len(terms):
        raise PointingError("fit", "these stars cannot tell the terms apart: add stars across the sky, or drop a term")
    left = offsets - rows @ coefficients
    variance = float(left @ left) / spare
    sigmas = np.sqrt(np.diag(np.linalg.inv(rows.T @ rows)) * variance)
    per_star = np.hypot(left[0::2], left[1::2])
    return coefficients, sigmas, per_star


def fit(sightings: Sequence[Sighting], terms: Sequence[Term]) -> FitResult:
    """Fit terms to sightings, rejecting outliers over at most MAX_PASSES passes."""
    keep = list(range(len(sightings)))
    rejected: list[int] = []
    coefficients, sigmas, per_star = solve(sightings, terms)
    for _ in range(MAX_PASSES):
        rms = float(np.sqrt(np.mean(per_star**2)))
        worst = [keep[i] for i in range(len(keep)) if per_star[i] > CLIP * rms]
        if not worst:
            break
        rejected.extend(worst)
        keep = [index for index in keep if index not in worst]
        coefficients, sigmas, per_star = solve([sightings[i] for i in keep], terms)
    model = PointingModel(terms=tuple(terms), coefficients=tuple(float(value) for value in coefficients))
    return FitResult(
        model=model,
        sigmas=tuple(float(value) for value in sigmas),
        rms_rad=float(np.sqrt(np.mean(per_star**2))),
        used=len(keep),
        rejected=tuple(sorted(rejected)),
    )


def rms(model: PointingModel, sightings: Sequence[Sighting]) -> float:
    """The RMS of the on-sky residual, in radians, of model over sightings: C17 on held-out stars."""
    if not sightings:
        raise PointingError("rms", "no sightings to measure")
    total = 0.0
    for sighting in sightings:
        across, up = sky_offset(sighting.sky, to_sky(model, sighting.encoder))
        total += across * across + up * up
    return math.sqrt(total / len(sightings))


def split(sightings: Sequence[Sighting], fraction: float, seed: int) -> tuple[list[Sighting], list[Sighting]]:
    """The sightings to fit and those held out: fraction of them, at random but the same for a seed."""
    if not 0.0 < fraction < 1.0:
        raise PointingError("split", f"a held-out fraction of {fraction} is not between 0 and 1")
    held = round(len(sightings) * fraction)
    order = np.random.default_rng(seed).permutation(len(sightings))
    held_out = sorted(int(i) for i in order[:held])
    kept = sorted(int(i) for i in order[held:])
    return [sightings[i] for i in kept], [sightings[i] for i in held_out]
