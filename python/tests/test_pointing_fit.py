"""Fitting the pointing model, and ICS-035's "Done when": C17 on synthetic nights."""

import math
from dataclasses import replace
from datetime import UTC, datetime

import numpy as np
import pytest

from ics_models.pointing.astrometry import Conditions, Site, Weather
from ics_models.pointing.fit import Sighting, fit, rms, split
from ics_models.pointing.model import BASIC_TERMS, AltAz, PointingError, PointingModel, Term
from ics_models.pointing.observations import sighting
from ics_models.pointing.synth import Night, synthetic_night

MICRORADIAN: float = 1e-6
# C17: 10 µrad RMS on held-out stars.
C17_RAD: float = 10 * MICRORADIAN
NIGHTS: int = 50
STARS: int = 60
HELD_OUT: float = 0.25
# No estimate may be further than this many standard deviations from the truth.
WORST_SIGMAS: float = 5.0
REJECTION_PASSES: int = 5
SMALL_NIGHT: int = 20
NOISELESS_RAD: float = 1e-9
# A mount's errors, in microradians, for the basic terms.
TRUTH_URAD: tuple[float, ...] = (2000.0, -1000.0, 300.0, 150.0, 80.0, -60.0, 100.0)
CONDITIONS: Conditions = Conditions(
    site=Site(latitude_deg=32.38, longitude_deg=-106.48, height_m=1200.0),
    weather=Weather(pressure_hpa=880.0, temperature_c=12.0, relative_humidity=0.3, wavelength_um=0.6),
    dut1_s=0.12,
)
NIGHT: Night = Night(
    conditions=CONDITIONS, start=datetime(2026, 10, 16, 3, 0, tzinfo=UTC), hours=6.0, noise_rad=3 * MICRORADIAN
)


def truth(extra: tuple[tuple[Term, float], ...] = ()) -> PointingModel:
    """The true model: the basic terms, and any extra ones, in microradians."""
    terms = BASIC_TERMS + tuple(term for term, _ in extra)
    values = TRUTH_URAD + tuple(value for _, value in extra)
    return PointingModel(terms=terms, coefficients=tuple(value * MICRORADIAN for value in values))


def night_of(seed: int, model: PointingModel, night: Night = NIGHT, stars: int = STARS) -> list[Sighting]:
    """A synthetic night's sightings."""
    return [sighting(o) for o in synthetic_night(np.random.default_rng(seed), night, model, stars)]


def test_meets_c17_on_held_out_stars_and_recovers_every_term() -> None:
    model = truth()
    within_3_sigma = 0
    estimates = 0
    worst = 0.0
    for seed in range(NIGHTS):
        kept, held = split(night_of(seed, model), HELD_OUT, seed)
        result = fit(kept, BASIC_TERMS)
        worst = max(worst, rms(result.model, held))
        for value, sigma, true in zip(result.model.coefficients, result.sigmas, model.coefficients, strict=True):
            assert abs(value - true) < WORST_SIGMAS * sigma
            within_3_sigma += abs(value - true) <= 3 * sigma
            estimates += 1
    assert worst <= C17_RAD
    # A Gaussian estimate lies within 3 sigma 99.7% of the time.
    assert within_3_sigma >= 0.98 * estimates


def test_a_model_missing_a_term_fails_c17() -> None:
    model = truth()
    kept, held = split(night_of(1, model), HELD_OUT, 1)
    without_flexure = fit(kept, BASIC_TERMS[:-1])
    assert rms(without_flexure.model, held) > C17_RAD


def test_fits_a_harmonic_the_operator_adds() -> None:
    model = truth(((Term.HESA2, 40.0),))
    kept, held = split(night_of(2, model), HELD_OUT, 2)
    assert rms(fit(kept, BASIC_TERMS).model, held) > C17_RAD
    result = fit(kept, (*BASIC_TERMS, Term.HESA2))
    assert rms(result.model, held) <= C17_RAD
    assert result.model.coefficients[-1] == pytest.approx(40 * MICRORADIAN, abs=3 * result.sigmas[-1])


def test_rejects_misidentified_stars() -> None:
    sightings = night_of(3, truth())
    wrong = (5, 17)
    for index in wrong:
        off = sightings[index].sky
        sightings[index] = replace(sightings[index], sky=AltAz(off.azimuth, off.elevation + 500 * MICRORADIAN))
    result = fit(sightings, BASIC_TERMS)
    assert result.rejected == wrong
    assert result.used == STARS - len(wrong)
    assert result.rms_rad < 6 * MICRORADIAN


def test_gives_up_rejecting_after_five_passes() -> None:
    # Errors that grow star by star: each pass finds a new worst one.
    sightings = night_of(4, truth(), stars=40)
    for index in range(len(sightings)):
        off = sightings[index].sky
        grown = AltAz(off.azimuth, off.elevation + (1.3**index) * MICRORADIAN)
        sightings[index] = replace(sightings[index], sky=grown)
    result = fit(sightings, BASIC_TERMS)
    assert result.used == len(sightings) - len(result.rejected)
    assert len(result.rejected) >= REJECTION_PASSES


def test_refuses_too_few_stars_or_terms_it_cannot_tell_apart() -> None:
    sightings = night_of(5, truth(), stars=3)
    with pytest.raises(PointingError, match="too few"):
        fit(sightings, BASIC_TERMS)
    same_place = [replace(sightings[0], encoder=sightings[0].encoder)] * 10
    with pytest.raises(PointingError, match="cannot tell"):
        fit(same_place, BASIC_TERMS)


def test_splits_the_same_way_for_a_seed() -> None:
    sightings = night_of(6, truth(), stars=SMALL_NIGHT)
    kept, held = split(sightings, HELD_OUT, 7)
    assert len(held) == round(SMALL_NIGHT * HELD_OUT)
    assert len(kept) == SMALL_NIGHT - len(held)
    assert split(sightings, HELD_OUT, 7) == (kept, held)
    assert {id(s) for s in kept} | {id(s) for s in held} == {id(s) for s in sightings}
    with pytest.raises(PointingError, match="between 0 and 1"):
        split(sightings, 1.0, 7)


def test_measures_nothing_without_stars() -> None:
    with pytest.raises(PointingError, match="no sightings"):
        rms(truth(), [])


def test_the_rms_of_a_perfect_model_is_its_noise() -> None:
    quiet = replace(NIGHT, noise_rad=0.0)
    sightings = night_of(8, truth(), night=quiet, stars=SMALL_NIGHT)
    assert rms(truth(), sightings) < NOISELESS_RAD
    assert math.isfinite(rms(PointingModel((), ()), sightings))
