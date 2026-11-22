"""The calibration's input files, as the capture side writes them (ICS-042).

targets.csv holds one row per target point found in a view of the board:
the view's name, the point's number on the board, its place on the board in
metres, and its pixel. sightings.csv holds one row per sighting of a known
direction (a surveyed fiducial, or a star's observed place): the mount's line
of sight at the frame's time, through the pointing model, the direction, both
in radians, and the pixel it was seen on.
"""

import csv
import math
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from ics_models.camera.model import CameraError
from ics_models.camera.pose import View
from ics_models.pointing.model import AltAz

Row = dict[str, str]

TARGET_COLUMNS: tuple[str, ...] = ("view", "point", "board_x_m", "board_y_m", "u_px", "v_px")
SIGHTING_COLUMNS: tuple[str, ...] = (
    "los_az_rad",
    "los_el_rad",
    "target_az_rad",
    "target_el_rad",
    "u_px",
    "v_px",
)


@dataclass(frozen=True)
class TargetPoint:
    """A target point found in a view: the view, the point's number, its place on the board and its pixel."""

    view: str
    point: int
    board_x_m: float
    board_y_m: float
    u_px: float
    v_px: float


@dataclass(frozen=True)
class Sighting:
    """A known direction seen on a pixel, with the mount's line of sight at the frame's time."""

    line_of_sight: AltAz
    target: AltAz
    u_px: float
    v_px: float


def number(row: Row, column: str) -> float:
    """A row's finite number. CameraError naming the column otherwise."""
    text = row.get(column) or ""
    try:
        value = float(text)
    except ValueError:
        raise CameraError(column, f"not a number: {text!r}") from None
    if not math.isfinite(value):
        raise CameraError(column, f"not a finite number: {text!r}")
    return value


def read_rows(path: Path, columns: tuple[str, ...]) -> list[Row]:
    """A CSV file's rows. CameraError when a column is missing."""
    with path.open(newline="") as file:
        reader = csv.DictReader(file)
        missing = [column for column in columns if column not in (reader.fieldnames or [])]
        if missing:
            raise CameraError(str(path), f"lacks the columns {', '.join(missing)}")
        return list(reader)


def target_from(row: Row) -> TargetPoint:
    """A target point from its row."""
    view = row.get("view") or ""
    if not view:
        raise CameraError("view", "is empty")
    point = number(row, "point")
    if point != int(point):
        raise CameraError("point", f"not a whole number: {point}")
    values = [number(row, column) for column in TARGET_COLUMNS[2:]]
    return TargetPoint(view, int(point), values[0], values[1], values[2], values[3])


def sighting_from(row: Row) -> Sighting:
    """A sighting from its row."""
    values = [number(row, column) for column in SIGHTING_COLUMNS]
    return Sighting(AltAz(values[0], values[1]), AltAz(values[2], values[3]), values[4], values[5])


def read_targets(path: Path) -> list[TargetPoint]:
    """A calibration's target points. CameraError naming the line of a bad row, or for a point twice in a view."""
    points: list[TargetPoint] = []
    for line, row in enumerate(read_rows(path, TARGET_COLUMNS), start=2):
        try:
            points.append(target_from(row))
        except CameraError as error:
            raise CameraError(f"{path}:{line}", str(error)) from None
    if len({(point.view, point.point) for point in points}) != len(points):
        raise CameraError(str(path), "holds a point more than once in a view")
    return points


def read_sightings(path: Path) -> list[Sighting]:
    """A calibration's sightings. CameraError naming the line of a bad row."""
    sightings: list[Sighting] = []
    for line, row in enumerate(read_rows(path, SIGHTING_COLUMNS), start=2):
        try:
            sightings.append(sighting_from(row))
        except CameraError as error:
            raise CameraError(f"{path}:{line}", str(error)) from None
    return sightings


def write_targets(path: Path, points: Sequence[TargetPoint]) -> None:
    """Write target points under their header."""
    with path.open("w", newline="") as file:
        writer = csv.writer(file, lineterminator="\n")
        writer.writerow(TARGET_COLUMNS)
        for p in points:
            writer.writerow([p.view, p.point, repr(p.board_x_m), repr(p.board_y_m), repr(p.u_px), repr(p.v_px)])


def write_sightings(path: Path, sightings: Sequence[Sighting]) -> None:
    """Write sightings under their header."""
    with path.open("w", newline="") as file:
        writer = csv.writer(file, lineterminator="\n")
        writer.writerow(SIGHTING_COLUMNS)
        for s in sightings:
            values = (s.line_of_sight.azimuth, s.line_of_sight.elevation, s.target.azimuth, s.target.elevation)
            writer.writerow([repr(value) for value in (*values, s.u_px, s.v_px)])


def views_of(points: Sequence[TargetPoint]) -> dict[str, View]:
    """The points grouped by view, in the order the views first appear."""
    grouped: dict[str, list[TargetPoint]] = {}
    for point in points:
        grouped.setdefault(point.view, []).append(point)
    return {
        name: View(
            board=np.array([[p.board_x_m, p.board_y_m] for p in members]),
            pixels=np.array([[p.u_px, p.v_px] for p in members]),
        )
        for name, members in grouped.items()
    }


def split_targets(
    points: Sequence[TargetPoint], fraction: float, seed: int
) -> tuple[list[TargetPoint], list[TargetPoint]]:
    """The points to fit and those held out: fraction of each view's, at random but the same for a seed."""
    if not 0.0 < fraction < 1.0:
        raise CameraError("split", f"a held-out fraction of {fraction} is not between 0 and 1")
    generator = np.random.default_rng(seed)
    kept: list[TargetPoint] = []
    held: list[TargetPoint] = []
    for name in views_of(points):
        members = [point for point in points if point.view == name]
        order = generator.permutation(len(members))
        count = round(len(members) * fraction)
        held.extend(members[int(i)] for i in sorted(order[:count]))
        kept.extend(members[int(i)] for i in sorted(order[count:]))
    return kept, held


def split_sightings(sightings: Sequence[Sighting], fraction: float, seed: int) -> tuple[list[Sighting], list[Sighting]]:
    """The sightings to fit and those held out: fraction of them, at random but the same for a seed."""
    if not 0.0 < fraction < 1.0:
        raise CameraError("split", f"a held-out fraction of {fraction} is not between 0 and 1")
    order = np.random.default_rng(seed).permutation(len(sightings))
    count = round(len(sightings) * fraction)
    return [sightings[int(i)] for i in sorted(order[count:])], [sightings[int(i)] for i in sorted(order[:count])]
