"""Golden vectors for the pointing model (ICS-035), which ICS-036's C++ port must match.

golden/pointing/model.json is a parameter file whose model uses every term,
each a few hundred microradians: made up, not fitted. golden/pointing/vectors.csv
gives, across azimuth and from 5° to 85° of elevation, the encoder angles and
the sky direction ics_models.pointing.model.to_sky gives for them, in radians,
printed to round-trip exactly. The port must give the sky direction from the
encoder angles, and the encoder angles from the sky direction, each within
1e-12 rad.

Regenerate from python/ after changing the model's terms:

    uv run --locked python -m ics_golden.pointing ../golden/pointing
"""

import hashlib
import json
import math
import sys
from datetime import UTC, datetime
from pathlib import Path

from ics_models.pointing.astrometry import Site
from ics_models.pointing.fit import FitResult
from ics_models.pointing.model import ALL_TERMS, AltAz, PointingModel, to_sky
from ics_models.pointing.parameters import Provenance, document, validate

MICRORADIAN: float = 1e-6
# Each term's made-up coefficient, in microradians, in ALL_TERMS's order.
COEFFICIENTS_URAD: tuple[float, ...] = (
    2000.0,
    -1000.0,
    300.0,
    150.0,
    80.0,
    -60.0,
    100.0,
    25.0,
    -15.0,
    10.0,
    -8.0,
    12.0,
    -20.0,
    6.0,
    -4.0,
    30.0,
)
AZIMUTHS_DEG: tuple[float, ...] = tuple(float(step * 30) for step in range(12))
ELEVATIONS_DEG: tuple[float, ...] = (5.0, 20.0, 45.0, 70.0, 85.0)
HEADER: str = "encoder_az_rad,encoder_el_rad,sky_az_rad,sky_el_rad\n"
CREATED: datetime = datetime(2026, 10, 16, tzinfo=UTC)
EXIT_USAGE: int = 2


def golden_model() -> PointingModel:
    """The golden model: every term."""
    return PointingModel(terms=ALL_TERMS, coefficients=tuple(value * MICRORADIAN for value in COEFFICIENTS_URAD))


def vectors() -> str:
    """vectors.csv's text."""
    model = golden_model()
    lines = [HEADER]
    for elevation in ELEVATIONS_DEG:
        for azimuth in AZIMUTHS_DEG:
            encoder = AltAz(azimuth=math.radians(azimuth), elevation=math.radians(elevation))
            sky = to_sky(model, encoder)
            row = (encoder.azimuth, encoder.elevation, sky.azimuth, sky.elevation)
            lines.append(",".join(repr(value) for value in row) + "\n")
    return "".join(lines)


def model_json(vectors_text: str) -> str:
    """model.json's text: the golden model as a parameter file."""
    model = golden_model()
    result = FitResult(model=model, sigmas=(0.0,) * len(model.terms), rms_rad=0.0, used=1, rejected=())
    provenance = Provenance(
        model_id="golden",
        station_id="golden",
        created=CREATED,
        site=Site(latitude_deg=32.38, longitude_deg=-106.48, height_m=1200.0),
        observations_sha256=hashlib.sha256(vectors_text.encode()).hexdigest(),
        held_out=1,
        held_out_rms_rad=0.0,
    )
    content = document(result, provenance)
    validate(content)
    return json.dumps(content, indent=2) + "\n"


def write(directory: Path) -> None:
    """Write model.json and vectors.csv into directory."""
    directory.mkdir(parents=True, exist_ok=True)
    text = vectors()
    (directory / "vectors.csv").write_text(text)
    (directory / "model.json").write_text(model_json(text))


def main(argv: list[str]) -> int:
    """Write the golden files into the directory named."""
    if len(argv) != 1:
        sys.stderr.write("usage: python -m ics_golden.pointing DIRECTORY\n")
        return EXIT_USAGE
    write(Path(argv[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
