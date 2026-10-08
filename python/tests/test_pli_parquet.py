"""The PLI store's Parquet archive (ICS-030), read back by pyarrow.

The C++ store (cpp/store) appends each PliRecord and PliEvent to a segment log
and archives each closed segment as two Parquet files, with a writer of its
own. golden/pli holds one segment and its archive, written by the C++ test
ArchiveSegment.MatchesTheGoldenFiles, which also proves the store still writes
those bytes. These tests read the segment with a parser of their own, flatten
each message from its descriptor, and require pyarrow, an independent Parquet
reader, to find the same columns, types, nulls and values in the archive.
"""

import math
import struct
from collections.abc import Sequence
from pathlib import Path
from typing import Final

import pyarrow as pa
import pyarrow.parquet as pq
import pytest
from google.protobuf.descriptor import Descriptor, FieldDescriptor
from google.protobuf.message import Message
from ics.v1 import pli_pb2

from ics_golden.proto import message_descriptor

GOLDEN_DIR: Final = Path(__file__).resolve().parents[2] / "golden" / "pli"
MAGIC: Final = b"ICSPLI1\n"
# Each entry: payload length, CRC-32C of the kind and payload, kind.
ENTRY_HEADER: Final = struct.Struct("<IIB")
KIND_OFFSET: Final = 8
CRC_POLYNOMIAL: Final = 0x82F63B78
CRC_MASK: Final = 0xFFFFFFFF
BITS_PER_BYTE: Final = 8
RECORD_KIND: Final = 1
EVENT_KIND: Final = 2
# The rows per row group the golden archive was written with.
GOLDEN_ROW_GROUP_ROWS: Final = 3
# What cpp/store/test/golden_pli.hpp writes.
GOLDEN_RECORDS: Final = 7
GOLDEN_EVENTS: Final = 4
# CRC-32C of "123456789", from the catalogue of parametrised CRCs.
CRC32C_CHECK: Final = 0xE3069283
CREATED_BY: Final = "ics ics::store (ICS-030)"

type Cell = str | int | float | None
type Path_ = tuple[FieldDescriptor, ...]


def crc32c(data: bytes) -> int:
    """CRC-32C (Castagnoli), one bit at a time."""
    crc = CRC_MASK
    for byte in data:
        crc ^= byte
        for _ in range(BITS_PER_BYTE):
            crc = (crc >> 1) ^ (CRC_POLYNOMIAL if crc & 1 else 0)
    return crc ^ CRC_MASK


def golden_file(suffix: str) -> Path:
    """The one golden file whose name ends with suffix."""
    found = sorted(GOLDEN_DIR.glob(f"pli-*Z{suffix}"))
    assert len(found) == 1, f"golden/pli holds {len(found)} *{suffix} files"
    return found[0]


def read_segment(path: Path) -> tuple[list[Message], list[Message]]:
    """The records and events in a segment, which must have no torn tail."""
    data = path.read_bytes()
    assert data.startswith(MAGIC)
    records: list[Message] = []
    events: list[Message] = []
    offset = len(MAGIC)
    while offset < len(data):
        length, crc, kind = ENTRY_HEADER.unpack_from(data, offset)
        end = offset + ENTRY_HEADER.size + length
        assert end <= len(data)
        assert crc32c(data[offset + KIND_OFFSET : end]) == crc
        payload = data[offset + ENTRY_HEADER.size : end]
        if kind == RECORD_KIND:
            records.append(pli_pb2.PliRecord.FromString(payload))
        else:
            assert kind == EVENT_KIND
            events.append(pli_pb2.PliEvent.FromString(payload))
        offset = end
    return records, events


def leaf_paths(descriptor: Descriptor) -> list[tuple[Path_, bool]]:
    """Each scalar field under descriptor, depth first, and whether it may be null."""
    leaves: list[tuple[Path_, bool]] = []
    pending: list[tuple[Path_, bool]] = [((field,), False) for field in reversed(descriptor.fields)]
    while pending:
        path, inside_message = pending.pop()
        field = path[-1]
        if field.message_type is not None:
            pending.extend(((*path, child), True) for child in reversed(field.message_type.fields))
        else:
            leaves.append((path, inside_message or field.has_presence))
    return leaves


def column_name(path: Path_) -> str:
    """The column of a scalar field: the field names joined by dots."""
    return ".".join(field.name for field in path)


def cell(message: Message, path: Path_) -> Cell:
    """The value the store writes for the scalar at path, or None for a null."""
    holder = message
    for field in path[:-1]:
        if not holder.HasField(field.name):
            return None
        holder = getattr(holder, field.name)
    leaf = path[-1]
    if leaf.has_presence and not holder.HasField(leaf.name):
        return None
    value: Cell = getattr(holder, leaf.name)
    if leaf.enum_type is not None:
        named = leaf.enum_type.values_by_number.get(int(value or 0))
        return named.name if named is not None else str(value)
    return value


def flatten(messages: Sequence[Message]) -> list[dict[str, Cell]]:
    """One row per message, as pyarrow's Table.to_pylist gives them."""
    if not messages:
        return []
    leaves = leaf_paths(message_descriptor(messages[0]))
    return [{column_name(path): cell(message, path) for path, _ in leaves} for message in messages]


def arrow_type_name(field: FieldDescriptor) -> str:
    """The Arrow type pyarrow reads the store's column for field as."""
    if field.cpp_type in {FieldDescriptor.CPPTYPE_STRING, FieldDescriptor.CPPTYPE_ENUM}:
        return "string"
    if field.cpp_type == FieldDescriptor.CPPTYPE_DOUBLE:
        return "double"
    return "int64"


def golden_cases() -> list[tuple[str, int]]:
    """Each golden Parquet file's suffix and the index of its messages in read_segment's result."""
    return [(".records.parquet", 0), (".events.parquet", 1)]


def test_crc32c_matches_the_check_value() -> None:
    assert crc32c(b"123456789") == CRC32C_CHECK


def test_segment_holds_records_and_events() -> None:
    records, events = read_segment(golden_file(".icspli"))
    assert len(records) == GOLDEN_RECORDS
    assert len(events) == GOLDEN_EVENTS


@pytest.mark.parametrize(("suffix", "index"), golden_cases())
def test_pyarrow_reads_the_messages_the_segment_holds(suffix: str, index: int) -> None:
    messages = read_segment(golden_file(".icspli"))[index]
    table = pq.read_table(golden_file(suffix), use_threads=False)
    table.validate(full=True)
    assert table.to_pylist() == flatten(messages)


@pytest.mark.parametrize(("suffix", "index"), golden_cases())
def test_columns_follow_the_message_descriptor(suffix: str, index: int) -> None:
    descriptor = message_descriptor(read_segment(golden_file(".icspli"))[index][0])
    schema = pq.read_schema(golden_file(suffix))
    expected = [(column_name(path), arrow_type_name(path[-1]), nullable) for path, nullable in leaf_paths(descriptor)]
    found = [(field.name, str(field.type), field.nullable) for field in schema]
    assert found == expected


@pytest.mark.parametrize(("suffix", "index"), golden_cases())
def test_row_groups_hold_at_most_the_configured_rows(suffix: str, index: int) -> None:
    count = len(read_segment(golden_file(".icspli"))[index])
    metadata = pq.ParquetFile(golden_file(suffix)).metadata
    assert metadata.num_rows == count
    assert metadata.num_row_groups == math.ceil(count / GOLDEN_ROW_GROUP_ROWS)
    sizes = [metadata.row_group(group).num_rows for group in range(metadata.num_row_groups)]
    assert all(size <= GOLDEN_ROW_GROUP_ROWS for size in sizes)
    assert metadata.created_by == CREATED_BY


def test_nulls_and_unnamed_enum_values_survive() -> None:
    table = pq.read_table(golden_file(".records.parquet"), use_threads=False)
    rows = table.to_pylist()
    assert rows[1]["velocity_enu_mps.east"] is None
    assert rows[2]["role"] == "99"
    assert rows[2]["entity_id"] == "drone-é✈"
    assert table.schema.field("valid_utc_ns").type == pa.int64()
