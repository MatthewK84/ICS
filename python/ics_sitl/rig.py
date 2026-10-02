"""The SITL rig's ground station: UDP links to both autopilots (ICS-018).

Each autopilot is given the rig's address at start-up and sends its MAVLink
there, to its own port; the rig listens on both and replies to whoever sent.
Every datagram each way is counted: the emulated TAP port must carry at least
as many (deploy/sitl/README.md).

The run writes truth.jsonl, one JSON object per position, mission event,
autopilot text, refused command and phase change, and summary.json, the
verdict with the datagram counts.
"""

from __future__ import annotations

import json
import select
import socket
import time
from collections.abc import Callable, Mapping
from dataclasses import asdict, dataclass
from pathlib import Path
from types import TracebackType
from typing import Final, Self, TextIO

from ics_sitl.engagement import Controller, Outgoing, Verdict
from ics_sitl.mavlink import decode, encode
from ics_sitl.profiles import Engagement
from ics_sitl.vehicle import Record

GROUND_SYSTEM: Final = 255
GROUND_COMPONENT: Final = 190
POLL_S: Final = 0.02
MAX_DATAGRAM: Final = 65_535
# Datagrams read from one link per poll, so neither autopilot can starve the other.
MAX_BATCH: Final = 64

type Address = tuple[str, int]
type Clock = Callable[[], float]


class Link:
    """A UDP socket to one autopilot, counting what passes."""

    def __init__(self, autopilot: str, udp: socket.socket) -> None:
        self.autopilot = autopilot
        self.udp = udp
        self.peer: Address | None = None
        self.sequence = 0
        self.received = 0
        self.sent = 0

    @classmethod
    def open(cls, autopilot: str, bind: Address) -> Self:
        """A link listening at ``bind``; it replies to whoever sent to it first."""
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.setblocking(False)
        udp.bind(bind)
        return cls(autopilot, udp)

    def receive(self) -> list[bytes]:
        """The datagrams waiting, up to MAX_BATCH."""
        datagrams: list[bytes] = []
        for _ in range(MAX_BATCH):
            try:
                data, sender = self.udp.recvfrom(MAX_DATAGRAM)
            except BlockingIOError:
                break
            self.peer = self.peer or sender
            self.received += 1
            datagrams.append(data)
        return datagrams

    def send(self, data: bytes) -> None:
        if self.peer is None:
            return
        self.udp.sendto(data, self.peer)
        self.sent += 1

    def frame(self, outgoing: Outgoing) -> bytes:
        """The next frame for this link."""
        data = encode(outgoing.message, self.sequence, GROUND_SYSTEM, GROUND_COMPONENT)
        self.sequence = (self.sequence + 1) % 256
        return data

    def close(self) -> None:
        self.udp.close()


@dataclass(frozen=True)
class Summary:
    """The verdict, and the datagrams each link carried."""

    verdict: Verdict
    datagrams: Mapping[str, Mapping[str, int]]


class TruthLog:
    """truth.jsonl, written as the run goes."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.file: TextIO | None = None

    def __enter__(self) -> Self:
        self.file = self.path.open("w", encoding="utf-8")
        return self

    def __exit__(
        self, kind: type[BaseException] | None, error: BaseException | None, trace: TracebackType | None
    ) -> None:
        if self.file is not None:
            self.file.close()

    def write(self, record: Record) -> None:
        if self.file is None:
            return
        line = {"time_s": round(record.time_s, 3), "role": record.role, "autopilot": record.autopilot}
        line |= {"event": record.event, **record.values}
        self.file.write(json.dumps(line, sort_keys=True) + "\n")


def run(engagement: Engagement, links: Mapping[str, Link], out: Path, clock: Clock = time.monotonic) -> Summary:
    """Fly ``engagement`` over ``links`` (one per autopilot) and write its logs to ``out``."""
    out.mkdir(parents=True, exist_ok=True)
    started = clock()
    with TruthLog(out / "truth.jsonl") as log:
        controller = Controller(engagement, log.write)
        now = 0.0
        while not controller.finished(now):
            poll(controller, links, now)
            for outgoing in controller.tick(now):
                send(links, outgoing)
            now = clock() - started
    counts = {name: {"received": link.received, "sent": link.sent} for name, link in links.items()}
    summary = Summary(controller.verdict(), counts)
    (out / "summary.json").write_text(json.dumps(asdict(summary), indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return summary


def poll(controller: Controller, links: Mapping[str, Link], now: float) -> None:
    """Pass every waiting frame to the controller, and send its replies."""
    readable, _, _ = select.select([link.udp for link in links.values()], [], [], POLL_S)
    for name, link in links.items():
        if link.udp not in readable:
            continue
        for datagram in link.receive():
            for frame in decode(datagram).frames:
                for outgoing in controller.receive(name, frame, now):
                    send(links, outgoing)


def send(links: Mapping[str, Link], outgoing: Outgoing) -> None:
    link = links[outgoing.autopilot]
    link.send(link.frame(outgoing))
