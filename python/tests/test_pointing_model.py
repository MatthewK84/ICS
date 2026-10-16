"""The pointing model's terms, both directions, and their signs (ICS-035)."""

import math

import pytest
from hypothesis import given
from hypothesis import strategies as st

from ics_models.pointing.model import (
    ALL_TERMS,
    BASIC_TERMS,
    AltAz,
    PointingError,
    PointingModel,
    Term,
    basis,
    sky_offset,
    to_encoder,
    to_sky,
    wrap_pi,
)

MICRORADIAN: float = 1e-6
# A small tilt, to check the tilt terms against geometry.
TILT_RAD: float = 1e-4
# Second order in the tilt: 1e-8 rad squared, times tan E.
TILT_ORDER_RAD: float = 1e-7
ROUND_TRIP_RAD: float = 1e-12
TERM_COUNT: int = 16


def unit(direction: AltAz) -> tuple[float, float, float]:
    """A direction as a unit vector: north, east, up."""
    cos_e = math.cos(direction.elevation)
    return (cos_e * math.cos(direction.azimuth), cos_e * math.sin(direction.azimuth), math.sin(direction.elevation))


def direction(vector: tuple[float, float, float]) -> AltAz:
    """A unit vector as a direction."""
    north, east, up = vector
    return AltAz(azimuth=math.atan2(east, north) % (2 * math.pi), elevation=math.asin(up))


def tilted(encoder: AltAz, toward_rad: float) -> AltAz:
    """Where the line of sight points when the azimuth axis leans TILT_RAD toward an azimuth.

    The mount's frame is the true frame turned by TILT_RAD about the
    horizontal axis square to toward_rad, which carries its vertical toward
    toward_rad: a small rotation, d + w cross d.
    """
    w = (-TILT_RAD * math.sin(toward_rad), TILT_RAD * math.cos(toward_rad), 0.0)
    d = unit(encoder)
    cross = (w[1] * d[2] - w[2] * d[1], w[2] * d[0] - w[0] * d[2], w[0] * d[1] - w[1] * d[0])
    turned = (d[0] + cross[0], d[1] + cross[1], d[2] + cross[2])
    norm = math.sqrt(sum(value * value for value in turned))
    return direction((turned[0] / norm, turned[1] / norm, turned[2] / norm))


def model_with(term: Term, value: float) -> PointingModel:
    """A model of one term."""
    return PointingModel(terms=(term,), coefficients=(value,))


def test_lists_every_term_once_with_the_basic_set_first() -> None:
    assert len(ALL_TERMS) == len(set(ALL_TERMS)) == TERM_COUNT
    assert ALL_TERMS[: len(BASIC_TERMS)] == BASIC_TERMS


@pytest.mark.parametrize(("term", "toward_deg"), [(Term.AN, 0.0), (Term.AW, 270.0)])
@pytest.mark.parametrize("azimuth_deg", [0.0, 37.0, 90.0, 200.0, 315.0])
@pytest.mark.parametrize("elevation_deg", [10.0, 45.0, 80.0])
def test_tilt_terms_match_a_leaning_azimuth_axis(
    term: Term, toward_deg: float, azimuth_deg: float, elevation_deg: float
) -> None:
    encoder = AltAz(azimuth=math.radians(azimuth_deg), elevation=math.radians(elevation_deg))
    truth = tilted(encoder, math.radians(toward_deg))
    modelled = to_sky(model_with(term, TILT_RAD), encoder)
    across, up = sky_offset(modelled, truth)
    assert math.hypot(across, up) < TILT_ORDER_RAD


def test_basis_follows_the_table() -> None:
    at = AltAz(azimuth=math.radians(30.0), elevation=math.radians(60.0))
    sin_a, cos_a = 0.5, math.sqrt(3) / 2
    sin_e, cos_e = math.sqrt(3) / 2, 0.5
    expected = {
        Term.IA: (1.0, 0.0),
        Term.IE: (0.0, 1.0),
        Term.CA: (1 / cos_e, 0.0),
        Term.NPAE: (sin_e / cos_e, 0.0),
        Term.TF: (0.0, -cos_e),
        Term.HASA: (sin_a, 0.0),
        Term.HACA: (cos_a, 0.0),
        Term.HASA2: (math.sin(math.radians(60.0)), 0.0),
        Term.HACA2: (math.cos(math.radians(60.0)), 0.0),
        Term.HESA: (0.0, sin_a),
        Term.HECA: (0.0, cos_a),
        Term.HESA2: (0.0, math.sin(math.radians(60.0))),
        Term.HECA2: (0.0, math.cos(math.radians(60.0))),
        Term.HESE: (0.0, sin_e),
    }
    for term, (az_per, el_per) in expected.items():
        (got,) = basis((term,), at)
        assert got == pytest.approx((az_per, el_per), abs=1e-15), term


def test_a_model_without_terms_points_where_the_encoders_say() -> None:
    nothing = PointingModel(terms=(), coefficients=())
    encoder = AltAz(azimuth=1.0, elevation=0.5)
    assert to_sky(nothing, encoder) == encoder
    assert to_encoder(nothing, encoder) == encoder


def test_index_terms_offset_both_axes() -> None:
    model = PointingModel(terms=(Term.IA, Term.IE), coefficients=(2e-3, -1e-3))
    sky = to_sky(model, AltAz(azimuth=2 * math.pi - 1e-3, elevation=0.3))
    assert sky.azimuth == pytest.approx(1e-3, abs=1e-15)
    assert sky.elevation == pytest.approx(0.299, abs=1e-15)


@given(
    st.floats(0.0, 2 * math.pi, exclude_max=True),
    st.floats(math.radians(5.0), math.radians(85.0)),
    st.lists(st.floats(-2e-3, 2e-3), min_size=16, max_size=16),
)
def test_sky_and_encoder_are_inverses(azimuth: float, elevation: float, values: list[float]) -> None:
    model = PointingModel(terms=ALL_TERMS, coefficients=tuple(values))
    encoder = AltAz(azimuth=azimuth, elevation=elevation)
    back = to_encoder(model, to_sky(model, encoder))
    across, up = sky_offset(back, encoder)
    assert math.hypot(across, up) < ROUND_TRIP_RAD


def test_refuses_a_bad_model() -> None:
    with pytest.raises(PointingError, match="2 terms but 1 coefficients"):
        PointingModel(terms=(Term.IA, Term.IE), coefficients=(0.0,))
    with pytest.raises(PointingError, match="twice"):
        PointingModel(terms=(Term.IA, Term.IA), coefficients=(0.0, 0.0))
    with pytest.raises(PointingError, match="finite"):
        PointingModel(terms=(Term.IA,), coefficients=(math.nan,))


def test_says_when_sky_to_encoder_does_not_converge() -> None:
    # An elevation error of 1.5 sin E, which no mount has, feeds on itself.
    wild = model_with(Term.HESE, 1.5)
    with pytest.raises(PointingError, match="did not converge"):
        to_encoder(wild, AltAz(azimuth=0.0, elevation=0.3))


def test_measures_offsets_on_the_sky() -> None:
    reference = AltAz(azimuth=0.001, elevation=math.radians(60.0))
    measured = AltAz(azimuth=2 * math.pi - 0.001, elevation=math.radians(60.0) + 5 * MICRORADIAN)
    across, up = sky_offset(measured, reference)
    assert across == pytest.approx(-0.002 * 0.5, rel=1e-9)
    assert up == pytest.approx(5 * MICRORADIAN, rel=1e-6)
    assert wrap_pi(math.pi) == pytest.approx(-math.pi)
