"""Golden protobuf messages for the cross-language contract tests (ICS-012).

Each top-level ICS message gets one instance with every field set, written to
golden/proto/<name>.binpb. The C++ and TypeScript tests parse every file and
must serialize it back to the same bytes, which proves the three generated
packages agree on every field. The values are arbitrary but deterministic: each
depends only on the field's number, type and position in a repeated field.

Regenerate from python/ after changing proto/:

    PYTHONPATH=gen uv run --locked python -m ics_golden.proto ../golden/proto
"""

import sys
from collections.abc import Callable, Sequence
from pathlib import Path

from google.protobuf.descriptor import Descriptor, FieldDescriptor
from google.protobuf.message import Message
from ics.v1 import (
    camera_frame_meta_pb2,
    footprint_pb2,
    fragment_pb2,
    kill_assessment_pb2,
    mount_sample_pb2,
    pli_pb2,
    pli_query_pb2,
    run_record_pb2,
    time_quality_pb2,
    track_pb2,
    trigger_event_pb2,
)

# The deepest nesting in ics.v1 is RunRecord > Footprint > Region > Polygon >
# GeodeticPoint: depth 4.
MAX_DEPTH: int = 6
# More messages than any golden instance holds; a bound for the fill loop.
MAX_MESSAGES: int = 1000
# Entries in every repeated field.
REPEATED_COUNT: int = 2
# The exit status for a command-line usage error.
EXIT_USAGE: int = 2

type ScalarValue = int | float | bool | str | bytes


class GoldenError(Exception):
    """A message cannot be filled."""


class UnsupportedTypeError(GoldenError):
    """A scalar field of a type the golden values do not cover."""

    def __init__(self, field: str, cpp_type: int) -> None:
        super().__init__(f"{field}: unsupported C++ type {cpp_type}")


class MapFieldError(GoldenError):
    """A map field: its entry order is not deterministic across languages."""

    def __init__(self, field: str) -> None:
        super().__init__(f"{field}: map fields are not allowed in ICS contracts")


class NestingError(GoldenError):
    """A message nested deeper than MAX_DEPTH."""

    def __init__(self, message: str) -> None:
        super().__init__(f"{message} nests deeper than {MAX_DEPTH} levels")


class SizeError(GoldenError):
    """More sub-messages than MAX_MESSAGES."""

    def __init__(self) -> None:
        super().__init__(f"more than {MAX_MESSAGES} messages to fill")


def message_descriptor(message: Message) -> Descriptor:
    """The descriptor of `message`, typed for the descriptor module's API."""
    descriptor = message.DESCRIPTOR
    if not isinstance(descriptor, Descriptor):
        raise TypeError(type(descriptor).__name__)
    return descriptor


def golden_types() -> tuple[tuple[str, type[Message]], ...]:
    """Each top-level ICS message and the name of its golden file."""
    return (
        ("camera_frame_meta", camera_frame_meta_pb2.CameraFrameMeta),
        ("footprint", footprint_pb2.Footprint),
        ("fragment", fragment_pb2.Fragment),
        ("kill_assessment", kill_assessment_pb2.KillAssessment),
        ("mount_sample", mount_sample_pb2.MountSample),
        ("pli_event", pli_pb2.PliEvent),
        ("pli_record", pli_pb2.PliRecord),
        ("query_pli_request", pli_query_pb2.QueryPliRequest),
        ("query_pli_response", pli_query_pb2.QueryPliResponse),
        ("run_record", run_record_pb2.RunRecord),
        ("time_quality", time_quality_pb2.TimeQuality),
        ("track", track_pb2.Track),
        ("trigger_event", trigger_event_pb2.TriggerEvent),
    )


def _sign(seed: int) -> int:
    """-1 for odd seeds, so golden values have both signs."""
    return -1 if seed % 2 else 1


def _numeric_makers() -> dict[int, Callable[[int], ScalarValue]]:
    """A value for each numeric or string C++ type, from a field's seed."""
    return {
        FieldDescriptor.CPPTYPE_INT32: lambda seed: _sign(seed) * seed * 1_000,
        FieldDescriptor.CPPTYPE_INT64: lambda seed: _sign(seed) * seed * 1_000_000_000_007,
        FieldDescriptor.CPPTYPE_UINT32: lambda seed: seed * 1_000 + 7,
        FieldDescriptor.CPPTYPE_UINT64: lambda seed: seed * 1_000_000_000_007,
        FieldDescriptor.CPPTYPE_DOUBLE: lambda seed: _sign(seed) * (seed + 0.25),
        FieldDescriptor.CPPTYPE_FLOAT: lambda seed: seed + 0.5,
        FieldDescriptor.CPPTYPE_BOOL: lambda _seed: True,
    }


def scalar_value(field: FieldDescriptor, index: int, salt: int = 0) -> ScalarValue:
    """The golden value of a scalar field, or of entry `index` of a repeated one.

    `salt` tells apart the entries of a repeated message field.
    """
    seed = salt * 1_000 + field.number * 10 + index + 1
    if field.enum_type is not None:
        numbers = sorted(value.number for value in field.enum_type.values)
        return numbers[-1 - index % len(numbers)]
    if field.type == FieldDescriptor.TYPE_BYTES:
        return f"{field.name}-{seed}".encode()
    if field.cpp_type == FieldDescriptor.CPPTYPE_STRING:
        # Non-ASCII, so every language must agree on UTF-8.
        return f"{field.name}-{seed}-µ"
    maker = _numeric_makers().get(field.cpp_type)
    if maker is None:
        raise UnsupportedTypeError(field.full_name, field.cpp_type)
    return maker(seed)


def _is_set_in_golden(field: FieldDescriptor) -> bool:
    """Only the first member of a oneof is set; setting another would clear it."""
    oneof = field.containing_oneof
    return oneof is None or oneof.fields[0].number == field.number


def _fill_field(message: Message, field: FieldDescriptor, salt: int) -> list[tuple[Message, int]]:
    """Set one field; return the sub-messages that still need filling, with their salts."""
    value = getattr(message, field.name)
    message_type = field.message_type
    is_message = message_type is not None
    if message_type is not None and message_type.GetOptions().map_entry:
        raise MapFieldError(field.full_name)
    if field.is_repeated and is_message:
        return [(value.add(), salt * REPEATED_COUNT + index + 1) for index in range(REPEATED_COUNT)]
    if field.is_repeated:
        value.extend(scalar_value(field, index, salt) for index in range(REPEATED_COUNT))
        return []
    if is_message:
        value.SetInParent()
        return [(value, salt)]
    setattr(message, field.name, scalar_value(field, 0, salt))
    return []


def fill(message: Message) -> None:
    """Set every field of `message` and its sub-messages, without recursion."""
    pending: list[tuple[Message, int, int]] = [(message, 0, 0)]
    for _ in range(MAX_MESSAGES):
        if not pending:
            return
        current, depth, salt = pending.pop()
        children: list[tuple[Message, int]] = []
        descriptor = message_descriptor(current)
        for field in descriptor.fields:
            if _is_set_in_golden(field):
                children.extend(_fill_field(current, field, salt))
        if children and depth + 1 > MAX_DEPTH:
            raise NestingError(descriptor.full_name)
        pending.extend((child, depth + 1, child_salt) for child, child_salt in children)
    raise SizeError


def golden_bytes(message_type: type[Message]) -> bytes:
    """The golden instance of `message_type`, serialized deterministically."""
    message = message_type()
    fill(message)
    return message.SerializeToString(deterministic=True)


def write_all(directory: Path) -> list[Path]:
    """Write every golden file into `directory`; return their paths."""
    directory.mkdir(parents=True, exist_ok=True)
    written: list[Path] = []
    for name, message_type in golden_types():
        path = directory / f"{name}.binpb"
        path.write_bytes(golden_bytes(message_type))
        written.append(path)
    return written


def main(argv: Sequence[str]) -> int:
    """Command line: write the golden files into the folder given."""
    if len(argv) != 1:
        sys.stderr.write("usage: python -m ics_golden.proto OUTPUT_DIR\n")
        return EXIT_USAGE
    for path in write_all(Path(argv[0])):
        sys.stdout.write(f"{path}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
