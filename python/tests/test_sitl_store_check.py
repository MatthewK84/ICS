"""Tests for the check of ics-plid's store against a replay (ICS-030, python/ics_sitl/store_check.py)."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Final

import pytest

from ics_sitl.__main__ import main
from ics_sitl.compare import CompareError
from ics_sitl.store_check import finished, messages, store_problems, summary

EXIT_FAILED: Final = 1
EXIT_USAGE: Final = 2


def record() -> dict[str, object]:
    return {"entity_id": "1", "position": {"latitude_deg": 40.0}, "valid_utc_ns": "1790000000000000000"}


def event() -> dict[str, object]:
    return {"entity_id": "2", "kind": "KIND_ARMED"}


def write_lines(path: Path, entries: list[dict[str, object]]) -> Path:
    path.write_text("".join(json.dumps(entry) + "\n" for entry in entries), encoding="utf-8")
    return path


def replay_lines() -> list[dict[str, object]]:
    return [
        {"kind": "position", "system": 1, "time_boot_ms": 5, "msl_m": 1.0, "record": record()},
        {"kind": "position", "system": 1, "time_boot_ms": 5, "msl_m": 1.0, "record": record()},
        {"kind": "event", "event": event()},
    ]


def store_lines(*, records: int = 2, truncated: bool = False) -> list[dict[str, object]]:
    found: list[dict[str, object]] = [{"kind": "record", "record": record()} for _ in range(records)]
    # The same event with its fields in another order.
    reordered = dict(reversed(list(event().items())))
    return [
        *found,
        {"kind": "done", "count": records, "truncated": truncated},
        {"kind": "event", "event": reordered},
        {"kind": "done", "count": 1, "truncated": False},
    ]


def test_counts_each_message_by_its_canonical_json(tmp_path: Path) -> None:
    replay = messages(write_lines(tmp_path / "replay.jsonl", replay_lines()), {"position": "record", "event": "event"})
    assert sorted(replay.values()) == [1, 2]


def test_a_store_holding_the_replay_matches(tmp_path: Path) -> None:
    replay = write_lines(tmp_path / "replay.jsonl", replay_lines())
    store = write_lines(tmp_path / "store.jsonl", store_lines())
    assert store_problems(replay, store) == []
    assert summary(replay, store) == (["store: the 3 records and events of the replay, each as often"], True)


def test_finds_what_is_missing_or_extra(tmp_path: Path) -> None:
    replay = write_lines(tmp_path / "replay.jsonl", replay_lines())
    short = write_lines(tmp_path / "short.jsonl", store_lines(records=1))
    assert store_problems(replay, short) == [
        f"1 records or events in the replay are not in the store, such as record {json.dumps(record(), sort_keys=True)}"
    ]
    extra = write_lines(tmp_path / "extra.jsonl", store_lines(records=3))
    assert store_problems(replay, extra)[0].startswith("1 records or events in the store are not in the replay")


def test_the_queries_must_finish_untruncated(tmp_path: Path) -> None:
    replay = write_lines(tmp_path / "replay.jsonl", replay_lines())
    truncated = write_lines(tmp_path / "truncated.jsonl", store_lines(truncated=True))
    assert store_problems(replay, truncated) == ["ics-pli-query did not finish, or truncated its answer"]
    assert not finished([])
    lines, matched = summary(replay, truncated)
    assert not matched
    assert lines == ["store: ics-pli-query did not finish, or truncated its answer"]


def test_rejects_a_line_without_its_message(tmp_path: Path) -> None:
    replay = write_lines(tmp_path / "replay.jsonl", [{"kind": "event"}])
    with pytest.raises(CompareError, match="event must be an object"):
        messages(replay, {"event": "event"})


def test_the_command_line_checks_the_store(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    replay = str(write_lines(tmp_path / "replay.jsonl", replay_lines()))
    store = str(write_lines(tmp_path / "store.jsonl", store_lines()))
    assert main(["store-check", "--replay", replay, "--store", store]) == 0
    assert "each as often" in capsys.readouterr().out
    short = str(write_lines(tmp_path / "short.jsonl", store_lines(records=1)))
    assert main(["store-check", "--replay", replay, "--store", short]) == EXIT_FAILED
    assert main(["store-check", "--replay", replay, "--store", str(tmp_path / "missing.jsonl")]) == EXIT_USAGE
