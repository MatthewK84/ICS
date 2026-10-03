"""Tests for the MAVLink replay check (ICS-021, python/ics_sitl/compare.py)."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Final

import pytest

from ics_sitl.__main__ import main
from ics_sitl.compare import (
    PROBLEMS_SHOWN,
    CompareError,
    Fix,
    Log,
    Report,
    compare,
    integer,
    load_replay,
    load_truth,
    printable,
    same_fix,
    summary_lines,
)

EXIT_FAILED: Final = 1
EXIT_USAGE: Final = 2


def truth_position(autopilot: str, boot_ms: int, latitude: float = 40.0, msl: float = 735.25) -> dict[str, object]:
    return {
        "autopilot": autopilot,
        "event": "position",
        "time_boot_ms": boot_ms,
        "latitude_deg": latitude,
        "longitude_deg": -100.0,
        "height_m": 35.0,
        "msl_m": msl,
        "role": "target",
        "time_s": 1.0,
    }


def replay_position(system: int, boot_ms: int, latitude: float = 40.0, ellipsoid: float = 710.0) -> dict[str, object]:
    record = {
        "entity_id": str(system),
        "position": {"latitude_deg": latitude, "longitude_deg": -100.0, "height_ellipsoid_m": ellipsoid},
    }
    return {"kind": "position", "system": system, "time_boot_ms": boot_ms, "msl_m": 735.25, "record": record}


def replay_event(system: int, kind: str, **fields: object) -> dict[str, object]:
    return {
        "kind": "event",
        "event": {"entity_id": str(system), "kind": kind, "time_utc_ns": "1790000000000000000", **fields},
    }


TRUTH: Final = [
    {"autopilot": "both", "event": "engage", "time_s": 0.5},
    {"autopilot": "px4", "event": "phase", "phase": "climbing", "time_s": 0.6},
    truth_position("px4", 1000),
    truth_position("ardupilot", 1200),
    {"autopilot": "ardupilot", "event": "text", "severity": 6, "text": "Arm: Need � Estimate\t", "time_s": 2.0},
    {"autopilot": "ardupilot", "event": "refused", "command": 400, "result": 4, "time_s": 2.1},
]

REPLAY: Final = [
    replay_event(2, "KIND_MODE_CHANGED", detail="STABILIZE"),
    replay_position(1, 1000),
    replay_position(2, 1200),
    replay_event(2, "KIND_STATUS_TEXT", detail="Arm: Need ? Estimate?"),
    replay_event(2, "KIND_COMMAND_ACK", command=400, command_result=4),
    replay_event(2, "KIND_COMMAND_ACK", command=400),
]


def write_lines(path: Path, entries: list[dict[str, object]]) -> Path:
    path.write_text("".join(json.dumps(entry) + "\n" for entry in entries) + "\n", encoding="utf-8")
    return path


def test_printable_replaces_what_is_not_printable_ascii() -> None:
    assert printable("ok \t�~\x7f") == "ok ??~?"


def test_reads_a_truth_log_by_system(tmp_path: Path) -> None:
    log = load_truth_from(TRUTH, tmp_path)
    assert set(log.positions) == {(1, 1000), (2, 1200)}
    assert log.texts == {(2, "Arm: Need ? Estimate?"): 1}
    assert log.refusals == {(2, 400, 4): 1}


def load_truth_from(entries: list[dict[str, object]], folder: Path) -> Log:
    return load_truth(write_lines(folder / "truth.jsonl", entries))


def test_a_matching_replay_passes(tmp_path: Path) -> None:
    report = compare(load_truth_from(TRUTH, tmp_path), load_replay(write_lines(tmp_path / "replay.jsonl", REPLAY)))
    assert report.problems == []
    assert report.matched == {1: (1, 1), 2: (1, 1)}
    assert summary_lines(report)[-1] == "MAVLink replay: matches the truth log"


def test_finds_missing_and_different_positions(tmp_path: Path) -> None:
    replay = [replay_position(1, 1000, latitude=40.00001), *REPLAY[3:]]
    report = compare(load_truth_from(TRUTH, tmp_path), load_replay(write_lines(tmp_path / "replay.jsonl", replay)))
    assert report.matched == {1: (0, 1), 2: (0, 1)}
    moved, missing = report.problems
    assert "logged Fix(latitude_deg=40.0" in moved
    assert "replayed nothing" in missing


def test_finds_texts_and_refusals_replayed_too_rarely(tmp_path: Path) -> None:
    report = compare(load_truth_from(TRUTH, tmp_path), load_replay(write_lines(tmp_path / "replay.jsonl", REPLAY[:3])))
    assert report.problems == [
        "status text (2, 'Arm: Need ? Estimate?'): logged 1 times, replayed 0",
        "refusal (system, command, result) (2, 400, 4): logged 1 times, replayed 0",
    ]


def test_a_fix_must_match_in_every_coordinate_and_lie_within_the_geoid() -> None:
    logged = Fix(40.0, -100.0, 735.25)
    assert same_fix(logged, Fix(40.0, -100.0, 735.2504, 710.0))
    assert not same_fix(logged, Fix(40.0, -100.00001, 735.25, 710.0))
    assert not same_fix(logged, Fix(40.0, -100.0, 735.252, 710.0))
    assert not same_fix(logged, Fix(40.0, -100.0, 735.25, 900.0))
    assert not same_fix(logged, Fix(40.0, -100.0, 735.25, 600.0))
    assert same_fix(logged, Fix(40.0, -100.0, 735.25))


def test_shows_only_the_first_problems() -> None:
    assert compare(Log(), Log()) == Report({}, [])
    many = [f"problem {index}" for index in range(PROBLEMS_SHOWN + 3)]
    lines = summary_lines(Report({1: (0, 2)}, many))
    assert lines[0] == "px4 (system 1): 0 of 2 positions matched"
    assert lines[-2] == "... and 3 more"
    assert lines[-1] == f"MAVLink replay: {PROBLEMS_SHOWN + 3} problems"


def test_reads_proto_json_integers() -> None:
    assert integer({"n": "-17"}, "n", "here") == int("-17")
    assert integer({}, "n", "here", 0) == 0
    with pytest.raises(CompareError, match="n must be an integer"):
        integer({"n": True}, "n", "here")


@pytest.mark.parametrize(
    ("content", "problem"),
    [
        ("{not json\n", "Expecting property name"),
        ("[1, 2]\n", "every line must be a JSON object"),
        ('{"autopilot": 3, "event": "position"}\n', "autopilot must be text"),
        ('{"autopilot": "px4", "event": "position", "latitude_deg": "north"}\n', "latitude_deg must be a number"),
    ],
)
def test_rejects_a_truth_log_it_cannot_read(tmp_path: Path, content: str, problem: str) -> None:
    path = tmp_path / "truth.jsonl"
    path.write_text(content, encoding="utf-8")
    with pytest.raises(CompareError, match=problem):
        load_truth(path)


def test_rejects_a_replay_without_its_objects(tmp_path: Path) -> None:
    path = write_lines(tmp_path / "replay.jsonl", [{"kind": "position", "system": 1, "time_boot_ms": 5}])
    with pytest.raises(CompareError, match="record must be an object"):
        load_replay(path)


def test_rejects_a_missing_file(tmp_path: Path) -> None:
    with pytest.raises(CompareError, match="No such file"):
        load_replay(tmp_path / "none.jsonl")


def test_the_command_line_compares(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    truth = write_lines(tmp_path / "truth.jsonl", TRUTH)
    replay = write_lines(tmp_path / "replay.jsonl", REPLAY)
    assert main(["compare", "--truth", str(truth), "--replay", str(replay)]) == 0
    assert "px4 (system 1): 1 of 1 positions matched" in capsys.readouterr().out
    short = write_lines(tmp_path / "short.jsonl", REPLAY[:2])
    assert main(["compare", "--truth", str(truth), "--replay", str(short)]) == EXIT_FAILED
    assert main(["compare", "--truth", str(truth), "--replay", str(tmp_path / "none")]) == EXIT_USAGE
    assert "error:" in capsys.readouterr().err
