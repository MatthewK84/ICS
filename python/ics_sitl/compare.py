"""Checks a MAVLink replay of a SITL run against the rig's truth log (ICS-021).

``ics-mavlink-replay`` (cpp/testing/mavlink_replay) runs a run's tap.pcap
through the MAVLink adapter (cpp/mavlink) and writes what it produced as JSON
lines. The replay matches the truth log when, for each autopilot:

- every position the rig logged has a record from the same MAVLink system with
  the same time_boot_ms, the same latitude and longitude to 1e-9 degrees, and
  the same height above mean sea level to 1 mm; and the record's height above
  the ellipsoid differs from that by no more than the geoid does anywhere;
- every status text the rig logged is a STATUS_TEXT event from that system, at
  least as often, with characters outside printable ASCII shown as '?';
- every command the autopilot refused is a COMMAND_ACK event from that system
  with the same command and result, at least as often.

The rig hears each autopilot on its own link, as the TAP port sees it; the
replay tells them apart by MAVLink system ID.
"""

from __future__ import annotations

import json
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Final

# The system IDs the rig's images give each autopilot (deploy/sitl/image).
SYSTEMS: Final = {"px4": 1, "ardupilot": 2}
DEGREES_TOLERANCE: Final = 1e-9
MSL_TOLERANCE_M: Final = 1e-3
# The EGM96 geoid's lowest and highest heights above the ellipsoid, rounded out.
GEOID_LOWEST_M: Final = -107.0
GEOID_HIGHEST_M: Final = 86.0
FIRST_PRINTABLE: Final = " "
LAST_PRINTABLE: Final = "~"
PROBLEMS_SHOWN: Final = 10

type Json = dict[str, object]
type PositionKey = tuple[int, int]


class CompareError(ValueError):
    """A truth log or replay that cannot be read."""

    def __init__(self, where: str, problem: str) -> None:
        super().__init__(f"{where}: {problem}")


@dataclass(frozen=True)
class Fix:
    """A position: latitude and longitude in degrees, and height above mean sea level."""

    latitude_deg: float
    longitude_deg: float
    msl_m: float
    ellipsoid_m: float | None = None


@dataclass
class Log:
    """What a truth log or a replay holds, by MAVLink system."""

    positions: defaultdict[PositionKey, list[Fix]] = field(default_factory=lambda: defaultdict(list))
    texts: Counter[tuple[int, str]] = field(default_factory=Counter)
    refusals: Counter[tuple[int, int, int]] = field(default_factory=Counter)


@dataclass(frozen=True)
class Report:
    """The comparison: what matched for each system, and every problem found."""

    matched: dict[int, tuple[int, int]]
    problems: list[str]


def printable(text: str) -> str:
    """The text with each character outside printable ASCII as '?', as the adapter writes it."""
    return "".join(c if FIRST_PRINTABLE <= c <= LAST_PRINTABLE else "?" for c in text)


def read_lines(path: Path) -> list[Json]:
    """Each line of a JSON-lines file, as an object."""
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
        entries: list[object] = [json.loads(line) for line in lines if line.strip()]
    except (OSError, json.JSONDecodeError) as error:
        raise CompareError(str(path), str(error)) from error
    objects = [entry for entry in entries if isinstance(entry, dict)]
    if len(objects) != len(entries):
        raise CompareError(str(path), "every line must be a JSON object")
    return objects


def number(entry: Json, key: str, where: str) -> float:
    value = entry.get(key)
    if isinstance(value, bool) or not isinstance(value, int | float):
        raise CompareError(where, f"{key} must be a number, not {value!r}")
    return float(value)


def integer(entry: Json, key: str, where: str, default: int | None = None) -> int:
    """An integer field; proto JSON writes 64-bit integers as strings, and leaves out zeros."""
    value = entry.get(key, default)
    if isinstance(value, str) and value.lstrip("-").isdigit():
        return int(value)
    if isinstance(value, bool) or not isinstance(value, int):
        raise CompareError(where, f"{key} must be an integer, not {value!r}")
    return value


def text(entry: Json, key: str, where: str, default: str | None = None) -> str:
    value = entry.get(key, default)
    if not isinstance(value, str):
        raise CompareError(where, f"{key} must be text, not {value!r}")
    return value


def child(entry: Json, key: str, where: str) -> Json:
    value = entry.get(key)
    if not isinstance(value, dict):
        raise CompareError(where, f"{key} must be an object")
    return value


def load_truth(path: Path) -> Log:
    """The positions, texts and refusals in a rig's truth.jsonl."""
    log = Log()
    for index, entry in enumerate(read_lines(path), start=1):
        where = f"{path}:{index}"
        system = SYSTEMS.get(text(entry, "autopilot", where))
        event = text(entry, "event", where)
        if system is None or event not in {"position", "text", "refused"}:
            continue
        if event == "position":
            fix = Fix(
                number(entry, "latitude_deg", where),
                number(entry, "longitude_deg", where),
                number(entry, "msl_m", where),
            )
            log.positions[(system, integer(entry, "time_boot_ms", where))].append(fix)
        elif event == "text":
            log.texts[(system, printable(text(entry, "text", where)))] += 1
        else:
            log.refusals[(system, integer(entry, "command", where), integer(entry, "result", where))] += 1
    return log


def add_event(log: Log, event: Json, where: str) -> None:
    system = integer(event, "entity_id", where)
    kind = text(event, "kind", where)
    if kind == "KIND_STATUS_TEXT":
        log.texts[(system, text(event, "detail", where, ""))] += 1
    elif kind == "KIND_COMMAND_ACK":
        result = integer(event, "command_result", where, 0)
        log.refusals[(system, integer(event, "command", where, 0), result)] += 1


def load_replay(path: Path) -> Log:
    """The positions and events in ics-mavlink-replay's output."""
    log = Log()
    for index, entry in enumerate(read_lines(path), start=1):
        where = f"{path}:{index}"
        kind = text(entry, "kind", where)
        if kind == "event":
            add_event(log, child(entry, "event", where), where)
            continue
        position = child(child(entry, "record", where), "position", where)
        fix = Fix(
            number(position, "latitude_deg", where),
            number(position, "longitude_deg", where),
            number(entry, "msl_m", where),
            number(position, "height_ellipsoid_m", where),
        )
        log.positions[(integer(entry, "system", where), integer(entry, "time_boot_ms", where))].append(fix)
    return log


def same_fix(truth: Fix, replayed: Fix) -> bool:
    """Whether a replayed record matches a logged position."""
    separation = (replayed.ellipsoid_m if replayed.ellipsoid_m is not None else replayed.msl_m) - replayed.msl_m
    return (
        abs(truth.latitude_deg - replayed.latitude_deg) <= DEGREES_TOLERANCE
        and abs(truth.longitude_deg - replayed.longitude_deg) <= DEGREES_TOLERANCE
        and abs(truth.msl_m - replayed.msl_m) <= MSL_TOLERANCE_M
        and GEOID_LOWEST_M <= separation <= GEOID_HIGHEST_M
    )


def position_problems(truth: Log, replay: Log) -> tuple[Counter[int], list[str]]:
    """How many logged positions each system matched, and those it did not."""
    matched: Counter[int] = Counter()
    problems: list[str] = []
    for (system, boot_ms), fixes in sorted(truth.positions.items()):
        candidates = replay.positions.get((system, boot_ms), [])
        for fix in fixes:
            if any(same_fix(fix, candidate) for candidate in candidates):
                matched[system] += 1
            else:
                problems.append(f"system {system} at {boot_ms} ms: logged {fix}, replayed {candidates or 'nothing'}")
    return matched, problems


def count_problems[K: tuple[int, ...] | tuple[int, str]](what: str, truth: Counter[K], replay: Counter[K]) -> list[str]:
    """Each logged item the replay holds fewer times than the log."""
    return [
        f"{what} {key}: logged {count} times, replayed {replay[key]}"
        for key, count in sorted(truth.items(), key=lambda item: str(item[0]))
        if replay[key] < count
    ]


def compare(truth: Log, replay: Log) -> Report:
    """Every way the replay falls short of the truth log."""
    matched, problems = position_problems(truth, replay)
    problems += count_problems("status text", truth.texts, replay.texts)
    problems += count_problems("refusal (system, command, result)", truth.refusals, replay.refusals)
    totals: Counter[int] = Counter()
    for (system, _), fixes in truth.positions.items():
        totals[system] += len(fixes)
    return Report({system: (matched[system], totals[system]) for system in sorted(totals)}, problems)


def summary_lines(report: Report) -> list[str]:
    """The report as lines of text: what matched, then the first problems."""
    names = {system: name for name, system in SYSTEMS.items()}
    lines = [
        f"{names.get(system, system)} (system {system}): {matched} of {total} positions matched"
        for system, (matched, total) in report.matched.items()
    ]
    lines += report.problems[:PROBLEMS_SHOWN]
    if len(report.problems) > PROBLEMS_SHOWN:
        lines.append(f"... and {len(report.problems) - PROBLEMS_SHOWN} more")
    verdict = "matches the truth log" if not report.problems else f"{len(report.problems)} problems"
    lines.append(f"MAVLink replay: {verdict}")
    return lines
