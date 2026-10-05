"""The SITL rig's command line (ICS-018); deploy/sitl/run-engagements.sh calls it.

    python -m ics_sitl env ENGAGEMENT
        Print the containers' settings for an engagement as shell assignments.
    python -m ics_sitl run ENGAGEMENT --px4 HOST:PORT --ardupilot HOST:PORT --out DIR
        Fly the engagement, listening for each autopilot at its address; exit 1 if it fails.
    python -m ics_sitl tap-check --summary FILE --counts FILE
        Check the TAP capture carried every datagram the rig counted; exit 1 if not.
    python -m ics_sitl compare --truth FILE --replay FILE
        Check ics-mavlink-replay's output against a run's truth log (ICS-021); exit 1 if it falls short.
    python -m ics_sitl cut-log SOURCE TARGET --window START:END... --keep NAME...
        Cut windows of an onboard ULog or DataFlash log into a test fixture (ICS-025).

A usage or engagement-file error exits 2.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Final

from ics_sitl.compare import CompareError, compare, load_replay, load_truth, summary_lines
from ics_sitl.logcut import LogCutError, cut_log, parse_window
from ics_sitl.profiles import Engagement, ProfileError, load_engagement
from ics_sitl.rig import Address, Link, run

EXIT_FAILED: Final = 1
EXIT_USAGE: Final = 2
PORT_MAX: Final = 0xFFFF
# For each autopilot: datagrams it sent that the rig received, and the rig's datagrams to it.
TAP_DIRECTIONS: Final = (("from", "received"), ("to", "sent"))


class UsageError(ValueError):
    """A command line or input file that cannot be used."""

    def __init__(self, where: str, problem: str) -> None:
        super().__init__(f"{where}: {problem}")


def address(text: str) -> Address:
    """HOST:PORT as an address."""
    host, _, port = text.rpartition(":")
    if not host or not port.isdigit() or int(port) > PORT_MAX:
        problem = f"expected HOST:PORT, got {text!r}"
        raise argparse.ArgumentTypeError(problem)
    return (host, int(port))


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python -m ics_sitl", description="ICS SITL rig (ICS-018)")
    commands = parser.add_subparsers(dest="command", required=True)
    env = commands.add_parser("env", help="print the containers' settings for an engagement")
    env.add_argument("engagement", type=Path)
    fly = commands.add_parser("run", help="fly an engagement")
    fly.add_argument("engagement", type=Path)
    fly.add_argument("--px4", type=address, required=True, help="where the rig listens for PX4")
    fly.add_argument("--ardupilot", type=address, required=True, help="where the rig listens for ArduPilot")
    fly.add_argument("--out", type=Path, required=True, help="folder for truth.jsonl and summary.json")
    tap = commands.add_parser("tap-check", help="check the TAP capture's datagram counts")
    tap.add_argument("--summary", type=Path, required=True)
    tap.add_argument("--counts", type=Path, required=True)
    check = commands.add_parser("compare", help="check a MAVLink replay against a truth log")
    check.add_argument("--truth", type=Path, required=True, help="the run's truth.jsonl")
    check.add_argument("--replay", type=Path, required=True, help="ics-mavlink-replay's output for the run's tap.pcap")
    cut = commands.add_parser("cut-log", help="cut windows of an onboard log into a test fixture")
    cut.add_argument("source", type=Path)
    cut.add_argument("target", type=Path)
    cut.add_argument("--window", action="append", default=[], help="START:END in seconds since boot")
    cut.add_argument("--keep", action="append", default=[], help="a topic or message type to keep")
    return parser.parse_args(argv)


def env_lines(engagement: Engagement) -> list[str]:
    """Shell assignments: the ground level, and each autopilot's home."""
    lines = [f"ICS_GROUND_MSL={engagement.ground_msl_m!r}"]
    for profile in engagement.profiles():
        prefix = f"ICS_{profile.autopilot.upper()}"
        lines.append(f"{prefix}_LAT={profile.home.latitude_deg!r}")
        lines.append(f"{prefix}_LON={profile.home.longitude_deg!r}")
    return lines


def fly(arguments: argparse.Namespace) -> int:
    engagement = load_engagement(arguments.engagement)
    links = {name: Link.open(name, getattr(arguments, name)) for name in ("px4", "ardupilot")}
    try:
        summary = run(engagement, links, arguments.out)
    finally:
        for link in links.values():
            link.close()
    verdict = summary.verdict
    sys.stdout.write(f"{verdict.engagement}: {'passed' if verdict.passed else 'FAILED'}\n")
    sys.stdout.writelines(f"  {failure}\n" for failure in verdict.failures)
    return 0 if verdict.passed else EXIT_FAILED


def read_json(path: Path) -> object:
    try:
        data: object = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise UsageError(str(path), str(error)) from error
    return data


def counts_from(data: object, where: str, keys: tuple[str, str]) -> Mapping[str, Mapping[str, int]]:
    """{autopilot: {key: count}} for both autopilots, each count a non-negative integer."""
    counts: dict[str, dict[str, int]] = {}
    for autopilot in ("px4", "ardupilot"):
        entry = data.get(autopilot) if isinstance(data, dict) else None
        values = [entry.get(key) if isinstance(entry, dict) else None for key in keys]
        checked = [value for value in values if isinstance(value, int) and not isinstance(value, bool) and value >= 0]
        if len(checked) != len(keys):
            raise UsageError(where, f"needs {autopilot}.{keys[0]} and {autopilot}.{keys[1]} as counts")
        counts[autopilot] = dict(zip(keys, checked, strict=True))
    return counts


def tap_check(arguments: argparse.Namespace) -> int:
    summary = read_json(arguments.summary)
    datagrams = summary.get("datagrams") if isinstance(summary, dict) else None
    rig = counts_from(datagrams, str(arguments.summary), ("received", "sent"))
    tap = counts_from(read_json(arguments.counts), str(arguments.counts), ("from", "to"))
    shortfalls = [
        f"{autopilot}: the TAP carried {tap[autopilot][side]} datagrams {side} it, the rig {rig[autopilot][own]}"
        for autopilot in ("px4", "ardupilot")
        for side, own in TAP_DIRECTIONS
        if tap[autopilot][side] < rig[autopilot][own]
    ]
    sys.stdout.writelines(f"{line}\n" for line in shortfalls or ["TAP capture: every counted datagram was mirrored"])
    return EXIT_FAILED if shortfalls else 0


def replay_check(arguments: argparse.Namespace) -> int:
    report = compare(load_truth(arguments.truth), load_replay(arguments.replay))
    sys.stdout.writelines(f"{line}\n" for line in summary_lines(report))
    return EXIT_FAILED if report.problems else 0


def log_cut(arguments: argparse.Namespace) -> int:
    windows = [parse_window(text) for text in arguments.window]
    written = cut_log(arguments.source, arguments.target, arguments.keep, windows)
    sys.stdout.write(f"{arguments.target}: {written} bytes\n")
    return 0


def main(argv: Sequence[str]) -> int:
    arguments = parse_args(argv)
    try:
        if arguments.command == "env":
            sys.stdout.writelines(f"{line}\n" for line in env_lines(load_engagement(arguments.engagement)))
            return 0
        if arguments.command == "run":
            return fly(arguments)
        if arguments.command == "compare":
            return replay_check(arguments)
        if arguments.command == "cut-log":
            return log_cut(arguments)
        return tap_check(arguments)
    except (CompareError, LogCutError, ProfileError, UsageError) as error:
        sys.stderr.write(f"error: {error}\n")
        return EXIT_USAGE


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
