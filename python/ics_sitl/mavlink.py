"""MAVLink framing for the SITL rig (ICS-018): MAVLink 2 out, MAVLink 1 and 2 in.

``decode`` reads every frame in a datagram the way an autopilot's parser does:
a byte that does not start a valid frame is skipped and counted, and the
search resumes at the next byte. A frame of a message the rig does not read is
stepped over by its length, since its checksum cannot be checked without the
message's CRC_EXTRA. Signed frames, and any other incompatibility flag, are
rejected: the rig never signs and accepts nothing it cannot verify.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Final

from ics_sitl.checksum import x25
from ics_sitl.messages import EncodeError, Message, message_type, pack, unpack

MAGIC_V1: Final = 0xFE
MAGIC_V2: Final = 0xFD
# Bytes before the payload: magic, length, sequence, system, component, message ID.
V1_HEADER: Final = 6
# MAVLink 2 adds two flag bytes and a 24-bit message ID.
V2_HEADER: Final = 10
CHECKSUM: Final = 2
BYTE_MAX: Final = 0xFF


@dataclass(frozen=True)
class Frame:
    """One frame: who sent it, its sequence number and its message."""

    sequence: int
    system: int
    component: int
    message: Message


@dataclass(frozen=True)
class Decoded:
    """The frames found in a datagram, the frames of messages the rig does not read, and bytes rejected."""

    frames: tuple[Frame, ...]
    unknown: int
    rejected: int


class Outcome(Enum):
    FRAME = "frame"
    UNKNOWN = "unknown"
    REJECTED = "rejected"


@dataclass(frozen=True)
class Step:
    """What one position in a datagram held, and how many bytes it took."""

    outcome: Outcome
    consumed: int
    frame: Frame | None = None


REJECT_ONE: Final = Step(Outcome.REJECTED, 1)


def encode(message: Message, sequence: int, system: int, component: int) -> bytes:
    """A MAVLink 2 frame, its payload trimmed of trailing zeros as MAVLink 2 requires."""
    header_values = (sequence, system, component)
    if any(not 0 <= value <= BYTE_MAX for value in header_values):
        raise EncodeError(message.SPEC.name, f"sequence, system and component must be bytes, not {header_values}")
    payload = pack(message).rstrip(b"\x00") or b"\x00"
    header = bytes((MAGIC_V2, len(payload), 0, 0, *header_values)) + message.SPEC.message_id.to_bytes(3, "little")
    checksum = x25(bytes((message.SPEC.crc_extra,)), x25(header[1:] + payload))
    return header + payload + checksum.to_bytes(CHECKSUM, "little")


def decode(data: bytes) -> Decoded:
    """Every frame in ``data``, in order."""
    frames: list[Frame] = []
    unknown = 0
    rejected = 0
    offset = 0
    while offset < len(data):
        step = step_at(data, offset)
        offset += step.consumed
        if step.frame is not None:
            frames.append(step.frame)
        elif step.outcome is Outcome.UNKNOWN:
            unknown += 1
        else:
            rejected += step.consumed
    return Decoded(tuple(frames), unknown, rejected)


def step_at(data: bytes, offset: int) -> Step:
    """The frame, unknown frame or rejected byte at ``offset``."""
    if data[offset] == MAGIC_V2:
        return v2_step(data, offset)
    if data[offset] == MAGIC_V1:
        return v1_step(data, offset)
    return REJECT_ONE


def v2_step(data: bytes, offset: int) -> Step:
    header = data[offset : offset + V2_HEADER]
    if len(header) < V2_HEADER or header[2] != 0:
        return REJECT_ONE
    message_id = int.from_bytes(header[7:V2_HEADER], "little")
    return checked_step(data, offset, V2_HEADER, message_id, header[4:7])


def v1_step(data: bytes, offset: int) -> Step:
    header = data[offset : offset + V1_HEADER]
    if len(header) < V1_HEADER:
        return REJECT_ONE
    return checked_step(data, offset, V1_HEADER, header[5], header[2:5])


def checked_step(data: bytes, offset: int, header_size: int, message_id: int, sender: bytes) -> Step:
    """The frame after its header, if it fits in ``data`` and its checksum holds."""
    end = offset + header_size + data[offset + 1] + CHECKSUM
    if end > len(data):
        return REJECT_ONE
    kind = message_type(message_id)
    if kind is None:
        return Step(Outcome.UNKNOWN, end - offset)
    body = data[offset + 1 : end - CHECKSUM]
    expected = int.from_bytes(data[end - CHECKSUM : end], "little")
    if x25(bytes((kind.SPEC.crc_extra,)), x25(body)) != expected:
        return REJECT_ONE
    message = unpack(kind, data[offset + header_size : end - CHECKSUM])
    sequence, system, component = sender
    return Step(Outcome.FRAME, end - offset, Frame(sequence, system, component, message))
