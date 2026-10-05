"""Tests for cutting onboard logs into fixtures (ics_sitl.logcut, ICS-025)."""

from __future__ import annotations

import struct
from pathlib import Path
from typing import Final

import pytest

from ics_sitl.__main__ import main
from ics_sitl.logcut import LogCutError, Window, cut_dataflash, cut_log, cut_ulog, parse_window

EXIT_USAGE: Final = 2
ULOG_HEADER: Final = b"ULog\x01\x12\x35\x01" + struct.pack("<Q", 0)
INSIDE: Final = (Window(1_000_000, 2_000_000),)


def ulog_message(kind: str, payload: bytes) -> bytes:
    return struct.pack("<HB", len(payload), ord(kind)) + payload


def flag_bits(incompat: int = 0) -> bytes:
    return ulog_message("B", bytes(8) + bytes([incompat]) + bytes(7) + bytes(24))


def add_logged(msg_id: int, name: str) -> bytes:
    return ulog_message("A", struct.pack("<BH", 0, msg_id) + name.encode())


def data(msg_id: int, time_us: int) -> bytes:
    return ulog_message("D", struct.pack("<HQ", msg_id, time_us) + b"\x01\x02")


def logging(time_us: int, text: str) -> bytes:
    return ulog_message("L", struct.pack("<BQ", 54, time_us) + text.encode())


def tagged(time_us: int, text: str) -> bytes:
    return ulog_message("C", struct.pack("<BHQ", 54, 7, time_us) + text.encode())


def ulog(*messages: bytes) -> bytes:
    return ULOG_HEADER + b"".join(messages)


def test_ulog_keeps_definitions_and_kept_samples_in_the_windows() -> None:
    definitions = [flag_bits(), ulog_message("F", b"pos:uint64_t timestamp;"), ulog_message("P", b"\x05int32_t X1234")]
    kept = [add_logged(0, "pos"), data(0, 1_000_000), logging(1_500_000, "in"), tagged(2_000_000, "edge")]
    dropped = [add_logged(1, "att"), data(1, 1_500_000), data(0, 999_999), data(0, 2_000_001), logging(5, "early")]
    log = ulog(*definitions, kept[0], dropped[0], kept[1], dropped[1], dropped[2], kept[2], kept[3], dropped[3])
    log += dropped[4]
    assert cut_ulog(log, frozenset({"pos"}), INSIDE) == ulog(*definitions, *kept)


def test_ulog_keeps_a_kept_topics_removal_and_other_messages() -> None:
    removal = ulog_message("R", struct.pack("<H", 0))
    other_removal = ulog_message("R", struct.pack("<H", 1))
    sync = ulog_message("S", b"\x2f\x73\x13\x20\x25\x0c\xbb\x12")
    log = ulog(add_logged(0, "pos"), add_logged(1, "att"), sync, removal, other_removal)
    assert cut_ulog(log, frozenset({"pos"}), INSIDE) == ulog(add_logged(0, "pos"), sync, removal)


@pytest.mark.parametrize(
    ("log", "problem"),
    [
        (b"ULog\x01\x12\x35", "not a ULog log"),
        (b"PK\x03\x04" + bytes(12), "not a ULog log"),
        (ulog(flag_bits(incompat=1)), "appended data"),
        (ulog(b"\x05"), "header cut short"),
        (ulog(struct.pack("<HB", 9, ord("D")) + b"\x00"), "runs past the end"),
        (ulog(add_logged(0, "pos"), ulog_message("D", b"\x00\x00\x01")), "too short for its time stamp"),
    ],
)
def test_ulog_refuses_what_it_cannot_cut(log: bytes, problem: str) -> None:
    with pytest.raises(LogCutError, match=problem):
        cut_ulog(log, frozenset({"pos"}), INSIDE)


FMT_LENGTH: Final = 89


def fmt(number: int, length: int, name: str, layout: str, columns: str) -> bytes:
    body = struct.pack("<BB4s16s64s", number, length, name.encode(), layout.encode(), columns.encode())
    return b"\xa3\x95\x80" + body


def timed(number: int, time_us: int) -> bytes:
    return b"\xa3\x95" + bytes([number]) + struct.pack("<Qf", time_us, 1.0)


def param() -> bytes:
    return b"\xa3\x95\x40" + struct.pack("<Q16sf", 1, b"SYSID_THISMAV", 2.0)


DATAFLASH_FORMATS: Final = (
    fmt(128, FMT_LENGTH, "FMT", "BBnNZ", "Type,Length,Name,Format,Columns"),
    fmt(64, 31, "PARM", "QNf", "TimeUS,Name,Value"),
    fmt(65, 15, "POS", "Qf", "TimeUS,Alt"),
    fmt(66, 15, "ATT", "Qf", "TimeUS,Roll"),
    fmt(67, 7, "VER", "BBBB", "A,B,C,D"),
)


def test_dataflash_keeps_formats_parameters_and_kept_types_in_the_windows() -> None:
    samples = [timed(65, 999_999), timed(65, 1_000_000), timed(66, 1_500_000), timed(65, 2_000_000)]
    log = b"".join([*DATAFLASH_FORMATS, param(), *samples])
    expected = b"".join([*DATAFLASH_FORMATS, param(), timed(65, 1_000_000), timed(65, 2_000_000)])
    assert cut_dataflash(log, frozenset({"POS"}), INSIDE) == expected


def test_dataflash_refuses_a_message_before_its_format() -> None:
    # Its length is not known yet, so the log cannot be walked.
    log = DATAFLASH_FORMATS[0] + timed(65, 1_000_000) + DATAFLASH_FORMATS[2]
    with pytest.raises(LogCutError, match="no header or no format"):
        cut_dataflash(log, frozenset({"POS"}), INSIDE)


def test_dataflash_drops_a_last_message_cut_short() -> None:
    log = b"".join([*DATAFLASH_FORMATS, timed(65, 1_500_000), timed(65, 1_600_000)[:9]])
    assert cut_dataflash(log, frozenset({"POS"}), INSIDE) == b"".join([*DATAFLASH_FORMATS, timed(65, 1_500_000)])


@pytest.mark.parametrize(
    ("log", "keep", "problem"),
    [
        (b"".join(DATAFLASH_FORMATS) + b"\x00\x00\x41", "POS", "no header or no format"),
        (b"".join(DATAFLASH_FORMATS) + b"\xa3\x95", "POS", "no header or no format"),
        (b"".join(DATAFLASH_FORMATS) + b"\xa3\x95\x43" + bytes(4), "VER", "no TimeUS"),
    ],
)
def test_dataflash_refuses_what_it_cannot_cut(log: bytes, keep: str, problem: str) -> None:
    with pytest.raises(LogCutError, match=problem):
        cut_dataflash(log, frozenset({keep}), INSIDE)


def test_windows_are_read_in_seconds() -> None:
    assert parse_window("1.5:2") == Window(1_500_000, 2_000_000)
    assert parse_window("0:0") == Window(0, 0)
    for text in ("2:1", "-1:2", "1", "a:b", "1:"):
        with pytest.raises(LogCutError, match="START:END"):
            parse_window(text)


def test_cut_log_writes_the_cut_chosen_by_the_first_bytes(tmp_path: Path) -> None:
    source = tmp_path / "log.ulg"
    source.write_bytes(ulog(add_logged(0, "pos"), data(0, 1_500_000)))
    target = tmp_path / "cut.ulg"
    assert cut_log(source, target, ["pos"], list(INSIDE)) == target.stat().st_size
    assert target.read_bytes() == source.read_bytes()
    with pytest.raises(LogCutError, match="at least one window"):
        cut_log(source, target, [], list(INSIDE))
    with pytest.raises(LogCutError, match="No such file"):
        cut_log(tmp_path / "absent.bin", target, ["POS"], list(INSIDE))
    with pytest.raises(LogCutError, match="Is a directory"):
        cut_log(source, tmp_path, ["pos"], list(INSIDE))


def test_the_command_line_cuts_and_reports_errors(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    source = tmp_path / "log.bin"
    source.write_bytes(b"".join([*DATAFLASH_FORMATS, timed(65, 1_500_000)]))
    target = tmp_path / "cut.bin"
    assert main(["cut-log", str(source), str(target), "--window", "1:2", "--keep", "POS"]) == 0
    assert f"{target}: {source.stat().st_size} bytes" in capsys.readouterr().out
    assert main(["cut-log", str(source), str(target), "--window", "2:1", "--keep", "POS"]) == EXIT_USAGE
    assert "START:END" in capsys.readouterr().err
