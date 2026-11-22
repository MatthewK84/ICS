"""Golden vectors for the camera model (ICS-042), which ICS-058's C++ port must match.

golden/camera/narrow.json, wide.json and mwir.json are parameter files for
the three made-up cameras in ics_models.camera.synth: the wide one mirrors
and has the strongest distortion. Not fitted; every standard deviation is
zero. For each camera, across a grid of pixels from corner to corner:

- project.csv gives points in the camera frame at three depths and their pixels;
- undistort.csv gives each pixel's normalised image point;
- sky.csv gives each pixel's direction on the sky along four lines of sight.

Every number is printed to round-trip exactly. The port must give each pixel
from its point, each normalised point from its pixel, each direction from its
pixel and each pixel from its direction, within 1e-12 of the larger of the
number and 1.

Regenerate from python/ after changing the model:

    uv run --locked python -m ics_golden.camera ../golden/camera
"""

import hashlib
import json
import math
import sys
from datetime import UTC, datetime
from pathlib import Path

from ics_models.camera.boresight import Camera, sky_of_pixel
from ics_models.camera.fit import CameraFit, Setup
from ics_models.camera.model import project, undistort
from ics_models.camera.parameters import HeldOut, Provenance, document, validate
from ics_models.camera.synth import CAMERAS, terms_for
from ics_models.pointing.model import AltAz

GRID: int = 5
DEPTHS_M: tuple[float, ...] = (1.0, 37.5, 2500.0)
LINES_OF_SIGHT_DEG: tuple[tuple[float, float], ...] = ((0.0, 5.0), (95.0, 30.0), (200.0, 60.0), (330.0, 85.0))
CREATED: datetime = datetime(2026, 11, 22, tzinfo=UTC)
EXIT_USAGE: int = 2


def pixels_of(camera: Camera) -> list[tuple[float, float]]:
    """A GRID x GRID grid of pixels from the image's top-left corner to its bottom-right."""
    width, height = camera.intrinsics.width - 1.0, camera.intrinsics.height - 1.0
    steps = [index / (GRID - 1) for index in range(GRID)]
    return [(column * width, row * height) for row in steps for column in steps]


def line(values: tuple[str | float, ...]) -> str:
    """A CSV line, numbers printed to round-trip."""
    return ",".join(value if isinstance(value, str) else repr(value) for value in values) + "\n"


def project_csv() -> str:
    """project.csv's text."""
    lines = ["camera,x_m,y_m,z_m,u_px,v_px\n"]
    for name, camera in CAMERAS:
        for u, v in pixels_of(camera):
            x, y = undistort(u, v, camera.intrinsics)
            for depth in DEPTHS_M:
                point = (x * depth, y * depth, depth)
                lines.append(line((name, *point, *project(point, camera.intrinsics))))
    return "".join(lines)


def undistort_csv() -> str:
    """undistort.csv's text."""
    lines = ["camera,u_px,v_px,x,y\n"]
    for name, camera in CAMERAS:
        for u, v in pixels_of(camera):
            lines.append(line((name, u, v, *undistort(u, v, camera.intrinsics))))
    return "".join(lines)


def sky_csv() -> str:
    """sky.csv's text."""
    lines = ["camera,los_az_rad,los_el_rad,u_px,v_px,az_rad,el_rad\n"]
    for name, camera in CAMERAS:
        for azimuth, elevation in LINES_OF_SIGHT_DEG:
            sight = AltAz(math.radians(azimuth), math.radians(elevation))
            for u, v in pixels_of(camera):
                direction = sky_of_pixel(u, v, sight, camera)
                values = (sight.azimuth, sight.elevation, u, v, direction.azimuth, direction.elevation)
                lines.append(line((name, *values)))
    return "".join(lines)


def model_json(name: str, camera: Camera, digest: str) -> str:
    """A golden camera's parameter file."""
    fit = CameraFit(camera, (0.0,) * 12, {}, 0.0, 0.0, 1, 1)
    setup = Setup(camera.intrinsics.width, camera.intrinsics.height, camera.boresight.mirrored, terms_for(name))
    provenance = Provenance(f"golden-{name}", "golden", name, CREATED, digest, digest, 3, HeldOut(1, 0.0, 1, 0.0))
    content = document(fit, setup, provenance)
    validate(content)
    return json.dumps(content, indent=2) + "\n"


def write(directory: Path) -> None:
    """Write the parameter files and vectors into directory."""
    directory.mkdir(parents=True, exist_ok=True)
    texts = {"project.csv": project_csv(), "undistort.csv": undistort_csv(), "sky.csv": sky_csv()}
    for file_name, text in texts.items():
        (directory / file_name).write_text(text)
    digest = hashlib.sha256(texts["sky.csv"].encode()).hexdigest()
    for name, camera in CAMERAS:
        (directory / f"{name}.json").write_text(model_json(name, camera, digest))


def main(argv: list[str]) -> int:
    """Write the golden files into the directory named."""
    if len(argv) != 1:
        sys.stderr.write("usage: python -m ics_golden.camera DIRECTORY\n")
        return EXIT_USAGE
    write(Path(argv[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
