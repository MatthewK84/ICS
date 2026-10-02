"""A fake autopilot for the SITL rig's tests (ICS-018).

It answers the way PX4 and ArduPilot SITL do for the rig's purposes: a
heartbeat naming its autopilot, acknowledgements, MAVLink's mission upload,
and position reports. Instead of flying, it jumps: to its start point when the
mission starts, then to one path waypoint per tick once the engagement starts,
reporting each as reached. It reads the start point and path from the mission
it was sent, so a passing engagement also shows the upload was right.
"""

from __future__ import annotations

import select
import socket
import threading
from typing import Final

from ics_sitl import mission
from ics_sitl.mavlink import decode, encode
from ics_sitl.messages import (
    AttitudeQuaternion,
    CommandAck,
    CommandLong,
    GlobalPositionInt,
    GpsRawInt,
    Heartbeat,
    Message,
    MissionAck,
    MissionCount,
    MissionCurrent,
    MissionItemInt,
    MissionItemReached,
    MissionRequest,
    MissionRequestInt,
    MissionSetCurrent,
    StatusText,
    SystemTime,
)

AUTOPILOT_IDS: Final = (("px4", 12), ("ardupilot", 3))
RESULT_ACCEPTED: Final = 0
RESULT_TEMPORARILY_REJECTED: Final = 1
RESULT_FAILED: Final = 4
MISSION_NO_SPACE: Final = 4
# PX4 refuses an interval for MISSION_CURRENT, which it sends unasked.
MISSION_CURRENT_ID: Final = 42.0
QUADROTOR: Final = 2


class FakeAutopilot:
    """One autopilot's side of the rig's MAVLink."""

    def __init__(
        self,
        autopilot: str,
        system: int,
        *,
        refuse_once: frozenset[int] = frozenset(),
        refuse_always: frozenset[int] = frozenset(),
        refuse_upload: bool = False,
    ) -> None:
        self.autopilot = autopilot
        self.system = system
        self.autopilot_id = dict(AUTOPILOT_IDS)[autopilot]
        self.refuse_once = set(refuse_once)
        self.refuse_always = refuse_always
        self.refuse_upload = refuse_upload
        # Added to the reported latitude on the path, in 1e-7 degrees: a vehicle that strays.
        self.stray_e7 = 0
        self.items: dict[int, MissionItemInt] = {}
        self.count = 0
        self.flying = False
        self.position: MissionItemInt | None = None
        self.path_index: int | None = None
        self.next_heartbeat = 0.0
        self.silent = False

    def handle(self, message: Message) -> list[Message]:
        """Replies to one message from the rig."""
        if isinstance(message, CommandLong):
            return self.command(message)
        if isinstance(message, MissionCount):
            return self.counted(message)
        if isinstance(message, MissionItemInt):
            return self.item(message)
        if isinstance(message, MissionSetCurrent):
            self.path_index = 0 if message.seq in [item.seq for item in self.path()] else self.path_index
            return [MissionCurrent(message.seq, len(self.items), 0)]
        return []

    def command(self, message: CommandLong) -> list[Message]:
        if message.command in self.refuse_once or message.command in self.refuse_always:
            self.refuse_once.discard(message.command)
            text = StatusText(4, b"PreArm: waiting for the fake".ljust(50, b"\x00"))
            return [text, CommandAck(message.command, RESULT_TEMPORARILY_REJECTED, 0, 0, 255, 190)]
        if message.command == mission.SET_MESSAGE_INTERVAL and message.param1 == MISSION_CURRENT_ID:
            return [CommandAck(message.command, RESULT_FAILED, 0, 0, 255, 190)]
        if message.command == mission.MISSION_START:
            self.flying = True
            self.position = self.loiter()
        return [CommandAck(message.command, RESULT_ACCEPTED, 0, 0, 255, 190)]

    def counted(self, message: MissionCount) -> list[Message]:
        if self.refuse_upload:
            self.refuse_upload = False
            return [MissionAck(255, 190, MISSION_NO_SPACE, 0)]
        self.count = message.count
        self.items = {}
        return [self.request(0)]

    def item(self, message: MissionItemInt) -> list[Message]:
        self.items[message.seq] = message
        if message.seq + 1 < self.count:
            return [self.request(message.seq + 1)]
        return [MissionAck(255, 190, 0, 0)]

    def request(self, seq: int) -> Message:
        """PX4 asks with MISSION_REQUEST_INT; this fake's ArduPilot uses the older MISSION_REQUEST."""
        if self.autopilot == "px4":
            return MissionRequestInt(255, 190, seq, 0)
        return MissionRequest(255, 190, seq, 0)

    def loiter(self) -> MissionItemInt | None:
        return next((item for item in self.items.values() if item.command == mission.NAV_LOITER_UNLIM), None)

    def path(self) -> list[MissionItemInt]:
        start = self.loiter()
        first = start.seq + 1 if start is not None else len(self.items)
        return [self.items[seq] for seq in range(first, len(self.items))]

    def tick(self, now: float) -> list[Message]:
        """Heartbeat once a second; while flying, the next waypoint and a full set of reports."""
        if self.silent:
            return []
        messages: list[Message] = []
        if now >= self.next_heartbeat:
            self.next_heartbeat = now + 1.0
            messages.append(Heartbeat(QUADROTOR, self.autopilot_id, 0, 0, 4, 3))
        if not self.flying or self.position is None:
            return messages
        path = self.path()
        if self.path_index is not None and self.path_index < len(path):
            self.position = path[self.path_index]
            self.path_index += 1
            messages.append(MissionItemReached(self.position.seq))
        return messages + self.reports(self.position)

    def reports(self, at: MissionItemInt) -> list[Message]:
        height_mm = round(at.z * 1000.0)
        latitude = at.x + (self.stray_e7 if self.path_index is not None else 0)
        return [
            GlobalPositionInt(1000, latitude, at.y, 700_000 + height_mm, height_mm, 0, 0, 0, 0),
            GpsRawInt(1, 3, latitude, at.y, 700_000 + height_mm, 100, 100, 0, 0, 10, 0),
            AttitudeQuaternion(1000, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
            SystemTime(1_791_212_400_000_000, 1000),
        ]


class UdpFake:
    """A fake autopilot on a loopback socket, answering in a thread until stopped."""

    def __init__(self, fake: FakeAutopilot, udp: socket.socket, peer: tuple[str, int] | None) -> None:
        self.fake = fake
        self.udp = udp
        self.peer = peer
        self.sequence = 0
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.serve, daemon=True)

    def send(self, messages: list[Message]) -> None:
        for message in messages:
            if self.peer is not None:
                self.udp.sendto(encode(message, self.sequence, self.fake.system, 1), self.peer)
                self.sequence = (self.sequence + 1) % 256

    def serve(self) -> None:
        now = 0.0
        while not self.stop.is_set():
            readable, _, _ = select.select([self.udp], [], [], 0.01)
            if readable:
                data, sender = self.udp.recvfrom(65_535)
                self.peer = self.peer or sender
                for frame in decode(data).frames:
                    self.send(self.fake.handle(frame.message))
            now += 0.05
            self.send(self.fake.tick(now))
