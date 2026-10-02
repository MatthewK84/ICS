"""The MAVLink messages the SITL rig sends and reads (ICS-018).

A typed subset of MAVLink's common message set: enough to fly PX4 and
ArduPilot SITL through a mission and to see the messages the MAVLink adapter
(ICS-021) will parse. Each message lists its fields as common.xml declares
them. The wire order, the payload layout and the CRC_EXTRA all follow from that
list by MAVLink 2's serialization rules, and the tests recompute every
CRC_EXTRA from the fields, so a field list that disagrees with common.xml
cannot pass.

Only the leading extension fields the rig reads are listed. A receiver ignores
extension bytes it does not know, and fills fields the sender trimmed with
zeros, so a message from a newer or older dialect still decodes.
"""

from __future__ import annotations

import struct
from collections.abc import Iterator
from dataclasses import dataclass
from types import MappingProxyType
from typing import ClassVar, Final

from ics_sitl.checksum import x25

# struct codes of MAVLink's field types; a char field is a byte string.
TYPE_CODES: Final = MappingProxyType(
    {
        "char": "s",
        "uint8_t": "B",
        "int8_t": "b",
        "uint16_t": "H",
        "int16_t": "h",
        "uint32_t": "I",
        "int32_t": "i",
        "uint64_t": "Q",
        "int64_t": "q",
        "float": "f",
        "double": "d",
    }
)

type Value = int | float | bytes


class MavlinkError(ValueError):
    """MAVLink that the rig cannot encode or define."""


class DefinitionError(MavlinkError):
    """A field definition that is not valid MAVLink."""

    def __init__(self, field: str, problem: str) -> None:
        super().__init__(f"field {field}: {problem}")


class EncodeError(MavlinkError):
    """A message or frame with a value its field cannot hold."""

    def __init__(self, what: str, reason: str) -> None:
        super().__init__(f"cannot encode {what}: {reason}")


@dataclass(frozen=True)
class Field:
    """One field of a message, as common.xml declares it. Only char fields are arrays."""

    name: str
    kind: str
    length: int = 0
    extension: bool = False

    def type_code(self) -> str:
        """The struct code of one element."""
        code = TYPE_CODES.get(self.kind)
        if code is None:
            raise DefinitionError(self.name, f"unknown type {self.kind}")
        return code

    def code(self) -> str:
        """The field's struct code: a byte string for a char array, else one element."""
        is_text = self.kind == "char"
        if is_text != (self.length > 0):
            raise DefinitionError(self.name, "only char fields are arrays, and they need a length")
        return f"{self.length}s" if is_text else self.type_code()

    def element_size(self) -> int:
        """The size of one element, which orders the fields on the wire."""
        return struct.calcsize("<" + self.type_code())


@dataclass(frozen=True)
class Spec:
    """A message's identity and its fields in common.xml order."""

    message_id: int
    name: str
    crc_extra: int
    fields: tuple[Field, ...]

    def wire_fields(self) -> tuple[Field, ...]:
        """Base fields by element size, largest first (a stable sort), then extensions as declared."""
        base = sorted((field for field in self.fields if not field.extension), key=Field.element_size, reverse=True)
        return (*base, *(field for field in self.fields if field.extension))

    def layout(self) -> struct.Struct:
        """The little-endian payload layout, with every listed field present."""
        return struct.Struct("<" + "".join(field.code() for field in self.wire_fields()))


def computed_crc_extra(spec: Spec) -> int:
    """CRC_EXTRA as mavgen computes it: the name, then each base field's type, name and array length."""
    crc = x25(f"{spec.name} ".encode())
    for field in spec.wire_fields():
        if field.extension:
            continue
        crc = x25(f"{field.kind} {field.name} ".encode(), crc)
        if field.length:
            crc = x25(bytes((field.length,)), crc)
    return (crc & 0xFF) ^ (crc >> 8)


@dataclass(frozen=True)
class Message:
    """A MAVLink message; each subclass names its Spec and has one attribute per field."""

    SPEC: ClassVar[Spec]

    def values(self) -> Iterator[Value]:
        """The field values in wire order."""
        for field in self.SPEC.wire_fields():
            value: Value = getattr(self, field.name)
            yield value


def u8(name: str) -> Field:
    return Field(name, "uint8_t")


def u16(name: str) -> Field:
    return Field(name, "uint16_t")


def i32(name: str) -> Field:
    return Field(name, "int32_t")


def f32(name: str) -> Field:
    return Field(name, "float")


def ext(name: str, kind: str) -> Field:
    return Field(name, kind, extension=True)


@dataclass(frozen=True)
class Heartbeat(Message):
    SPEC: ClassVar[Spec] = Spec(
        0,
        "HEARTBEAT",
        50,
        (
            u8("type"),
            u8("autopilot"),
            u8("base_mode"),
            Field("custom_mode", "uint32_t"),
            u8("system_status"),
            u8("mavlink_version"),
        ),
    )
    type: int
    autopilot: int
    base_mode: int
    custom_mode: int
    system_status: int
    mavlink_version: int


@dataclass(frozen=True)
class SystemTime(Message):
    SPEC: ClassVar[Spec] = Spec(
        2, "SYSTEM_TIME", 137, (Field("time_unix_usec", "uint64_t"), Field("time_boot_ms", "uint32_t"))
    )
    time_unix_usec: int
    time_boot_ms: int


@dataclass(frozen=True)
class GpsRawInt(Message):
    SPEC: ClassVar[Spec] = Spec(
        24,
        "GPS_RAW_INT",
        24,
        (
            Field("time_usec", "uint64_t"),
            u8("fix_type"),
            i32("lat"),
            i32("lon"),
            i32("alt"),
            u16("eph"),
            u16("epv"),
            u16("vel"),
            u16("cog"),
            u8("satellites_visible"),
            ext("alt_ellipsoid", "int32_t"),
        ),
    )
    time_usec: int
    fix_type: int
    lat: int
    lon: int
    alt: int
    eph: int
    epv: int
    vel: int
    cog: int
    satellites_visible: int
    alt_ellipsoid: int


@dataclass(frozen=True)
class AttitudeQuaternion(Message):
    SPEC: ClassVar[Spec] = Spec(
        31,
        "ATTITUDE_QUATERNION",
        246,
        (
            Field("time_boot_ms", "uint32_t"),
            f32("q1"),
            f32("q2"),
            f32("q3"),
            f32("q4"),
            f32("rollspeed"),
            f32("pitchspeed"),
            f32("yawspeed"),
        ),
    )
    time_boot_ms: int
    q1: float
    q2: float
    q3: float
    q4: float
    rollspeed: float
    pitchspeed: float
    yawspeed: float


@dataclass(frozen=True)
class GlobalPositionInt(Message):
    SPEC: ClassVar[Spec] = Spec(
        33,
        "GLOBAL_POSITION_INT",
        104,
        (
            Field("time_boot_ms", "uint32_t"),
            i32("lat"),
            i32("lon"),
            i32("alt"),
            i32("relative_alt"),
            Field("vx", "int16_t"),
            Field("vy", "int16_t"),
            Field("vz", "int16_t"),
            u16("hdg"),
        ),
    )
    time_boot_ms: int
    lat: int
    lon: int
    alt: int
    relative_alt: int
    vx: int
    vy: int
    vz: int
    hdg: int


@dataclass(frozen=True)
class MissionRequest(Message):
    SPEC: ClassVar[Spec] = Spec(
        40,
        "MISSION_REQUEST",
        230,
        (u8("target_system"), u8("target_component"), u16("seq"), ext("mission_type", "uint8_t")),
    )
    target_system: int
    target_component: int
    seq: int
    mission_type: int


@dataclass(frozen=True)
class MissionSetCurrent(Message):
    SPEC: ClassVar[Spec] = Spec(
        41, "MISSION_SET_CURRENT", 28, (u8("target_system"), u8("target_component"), u16("seq"))
    )
    target_system: int
    target_component: int
    seq: int


@dataclass(frozen=True)
class MissionCurrent(Message):
    SPEC: ClassVar[Spec] = Spec(
        42, "MISSION_CURRENT", 28, (u16("seq"), ext("total", "uint16_t"), ext("mission_state", "uint8_t"))
    )
    seq: int
    total: int
    mission_state: int


@dataclass(frozen=True)
class MissionCount(Message):
    SPEC: ClassVar[Spec] = Spec(
        44,
        "MISSION_COUNT",
        221,
        (u8("target_system"), u8("target_component"), u16("count"), ext("mission_type", "uint8_t")),
    )
    target_system: int
    target_component: int
    count: int
    mission_type: int


@dataclass(frozen=True)
class MissionItemReached(Message):
    SPEC: ClassVar[Spec] = Spec(46, "MISSION_ITEM_REACHED", 11, (u16("seq"),))
    seq: int


@dataclass(frozen=True)
class MissionAck(Message):
    SPEC: ClassVar[Spec] = Spec(
        47,
        "MISSION_ACK",
        153,
        (u8("target_system"), u8("target_component"), u8("type"), ext("mission_type", "uint8_t")),
    )
    target_system: int
    target_component: int
    type: int
    mission_type: int


@dataclass(frozen=True)
class MissionRequestInt(Message):
    SPEC: ClassVar[Spec] = Spec(
        51,
        "MISSION_REQUEST_INT",
        196,
        (u8("target_system"), u8("target_component"), u16("seq"), ext("mission_type", "uint8_t")),
    )
    target_system: int
    target_component: int
    seq: int
    mission_type: int


@dataclass(frozen=True)
class MissionItemInt(Message):
    SPEC: ClassVar[Spec] = Spec(
        73,
        "MISSION_ITEM_INT",
        38,
        (
            u8("target_system"),
            u8("target_component"),
            u16("seq"),
            u8("frame"),
            u16("command"),
            u8("current"),
            u8("autocontinue"),
            f32("param1"),
            f32("param2"),
            f32("param3"),
            f32("param4"),
            i32("x"),
            i32("y"),
            f32("z"),
            ext("mission_type", "uint8_t"),
        ),
    )
    target_system: int
    target_component: int
    seq: int
    frame: int
    command: int
    current: int
    autocontinue: int
    param1: float
    param2: float
    param3: float
    param4: float
    x: int
    y: int
    z: float
    mission_type: int


@dataclass(frozen=True)
class CommandLong(Message):
    SPEC: ClassVar[Spec] = Spec(
        76,
        "COMMAND_LONG",
        152,
        (
            u8("target_system"),
            u8("target_component"),
            u16("command"),
            u8("confirmation"),
            f32("param1"),
            f32("param2"),
            f32("param3"),
            f32("param4"),
            f32("param5"),
            f32("param6"),
            f32("param7"),
        ),
    )
    target_system: int
    target_component: int
    command: int
    confirmation: int
    param1: float
    param2: float
    param3: float
    param4: float
    param5: float
    param6: float
    param7: float


@dataclass(frozen=True)
class CommandAck(Message):
    SPEC: ClassVar[Spec] = Spec(
        77,
        "COMMAND_ACK",
        143,
        (
            u16("command"),
            u8("result"),
            ext("progress", "uint8_t"),
            ext("result_param2", "int32_t"),
            ext("target_system", "uint8_t"),
            ext("target_component", "uint8_t"),
        ),
    )
    command: int
    result: int
    progress: int
    result_param2: int
    target_system: int
    target_component: int


@dataclass(frozen=True)
class StatusText(Message):
    SPEC: ClassVar[Spec] = Spec(253, "STATUSTEXT", 83, (u8("severity"), Field("text", "char", 50)))
    severity: int
    text: bytes

    def string(self) -> str:
        """The text up to its first NUL, with any byte that is not ASCII replaced."""
        return self.text.split(b"\x00", 1)[0].decode("ascii", errors="replace")


MESSAGE_TYPES: Final[tuple[type[Message], ...]] = (
    Heartbeat,
    SystemTime,
    GpsRawInt,
    AttitudeQuaternion,
    GlobalPositionInt,
    MissionRequest,
    MissionSetCurrent,
    MissionCurrent,
    MissionCount,
    MissionItemReached,
    MissionAck,
    MissionRequestInt,
    MissionItemInt,
    CommandLong,
    CommandAck,
    StatusText,
)


def message_type(message_id: int) -> type[Message] | None:
    """The rig's class for a message ID, or None for a message it does not read."""
    for kind in MESSAGE_TYPES:
        if kind.SPEC.message_id == message_id:
            return kind
    return None


def pack(message: Message) -> bytes:
    """The message's full payload, every listed field present (MAVLink 2 then trims trailing zeros)."""
    try:
        return message.SPEC.layout().pack(*message.values())
    except struct.error as error:
        raise EncodeError(message.SPEC.name, str(error)) from error


def unpack(kind: type[Message], payload: bytes) -> Message:
    """A message from its payload: bytes past the listed fields are ignored, missing ones are zero."""
    layout = kind.SPEC.layout()
    padded = payload[: layout.size].ljust(layout.size, b"\x00")
    names = (field.name for field in kind.SPEC.wire_fields())
    return kind(**dict(zip(names, layout.unpack(padded), strict=True)))
