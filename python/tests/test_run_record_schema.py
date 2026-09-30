"""The run-record JSON Schema and its 20 sample records (ICS-013).

schemas/run-record.schema.json must be valid Draft 2020-12, describe exactly
the fields of ics.v1.RunRecord, accept every committed sample, and reject each
seeded defect.
"""

import copy
import json
from collections.abc import Callable
from pathlib import Path

import pytest
from google.protobuf.descriptor import Descriptor
from ics.v1 import common_pb2, footprint_pb2, kill_assessment_pb2, run_record_pb2
from jsonschema import Draft202012Validator

from ics_golden import run_records
from ics_golden.proto import message_descriptor

REPO: Path = Path(__file__).resolve().parents[2]
SCHEMA_PATH: Path = REPO / "schemas" / "run-record.schema.json"
SAMPLES_DIR: Path = REPO / "schemas" / "samples"
SAMPLE_COUNT: int = 20

type Json = dict[str, Json] | list[Json] | str | int | float | bool | None
type Mutation = Callable[[dict[str, Json]], None]


def load_json(path: Path) -> Json:
    """Parse a JSON file."""
    value: Json = json.loads(path.read_text())
    return value


def as_object(value: Json) -> dict[str, Json]:
    """Narrow a JSON value to an object."""
    assert isinstance(value, dict)
    return value


def as_array(value: Json) -> list[Json]:
    """Narrow a JSON value to an array."""
    assert isinstance(value, list)
    return value


def validator() -> Draft202012Validator:
    """A validator for the run-record schema."""
    return Draft202012Validator(as_object(load_json(SCHEMA_PATH)))


def errors(instance: Json) -> list[str]:
    """The schema's complaints about `instance`; empty if it validates."""
    return [error.message for error in validator().iter_errors(instance)]


def sample(index: int) -> dict[str, Json]:
    """A fresh copy of committed sample `index`."""
    return copy.deepcopy(as_object(load_json(SAMPLES_DIR / run_records.sample_name(index))))


def test_schema_is_valid_draft_2020_12() -> None:
    Draft202012Validator.check_schema(as_object(load_json(SCHEMA_PATH)))


@pytest.mark.parametrize("index", range(1, SAMPLE_COUNT + 1))
def test_committed_samples_are_current(index: int) -> None:
    scenario = run_records.scenarios()[index - 1]
    expected = run_records.record_json(run_records.run_record(index, scenario))
    committed = (SAMPLES_DIR / run_records.sample_name(index)).read_text()
    assert committed == expected, "run: PYTHONPATH=gen python -m ics_golden.run_records ../schemas/samples"


def test_samples_folder_holds_exactly_the_samples() -> None:
    names = {path.name for path in SAMPLES_DIR.iterdir()}
    assert names == {run_records.sample_name(index) for index in range(1, SAMPLE_COUNT + 1)}


@pytest.mark.parametrize("index", range(1, SAMPLE_COUNT + 1))
def test_every_sample_validates(index: int) -> None:
    assert errors(sample(index)) == []


def test_samples_cover_every_kill_class_and_flag() -> None:
    chosen = run_records.scenarios()
    classes = {scenario.in_force() for scenario in chosen} | {scenario.computed for scenario in chosen}
    flags = {flag for scenario in chosen for flag in scenario.flags}
    every_class = set(kill_assessment_pb2.KillAssessment.KillClass.values()) - {0}
    assert classes == every_class
    assert flags == set(run_record_pb2.RunRecord.Flag.values()) - {0}


def schema_messages() -> tuple[tuple[str, Descriptor], ...]:
    """Each object definition in the schema and the message it describes."""
    return (
        ("geodetic_point", message_descriptor(common_pb2.GeodeticPoint())),
        ("enu_vector", message_descriptor(common_pb2.EnuVector())),
        ("file_digest", message_descriptor(common_pb2.FileDigest())),
        ("station", message_descriptor(run_record_pb2.RunRecord.Station())),
        ("threshold", message_descriptor(kill_assessment_pb2.KillAssessment.Threshold())),
        ("override", message_descriptor(kill_assessment_pb2.KillAssessment.Override())),
        ("kill_assessment", message_descriptor(kill_assessment_pb2.KillAssessment())),
        ("polygon", message_descriptor(footprint_pb2.Footprint.Polygon())),
        ("region", message_descriptor(footprint_pb2.Footprint.Region())),
        ("footprint", message_descriptor(footprint_pb2.Footprint())),
    )


def property_names(definition: Json) -> set[str]:
    """The property names of one schema object definition."""
    return set(as_object(as_object(definition)["properties"]))


def test_schema_describes_exactly_the_proto_fields() -> None:
    schema = as_object(load_json(SCHEMA_PATH))
    definitions = as_object(schema["$defs"])
    assert property_names(schema) == {field.name for field in run_record_pb2.RunRecord.DESCRIPTOR.fields}
    for name, descriptor in schema_messages():
        assert property_names(definitions[name]) == {field.name for field in descriptor.fields}, name
    measurement_fields = {field.name for field in run_record_pb2.RunRecord.Measurement.DESCRIPTOR.fields}
    for branch in as_array(as_object(definitions["measurement"])["oneOf"]):
        reference = as_object(branch)["$ref"]
        assert isinstance(reference, str)
        assert property_names(definitions[reference.removeprefix("#/$defs/")]) == measurement_fields, reference


def find_measurement(record: dict[str, Json], criterion_id: str) -> dict[str, Json]:
    """The one measurement with `criterion_id` in `record`."""
    items = [as_object(item) for item in as_array(record["measurements"])]
    matches = [item for item in items if item["criterion_id"] == criterion_id]
    assert len(matches) == 1, criterion_id
    return matches[0]


def unknown_criterion(record: dict[str, Json]) -> None:
    find_measurement(record, "INT-6")["criterion_id"] = "9.9.9"


def wrong_unit(record: dict[str, Json]) -> None:
    find_measurement(record, "INT-6")["unit"] = "km"


def pk_not_zero_or_one(record: dict[str, Json]) -> None:
    find_measurement(record, "3.1.2")["value"] = 0.5


def extra_property(record: dict[str, Json]) -> None:
    record["operator_notes"] = "not in the contract"


def missing_run_id(record: dict[str, Json]) -> None:
    del record["run_id"]


def int64_as_number(record: dict[str, Json]) -> None:
    record["start_utc_ns"] = 1_791_212_400_000_000_000


def bad_digest(record: dict[str, Json]) -> None:
    as_object(as_array(record["artifacts"])[0])["sha256_hex"] = "ABC123"


def computed_no_test(record: dict[str, Json]) -> None:
    as_object(record["kill_assessment"])["computed_class"] = "KILL_CLASS_NO_TEST"


def empty_override_reason(record: dict[str, Json]) -> None:
    overrides = as_array(as_object(record["kill_assessment"])["overrides"])
    as_object(overrides[0])["reason"] = ""


def unspecified_flag(record: dict[str, Json]) -> None:
    record["flags"] = ["FLAG_UNSPECIFIED"]


@pytest.mark.parametrize(
    "mutation",
    [
        unknown_criterion,
        wrong_unit,
        pk_not_zero_or_one,
        extra_property,
        missing_run_id,
        int64_as_number,
        bad_digest,
        computed_no_test,
        empty_override_reason,
        unspecified_flag,
    ],
)
def test_rejects_each_seeded_defect(mutation: Mutation) -> None:
    record = sample(6)
    assert errors(record) == []
    mutation(record)
    assert errors(record) != [], mutation.__name__


@pytest.mark.parametrize(("criterion_id", "valid"), [("C7", True), ("C19", True), ("C0", False), ("C20", False)])
def test_accepts_validation_criteria_c1_to_c19(criterion_id: str, valid: bool) -> None:
    record = sample(1)
    as_array(record["measurements"]).append({"criterion_id": criterion_id, "value": 0.12, "sigma": 0.01, "unit": "m"})
    assert (errors(record) == []) is valid


def test_main_writes_every_sample(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert run_records.main([str(tmp_path)]) == 0
    assert {path.name for path in tmp_path.iterdir()} == {path.name for path in SAMPLES_DIR.iterdir()}
    assert len(capsys.readouterr().out.splitlines()) == SAMPLE_COUNT


def test_main_rejects_a_missing_folder_argument(capsys: pytest.CaptureFixture[str]) -> None:
    assert run_records.main([]) == run_records.EXIT_USAGE
    assert "usage" in capsys.readouterr().err
