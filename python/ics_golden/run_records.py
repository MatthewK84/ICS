"""Synthetic sample run records for the run-record JSON Schema (ICS-013).

Twenty ics.v1.RunRecord instances, written as the JSON that
schemas/run-record.schema.json validates: protobuf JSON with proto field names
and every field printed. They are examples, not test results: every event_id is
"EXAMPLE", every run_id "example-NN", and every value is made up. They vary the
kill class, evaluator overrides, flags, station count and optional fields, so
the schema is exercised on each.

Regenerate from python/ after changing the schema's contract:

    PYTHONPATH=gen uv run --locked python -m ics_golden.run_records ../schemas/samples
"""

import hashlib
import math
import sys
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from google.protobuf import json_format
from ics.v1 import common_pb2, footprint_pb2, kill_assessment_pb2, run_record_pb2

KillAssessment = kill_assessment_pb2.KillAssessment
RunRecord = run_record_pb2.RunRecord
type KillClass = kill_assessment_pb2.KillAssessment.KillClass
type Flag = run_record_pb2.RunRecord.Flag

EVENT_ID: str = "EXAMPLE"
# 2026-10-05T15:00:00Z, and one run every 20 minutes.
BASE_UTC_NS: int = 1_791_212_400_000_000_000
RUN_SPACING_NS: int = 20 * 60 * 1_000_000_000
RUN_LENGTH_NS: int = 180 * 1_000_000_000
NS_PER_S: int = 1_000_000_000
# The made-up range: its origin, the interceptor's launch point in the range
# ENU frame, and metres per degree of latitude.
ORIGIN_LATITUDE_DEG: float = 40.0
ORIGIN_LONGITUDE_DEG: float = -100.0
ORIGIN_HEIGHT_M: float = 700.0
LAUNCH_ENU_M: tuple[float, float, float] = (-400.0, -300.0, 0.0)
METRES_PER_DEGREE: float = 111_320.0
EXIT_USAGE: int = 2


@dataclass(frozen=True)
class Scenario:
    """What one sample run shows."""

    computed: KillClass
    override: KillClass | None
    stations: int
    contact: bool
    pieces: int
    mission_denied: bool
    fitted: bool
    flags: tuple[Flag, ...]

    def in_force(self) -> KillClass:
        """The class in force: the evaluator's override if any, else the computed class."""
        return self.override if self.override is not None else self.computed


def scenarios() -> tuple[Scenario, ...]:
    """The twenty sample runs, in order."""
    cat, hard, mission = (
        KillAssessment.KILL_CLASS_CATASTROPHIC_KILL,
        KillAssessment.KILL_CLASS_HARD_KILL,
        KillAssessment.KILL_CLASS_MISSION_KILL,
    )
    miss, undetermined, no_test = (
        KillAssessment.KILL_CLASS_NO_KILL,
        KillAssessment.KILL_CLASS_UNDETERMINED,
        KillAssessment.KILL_CLASS_NO_TEST,
    )
    truth, coverage, time = (
        RunRecord.FLAG_REDUCED_TRUTH,
        RunRecord.FLAG_INCOMPLETE_COVERAGE,
        RunRecord.FLAG_TIME_DEGRADED,
    )
    return (
        Scenario(cat, None, 3, contact=True, pieces=14, mission_denied=True, fitted=True, flags=()),
        Scenario(hard, None, 3, contact=True, pieces=0, mission_denied=True, fitted=True, flags=()),
        Scenario(miss, None, 3, contact=False, pieces=0, mission_denied=False, fitted=True, flags=()),
        Scenario(mission, None, 2, contact=True, pieces=0, mission_denied=True, fitted=False, flags=()),
        Scenario(cat, None, 3, contact=True, pieces=9, mission_denied=True, fitted=True, flags=(truth,)),
        Scenario(undetermined, hard, 3, contact=True, pieces=0, mission_denied=True, fitted=True, flags=(coverage,)),
        Scenario(miss, None, 1, contact=False, pieces=0, mission_denied=False, fitted=False, flags=(time,)),
        Scenario(undetermined, no_test, 2, contact=False, pieces=0, mission_denied=False, fitted=False, flags=()),
        Scenario(hard, None, 3, contact=True, pieces=0, mission_denied=True, fitted=True, flags=()),
        Scenario(cat, None, 3, contact=True, pieces=22, mission_denied=True, fitted=True, flags=()),
        Scenario(miss, None, 3, contact=False, pieces=0, mission_denied=False, fitted=True, flags=(truth, time)),
        Scenario(mission, hard, 3, contact=True, pieces=0, mission_denied=True, fitted=True, flags=()),
        Scenario(cat, None, 2, contact=True, pieces=6, mission_denied=True, fitted=True, flags=(coverage,)),
        Scenario(
            undetermined, None, 1, contact=False, pieces=0, mission_denied=False, fitted=False, flags=(truth, coverage)
        ),
        Scenario(hard, cat, 3, contact=True, pieces=11, mission_denied=True, fitted=True, flags=()),
        Scenario(miss, None, 3, contact=False, pieces=0, mission_denied=False, fitted=True, flags=()),
        Scenario(cat, None, 3, contact=True, pieces=17, mission_denied=True, fitted=True, flags=(time,)),
        Scenario(mission, None, 3, contact=False, pieces=0, mission_denied=True, fitted=False, flags=()),
        Scenario(miss, no_test, 3, contact=False, pieces=0, mission_denied=False, fitted=False, flags=(truth,)),
        Scenario(hard, None, 2, contact=True, pieces=0, mission_denied=True, fitted=True, flags=()),
    )


def is_kill(kill_class: KillClass) -> bool:
    """Whether a class counts as a defeat for Pk."""
    return kill_class in (
        KillAssessment.KILL_CLASS_MISSION_KILL,
        KillAssessment.KILL_CLASS_HARD_KILL,
        KillAssessment.KILL_CLASS_CATASTROPHIC_KILL,
    )


def geodetic(east_m: float, north_m: float, up_m: float) -> common_pb2.GeodeticPoint:
    """A point near the made-up range origin, from its ENU offset (flat-earth; fine for examples)."""
    metres_per_degree_east = METRES_PER_DEGREE * math.cos(math.radians(ORIGIN_LATITUDE_DEG))
    return common_pb2.GeodeticPoint(
        latitude_deg=round(ORIGIN_LATITUDE_DEG + north_m / METRES_PER_DEGREE, 7),
        longitude_deg=round(ORIGIN_LONGITUDE_DEG + east_m / metres_per_degree_east, 7),
        height_ellipsoid_m=round(ORIGIN_HEIGHT_M + up_m, 3),
    )


def digest(run_id: str, path: str, media_type: str) -> common_pb2.FileDigest:
    """A file entry whose digest and size are derived from its name."""
    name = f"{run_id}/{path}"
    return common_pb2.FileDigest(
        path=path,
        sha256_hex=hashlib.sha256(name.encode()).hexdigest(),
        size_bytes=1_000 + 37 * len(name) * sum(name.encode()),
        media_type=media_type,
    )


def stations(count: int) -> list[RunRecord.Station]:
    """The first `count` of three stations around the range origin."""
    layout = (("north", 0.0, 1_500.0), ("east", 1_500.0, 0.0), ("west", -1_500.0, 0.0))
    return [
        RunRecord.Station(
            station_id=name,
            position=geodetic(east, north, 2.0),
            camera_ids=[f"{name}-narrow", f"{name}-wide", f"{name}-mwir"],
        )
        for name, east, north in layout[:count]
    ]


def thresholds(run_id: str) -> tuple[list[KillAssessment.Threshold], common_pb2.FileDigest]:
    """Made-up kill thresholds and the parameter file they come from."""
    values = (("N", 5.0, "1"), ("m", 0.01, "kg"), ("T", 10.0, "s"), ("residual_gate", 3.0, "1"))
    listed = [KillAssessment.Threshold(name=name, value=value, unit=unit) for name, value, unit in values]
    return listed, digest(run_id, "parameters/kill-thresholds.toml", "application/toml")


def override(scenario: Scenario, time_utc_ns: int) -> list[KillAssessment.Override]:
    """The evaluator's override, if the scenario has one."""
    reasons = {
        KillAssessment.KILL_CLASS_HARD_KILL: "Imagery shows an uncontrolled descent the classifier could not fit.",
        KillAssessment.KILL_CLASS_CATASTROPHIC_KILL: "The ground survey recovered more pieces than imagery resolved.",
        KillAssessment.KILL_CLASS_NO_TEST: "The target left the test area before the engagement.",
    }
    if scenario.override is None:
        return []
    return [
        KillAssessment.Override(
            kill_class=scenario.override,
            user="evaluator.example",
            time_utc_ns=time_utc_ns,
            reason=reasons[scenario.override],
        )
    ]


def engagement_times(index: int) -> tuple[int, int, int]:
    """Run start, engage command and closest approach, UTC nanoseconds."""
    start = BASE_UTC_NS + (index - 1) * RUN_SPACING_NS
    engage = start + 20 * NS_PER_S + index * NS_PER_S // 2
    closest = engage + 9 * NS_PER_S + index * 7 * NS_PER_S // 10
    return start, engage, closest


def intercept_point(index: int) -> common_pb2.EnuVector:
    """Where closest approach happened, in the range ENU frame, in metres."""
    return common_pb2.EnuVector(east=200.0 + 37 * index, north=900.0 + 23 * index, up=120.0 + 5 * index)


def kill_assessment(index: int, scenario: Scenario) -> KillAssessment:
    """The kill assessment of one sample run."""
    run_id = f"example-{index:02d}"
    _, _, closest = engagement_times(index)
    listed, thresholds_file = thresholds(run_id)
    assessment = KillAssessment(
        run_id=run_id,
        target_track_id="target",
        interceptor_track_id="interceptor",
        computed_class=scenario.computed,
        closest_approach_utc_ns=closest,
        miss_distance_m=round(0.05 + 0.01 * index if scenario.contact else 1.5 + 0.25 * index, 3),
        miss_distance_sigma_m=0.02,
        intercept_point_enu_m=intercept_point(index),
        breakup_piece_count=scenario.pieces,
        mission_denied=scenario.mission_denied,
        thresholds=listed,
        thresholds_file=thresholds_file,
        overrides=override(scenario, closest + 3_600 * NS_PER_S),
    )
    if scenario.contact:
        assessment.contact_utc_ns = closest
    if scenario.fitted:
        assessment.ballistic_fit_residual = round(0.6 + 0.05 * index, 3)
    return assessment


def footprint(index: int, scenario: Scenario) -> footprint_pb2.Footprint | None:
    """The debris footprint, for runs where something fell."""
    if scenario.in_force() not in (KillAssessment.KILL_CLASS_HARD_KILL, KillAssessment.KILL_CLASS_CATASTROPHIC_KILL):
        return None
    run_id = f"example-{index:02d}"
    point = intercept_point(index)
    pieces = [f"fragment-{piece:02d}" for piece in range(1, scenario.pieces + 1)] or ["target-body"]
    regions = [
        footprint_pb2.Footprint.Region(probability=probability, polygons=[square(point.east + 30.0, point.north, half)])
        for probability, half in ((0.5, 25.0), (0.9, 60.0), (0.99, 110.0))
    ]
    inputs = [
        digest(run_id, "inputs/wind-profile.json", "application/json"),
        digest(run_id, "parameters/drag-table.toml", "application/toml"),
    ]
    return footprint_pb2.Footprint(
        run_id=run_id, sample_count=10_000, fragment_ids=pieces, regions=regions, inputs=inputs
    )


def square(east_m: float, north_m: float, half_m: float) -> footprint_pb2.Footprint.Polygon:
    """A square on the ground, vertices counterclockwise seen from above."""
    corners = ((-half_m, -half_m), (half_m, -half_m), (half_m, half_m), (-half_m, half_m))
    return footprint_pb2.Footprint.Polygon(
        vertices=[geodetic(east_m + east, north_m + north, 0.0) for east, north in corners],
    )


def measurement(criterion_id: str, value: float, sigma: float, unit: str) -> RunRecord.Measurement:
    """One measurement, rounded for readability."""
    return RunRecord.Measurement(criterion_id=criterion_id, value=round(value, 3), sigma=sigma, unit=unit)


def measurements(index: int, scenario: Scenario, assessment: KillAssessment) -> list[RunRecord.Measurement]:
    """The C4 measurements of one run; none for a run the evaluator ruled no test."""
    in_force = scenario.in_force()
    if in_force == KillAssessment.KILL_CLASS_NO_TEST:
        return []
    listed: list[RunRecord.Measurement] = []
    if in_force != KillAssessment.KILL_CLASS_UNDETERMINED:
        listed.append(measurement("3.1.2", 1.0 if is_kill(in_force) else 0.0, 0.0, "1"))
    if is_kill(in_force):
        point = assessment.intercept_point_enu_m
        defeat_range = math.dist(LAUNCH_ENU_M, (point.east, point.north, point.up))
        _, engage, closest = engagement_times(index)
        listed.append(measurement("3.1.3", defeat_range, 1.5, "m"))
        listed.append(measurement("3.1.4", (closest - engage) / NS_PER_S, 0.001, "s"))
    listed.append(measurement("INT-1", 60.0 + 1.5 * index, 0.5, "m/s"))
    listed.append(measurement("INT-2", 35.0 + 2.0 * index, 1.0, "m/s2"))
    listed.append(measurement("INT-6", assessment.miss_distance_m, assessment.miss_distance_sigma_m, "m"))
    return listed


def artifacts(run_id: str, scenario: Scenario) -> list[common_pb2.FileDigest]:
    """The run's other artifacts."""
    listed = [
        digest(run_id, "tracks.binpb", "application/x-protobuf"),
        digest(run_id, "frame-metadata.binpb", "application/x-protobuf"),
        digest(run_id, "pli.binpb", "application/x-protobuf"),
    ]
    if scenario.pieces:
        listed.append(digest(run_id, "fragments.binpb", "application/x-protobuf"))
    return listed


def run_record(index: int, scenario: Scenario) -> RunRecord:
    """Sample run `index`, counting from 1."""
    run_id = f"example-{index:02d}"
    start, _, _ = engagement_times(index)
    assessment = kill_assessment(index, scenario)
    record = RunRecord(
        run_id=run_id,
        event_id=EVENT_ID,
        start_utc_ns=start,
        end_utc_ns=start + RUN_LENGTH_NS,
        range_origin=geodetic(0.0, 0.0, 0.0),
        stations=stations(scenario.stations),
        software_revision=hashlib.sha256(b"ics-example-revision").hexdigest()[:40],
        parameter_files=[
            digest(run_id, "parameters/pointing-model.toml", "application/toml"),
            digest(run_id, "parameters/drag-table.toml", "application/toml"),
            assessment.thresholds_file,
        ],
        models=[digest(run_id, "models/segmentation.onnx", "application/onnx")],
        artifacts=artifacts(run_id, scenario),
        kill_assessment=assessment,
        measurements=measurements(index, scenario, assessment),
        flags=scenario.flags,
    )
    fallout = footprint(index, scenario)
    if fallout is not None:
        record.footprint.CopyFrom(fallout)
    return record


def record_json(record: RunRecord) -> str:
    """A run record as the schema expects it: proto field names, every field printed."""
    text = json_format.MessageToJson(
        record,
        preserving_proto_field_name=True,
        always_print_fields_with_no_presence=True,
        indent=2,
    )
    return text + "\n"


def sample_name(index: int) -> str:
    """The file name of sample `index`."""
    return f"run-record-{index:02d}.json"


def write_all(directory: Path) -> list[Path]:
    """Write every sample into `directory`; return their paths."""
    directory.mkdir(parents=True, exist_ok=True)
    written: list[Path] = []
    for index, scenario in enumerate(scenarios(), start=1):
        path = directory / sample_name(index)
        path.write_text(record_json(run_record(index, scenario)))
        written.append(path)
    return written


def main(argv: Sequence[str]) -> int:
    """Command line: write the samples into the folder given."""
    if len(argv) != 1:
        sys.stderr.write("usage: python -m ics_golden.run_records OUTPUT_DIR\n")
        return EXIT_USAGE
    for path in write_all(Path(argv[0])):
        sys.stdout.write(f"{path}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
