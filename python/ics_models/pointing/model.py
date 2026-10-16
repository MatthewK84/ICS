"""TPOINT-style pointing terms for an alt-az mount, applied both ways (ICS-035).

Angles are in radians: azimuth from north through east, elevation up from the
horizon (docs/frames-and-time.md). The encoder angles are the mount's own, as
the time-tagger reports them; the sky direction is where the line of sight
truly points, which for a star is its observed place, refraction included.

The model adds a small correction to the encoder angles:

    sky = encoder + sum(coefficient * basis(term, encoder))

Each term has TPOINT's name and functional form (Wallace, TPOINT); ICS fixes
the signs as below, and the golden vectors in golden/pointing pin them, so a
coefficient's sign may differ from TPOINT's own.

| Term  | Azimuth          | Elevation | Cause                                  |
|-------|------------------|-----------|----------------------------------------|
| IA    | 1                | 0         | Azimuth encoder zero                   |
| IE    | 0                | 1         | Elevation encoder zero                 |
| CA    | 1 / cos E        | 0         | Optics not square to the elevation axis |
| NPAE  | tan E            | 0         | Elevation axis not square to azimuth's |
| AN    | -sin A tan E     | -cos A    | Azimuth axis leaning north             |
| AW    | -cos A tan E     | sin A     | Azimuth axis leaning west              |
| TF    | 0                | -cos E    | Tube flexure: the tube droops          |
| HASA  | sin A            | 0         | Azimuth harmonics, added by an         |
| HACA  | cos A            | 0         | operator when the data need them       |
| HASA2 | sin 2A           | 0         |                                        |
| HACA2 | cos 2A           | 0         |                                        |
| HESA  | 0                | sin A     | Elevation harmonics in azimuth         |
| HECA  | 0                | cos A     |                                        |
| HESA2 | 0                | sin 2A    |                                        |
| HECA2 | 0                | cos 2A    |                                        |
| HESE  | 0                | sin E     | Elevation harmonic in elevation        |
"""

import math
from collections.abc import Sequence
from dataclasses import dataclass
from enum import StrEnum

TWO_PI: float = 2.0 * math.pi
# Sky to encoder is solved by fixed-point iteration: at most this many passes,
# stopping once a pass moves the answer by less than the tolerance.
MAX_PASSES: int = 10
CONVERGED_RAD: float = 1e-13


class PointingError(ValueError):
    """Input the pointing model cannot use: where, and what is wrong."""

    def __init__(self, where: str, problem: str) -> None:
        """Say where the problem is, and what it is."""
        super().__init__(f"{where}: {problem}")


class Term(StrEnum):
    """A pointing term, by its TPOINT name."""

    IA = "IA"
    IE = "IE"
    CA = "CA"
    NPAE = "NPAE"
    AN = "AN"
    AW = "AW"
    TF = "TF"
    HASA = "HASA"
    HACA = "HACA"
    HASA2 = "HASA2"
    HACA2 = "HACA2"
    HESA = "HESA"
    HECA = "HECA"
    HESA2 = "HESA2"
    HECA2 = "HECA2"
    HESE = "HESE"


# Every term, in the order basis_table lists them.
ALL_TERMS: tuple[Term, ...] = tuple(Term)
# The basic alt-az set every model fits.
BASIC_TERMS: tuple[Term, ...] = (Term.IA, Term.IE, Term.CA, Term.NPAE, Term.AN, Term.AW, Term.TF)


@dataclass(frozen=True)
class AltAz:
    """A direction: azimuth and elevation, in radians."""

    azimuth: float
    elevation: float


@dataclass(frozen=True)
class PointingModel:
    """Terms and their coefficients, in radians, in the same order."""

    terms: tuple[Term, ...]
    coefficients: tuple[float, ...]

    def __post_init__(self) -> None:
        """Refuse a model with a coefficient missing or not finite, or a term twice."""
        if len(self.terms) != len(self.coefficients):
            raise PointingError("model", f"{len(self.terms)} terms but {len(self.coefficients)} coefficients")
        if len(set(self.terms)) != len(self.terms):
            raise PointingError("model", "a term appears twice")
        if not all(math.isfinite(value) for value in self.coefficients):
            raise PointingError("model", "a coefficient is not a finite number")


def wrap_turn(angle: float) -> float:
    """An angle in [0, 2π)."""
    return angle % TWO_PI


def wrap_pi(angle: float) -> float:
    """An angle in [-π, π)."""
    return (angle + math.pi) % TWO_PI - math.pi


def basis_table(at: AltAz) -> tuple[tuple[float, float], ...]:
    """Each term's azimuth and elevation correction per radian of coefficient, in ALL_TERMS's order."""
    sin_a, cos_a = math.sin(at.azimuth), math.cos(at.azimuth)
    sin_2a, cos_2a = math.sin(2.0 * at.azimuth), math.cos(2.0 * at.azimuth)
    sin_e, cos_e = math.sin(at.elevation), math.cos(at.elevation)
    tan_e = sin_e / cos_e
    return (
        (1.0, 0.0),
        (0.0, 1.0),
        (1.0 / cos_e, 0.0),
        (tan_e, 0.0),
        (-sin_a * tan_e, -cos_a),
        (-cos_a * tan_e, sin_a),
        (0.0, -cos_e),
        (sin_a, 0.0),
        (cos_a, 0.0),
        (sin_2a, 0.0),
        (cos_2a, 0.0),
        (0.0, sin_a),
        (0.0, cos_a),
        (0.0, sin_2a),
        (0.0, cos_2a),
        (0.0, sin_e),
    )


def basis(terms: Sequence[Term], at: AltAz) -> tuple[tuple[float, float], ...]:
    """Each of terms' azimuth and elevation correction per radian of coefficient."""
    table = basis_table(at)
    return tuple(table[ALL_TERMS.index(term)] for term in terms)


def correction(model: PointingModel, encoder: AltAz) -> tuple[float, float]:
    """The model's azimuth and elevation correction at these encoder angles."""
    d_az = 0.0
    d_el = 0.0
    for coefficient, (az_per, el_per) in zip(model.coefficients, basis(model.terms, encoder), strict=True):
        d_az += coefficient * az_per
        d_el += coefficient * el_per
    return d_az, d_el


def to_sky(model: PointingModel, encoder: AltAz) -> AltAz:
    """Where the line of sight points for these encoder angles."""
    d_az, d_el = correction(model, encoder)
    return AltAz(azimuth=wrap_turn(encoder.azimuth + d_az), elevation=encoder.elevation + d_el)


def to_encoder(model: PointingModel, sky: AltAz) -> AltAz:
    """The encoder angles that point the line of sight at sky.

    Solved by fixed-point iteration from sky itself, at most MAX_PASSES
    passes. PointingError if it has not converged by then, which happens
    only for corrections far larger than a mount's or near the zenith.
    """
    encoder = sky
    for _ in range(MAX_PASSES):
        d_az, d_el = correction(model, encoder)
        nearer = AltAz(azimuth=wrap_turn(sky.azimuth - d_az), elevation=sky.elevation - d_el)
        moved = max(abs(wrap_pi(nearer.azimuth - encoder.azimuth)), abs(nearer.elevation - encoder.elevation))
        encoder = nearer
        if moved < CONVERGED_RAD:
            return encoder
    raise PointingError("to_encoder", "did not converge")


def sky_offset(measured: AltAz, reference: AltAz) -> tuple[float, float]:
    """measured minus reference on the sky, in radians: across (Δaz·cos el) and up (Δel)."""
    across = wrap_pi(measured.azimuth - reference.azimuth) * math.cos(reference.elevation)
    return across, measured.elevation - reference.elevation
