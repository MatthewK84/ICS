"""Checks ics-plid's store of a SITL run against the MAVLink replay (ICS-030).

``deploy/sitl/plid-check.sh`` replays a run's tap.pcap through ics-plid into a
fresh store, then asks for every record and every event back with
``ics-pli-query``. The store matches when it holds exactly the records and
events ``ics-mavlink-replay`` made of the same capture, each as often, and each
query finished without being truncated.
"""

from __future__ import annotations

import json
from collections import Counter
from collections.abc import Mapping
from pathlib import Path
from types import MappingProxyType
from typing import Final

from ics_sitl.compare import Json, child, read_lines, text

# The line kinds each file holds, and the field that holds the message.
REPLAY_KINDS: Final = MappingProxyType({"position": "record", "event": "event"})
STORE_KINDS: Final = MappingProxyType({"record": "record", "event": "event"})
SHOWN_CHARACTERS: Final = 200


def messages(path: Path, kinds: Mapping[str, str]) -> Counter[str]:
    """Each record and event in a JSON-lines file, as its key and canonical JSON, counted."""
    found: Counter[str] = Counter()
    for index, entry in enumerate(read_lines(path), start=1):
        where = f"{path}:{index}"
        key = kinds.get(text(entry, "kind", where))
        if key is not None:
            found[f"{key} {json.dumps(child(entry, key, where), sort_keys=True)}"] += 1
    return found


def finished(entries: list[Json]) -> bool:
    """Whether ics-pli-query's output holds a done line, and none says it truncated its answer."""
    done = [entry for entry in entries if entry.get("kind") == "done"]
    return bool(done) and all(entry.get("truncated") is False for entry in done)


def difference_line(what: str, counts: Counter[str]) -> str:
    """One problem: how many messages are only in one file, and the first of them."""
    first = next(iter(counts))
    return f"{counts.total()} {what}, such as {first[:SHOWN_CHARACTERS]}"


def store_problems(replay: Path, store: Path) -> list[str]:
    """Every way the store's answer differs from the replay."""
    expected = messages(replay, REPLAY_KINDS)
    stored = messages(store, STORE_KINDS)
    problems = []
    if expected - stored:
        problems.append(difference_line("records or events in the replay are not in the store", expected - stored))
    if stored - expected:
        problems.append(difference_line("records or events in the store are not in the replay", stored - expected))
    if not finished(read_lines(store)):
        problems.append("ics-pli-query did not finish, or truncated its answer")
    return problems


def summary(replay: Path, store: Path) -> tuple[list[str], bool]:
    """The lines to print, and whether the store matched."""
    problems = store_problems(replay, store)
    if problems:
        return [f"store: {problem}" for problem in problems], False
    count = messages(replay, REPLAY_KINDS).total()
    return [f"store: the {count} records and events of the replay, each as often"], True
