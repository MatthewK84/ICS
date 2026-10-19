"""Golden vectors for the closed-loop tracker (ICS-038), which the C++ port, ics::tracker, must match.

Each CSV file has a header, and every number is printed to round-trip:

- frame-NN.pgm: synthetic frames, as plain (P2) PGM, 16-bit: a sky of
  Gaussian noise with dark or bright targets, by day and by night;
- centroids.csv: find_target on windows of those frames, with what it found or
  ok = 0 when it found nothing;
- windows.csv: window_around for centres near and past the frame's edges;
- geometry.csv: pixels' tangent-plane points and directions, and the pixels
  those directions fall on, for cameras rolled and mirrored, at lines of sight
  low, high and across north;
- estimator.csv: an alpha-beta track of a weaving target, measured every 10 ms,
  with the track after each measurement;
- demands.csv: demands for targets and mounts at random states.

python -m ics_golden.tracker DIRECTORY writes them.
"""

import math
import sys
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from ics_models.pointing.model import AltAz
from ics_models.tracker.centroid import (
    CentroidSettings,
    Image,
    Polarity,
    TrackerError,
    Window,
    find_target,
    window_around,
)
from ics_models.tracker.demand import AxisState, demand
from ics_models.tracker.estimator import AngleEstimator, AngleState, EstimatorSettings
from ics_models.tracker.geometry import CameraGeometry, pixel_of_sky, sky_of_pixel, tangent_of_pixel

EXIT_USAGE: int = 2
SEED: int = 38
FRAME_WIDTH: int = 64
FRAME_HEIGHT: int = 48
MAX_PIXEL: int = 65535
DAY_SKY: float = 20000.0
DAY_NOISE: float = 40.0
NIGHT_SKY: float = 12.0
NIGHT_NOISE: float = 2.0
STEP_NS: int = 10_000_000
STEPS: int = 100
START_NS: int = 1_760_000_000_000_000_000
DEMANDS: int = 12


@dataclass(frozen=True)
class Spot:
    """A Gaussian spot: centre in pixels, its sigma in pixels, and its depth or height in counts."""

    x: float
    y: float
    sigma: float
    amplitude: float


@dataclass(frozen=True)
class Scene:
    """A frame's sky, its noise, and its spots."""

    sky: float
    noise: float
    spots: tuple[Spot, ...]


SCENES: tuple[Scene, ...] = (
    Scene(DAY_SKY, DAY_NOISE, (Spot(30.3, 22.7, 1.6, -4000.0),)),
    Scene(NIGHT_SKY, NIGHT_NOISE, (Spot(40.6, 18.2, 1.2, 180.0),)),
    Scene(DAY_SKY, DAY_NOISE, (Spot(18.4, 20.1, 1.4, -3000.0), Spot(44.7, 30.9, 1.4, -3000.0))),
    Scene(DAY_SKY, DAY_NOISE, (Spot(24.0, 24.0, 5.0, -3000.0), Spot(50.2, 12.5, 1.2, -3000.0))),
    Scene(1000.0, 0.0, (Spot(20.5, 30.25, 1.0, 500.0),)),
    Scene(DAY_SKY, DAY_NOISE, ()),
    Scene(DAY_SKY, DAY_NOISE, (Spot(4.2, 40.6, 1.3, -3500.0),)),
)


def render(scene: Scene, rng: np.random.Generator) -> Image:
    """A frame of a scene, rounded and clipped to 16-bit counts."""
    rows, columns = np.mgrid[0:FRAME_HEIGHT, 0:FRAME_WIDTH].astype(np.float64)
    frame = scene.sky + scene.noise * rng.standard_normal((FRAME_HEIGHT, FRAME_WIDTH))
    for spot in scene.spots:
        squared = ((columns - spot.x) ** 2 + (rows - spot.y) ** 2) / (2.0 * spot.sigma**2)
        frame += spot.amplitude * np.exp(-squared)
    return np.clip(np.rint(frame), 0.0, MAX_PIXEL)


def pgm(frame: Image) -> str:
    """A frame as plain PGM: a header, then one row of counts a line."""
    rows = "".join(" ".join(str(int(value)) for value in row) + "\n" for row in frame)
    return f"P2\n{FRAME_WIDTH} {FRAME_HEIGHT}\n{MAX_PIXEL}\n{rows}"


def numbers(values: Iterable[float | int | str]) -> str:
    """Values joined by commas, each float printed to round-trip."""
    return ",".join(repr(float(value)) if isinstance(value, float) else str(value) for value in values)


@dataclass(frozen=True)
class CentroidCase:
    """find_target's inputs: a frame, a window, a prediction and the settings."""

    frame: int
    window: Window
    prediction: tuple[float, float]
    settings: CentroidSettings


def centroid_cases() -> tuple[CentroidCase, ...]:
    """Windows that find each kind of target, and some that find none."""
    day = CentroidSettings(Polarity.DARK, 5.0, 50.0, 3, 60)
    night = CentroidSettings(Polarity.BRIGHT, 5.0, 10.0, 2, 60)
    either = CentroidSettings(Polarity.EITHER, 5.0, 50.0, 3, 60)
    return (
        CentroidCase(0, Window(14, 7, 32, 32), (30.0, 23.0), day),
        CentroidCase(0, Window(14, 7, 32, 32), (30.0, 23.0), either),
        CentroidCase(0, Window(14, 7, 32, 32), (30.0, 23.0), night),
        CentroidCase(1, Window(25, 3, 31, 29), (40.0, 18.0), night),
        CentroidCase(2, Window(0, 0, 64, 48), (20.0, 20.0), day),
        CentroidCase(2, Window(0, 0, 64, 48), (40.0, 33.0), day),
        CentroidCase(3, Window(0, 0, 64, 48), (24.0, 24.0), day),
        CentroidCase(4, Window(5, 15, 30, 30), (20.0, 30.0), CentroidSettings(Polarity.BRIGHT, 5.0, 100.0, 1, 60)),
        CentroidCase(5, Window(10, 10, 32, 32), (26.0, 26.0), day),
        CentroidCase(6, Window(0, 28, 20, 20), (5.0, 40.0), day),
        CentroidCase(0, Window(14, 7, 32, 32), (30.0, 23.0), CentroidSettings(Polarity.DARK, 5.0, 50.0, 100, 200)),
    )


def centroid_rows(frames: list[Image]) -> str:
    """centroids.csv's text."""
    header = "frame,x,y,width,height,predicted_x,predicted_y,polarity,k_sigma,min_contrast,min_area,max_area"
    lines = [header + ",ok,found_x,found_y,area,peak,background,sigma\n"]
    for case in centroid_cases():
        window, settings = case.window, case.settings
        inputs: list[float | int | str] = [case.frame, window.x, window.y, window.width, window.height]
        inputs += [*case.prediction]
        inputs += [str(settings.polarity), settings.k_sigma, settings.min_contrast]
        inputs += [settings.min_area, settings.max_area]
        try:
            found = find_target(frames[case.frame], window, case.prediction, settings)
            outputs: list[float | int] = [1, found.x, found.y, found.area, found.peak, found.background, found.sigma]
        except TrackerError:
            outputs = [0, 0.0, 0.0, 0, 0.0, 0.0, 0.0]
        lines.append(numbers([*inputs, *outputs]) + "\n")
    return "".join(lines)


WINDOW_CENTRES: tuple[tuple[float, float], ...] = ((32.0, 24.0), (0.2, 0.4), (63.9, 47.9), (10.5, 30.49), (-1.0, 5.0))


def window_rows() -> str:
    """windows.csv's text."""
    lines = ["centre_x,centre_y,width,height,frame_width,frame_height,ok,x,y\n"]
    for centre in WINDOW_CENTRES:
        for size in ((16, 12), (64, 48), (65, 10)):
            try:
                window = window_around(centre, size, (FRAME_WIDTH, FRAME_HEIGHT))
                outputs = [1, window.x, window.y]
            except TrackerError:
                outputs = [0, 0, 0]
            lines.append(numbers([*centre, *size, FRAME_WIDTH, FRAME_HEIGHT, *outputs]) + "\n")
    return "".join(lines)


CAMERAS: tuple[CameraGeometry, ...] = (
    CameraGeometry(32.0, 24.0, 2.0e-5, 0.0, False),
    CameraGeometry(31.5, 23.25, 1.5e-5, 0.3, False),
    CameraGeometry(32.25, 24.5, 2.5e-5, -2.1, True),
)
SIGHTS: tuple[AltAz, ...] = (AltAz(1.2, 0.4), AltAz(6.25, 1.3), AltAz(0.01, 0.05))
PIXELS: tuple[tuple[float, float], ...] = ((32.0, 24.0), (0.0, 0.0), (63.0, 47.0), (40.25, 10.75))


def geometry_rows() -> str:
    """geometry.csv's text."""
    header = "boresight_x,boresight_y,plate_scale,roll,mirrored,sight_azimuth,sight_elevation,x,y"
    lines = [header + ",xi,eta,azimuth,elevation,back_x,back_y\n"]
    for camera in CAMERAS:
        for sight in SIGHTS:
            for x, y in PIXELS:
                xi, eta = tangent_of_pixel(x, y, camera)
                sky = sky_of_pixel(x, y, sight, camera)
                back = pixel_of_sky(sky, sight, camera)
                inputs = [camera.boresight_x, camera.boresight_y, camera.plate_scale, camera.roll, int(camera.mirrored)]
                outputs = [xi, eta, sky.azimuth, sky.elevation, *back]
                lines.append(numbers([*inputs, sight.azimuth, sight.elevation, x, y, *outputs]) + "\n")
    return "".join(lines)


def weave(seconds: float) -> AltAz:
    """The target the estimator tracks, crossing north: azimuth wraps from just under 2π."""
    return AltAz((6.27 + 0.05 * seconds + 0.002 * math.sin(3.0 * seconds)) % (2.0 * math.pi), 0.3 + 0.02 * seconds)


def estimator_rows(rng: np.random.Generator) -> str:
    """estimator.csv's text: the first row starts the track; each after is a measurement and the track after it."""
    settings = EstimatorSettings(0.5, 0.1)
    track = AngleEstimator(settings)
    first = weave(0.0)
    track.start(AngleState(START_NS, first.azimuth, first.elevation, 0.04, 0.0))
    lines = ["alpha,beta,time_ns,measured_azimuth,measured_elevation,azimuth,elevation,azimuth_rate,elevation_rate\n"]
    start = track.state()
    lines.append(numbers([0.5, 0.1, START_NS, first.azimuth, first.elevation, *state_values(start)]) + "\n")
    for step in range(1, STEPS + 1):
        time_ns = START_NS + step * STEP_NS
        truth = weave(step * STEP_NS / 1e9)
        measured = AltAz(truth.azimuth + 1e-5 * rng.standard_normal(), truth.elevation + 1e-5 * rng.standard_normal())
        state = track.update(time_ns, measured)
        lines.append(numbers([0.5, 0.1, time_ns, measured.azimuth, measured.elevation, *state_values(state)]) + "\n")
    return "".join(lines)


def state_values(state: AngleState) -> list[float]:
    """A track's angles and rates."""
    return [state.azimuth, state.elevation, state.azimuth_rate, state.elevation_rate]


def demand_rows(rng: np.random.Generator) -> str:
    """demands.csv's text."""
    header = "gain,target_time_ns,target_azimuth,target_elevation,target_azimuth_rate,target_elevation_rate"
    lines = [header + ",mount_time_ns,mount_azimuth,mount_elevation,mount_azimuth_rate,mount_elevation_rate"]
    lines[0] += ",azimuth_rate,elevation_rate\n"
    for _ in range(DEMANDS):
        lead_ns = int(rng.integers(0, 200_000_000))
        azimuth, elevation = (float(value) for value in rng.uniform([0.0, 0.0], [6.28, 1.4]))
        azimuth_rate, elevation_rate = (float(value) for value in rng.normal(0.0, 0.1, 2))
        target = AxisState(START_NS + lead_ns, azimuth, elevation, azimuth_rate, elevation_rate)
        mount = AxisState(START_NS, float(rng.uniform(0.0, 6.28)), float(rng.uniform(0.0, 1.4)), 0.05, -0.02)
        gain = float(rng.uniform(0.0, 5.0))
        rates = demand(target, mount, gain)
        values = [target.time_ns, target.azimuth, target.elevation, target.azimuth_rate, target.elevation_rate]
        values += [mount.time_ns, mount.azimuth, mount.elevation, mount.azimuth_rate, mount.elevation_rate]
        lines.append(numbers([gain, *values, *rates]) + "\n")
    return "".join(lines)


def write(directory: Path) -> None:
    """Write every golden file into directory."""
    directory.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(SEED)
    frames = [render(scene, rng) for scene in SCENES]
    for index, frame in enumerate(frames):
        (directory / f"frame-{index:02d}.pgm").write_text(pgm(frame))
    (directory / "centroids.csv").write_text(centroid_rows(frames))
    (directory / "windows.csv").write_text(window_rows())
    (directory / "geometry.csv").write_text(geometry_rows())
    (directory / "estimator.csv").write_text(estimator_rows(rng))
    (directory / "demands.csv").write_text(demand_rows(rng))


def main(argv: list[str]) -> int:
    """Write the golden files into the directory named."""
    if len(argv) != 1:
        sys.stderr.write("usage: python -m ics_golden.tracker DIRECTORY\n")
        return EXIT_USAGE
    write(Path(argv[0]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
