"""The golden frame and geoid vectors in golden/frames (ICS-014).

GeographicLib generates them (golden/frames/generate.sh); CI regenerates them
in the ics-cpp image and requires an exact match. These tests check them
independently: well-formed, covering the edge cases, and agreeing with a
closed-form WGS84 geodetic-to-ECEF and ECEF-to-ENU computation to 1 µm. The
geoid rows must satisfy h = H + N and stay within EGM96's range.
"""

import csv
import math
from pathlib import Path

import pytest

FRAMES: Path = Path(__file__).resolve().parents[2] / "golden" / "frames"
# WGS84, as docs/frames-and-time.md gives it.
SEMI_MAJOR_AXIS_M: float = 6_378_137.0
FLATTENING: float = 1 / 298.257223563
# Far tighter than the 1 mm ICS-017 must meet; doubles and GeographicLib's
# nanometre output both leave room for it.
TOLERANCE_M: float = 1e-6
# GeoidEval prints heights to 0.1 mm.
GEOID_TOLERANCE_M: float = 1e-4
# EGM96 geoid heights lie between about -107 m and +86 m; cubic interpolation
# may overshoot the grid slightly.
GEOID_RANGE_M: tuple[float, float] = (-108.0, 87.0)
# Longitudes this close to ±180° test the antimeridian.
ANTIMERIDIAN_DEG: float = 179.9

type Row = dict[str, str]
type Vector = tuple[float, float, float]


def read_rows(name: str) -> list[Row]:
    """The rows of a CSV file in golden/frames."""
    with (FRAMES / name).open(newline="") as file:
        return list(csv.DictReader(file))


def number(row: Row, column: str) -> float:
    """One column of a row, as a number."""
    return float(row[column])


def ecef(latitude_deg: float, longitude_deg: float, height_m: float) -> Vector:
    """WGS84 geodetic to ECEF, in metres."""
    e2 = FLATTENING * (2 - FLATTENING)
    latitude, longitude = math.radians(latitude_deg), math.radians(longitude_deg)
    prime_vertical = SEMI_MAJOR_AXIS_M / math.sqrt(1 - e2 * math.sin(latitude) ** 2)
    return (
        (prime_vertical + height_m) * math.cos(latitude) * math.cos(longitude),
        (prime_vertical + height_m) * math.cos(latitude) * math.sin(longitude),
        (prime_vertical * (1 - e2) + height_m) * math.sin(latitude),
    )


def enu(origin: Vector, point: Vector) -> Vector:
    """A geodetic point in the east-north-up frame of a geodetic origin, in metres."""
    dx, dy, dz = (p - o for p, o in zip(ecef(*point), ecef(*origin), strict=True))
    latitude, longitude = math.radians(origin[0]), math.radians(origin[1])
    sin_lat, cos_lat = math.sin(latitude), math.cos(latitude)
    sin_lon, cos_lon = math.sin(longitude), math.cos(longitude)
    return (
        -sin_lon * dx + cos_lon * dy,
        -sin_lat * cos_lon * dx - sin_lat * sin_lon * dy + cos_lat * dz,
        cos_lat * cos_lon * dx + cos_lat * sin_lon * dy + sin_lat * dz,
    )


def geodetic(row: Row, prefix: str = "") -> Vector:
    """The geodetic point in a row, optionally from columns named with a prefix."""
    columns = (f"{prefix}latitude_deg", f"{prefix}longitude_deg", f"{prefix}height_ellipsoid_m")
    return (number(row, columns[0]), number(row, columns[1]), number(row, columns[2]))


def test_outputs_repeat_their_inputs_in_order() -> None:
    pairs = (
        ("geodetic-ecef.csv", "geodetic-points.csv"),
        ("geodetic-enu.csv", "enu-cases.csv"),
        ("egm96-5.csv", "geoid-points.csv"),
    )
    for output, source in pairs:
        inputs = read_rows(f"inputs/{source}")
        outputs = read_rows(output)
        assert [{key: row[key] for key in inputs[0]} for row in outputs] == inputs, output


def test_ids_are_unique() -> None:
    for name in ("geodetic-ecef.csv", "geodetic-enu.csv", "egm96-5.csv"):
        ids = [row["id"] for row in read_rows(name)]
        assert len(ids) == len(set(ids)), name


@pytest.mark.parametrize("row", read_rows("geodetic-ecef.csv"), ids=lambda row: row["id"])
def test_geodetic_to_ecef_matches_closed_form(row: Row) -> None:
    expected = ecef(*geodetic(row))
    actual = (number(row, "x_m"), number(row, "y_m"), number(row, "z_m"))
    assert math.dist(actual, expected) < TOLERANCE_M


@pytest.mark.parametrize("row", read_rows("geodetic-enu.csv"), ids=lambda row: row["id"])
def test_geodetic_to_enu_matches_closed_form(row: Row) -> None:
    expected = enu(geodetic(row, "origin_"), geodetic(row))
    actual = (number(row, "east_m"), number(row, "north_m"), number(row, "up_m"))
    assert math.dist(actual, expected) < TOLERANCE_M


@pytest.mark.parametrize("row", read_rows("egm96-5.csv"), ids=lambda row: row["id"])
def test_ellipsoid_height_is_msl_plus_geoid_height(row: Row) -> None:
    geoid_height = number(row, "geoid_height_m")
    assert GEOID_RANGE_M[0] <= geoid_height <= GEOID_RANGE_M[1]
    total = number(row, "msl_height_m") + geoid_height
    assert abs(number(row, "height_ellipsoid_m") - total) <= GEOID_TOLERANCE_M


def test_cases_cover_the_edges() -> None:
    points = [geodetic(row) for row in read_rows("geodetic-ecef.csv")]
    assert {90.0, -90.0} <= {latitude for latitude, _, _ in points}
    assert any(abs(longitude) > ANTIMERIDIAN_DEG for _, longitude, _ in points)
    assert any(height < 0 for _, _, height in points)
    crossings = [
        row
        for row in read_rows("geodetic-enu.csv")
        if min(abs(number(row, "origin_longitude_deg")), abs(number(row, "longitude_deg"))) > ANTIMERIDIAN_DEG
        and number(row, "origin_longitude_deg") * number(row, "longitude_deg") < 0
    ]
    assert crossings, "no ENU case crosses the antimeridian"
    geoid_heights = [number(row, "geoid_height_m") for row in read_rows("egm96-5.csv")]
    assert min(geoid_heights) < -100 < 80 < max(geoid_heights)
