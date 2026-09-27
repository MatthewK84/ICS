"""The generated protobuf modules (ICS-011) load and round-trip with the runtime."""

from ics.toolchain_check.v1 import sample_pb2, sample_set_pb2

TIME_UTC_NS: int = 1_790_000_000_123_456_789


def sample_set() -> sample_set_pb2.SampleSet:
    ranged = sample_pb2.Sample(
        time_utc_ns=TIME_UTC_NS,
        kind=sample_pb2.Sample.KIND_RANGE,
        values=[1523.5, 1524.25],
        position=sample_pb2.Sample.Position(east_m=10.0, north_m=-5.0, up_m=2.5),
        reading_count=4,
    )
    angled = sample_pb2.Sample(kind=sample_pb2.Sample.KIND_ANGLE, note="sun in view")
    return sample_set_pb2.SampleSet(station_id="north-1", samples=[ranged, angled])


def test_round_trips_through_the_binary_format() -> None:
    original = sample_set()
    parsed = sample_set_pb2.SampleSet.FromString(original.SerializeToString())
    assert parsed == original
    assert parsed.samples[1].WhichOneof("detail") == "note"


def test_keeps_nanosecond_times_exact() -> None:
    parsed = sample_set_pb2.SampleSet.FromString(sample_set().SerializeToString())
    assert parsed.samples[0].time_utc_ns == TIME_UTC_NS


def test_reads_an_empty_message_as_defaults() -> None:
    parsed = sample_pb2.Sample.FromString(b"")
    assert parsed.kind == sample_pb2.Sample.KIND_UNSPECIFIED
    assert parsed.WhichOneof("detail") is None
