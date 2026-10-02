"""Tests for the SITL rig's MAVLink messages and framing (ICS-018).

The fixed frames were produced by pymavlink 2.4 from common.xml, the reference
MAVLink implementation; pymavlink itself is not a dependency.
"""

from __future__ import annotations

from typing import Final

import pytest
from hypothesis import given
from hypothesis import strategies as st

from ics_sitl.checksum import x25
from ics_sitl.mavlink import Decoded, decode, encode
from ics_sitl.messages import (
    MESSAGE_TYPES,
    CommandAck,
    DefinitionError,
    EncodeError,
    Field,
    GlobalPositionInt,
    Heartbeat,
    Message,
    Spec,
    StatusText,
    computed_crc_extra,
    message_type,
    pack,
    unpack,
)

# HEARTBEAT from a ground station, system 255, component 190, sequence 7.
HEARTBEAT_FRAME: Final = bytes.fromhex("fd09000007ffbe0000000000000006080004037efa")
GROUND_HEARTBEAT: Final = Heartbeat(6, 8, 0, 0, 4, 3)
# GLOBAL_POSITION_INT from system 1, component 1, sequence 200.
POSITION_FRAME: Final = bytes.fromhex(
    "fd1c0000c8010121000040e201000084d717003665c454b00a00f4010000f4ff220000002823a420"
)
POSITION: Final = GlobalPositionInt(123456, 400000000, -1000000000, 700500, 500, -12, 34, 0, 9000)
# STATUSTEXT, its trailing zero bytes trimmed as MAVLink 2 requires.
TEXT_FRAME: Final = bytes.fromhex(
    "fd1f0000c80101fd00000650726541726d3a204e65656420506f736974696f6e20457374696d6174652709"
)
# A MAVLink 1 COMMAND_ACK from system 2, which has no extension fields.
V1_ACK_FRAME: Final = bytes.fromhex("fe030902014d9001003672")
BITS: Final = (("uint8_t", 8), ("uint16_t", 16), ("uint32_t", 32), ("uint64_t", 64))
SIGNED_BITS: Final = (("int8_t", 8), ("int16_t", 16), ("int32_t", 32), ("int64_t", 64))
# CRC-16/MCRF4XX of "123456789", the catalogued check value.
CHECK_VALUE: Final = 0x6F91
ARDUPILOT_SYSTEM: Final = 2
# A MAVLink 1 frame with an empty payload: header and checksum.
SMALLEST_FRAME: Final = 8


def single(decoded: Decoded) -> Message:
    assert len(decoded.frames) == 1
    assert decoded.rejected == 0
    return decoded.frames[0].message


def test_every_crc_extra_follows_from_its_fields() -> None:
    for kind in MESSAGE_TYPES:
        assert computed_crc_extra(kind.SPEC) == kind.SPEC.crc_extra, kind.SPEC.name


def test_message_ids_are_unique_and_found() -> None:
    ids = [kind.SPEC.message_id for kind in MESSAGE_TYPES]
    assert len(set(ids)) == len(ids)
    for kind in MESSAGE_TYPES:
        assert message_type(kind.SPEC.message_id) is kind
    assert message_type(30) is None


def test_checksum_matches_the_mcrf4xx_check_value() -> None:
    assert x25(b"123456789") == CHECK_VALUE


def test_encodes_as_pymavlink_does() -> None:
    assert encode(GROUND_HEARTBEAT, 7, 255, 190) == HEARTBEAT_FRAME
    assert encode(POSITION, 200, 1, 1) == POSITION_FRAME
    text = StatusText(6, b"PreArm: Need Position Estimate".ljust(50, b"\x00"))
    assert encode(text, 200, 1, 1) == TEXT_FRAME


def test_decodes_what_pymavlink_encodes() -> None:
    decoded = decode(POSITION_FRAME)
    assert single(decoded) == POSITION
    frame = decoded.frames[0]
    assert (frame.sequence, frame.system, frame.component) == (200, 1, 1)
    text = single(decode(TEXT_FRAME))
    assert isinstance(text, StatusText)
    assert text.string() == "PreArm: Need Position Estimate"


def test_decodes_mavlink_1_and_zero_fills_missing_extensions() -> None:
    decoded = decode(V1_ACK_FRAME)
    assert single(decoded) == CommandAck(400, 0, 0, 0, 0, 0)
    assert decoded.frames[0].system == ARDUPILOT_SYSTEM


def test_reads_several_frames_and_skips_what_is_not_mavlink() -> None:
    data = b"Init ArduCopter\n" + HEARTBEAT_FRAME + b"\x00" + POSITION_FRAME
    decoded = decode(data)
    assert [frame.message for frame in decoded.frames] == [GROUND_HEARTBEAT, POSITION]
    assert decoded.rejected == len(b"Init ArduCopter\n") + 1
    assert decoded.unknown == 0


def test_steps_over_messages_it_does_not_read() -> None:
    unknown = bytearray(HEARTBEAT_FRAME)
    unknown[7] = 30
    decoded = decode(bytes(unknown) + POSITION_FRAME)
    assert decoded.unknown == 1
    assert single(decoded) == POSITION


def test_rejects_a_bad_checksum_a_signed_frame_and_a_cut_frame() -> None:
    corrupt = bytearray(POSITION_FRAME)
    corrupt[12] ^= 0x01
    signed = bytearray(HEARTBEAT_FRAME)
    signed[2] = 0x01
    for data in (bytes(corrupt), bytes(signed), POSITION_FRAME[:-1], POSITION_FRAME[:5], V1_ACK_FRAME[:4]):
        decoded = decode(data)
        assert decoded.frames == ()
        assert decoded.rejected == len(data)


def test_ignores_extension_bytes_it_does_not_know() -> None:
    longer = pack(GROUND_HEARTBEAT) + b"\x07\x07"
    assert unpack(Heartbeat, longer) == GROUND_HEARTBEAT


def test_refuses_values_a_field_cannot_hold() -> None:
    with pytest.raises(EncodeError, match="HEARTBEAT"):
        encode(Heartbeat(256, 8, 0, 0, 4, 3), 0, 255, 190)
    with pytest.raises(EncodeError, match="must be bytes"):
        encode(GROUND_HEARTBEAT, 256, 255, 190)


def test_refuses_definitions_mavlink_does_not_have() -> None:
    with pytest.raises(DefinitionError, match="unknown type"):
        Spec(1, "X", 0, (Field("a", "uint7_t"),)).layout()
    with pytest.raises(DefinitionError, match="need a length"):
        Spec(1, "X", 0, (Field("a", "char"),)).layout()
    with pytest.raises(DefinitionError, match="only char fields"):
        Spec(1, "X", 0, (Field("a", "uint8_t", 4),)).layout()


def field_values(field: Field) -> st.SearchStrategy[int | float | bytes]:
    """Any value the field can carry."""
    unsigned = dict(BITS).get(field.kind)
    signed = dict(SIGNED_BITS).get(field.kind)
    if unsigned is not None:
        return st.integers(0, 2**unsigned - 1)
    if signed is not None:
        return st.integers(-(2 ** (signed - 1)), 2 ** (signed - 1) - 1)
    if field.kind == "char":
        return st.binary(max_size=field.length).map(lambda text: text.ljust(field.length, b"\x00"))
    return st.floats(width=32, allow_nan=False)


@st.composite
def messages(draw: st.DrawFn) -> Message:
    kind = draw(st.sampled_from(MESSAGE_TYPES))
    values = {field.name: draw(field_values(field)) for field in kind.SPEC.fields}
    return kind(**values)


@given(messages(), st.integers(0, 255), st.integers(0, 255), st.integers(0, 255))
def test_every_message_survives_a_round_trip(message: Message, sequence: int, system: int, component: int) -> None:
    frame = encode(message, sequence, system, component)
    decoded = decode(frame)
    assert single(decoded) == message
    assert (decoded.frames[0].sequence, decoded.frames[0].system) == (sequence, system)


@given(st.binary(max_size=600))
def test_decoding_any_bytes_never_fails(data: bytes) -> None:
    decoded = decode(data)
    assert 0 <= decoded.rejected <= len(data)
    assert (len(decoded.frames) + decoded.unknown) * SMALLEST_FRAME <= len(data)


# Bytes that cannot start a frame, so they can only be skipped.
noise = st.binary(max_size=40).map(lambda data: data.replace(b"\xfd", b"\x00").replace(b"\xfe", b"\x00"))


@given(messages(), noise, noise)
def test_a_frame_is_found_among_noise(message: Message, before: bytes, after: bytes) -> None:
    decoded = decode(before + encode(message, 1, 1, 1) + after)
    assert [frame.message for frame in decoded.frames] == [message]
    assert decoded.rejected == len(before) + len(after)
