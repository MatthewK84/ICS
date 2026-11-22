"""The camera model's command line (ICS-042).

    python -m ics_models.camera fit TARGETS.csv SIGHTINGS.csv --station ID --camera narrow|wide|mwir \
        --width PX --height PX [--mirrored] --model-id ID --out CAMERA.json

fit holds a quarter of each view's target points, and a quarter of the
sightings, out at random. It fits the intrinsics, the distortion terms named
by --distortion (all five unless told) and the boresight to the rest
together, measures the reprojection RMS on what was held out (C3), writes the
parameter file and prints a summary. It exits 2, saying why on stderr, for
input it refuses.
"""

import argparse
import hashlib
import sys
from collections.abc import Sequence
from datetime import UTC, datetime
from pathlib import Path

from ics_models.camera.fit import (
    BORESIGHT_NAMES,
    DISTORTION_NAMES,
    INTRINSIC_NAMES,
    CameraFit,
    Setup,
    camera_target_rms,
    fit_camera,
    intrinsics_vector,
    sighting_rms,
)
from ics_models.camera.model import CameraError
from ics_models.camera.observations import read_sightings, read_targets, split_sightings, split_targets, views_of
from ics_models.camera.parameters import HeldOut, Provenance, write_parameters

EXIT_REFUSED: int = 2
C3_PX: float = 0.5


def parser() -> argparse.ArgumentParser:
    """The command line's arguments."""
    top = argparse.ArgumentParser(prog="python -m ics_models.camera", description=__doc__.splitlines()[0])
    commands = top.add_subparsers(dest="command", required=True)
    fitting = commands.add_parser("fit", help="fit a camera to target points and sightings")
    fitting.add_argument("targets", type=Path)
    fitting.add_argument("sightings", type=Path)
    fitting.add_argument("--station", required=True)
    fitting.add_argument("--camera", required=True, choices=("narrow", "wide", "mwir"))
    fitting.add_argument("--width", type=int, required=True, help="the image's width in pixels")
    fitting.add_argument("--height", type=int, required=True, help="the image's height in pixels")
    fitting.add_argument("--mirrored", action="store_true", help="the optics mirror the image")
    fitting.add_argument("--distortion", default=",".join(DISTORTION_NAMES), help="comma-separated terms to fit")
    fitting.add_argument("--model-id", required=True)
    fitting.add_argument("--hold-out", type=float, default=0.25, help="the fraction held out")
    fitting.add_argument("--seed", type=int, default=42, help="picks the points held out")
    fitting.add_argument("--out", type=Path, required=True)
    return top


def summary(fit: CameraFit, held: HeldOut) -> str:
    """What the fit found, one parameter a line."""
    lines = [
        f"points used {fit.points}, RMS {fit.target_rms_px:.3f} px; sightings used {fit.sightings}, "
        f"RMS {fit.sighting_rms_px:.3f} px",
        f"points held out {held.points}, RMS {held.points_rms_px:.3f} px; sightings held out {held.sightings}, "
        f"RMS {held.sightings_rms_px:.3f} px (C3: {C3_PX} px or less)",
    ]
    values = intrinsics_vector(fit.camera.intrinsics)
    for name, value, sigma in zip(INTRINSIC_NAMES, values, fit.sigmas, strict=False):
        lines.append(f"{name:>6} {value:16.8g} ± {sigma:.3g}")
    angles = (fit.camera.boresight.yaw, fit.camera.boresight.pitch, fit.camera.boresight.roll)
    for name, value, sigma in zip(BORESIGHT_NAMES, angles, fit.sigmas[len(INTRINSIC_NAMES) :], strict=True):
        lines.append(f"{name:>6} {value * 1e6:13.2f} ± {sigma * 1e6:.2f} µrad")
    return "\n".join(lines) + "\n"


def run_fit(arguments: argparse.Namespace) -> str:
    """Fit, write the model, and say how it went."""
    points = read_targets(arguments.targets)
    sightings = read_sightings(arguments.sightings)
    if not points or not sightings:
        raise CameraError("fit", "needs target points and sightings")
    terms = tuple(term.strip() for term in arguments.distortion.split(",") if term.strip())
    setup = Setup(arguments.width, arguments.height, arguments.mirrored, terms)
    kept_points, held_points = split_targets(points, arguments.hold_out, arguments.seed)
    kept_sightings, held_sightings = split_sightings(sightings, arguments.hold_out, arguments.seed)
    fit = fit_camera(kept_points, kept_sightings, setup)
    held = HeldOut(
        len(held_points),
        camera_target_rms(held_points, fit),
        len(held_sightings),
        sighting_rms(held_sightings, fit.camera),
    )
    provenance = Provenance(
        model_id=arguments.model_id,
        station_id=arguments.station,
        camera=arguments.camera,
        created=datetime.now(UTC),
        targets_sha256=hashlib.sha256(arguments.targets.read_bytes()).hexdigest(),
        sightings_sha256=hashlib.sha256(arguments.sightings.read_bytes()).hexdigest(),
        views=len(views_of(points)),
        held_out=held,
    )
    write_parameters(arguments.out, fit, setup, provenance)
    return summary(fit, held)


def main(argv: Sequence[str] | None = None) -> int:
    """Run a command: 0 when done, EXIT_REFUSED for input it refuses."""
    arguments = parser().parse_args(argv)
    try:
        report = run_fit(arguments)
    except (ValueError, OSError) as error:
        sys.stderr.write(f"{error}\n")
        return EXIT_REFUSED
    sys.stdout.write(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
