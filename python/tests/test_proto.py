"""The ICS protobuf contracts (ICS-012) and their golden files.

ics_golden.proto writes one instance of each top-level message, with every
field set, to golden/proto. These tests prove the committed files are current
and complete; the C++ and TypeScript tests prove they read them identically.
"""

from pathlib import Path

import pytest
from google.protobuf import descriptor_pb2, struct_pb2, wrappers_pb2
from google.protobuf.message import Message
from ics.v1 import kill_assessment_pb2, pli_pb2, run_record_pb2, trigger_event_pb2

from ics_golden import proto as golden

GOLDEN_DIR: Path = Path(__file__).resolve().parents[2] / "golden" / "proto"
TIME_UTC_NS: int = 1_790_000_000_123_456_789


def unset_fields(message: Message) -> list[str]:
    """The fields a golden instance leaves unset, anywhere inside it."""
    missing: list[str] = []
    pending: list[Message] = [message]
    for _ in range(golden.MAX_MESSAGES):
        if not pending:
            return missing
        current = pending.pop()
        present = current.ListFields()
        numbers = {field.number for field, _ in present}
        for field in current.DESCRIPTOR.fields:
            oneof = field.containing_oneof
            is_first = oneof is None or oneof.fields[0].number == field.number
            if is_first and field.number not in numbers:
                missing.append(field.full_name)
        for field, value in present:
            if field.message_type is not None:
                pending.extend(value if field.is_repeated else [value])
    pytest.fail("more messages than a golden instance holds")


@pytest.mark.parametrize(("name", "message_type"), golden.golden_types())
def test_committed_golden_files_are_current(name: str, message_type: type[Message]) -> None:
    committed = (GOLDEN_DIR / f"{name}.binpb").read_bytes()
    assert committed == golden.golden_bytes(message_type), f"run: python -m ics_golden.proto ../golden/proto ({name})"


def test_golden_folder_holds_exactly_the_golden_files() -> None:
    names = {path.stem for path in GOLDEN_DIR.iterdir()}
    assert names == {name for name, _ in golden.golden_types()}


@pytest.mark.parametrize(("name", "message_type"), golden.golden_types())
def test_golden_instances_set_every_field(name: str, message_type: type[Message]) -> None:
    message = message_type.FromString(golden.golden_bytes(message_type))
    assert unset_fields(message) == [], name


def test_repeated_messages_get_distinct_values() -> None:
    record = run_record_pb2.RunRecord.FromString(golden.golden_bytes(run_record_pb2.RunRecord))
    assert record.stations[0].station_id != record.stations[1].station_id


def test_keeps_nanosecond_times_exact() -> None:
    event = trigger_event_pb2.TriggerEvent(time_utc_ns=TIME_UTC_NS, channels=["a", "b"])
    parsed = trigger_event_pb2.TriggerEvent.FromString(event.SerializeToString())
    assert parsed.time_utc_ns == TIME_UTC_NS
    assert list(parsed.channels) == ["a", "b"]


def test_optional_fields_keep_presence_even_at_zero() -> None:
    record = pli_pb2.PliRecord(horizontal_sigma_m=0.0)
    parsed = pli_pb2.PliRecord.FromString(record.SerializeToString())
    assert parsed.HasField("horizontal_sigma_m")
    assert not parsed.HasField("vertical_sigma_m")
    assert not parsed.HasField("received_utc_ns")


def test_reads_an_empty_message_as_defaults() -> None:
    record = run_record_pb2.RunRecord.FromString(b"")
    assert not record.HasField("kill_assessment")
    assert list(record.flags) == []
    assessment = kill_assessment_pb2.KillAssessment()
    assert assessment.computed_class == kill_assessment_pb2.KillAssessment.KILL_CLASS_UNSPECIFIED


@pytest.mark.parametrize(
    "wrapper",
    [
        wrappers_pb2.BoolValue,
        wrappers_pb2.BytesValue,
        wrappers_pb2.DoubleValue,
        wrappers_pb2.FloatValue,
        wrappers_pb2.Int32Value,
        wrappers_pb2.Int64Value,
        wrappers_pb2.StringValue,
        wrappers_pb2.UInt32Value,
        wrappers_pb2.UInt64Value,
    ],
)
def test_fills_every_scalar_type(wrapper: type[Message]) -> None:
    message = wrapper()
    golden.fill(message)
    assert unset_fields(message) == []
    assert wrapper.FromString(message.SerializeToString()) == message


def test_rejects_map_fields() -> None:
    with pytest.raises(golden.GoldenError, match="map fields"):
        golden.fill(struct_pb2.Struct())


def test_rejects_messages_nested_too_deep() -> None:
    with pytest.raises(golden.GoldenError, match="nests deeper"):
        golden.fill(descriptor_pb2.DescriptorProto())


def test_stops_after_max_messages(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(golden, "MAX_MESSAGES", 2)
    with pytest.raises(golden.GoldenError, match="more than 2 messages"):
        golden.fill(run_record_pb2.RunRecord())


def test_has_no_scalar_value_for_a_message_field() -> None:
    field = golden.message_descriptor(run_record_pb2.RunRecord()).fields_by_name["range_origin"]
    with pytest.raises(golden.GoldenError, match="unsupported C\\+\\+ type"):
        golden.scalar_value(field, 0)


def test_main_writes_every_golden_file(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert golden.main([str(tmp_path)]) == 0
    for name, message_type in golden.golden_types():
        assert (tmp_path / f"{name}.binpb").read_bytes() == golden.golden_bytes(message_type)
    assert len(capsys.readouterr().out.splitlines()) == len(golden.golden_types())


def test_main_rejects_a_missing_folder_argument(capsys: pytest.CaptureFixture[str]) -> None:
    assert golden.main([]) == golden.EXIT_USAGE
    assert "usage" in capsys.readouterr().err
