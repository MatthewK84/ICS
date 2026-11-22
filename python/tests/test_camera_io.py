"""The camera model's files and command line (ICS-042): inputs, parameter files and golden vectors."""

import json
import math
from pathlib import Path

import numpy as np
import pytest

from ics_golden.camera import EXIT_USAGE
from ics_golden.camera import main as golden_main
from ics_models.camera.__main__ import EXIT_REFUSED, main
from ics_models.camera.boresight import pixel_of_sky, sky_of_pixel
from ics_models.camera.model import CameraError, project, undistort
from ics_models.camera.observations import (
    TargetPoint,
    read_sightings,
    read_targets,
    split_sightings,
    split_targets,
    write_sightings,
    write_targets,
)
from ics_models.camera.parameters import camera_from, read_parameters
from ics_models.camera.synth import CAMERAS, camera_named, synthetic_sightings, synthetic_targets
from ics_models.pointing.model import AltAz

GOLDEN: Path = Path(__file__).resolve().parents[2] / "golden" / "camera"
GOLDEN_FILES: tuple[str, ...] = ("narrow.json", "wide.json", "mwir.json", "project.csv", "undistort.csv", "sky.csv")
RELATIVE: float = 1e-12
C3_PX: float = 0.5
HELD_PER_VIEW: int = 2


def same_field(written: str, golden: str) -> bool:
    """Whether a field is the golden one: a number to RELATIVE of the larger of it and 1, anything else exactly."""
    try:
        written_value, golden_value = float(written), float(golden)
    except ValueError:
        return written == golden
    return math.isclose(written_value, golden_value, rel_tol=RELATIVE, abs_tol=RELATIVE)


def fields_of(path: Path) -> list[list[str]]:
    """A file's lines, as fields split on commas, colons and spaces."""
    return [line.replace(",", " ").replace(":", " ").split() for line in path.read_text().splitlines()]


def rows_of(name: str) -> list[dict[str, str]]:
    """A golden CSV's rows, by column name."""
    lines = (GOLDEN / name).read_text().splitlines()
    header = lines[0].split(",")
    return [dict(zip(header, line.split(","), strict=True)) for line in lines[1:]]


def synthetic_files(directory: Path, name: str = "wide") -> tuple[Path, Path]:
    """A synthetic calibration's target and sighting files."""
    camera = camera_named(name)
    generator = np.random.default_rng(42)
    points, _ = synthetic_targets(generator, camera.intrinsics, 8, 0.2)
    sightings = synthetic_sightings(generator, camera, 32, 0.2)
    targets_path, sightings_path = directory / "targets.csv", directory / "sightings.csv"
    write_targets(targets_path, points)
    write_sightings(sightings_path, sightings)
    return targets_path, sightings_path


def fit_arguments(targets: Path, sightings: Path, out: Path, *extra: str) -> list[str]:
    """The fit command for the wide camera."""
    return [
        "fit", str(targets), str(sightings), "--station", "s1", "--camera", "wide",
        "--width", "1280", "--height", "800", "--mirrored", "--model-id", "s1-wide", "--out", str(out), *extra,
    ]  # fmt: skip


@pytest.fixture(scope="module")
def written_directory(tmp_path_factory: pytest.TempPathFactory) -> Path:
    directory = tmp_path_factory.mktemp("camera")
    assert golden_main([str(directory)]) == 0
    return directory


@pytest.mark.parametrize("name", GOLDEN_FILES)
def test_golden_files_are_the_references_output(written_directory: Path, name: str) -> None:
    written, golden = fields_of(written_directory / name), fields_of(GOLDEN / name)
    assert len(written) == len(golden)
    for line, (written_row, golden_row) in enumerate(zip(written, golden, strict=True)):
        assert len(written_row) == len(golden_row), (name, line)
        assert all(same_field(*pair) for pair in zip(written_row, golden_row, strict=True)), (name, line)


def test_golden_usage_is_refused() -> None:
    assert golden_main([]) == EXIT_USAGE


def test_the_golden_parameter_files_load_as_the_cameras() -> None:
    for name, camera in CAMERAS:
        assert read_parameters(GOLDEN / f"{name}.json") == camera


def test_the_golden_vectors_hold_both_ways() -> None:
    for row in rows_of("project.csv"):
        point = (float(row["x_m"]), float(row["y_m"]), float(row["z_m"]))
        u, v = project(point, camera_named(row["camera"]).intrinsics)
        assert (u, v) == pytest.approx((float(row["u_px"]), float(row["v_px"])), abs=1e-9)
    for row in rows_of("undistort.csv"):
        x, y = undistort(float(row["u_px"]), float(row["v_px"]), camera_named(row["camera"]).intrinsics)
        assert (x, y) == pytest.approx((float(row["x"]), float(row["y"])), abs=1e-12)
    for row in rows_of("sky.csv"):
        camera = camera_named(row["camera"])
        sight = AltAz(float(row["los_az_rad"]), float(row["los_el_rad"]))
        direction = AltAz(float(row["az_rad"]), float(row["el_rad"]))
        assert sky_of_pixel(float(row["u_px"]), float(row["v_px"]), sight, camera) == direction
        u, v = pixel_of_sky(direction, sight, camera)
        assert (u, v) == pytest.approx((float(row["u_px"]), float(row["v_px"])), abs=1e-6)


def test_inputs_round_trip(tmp_path: Path) -> None:
    targets, sightings = synthetic_files(tmp_path)
    points = read_targets(targets)
    write_targets(tmp_path / "again.csv", points)
    assert read_targets(tmp_path / "again.csv") == points
    seen = read_sightings(sightings)
    write_sightings(tmp_path / "again-s.csv", seen)
    assert read_sightings(tmp_path / "again-s.csv") == seen


@pytest.mark.parametrize(
    ("text", "match"),
    [
        ("view,point,board_x_m,board_y_m,u_px\n", "lacks the columns v_px"),
        ("view,point,board_x_m,board_y_m,u_px,v_px\n,1,0,0,1,1\n", ":2: view: is empty"),
        ("view,point,board_x_m,board_y_m,u_px,v_px\na,1.5,0,0,1,1\n", "not a whole number"),
        ("view,point,board_x_m,board_y_m,u_px,v_px\na,1,x,0,1,1\n", "board_x_m: not a number"),
        ("view,point,board_x_m,board_y_m,u_px,v_px\na,1,nan,0,1,1\n", "not a finite number"),
        ("view,point,board_x_m,board_y_m,u_px,v_px\na,1,0,0,1,1\na,1,0,0,2,2\n", "more than once"),
    ],
)
def test_bad_target_files_are_refused(tmp_path: Path, text: str, match: str) -> None:
    path = tmp_path / "targets.csv"
    path.write_text(text)
    with pytest.raises(CameraError, match=match):
        read_targets(path)


def test_a_bad_sighting_is_refused_with_its_line(tmp_path: Path) -> None:
    path = tmp_path / "sightings.csv"
    path.write_text("los_az_rad,los_el_rad,target_az_rad,target_el_rad,u_px,v_px\n0,0,0,0,1,1\n0,0,0,0,1,\n")
    with pytest.raises(CameraError, match=":3: v_px"):
        read_sightings(path)


def test_splits_hold_out_a_quarter_and_refuse_bad_fractions() -> None:
    points = [TargetPoint(view, i, 0.0, 0.0, 0.0, 0.0) for view in ("a", "b") for i in range(8)]
    kept, held = split_targets(points, 0.25, 1)
    assert len(held) == 2 * HELD_PER_VIEW
    assert sorted(p.view for p in held) == ["a", "a", "b", "b"]
    assert set(kept).isdisjoint(held)
    with pytest.raises(CameraError, match="not between 0 and 1"):
        split_targets(points, 1.0, 1)
    with pytest.raises(CameraError, match="not between 0 and 1"):
        split_sightings([], 0.0, 1)


def test_the_command_line_fits_and_writes_a_valid_file(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    targets, sightings = synthetic_files(tmp_path)
    out = tmp_path / "wide.json"
    assert main(fit_arguments(targets, sightings, out)) == 0
    report = capsys.readouterr().out
    assert "C3: 0.5 px or less" in report
    content = json.loads(out.read_text())
    assert content["fit"]["held_out_rms_px"] <= C3_PX
    assert content["fit"]["held_out_sighting_rms_px"] <= C3_PX
    camera = camera_from(content)
    truth = camera_named("wide")
    assert camera.boresight.mirrored
    assert camera.intrinsics.fx == pytest.approx(truth.intrinsics.fx, rel=1e-2)


def test_the_command_line_fits_only_the_terms_named(tmp_path: Path) -> None:
    targets, sightings = synthetic_files(tmp_path)
    out = tmp_path / "wide.json"
    assert main(fit_arguments(targets, sightings, out, "--distortion", "k1,k2")) == 0
    content = json.loads(out.read_text())
    assert content["fit"]["distortion_terms"] == ["k1", "k2"]
    assert content["intrinsics"]["k3"] == {"value": 0.0, "sigma": 0.0}


@pytest.mark.parametrize("extra", [("--distortion", "k9"), ("--hold-out", "2")])
def test_the_command_line_refuses_bad_input(
    tmp_path: Path, capsys: pytest.CaptureFixture[str], extra: tuple[str, ...]
) -> None:
    targets, sightings = synthetic_files(tmp_path)
    assert main(fit_arguments(targets, sightings, tmp_path / "out.json", *extra)) == EXIT_REFUSED
    assert capsys.readouterr().err


def test_the_command_line_needs_points_and_sightings(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    targets, sightings = synthetic_files(tmp_path)
    write_sightings(sightings, [])
    assert main(fit_arguments(targets, sightings, tmp_path / "out.json")) == EXIT_REFUSED
    assert "needs target points and sightings" in capsys.readouterr().err


def test_parameter_files_the_schema_refuses_are_refused(tmp_path: Path) -> None:
    content = json.loads((GOLDEN / "wide.json").read_text())
    content["camera"] = "zoom"
    with pytest.raises(CameraError, match="camera-model"):
        camera_from(content)
    path = tmp_path / "broken.json"
    path.write_text("{")
    with pytest.raises(CameraError, match="not JSON"):
        read_parameters(path)
