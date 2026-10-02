"""Scripted engagements for the SITL rig (ICS-018).

An engagement file (TOML) scripts two quadrotors: a target and an
interceptor, one flown by PX4 and the other by ArduPilot. Each takes off from
its own home, climbs to its start point and waits there; when both are
waiting, both fly their paths at once. The file also states what must happen
for the engagement to pass. Every key is required and no other key is
allowed, so a misspelt key fails loudly instead of being ignored.

Points are [latitude_deg, longitude_deg, height_m], the height above the
vehicle's home. Both homes share the range's ground level, ground_msl_m.
"""

from __future__ import annotations

import math
import tomllib
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Final

AUTOPILOTS: Final = ("px4", "ardupilot")
ROLES: Final = ("target", "interceptor")
ENGAGEMENT_KEYS: Final = frozenset(
    {"name", "description", "ground_msl_m", "time_limit_s", "miss_distance_m", "waypoint_tolerance_m", *ROLES}
)
PROFILE_KEYS: Final = frozenset({"autopilot", "home", "speed_m_s", "start", "path"})
MAX_PATH: Final = 32
POINT_SIZE: Final = 3
PAIR_SIZE: Final = 2
MAX_MISS_M: Final = 10_000.0
MAX_HEIGHT_M: Final = 120.0
MAX_SPEED_M_S: Final = 12.0


class ProfileError(ValueError):
    """An engagement file that cannot be flown as written."""

    def __init__(self, where: str, problem: str) -> None:
        super().__init__(f"{where}: {problem}")


@dataclass(frozen=True)
class Waypoint:
    """A point to fly through: WGS84 latitude and longitude, and height above home."""

    latitude_deg: float
    longitude_deg: float
    height_m: float


@dataclass(frozen=True)
class Profile:
    """One vehicle's script."""

    role: str
    autopilot: str
    home: Waypoint
    speed_m_s: float
    start: Waypoint
    path: tuple[Waypoint, ...]


@dataclass(frozen=True)
class Engagement:
    """Two vehicles' scripts and what the engagement must show."""

    name: str
    description: str
    ground_msl_m: float
    time_limit_s: float
    miss_distance_m: tuple[float, float]
    waypoint_tolerance_m: float
    target: Profile
    interceptor: Profile

    def profiles(self) -> tuple[Profile, Profile]:
        return (self.target, self.interceptor)


def load_engagement(path: Path) -> Engagement:
    """Read and check an engagement file."""
    try:
        with path.open("rb") as file:
            table = tomllib.load(file)
    except (OSError, tomllib.TOMLDecodeError) as error:
        raise ProfileError(str(path), str(error)) from error
    return engagement_from(table, str(path))


def engagement_from(table: Mapping[str, object], where: str) -> Engagement:
    """An engagement from a parsed file."""
    check_keys(table, ENGAGEMENT_KEYS, where)
    target = profile_from(table["target"], "target", where)
    interceptor = profile_from(table["interceptor"], "interceptor", where)
    if target.autopilot == interceptor.autopilot:
        raise ProfileError(where, "the target and the interceptor need different autopilots")
    miss_where = f"{where}: miss_distance_m"
    low, high = number_pair(table["miss_distance_m"], miss_where, 0.0, MAX_MISS_M)
    if low > high:
        raise ProfileError(miss_where, "low must not exceed high")
    return Engagement(
        name=text(table["name"], f"{where}: name"),
        description=text(table["description"], f"{where}: description"),
        ground_msl_m=number(table["ground_msl_m"], f"{where}: ground_msl_m", -500.0, 9000.0),
        time_limit_s=number(table["time_limit_s"], f"{where}: time_limit_s", 1.0, 3600.0),
        miss_distance_m=(low, high),
        waypoint_tolerance_m=number(table["waypoint_tolerance_m"], f"{where}: waypoint_tolerance_m", 0.1, 100.0),
        target=target,
        interceptor=interceptor,
    )


def profile_from(value: object, role: str, where: str) -> Profile:
    """One vehicle's script from its table."""
    label = f"{where}: {role}"
    table = mapping(value, label)
    check_keys(table, PROFILE_KEYS, label)
    autopilot = text(table["autopilot"], f"{label}.autopilot")
    if autopilot not in AUTOPILOTS:
        raise ProfileError(f"{label}.autopilot", f"must be one of {', '.join(AUTOPILOTS)}")
    path = sequence(table["path"], f"{label}.path")
    if not 1 <= len(path) <= MAX_PATH:
        raise ProfileError(f"{label}.path", f"needs 1 to {MAX_PATH} waypoints")
    return Profile(
        role=role,
        autopilot=autopilot,
        home=home_point(table["home"], f"{label}.home"),
        speed_m_s=number(table["speed_m_s"], f"{label}.speed_m_s", 0.5, MAX_SPEED_M_S),
        start=point(table["start"], f"{label}.start"),
        path=tuple(point(item, f"{label}.path[{index}]") for index, item in enumerate(path)),
    )


def point(value: object, where: str) -> Waypoint:
    """A waypoint from [latitude_deg, longitude_deg, height_m]; it must be at least 2 m up."""
    items = sequence(value, where)
    if len(items) != POINT_SIZE:
        raise ProfileError(where, "must be [latitude_deg, longitude_deg, height_m]")
    return Waypoint(
        latitude_deg=number(items[0], f"{where} latitude", -90.0, 90.0),
        longitude_deg=number(items[1], f"{where} longitude", -180.0, 180.0),
        height_m=number(items[2], f"{where} height", 2.0, MAX_HEIGHT_M),
    )


def home_point(value: object, where: str) -> Waypoint:
    """A home from [latitude_deg, longitude_deg], on the ground."""
    latitude, longitude = number_pair(value, where, -180.0, 180.0)
    return Waypoint(
        latitude_deg=number(latitude, f"{where} latitude", -90.0, 90.0),
        longitude_deg=longitude,
        height_m=0.0,
    )


def check_keys(table: Mapping[str, object], expected: frozenset[str], where: str) -> None:
    missing = sorted(expected - table.keys())
    unknown = sorted(table.keys() - expected)
    if missing or unknown:
        raise ProfileError(where, f"missing keys {missing}, unknown keys {unknown}")


def mapping(value: object, where: str) -> Mapping[str, object]:
    if not isinstance(value, dict):
        raise ProfileError(where, "must be a table")
    return {str(key): item for key, item in value.items()}


def sequence(value: object, where: str) -> tuple[object, ...]:
    if not isinstance(value, list):
        raise ProfileError(where, "must be an array")
    return tuple(value)


def text(value: object, where: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ProfileError(where, "must be a non-empty string")
    return value


def number(value: object, where: str, low: float, high: float) -> float:
    """A finite number in [low, high]; TOML integers are accepted, booleans are not."""
    if isinstance(value, bool) or not isinstance(value, int | float):
        raise ProfileError(where, "must be a number")
    result = float(value)
    if not math.isfinite(result) or not low <= result <= high:
        raise ProfileError(where, f"must be between {low} and {high}")
    return result


def number_pair(value: object, where: str, low: float, high: float) -> tuple[float, float]:
    items = sequence(value, where)
    if len(items) != PAIR_SIZE:
        raise ProfileError(where, "must be a pair of numbers")
    return (number(items[0], where, low, high), number(items[1], where, low, high))
