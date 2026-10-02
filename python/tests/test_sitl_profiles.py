"""Tests for the SITL rig's engagement files (ICS-018)."""

from __future__ import annotations

from pathlib import Path
from typing import Final

import pytest

from ics_sitl.profiles import Engagement, ProfileError, engagement_from, load_engagement

ENGAGEMENTS: Final = Path(__file__).resolve().parents[2] / "deploy" / "sitl" / "engagements"
SHIPPED: Final = ("crossing", "head-on", "tail-chase")


def profile_table() -> dict[str, object]:
    """A valid vehicle profile, as tomllib would read it."""
    return {
        "autopilot": "px4",
        "home": [40.0, -100.0],
        "speed_m_s": 5,
        "start": [40.001, -100.0, 30.0],
        "path": [[40.002, -100.0, 30.0]],
    }


def table() -> dict[str, object]:
    """A valid engagement, as tomllib would read it."""
    interceptor = profile_table() | {"autopilot": "ardupilot", "home": [39.999, -100.0]}
    return {
        "name": "test",
        "description": "a test engagement",
        "ground_msl_m": 700.0,
        "time_limit_s": 300,
        "miss_distance_m": [5.0, 20.0],
        "waypoint_tolerance_m": 10.0,
        "target": profile_table(),
        "interceptor": interceptor,
    }


def rejected(value: dict[str, object], match: str) -> None:
    with pytest.raises(ProfileError, match=match):
        engagement_from(value, "test.toml")


def test_the_shipped_engagements_load() -> None:
    names = sorted(path.stem for path in ENGAGEMENTS.glob("*.toml"))
    assert names == sorted(SHIPPED)
    for name in SHIPPED:
        engagement = load_engagement(ENGAGEMENTS / f"{name}.toml")
        assert engagement.name == name
        assert {profile.autopilot for profile in engagement.profiles()} == {"px4", "ardupilot"}


def test_each_autopilot_flies_both_roles_across_the_shipped_engagements() -> None:
    loaded = [load_engagement(ENGAGEMENTS / f"{name}.toml") for name in SHIPPED]
    assert {engagement.target.autopilot for engagement in loaded} == {"px4", "ardupilot"}


def test_reads_a_valid_table() -> None:
    engagement: Engagement = engagement_from(table(), "test.toml")
    assert engagement.time_limit_s == float(str(table()["time_limit_s"]))
    assert engagement.target.speed_m_s == float(str(profile_table()["speed_m_s"]))
    assert engagement.target.home.height_m == 0.0
    assert engagement.interceptor.path[0].height_m == engagement.interceptor.start.height_m
    assert engagement.miss_distance_m == (5.0, 20.0)


def test_rejects_missing_and_unknown_keys() -> None:
    missing = table()
    del missing["name"]
    rejected(missing, r"missing keys \['name'\]")
    extra = table() | {"speed": 3}
    rejected(extra, r"unknown keys \['speed'\]")
    profile = table()
    profile["target"] = profile_table() | {"colour": "red"}
    rejected(profile, r"unknown keys \['colour'\]")


@pytest.mark.parametrize(
    ("key", "value", "match"),
    [
        ("name", "", "non-empty string"),
        ("ground_msl_m", True, "must be a number"),
        ("ground_msl_m", "700", "must be a number"),
        ("time_limit_s", 0, "between"),
        ("time_limit_s", float("inf"), "between"),
        ("waypoint_tolerance_m", 0.0, "between"),
        ("miss_distance_m", [20.0, 5.0], "low must not exceed high"),
        ("miss_distance_m", [5.0], "pair of numbers"),
        ("miss_distance_m", "5-20", "must be an array"),
        ("target", "px4", "must be a table"),
    ],
)
def test_rejects_bad_engagement_values(key: str, value: object, match: str) -> None:
    rejected(table() | {key: value}, match)


@pytest.mark.parametrize(
    ("key", "value", "match"),
    [
        ("autopilot", "inav", "must be one of px4, ardupilot"),
        ("home", [91.0, 0.0], "latitude"),
        ("home", [40.0, -181.0], "between"),
        ("speed_m_s", 0.0, "between"),
        ("speed_m_s", 30.0, "between"),
        ("start", [40.0, -100.0], r"\[latitude_deg, longitude_deg, height_m\]"),
        ("start", [40.0, -100.0, 1.0], "height"),
        ("start", [40.0, -100.0, 500.0], "height"),
        ("path", [], "needs 1 to 32 waypoints"),
        ("path", [[40.0, -100.0, 30.0]] * 33, "needs 1 to 32 waypoints"),
        ("path", [[40.0, "east", 30.0]], "longitude"),
    ],
)
def test_rejects_bad_profile_values(key: str, value: object, match: str) -> None:
    bad = table()
    bad["target"] = profile_table() | {key: value}
    rejected(bad, match)


def test_needs_one_autopilot_of_each_kind() -> None:
    same = table()
    same["interceptor"] = profile_table()
    rejected(same, "different autopilots")


def test_reports_an_unreadable_or_malformed_file(tmp_path: Path) -> None:
    with pytest.raises(ProfileError, match="No such file"):
        load_engagement(tmp_path / "missing.toml")
    broken = tmp_path / "broken.toml"
    broken.write_text("name = \n", encoding="utf-8")
    with pytest.raises(ProfileError, match=r"broken\.toml"):
        load_engagement(broken)
