"""Which stars to take on a calibration night (ICS-035).

The night is cut into count evenly spaced slots. At each slot the planner
takes the catalog star, not yet taken, that lies between 20° and 85° of
elevation and farthest on the sky from every star already taken, so the stars
spread across azimuth and elevation as the fit needs. A slot with no star in
that band is left out.
"""

import csv
import math
from collections.abc import Sequence
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

from ics_models.pointing.astrometry import Conditions, Star, observed
from ics_models.pointing.model import AltAz, PointingError
from ics_models.pointing.observations import STAR_COLUMNS, read_rows, star_from

MIN_ELEVATION_RAD: float = math.radians(20.0)
MAX_ELEVATION_RAD: float = math.radians(85.0)
PLAN_COLUMNS: tuple[str, ...] = ("utc", "star_id", "azimuth_deg", "elevation_deg")


@dataclass(frozen=True)
class Planned:
    """A star to take, when, and where it will be seen."""

    utc: datetime
    star: Star
    sky: AltAz


def read_catalog(path: Path) -> list[Star]:
    """A catalog: one star per row, with STAR_COLUMNS. PointingError naming the line of a bad row."""
    stars: list[Star] = []
    for line, row in enumerate(read_rows(path, STAR_COLUMNS), start=2):
        try:
            stars.append(star_from(row))
        except PointingError as error:
            raise PointingError(f"{path}:{line}", str(error)) from None
    return stars


def separation(a: AltAz, b: AltAz) -> float:
    """The angle between two directions, in radians."""
    cosine = math.sin(a.elevation) * math.sin(b.elevation) + math.cos(a.elevation) * math.cos(b.elevation) * math.cos(
        a.azimuth - b.azimuth
    )
    return math.acos(max(-1.0, min(1.0, cosine)))


def pick(candidates: Sequence[tuple[Star, AltAz]], taken: Sequence[Planned]) -> tuple[Star, AltAz] | None:
    """The candidate farthest from every star taken, or None without a candidate."""
    best: tuple[Star, AltAz] | None = None
    best_gap = -1.0
    for star, sky in candidates:
        gap = min((separation(sky, planned.sky) for planned in taken), default=math.pi)
        if gap > best_gap:
            best, best_gap = (star, sky), gap
    return best


def plan_night(
    catalog: Sequence[Star], conditions: Conditions, start: datetime, end: datetime, count: int
) -> list[Planned]:
    """count stars across the night from start to end. PointingError for an empty or backward night."""
    if count < 1 or end <= start:
        raise PointingError("plan", "a plan needs at least one star and a night that ends after it starts")
    step = (end - start) / count
    taken: list[Planned] = []
    used: set[str] = set()
    for slot in range(count):
        when = start + step * (slot + 0.5)
        candidates: list[tuple[Star, AltAz]] = []
        for star in catalog:
            sky = observed(star, when, conditions)
            if star.star_id not in used and MIN_ELEVATION_RAD <= sky.elevation <= MAX_ELEVATION_RAD:
                candidates.append((star, sky))
        chosen = pick(candidates, taken)
        if chosen is not None:
            taken.append(Planned(utc=when, star=chosen[0], sky=chosen[1]))
            used.add(chosen[0].star_id)
    return taken


def write_plan(path: Path, plan: Sequence[Planned]) -> None:
    """Write a plan: when to take each star, and where it will be."""
    with path.open("w", newline="") as file:
        writer = csv.writer(file, lineterminator="\n")
        writer.writerow(PLAN_COLUMNS)
        for planned in plan:
            utc = planned.utc.isoformat().replace("+00:00", "Z")
            azimuth = f"{math.degrees(planned.sky.azimuth):.4f}"
            writer.writerow((utc, planned.star.star_id, azimuth, f"{math.degrees(planned.sky.elevation):.4f}"))
