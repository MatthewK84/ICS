"""Tests for the SITL rig's UDP runner and command line (ICS-018), over loopback.

Fake autopilots answer in threads on loopback sockets, sending to the rig as
the real ones do, so an engagement is flown end to end in about a second.
"""

from __future__ import annotations

import json
import runpy
import socket
import sys
from collections.abc import Iterator
from contextlib import contextmanager
from dataclasses import replace
from pathlib import Path
from typing import Final

import pytest

from ics_sitl.__main__ import main
from ics_sitl.rig import Address, Link, run
from tests.sitl_fakes import FakeAutopilot, UdpFake
from tests.test_sitl_engagement import ENGAGEMENT, SYSTEMS

LOOPBACK: Final = "127.0.0.1"
EXIT_FAILED: Final = 1
EXIT_USAGE: Final = 2
ENGAGEMENT_FILE: Final = Path(__file__).resolve().parents[2] / "deploy" / "sitl" / "engagements" / "tail-chase.toml"


def free_address() -> Address:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind((LOOPBACK, 0))
        port: int = probe.getsockname()[1]
    return (LOOPBACK, port)


@contextmanager
def autopilots(rig: dict[str, Address]) -> Iterator[dict[str, UdpFake]]:
    """Both fake autopilots, sending to the rig's addresses, until the block ends."""
    served: dict[str, UdpFake] = {}
    for name, system in SYSTEMS:
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.bind((LOOPBACK, 0))
        served[name] = UdpFake(FakeAutopilot(name, system), udp, rig[name])
        served[name].thread.start()
    try:
        yield served
    finally:
        for fake in served.values():
            fake.stop.set()
            fake.thread.join(timeout=5)
            fake.udp.close()


def test_flies_an_engagement_over_udp(tmp_path: Path) -> None:
    links = {name: Link.open(name, (LOOPBACK, 0)) for name, _ in SYSTEMS}
    addresses = {name: (LOOPBACK, int(link.udp.getsockname()[1])) for name, link in links.items()}
    with autopilots(addresses):
        summary = run(replace(ENGAGEMENT, time_limit_s=20.0), links, tmp_path)
    for link in links.values():
        link.close()
    assert summary.verdict.failures == ()
    assert all(counts["received"] > 0 and counts["sent"] > 0 for counts in summary.datagrams.values())
    written = json.loads((tmp_path / "summary.json").read_text(encoding="utf-8"))
    assert written["verdict"]["passed"] is True
    events = [json.loads(line)["event"] for line in (tmp_path / "truth.jsonl").read_text(encoding="utf-8").splitlines()]
    assert events.count("engage") == 1
    assert "position" in events


def test_a_link_sends_nothing_until_it_has_heard_from_its_autopilot() -> None:
    link = Link.open("px4", (LOOPBACK, 0))
    link.send(b"nobody to send to")
    assert link.sent == 0
    assert link.receive() == []
    link.close()


def test_the_command_line_flies_and_reports(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    rig = {name: free_address() for name, _ in SYSTEMS}
    arguments = [f"--{name}={host}:{port}" for name, (host, port) in rig.items()]
    with autopilots(rig):
        status = main(["run", str(ENGAGEMENT_FILE), *arguments, "--out", str(tmp_path)])
    out = capsys.readouterr().out
    assert out == "tail-chase: passed\n"
    assert status == 0
    assert (tmp_path / "summary.json").is_file()


def test_env_prints_each_containers_settings(capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["env", str(ENGAGEMENT_FILE)]) == 0
    lines = capsys.readouterr().out.splitlines()
    assert lines[0] == "ICS_GROUND_MSL=700.0"
    assert {line.split("=")[0] for line in lines[1:]} == {
        "ICS_PX4_LAT",
        "ICS_PX4_LON",
        "ICS_ARDUPILOT_LAT",
        "ICS_ARDUPILOT_LON",
    }


def write_counts(folder: Path, rig: tuple[int, int], tap: tuple[int, int]) -> tuple[Path, Path]:
    datagrams = {name: {"received": rig[0], "sent": rig[1]} for name, _ in SYSTEMS}
    summary = folder / "summary.json"
    summary.write_text(json.dumps({"verdict": {}, "datagrams": datagrams}), encoding="utf-8")
    counts = folder / "tap-counts.json"
    counts.write_text(json.dumps({name: {"from": tap[0], "to": tap[1]} for name, _ in SYSTEMS}), encoding="utf-8")
    return summary, counts


def test_tap_check_passes_when_the_capture_holds_every_datagram(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    summary, counts = write_counts(tmp_path, rig=(100, 10), tap=(104, 10))
    assert main(["tap-check", "--summary", str(summary), "--counts", str(counts)]) == 0
    assert capsys.readouterr().out == "TAP capture: every counted datagram was mirrored\n"


def test_tap_check_fails_when_the_capture_is_short(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    summary, counts = write_counts(tmp_path, rig=(100, 10), tap=(99, 10))
    assert main(["tap-check", "--summary", str(summary), "--counts", str(counts)]) == EXIT_FAILED
    lines = capsys.readouterr().out.splitlines()
    assert lines == [f"{name}: the TAP carried 99 datagrams from it, the rig 100" for name, _ in SYSTEMS]


@pytest.mark.parametrize(
    "counts",
    ["not json", json.dumps([1, 2]), json.dumps({"px4": {"from": 1, "to": 1}}), json.dumps({"px4": {"from": -1}})],
)
def test_tap_check_rejects_counts_it_cannot_read(
    tmp_path: Path, capsys: pytest.CaptureFixture[str], counts: str
) -> None:
    summary, path = write_counts(tmp_path, rig=(1, 1), tap=(1, 1))
    path.write_text(counts, encoding="utf-8")
    assert main(["tap-check", "--summary", str(summary), "--counts", str(path)]) == EXIT_USAGE
    assert capsys.readouterr().err.startswith("error: ")


def test_a_missing_file_or_bad_engagement_is_a_usage_error(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert (
        main(["tap-check", "--summary", str(tmp_path / "no.json"), "--counts", str(tmp_path / "no.json")]) == EXIT_USAGE
    )
    bad = tmp_path / "bad.toml"
    bad.write_text('name = "x"\n', encoding="utf-8")
    assert main(["env", str(bad)]) == EXIT_USAGE
    assert "missing keys" in capsys.readouterr().err


@pytest.mark.parametrize("text", ["nohost", ":14550", "host:port", "host:70000"])
def test_rejects_an_address_that_is_not_host_and_port(text: str) -> None:
    with pytest.raises(SystemExit):
        main(["run", str(ENGAGEMENT_FILE), "--px4", text, "--ardupilot", "127.0.0.1:1", "--out", "x"])


def test_runs_as_a_module(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]) -> None:
    monkeypatch.delitem(sys.modules, "ics_sitl.__main__", raising=False)
    monkeypatch.setattr(sys, "argv", ["ics_sitl", "env", str(ENGAGEMENT_FILE)])
    with pytest.raises(SystemExit) as exit_info:
        runpy.run_module("ics_sitl", run_name="__main__")
    assert exit_info.value.code == 0
    assert "ICS_GROUND_MSL=700.0" in capsys.readouterr().out
