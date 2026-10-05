"""Cut windows of an autopilot's onboard log into a small test fixture (ICS-025).

The SITL rig's onboard logs, PX4's ULog and ArduPilot's DataFlash, run to tens of megabytes. A cut keeps
every message that defines the log (the ULog header, formats, information and parameters; the DataFlash
FMT, FMTU, UNIT, MULT and PARM messages), and of the rest only the messages of the named topics or message
types whose time stamp falls in one of the windows. Kept messages are copied unchanged and in order, so a
reader sees a log of the same layout holding every sample of those topics in the windows.
"""

from __future__ import annotations

import struct
from collections.abc import Iterator, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Final

MICROSECONDS: Final = 1_000_000

ULOG_MAGIC: Final = b"ULog\x01\x12\x35"
ULOG_HEADER_BYTES: Final = 16
ULOG_MESSAGE: Final = struct.Struct("<HB")
ULOG_FLAG_BITS: Final = ord("B")
ULOG_INCOMPAT_FLAGS: Final = 8
ULOG_DATA_APPENDED: Final = 0x01
ULOG_ADD_LOGGED: Final = ord("A")
ULOG_DATA: Final = ord("D")
ULOG_LOGGING: Final = ord("L")
ULOG_LOGGING_TAGGED: Final = ord("C")
ULOG_REMOVE_LOGGED: Final = ord("R")
ULOG_MSG_ID: Final = struct.Struct("<H")
ULOG_TIMESTAMP: Final = struct.Struct("<Q")
# Where each kind of message holds its time stamp: after a message ID, a log level, or a level and a tag.
ULOG_TIMESTAMP_AT: Final = {ULOG_DATA: 2, ULOG_LOGGING: 1, ULOG_LOGGING_TAGGED: 3}

DATAFLASH_HEAD: Final = b"\xa3\x95"
DATAFLASH_FMT_TYPE: Final = 128
DATAFLASH_FMT: Final = struct.Struct("<3sBB4s16s64s")
DATAFLASH_TIME: Final = struct.Struct("<3sQ")
DATAFLASH_ALWAYS: Final = frozenset({"FMT", "FMTU", "UNIT", "MULT", "PARM"})


class LogCutError(ValueError):
    """A log, window or request that cannot be cut."""


@dataclass(frozen=True)
class Window:
    """Boot times from start_us to end_us, both included, in microseconds."""

    start_us: int
    end_us: int

    def contains(self, time_us: int) -> bool:
        return self.start_us <= time_us <= self.end_us


def parse_window(text: str) -> Window:
    """START:END in seconds since boot, as a window."""
    start, separator, end = text.partition(":")
    try:
        window = Window(round(float(start) * MICROSECONDS), round(float(end) * MICROSECONDS))
    except ValueError as error:
        message = f"expected START:END in seconds, got {text!r}"
        raise LogCutError(message) from error
    if not separator or window.start_us < 0 or window.end_us < window.start_us:
        message = f"expected START:END in seconds, START not after END, got {text!r}"
        raise LogCutError(message)
    return window


def in_windows(time_us: int, windows: Sequence[Window]) -> bool:
    return any(window.contains(time_us) for window in windows)


def ulog_messages(data: bytes) -> Iterator[tuple[int, bytes]]:
    """Each message after the header: its type and its whole bytes, header included."""
    offset = ULOG_HEADER_BYTES
    while offset < len(data):
        if offset + ULOG_MESSAGE.size > len(data):
            message = f"ULog message header cut short at byte {offset}"
            raise LogCutError(message)
        size, kind = ULOG_MESSAGE.unpack_from(data, offset)
        end = offset + ULOG_MESSAGE.size + size
        if end > len(data):
            message = f"ULog message at byte {offset} runs past the end of the file"
            raise LogCutError(message)
        yield kind, data[offset:end]
        offset = end


def ulog_time(kind: int, message: bytes) -> int | None:
    """The time stamp of a data or logging message, or None for any other."""
    at = ULOG_TIMESTAMP_AT.get(kind)
    if at is None:
        return None
    if len(message) < ULOG_MESSAGE.size + at + ULOG_TIMESTAMP.size:
        error = "ULog message too short for its time stamp"
        raise LogCutError(error)
    value: int = ULOG_TIMESTAMP.unpack_from(message, ULOG_MESSAGE.size + at)[0]
    return value


def ulog_keeps(kind: int, message: bytes, kept_ids: set[int], windows: Sequence[Window]) -> bool:
    """Whether a message other than an add-logged one stays."""
    flags_at = ULOG_MESSAGE.size + ULOG_INCOMPAT_FLAGS
    if kind == ULOG_FLAG_BITS and len(message) > flags_at and message[flags_at] & ULOG_DATA_APPENDED:
        error = "ULog logs with appended data are not supported"
        raise LogCutError(error)
    if kind in {ULOG_DATA, ULOG_REMOVE_LOGGED}:
        msg_id: int = ULOG_MSG_ID.unpack_from(message, ULOG_MESSAGE.size)[0]
        if msg_id not in kept_ids:
            return False
    time_us = ulog_time(kind, message)
    return time_us is None or in_windows(time_us, windows)


def cut_ulog(data: bytes, keep: frozenset[str], windows: Sequence[Window]) -> bytes:
    """A ULog log holding the definitions, and the samples of the kept topics in the windows."""
    if not data.startswith(ULOG_MAGIC) or len(data) < ULOG_HEADER_BYTES:
        error = "not a ULog log"
        raise LogCutError(error)
    kept = [data[:ULOG_HEADER_BYTES]]
    kept_ids: set[int] = set()
    for kind, message in ulog_messages(data):
        if kind == ULOG_ADD_LOGGED:
            name = message[ULOG_MESSAGE.size + 3 :].decode("ascii", errors="replace")
            if name in keep:
                kept_ids.add(ULOG_MSG_ID.unpack_from(message, ULOG_MESSAGE.size + 1)[0])
                kept.append(message)
        elif ulog_keeps(kind, message, kept_ids, windows):
            kept.append(message)
    return b"".join(kept)


@dataclass(frozen=True)
class DataFlashType:
    """A DataFlash message type, from its FMT message."""

    name: str
    length: int
    timed: bool


FMT_TYPE: Final = DataFlashType("FMT", DATAFLASH_FMT.size, timed=False)


def dataflash_type(message: bytes) -> tuple[int, DataFlashType]:
    """The type number and description an FMT message defines."""
    _, number, length, name, layout, columns = DATAFLASH_FMT.unpack(message)
    text = name.rstrip(b"\0").decode("ascii", errors="replace")
    timed = layout.startswith(b"Q") and columns.startswith(b"TimeUS")
    return number, DataFlashType(text, length, timed)


def dataflash_messages(data: bytes) -> Iterator[tuple[DataFlashType, bytes]]:
    """Each message: its type and its whole bytes, header included. A last message cut short, as when the
    autopilot stopped while writing it, is left out."""
    types = {DATAFLASH_FMT_TYPE: FMT_TYPE}
    offset = 0
    while offset < len(data):
        headed = offset + len(DATAFLASH_HEAD) < len(data) and data[offset : offset + 2] == DATAFLASH_HEAD
        found = types.get(data[offset + 2]) if headed else None
        if found is None:
            message = f"DataFlash message at byte {offset} has no header or no format"
            raise LogCutError(message)
        if offset + found.length > len(data):
            return
        message_bytes = data[offset : offset + found.length]
        if data[offset + 2] == DATAFLASH_FMT_TYPE:
            number, defined = dataflash_type(message_bytes)
            types[number] = defined
        yield found, message_bytes
        offset += found.length


def dataflash_keeps(found: DataFlashType, message: bytes, keep: frozenset[str], windows: Sequence[Window]) -> bool:
    if found.name in DATAFLASH_ALWAYS:
        return True
    if found.name not in keep:
        return False
    if not found.timed:
        error = f"DataFlash type {found.name} has no TimeUS to cut by"
        raise LogCutError(error)
    time_us: int = DATAFLASH_TIME.unpack_from(message)[1]
    return in_windows(time_us, windows)


def cut_dataflash(data: bytes, keep: frozenset[str], windows: Sequence[Window]) -> bytes:
    """A DataFlash log holding the formats and parameters, and the kept types' messages in the windows."""
    messages = dataflash_messages(data)
    return b"".join(message for found, message in messages if dataflash_keeps(found, message, keep, windows))


def cut_log(source: Path, target: Path, keep: Sequence[str], windows: Sequence[Window]) -> int:
    """Cut a ULog or DataFlash log, chosen by its first bytes, into target; returns the bytes written."""
    if not windows or not keep:
        error = "give at least one window and one topic or message type"
        raise LogCutError(error)
    try:
        data = source.read_bytes()
    except OSError as error:
        raise LogCutError(str(error)) from error
    cut = cut_ulog if data.startswith(ULOG_MAGIC) else cut_dataflash
    result = cut(data, frozenset(keep), windows)
    try:
        target.write_bytes(result)
    except OSError as error:
        raise LogCutError(str(error)) from error
    return len(result)
