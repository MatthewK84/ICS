"""The pointing model's command line (ICS-035).

    python -m ics_models.pointing fit OBSERVATIONS.csv --station ID --model-id ID --out MODEL.json
    python -m ics_models.pointing plan CATALOG.csv --site LAT,LON,HEIGHT --start UTC --end UTC --count N \
        --dut1 S --out PLAN.csv

fit holds a quarter of the stars out at random, fits the rest, writes the
model with its RMS on the stars held out (C17), and prints a summary. plan
picks the night's stars from a catalog. Both exit 2, saying why on stderr,
for input they refuse.
"""

import argparse
import sys
from collections.abc import Sequence
from datetime import UTC, datetime
from pathlib import Path

from ics_models.pointing.astrometry import Conditions, Site, Weather
from ics_models.pointing.fit import FitResult, fit, rms, split
from ics_models.pointing.model import BASIC_TERMS, PointingError, Term
from ics_models.pointing.observations import read_observations, sighting
from ics_models.pointing.parameters import Provenance, sha256_of, write_parameters
from ics_models.pointing.plan import plan_night, read_catalog, write_plan

EXIT_REFUSED: int = 2
# LAT,LON,HEIGHT.
SITE_FIELDS: int = 3
MICRORADIANS: float = 1e6


def site_from(text: str) -> Site:
    """LAT,LON,HEIGHT in degrees and metres."""
    parts = text.split(",")
    if len(parts) != SITE_FIELDS:
        raise PointingError("--site", f"wants LAT,LON,HEIGHT, not {text!r}")
    return Site(latitude_deg=float(parts[0]), longitude_deg=float(parts[1]), height_m=float(parts[2]))


def utc_from(text: str) -> datetime:
    """An ISO 8601 time with its offset."""
    when = datetime.fromisoformat(text)
    if when.tzinfo is None:
        raise PointingError(text, "must give its offset, for example a trailing Z")
    return when


def parser() -> argparse.ArgumentParser:
    """The command line's arguments."""
    top = argparse.ArgumentParser(prog="python -m ics_models.pointing", description=__doc__.splitlines()[0])
    commands = top.add_subparsers(dest="command", required=True)
    fitting = commands.add_parser("fit", help="fit a model to a night's observations")
    fitting.add_argument("observations", type=Path)
    fitting.add_argument("--station", required=True)
    fitting.add_argument("--model-id", required=True)
    fitting.add_argument("--terms", default=",".join(BASIC_TERMS), help="comma-separated TPOINT names")
    fitting.add_argument("--hold-out", type=float, default=0.25, help="the fraction of stars held out")
    fitting.add_argument("--seed", type=int, default=35, help="picks the stars held out")
    fitting.add_argument("--out", type=Path, required=True)
    planning = commands.add_parser("plan", help="pick a night's stars from a catalog")
    planning.add_argument("catalog", type=Path)
    planning.add_argument("--site", required=True, help="LAT,LON,HEIGHT in degrees and metres")
    planning.add_argument("--start", required=True)
    planning.add_argument("--end", required=True)
    planning.add_argument("--count", type=int, default=60)
    planning.add_argument("--dut1", type=float, required=True, help="UT1 - UTC in seconds, from IERS Bulletin A")
    planning.add_argument("--pressure", type=float, default=1013.25, help="hPa")
    planning.add_argument("--temperature", type=float, default=10.0, help="degrees C")
    planning.add_argument("--humidity", type=float, default=0.5, help="0 to 1")
    planning.add_argument("--wavelength", type=float, default=0.6, help="micrometres")
    planning.add_argument("--out", type=Path, required=True)
    return top


def summary(result: FitResult, held_out: int, held_out_rms: float) -> str:
    """What the fit found, one term a line."""
    lines = [
        f"stars used {result.used}, rejected {len(result.rejected)}, RMS {result.rms_rad * MICRORADIANS:.2f} µrad",
        f"stars held out {held_out}, RMS {held_out_rms * MICRORADIANS:.2f} µrad (C17: 10 µrad or less)",
    ]
    for term, value, sigma in zip(result.model.terms, result.model.coefficients, result.sigmas, strict=True):
        lines.append(f"{term:>6} {value * MICRORADIANS:12.2f} ± {sigma * MICRORADIANS:.2f} µrad")
    return "\n".join(lines) + "\n"


def run_fit(arguments: argparse.Namespace) -> str:
    """Fit, write the model, and say how it went."""
    observations = read_observations(arguments.observations)
    if not observations:
        raise PointingError(str(arguments.observations), "holds no observations")
    terms = tuple(Term(name.strip()) for name in arguments.terms.split(","))
    kept, held = split([sighting(observation) for observation in observations], arguments.hold_out, arguments.seed)
    result = fit(kept, terms)
    held_out_rms = rms(result.model, held)
    provenance = Provenance(
        model_id=arguments.model_id,
        station_id=arguments.station,
        created=datetime.now(UTC),
        site=observations[0].conditions.site,
        observations_sha256=sha256_of(arguments.observations),
        held_out=len(held),
        held_out_rms_rad=held_out_rms,
    )
    write_parameters(arguments.out, result, provenance)
    return summary(result, len(held), held_out_rms)


def run_plan(arguments: argparse.Namespace) -> str:
    """Plan the night, write it, and say how many stars it took."""
    conditions = Conditions(
        site=site_from(arguments.site),
        weather=Weather(
            pressure_hpa=arguments.pressure,
            temperature_c=arguments.temperature,
            relative_humidity=arguments.humidity,
            wavelength_um=arguments.wavelength,
        ),
        dut1_s=arguments.dut1,
    )
    start, end = utc_from(arguments.start), utc_from(arguments.end)
    plan = plan_night(read_catalog(arguments.catalog), conditions, start, end, arguments.count)
    write_plan(arguments.out, plan)
    return f"planned {len(plan)} of {arguments.count} stars\n"


def main(argv: Sequence[str] | None = None) -> int:
    """Run a command: 0 when done, EXIT_REFUSED for input it refuses."""
    arguments = parser().parse_args(argv)
    try:
        report = run_fit(arguments) if arguments.command == "fit" else run_plan(arguments)
    except (ValueError, OSError) as error:
        sys.stderr.write(f"{error}\n")
        return EXIT_REFUSED
    sys.stdout.write(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
