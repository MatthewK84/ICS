# schemas

The run-record JSON Schema ([ICS-013](https://github.com/MatthewK84/ICS/issues/13)), keyed to the C4 MOP and KPP IDs. Every run record the pipeline writes must validate against it; the C++ record module ([ICS-079](https://github.com/MatthewK84/ICS/issues/79)) builds on it.

**Language:** JSON Schema. **Lead role:** Architect.

| File | Content |
|---|---|
| [`run-record.schema.json`](run-record.schema.json) | The schema, JSON Schema Draft 2020-12, `$id` `urn:ics:schema:run-record:v1` |
| [`samples/`](samples) | 20 synthetic run records that validate against it |

## What a run record is

A run record is the protobuf JSON form of `ics.v1.RunRecord` ([`proto/ics/v1/run_record.proto`](../proto/ics/v1/run_record.proto)), printed with two options:

- **proto field names**, such as `miss_distance_m`, so every unit suffix stays readable;
- **every field without presence**, even when it is empty, so the schema can require them.

In C++ that is `JsonPrintOptions` with `preserve_proto_field_names` and `always_print_fields_with_no_presence`; in Python, `json_format.MessageToJson(record, preserving_proto_field_name=True, always_print_fields_with_no_presence=True)`.

Protobuf JSON writes 64-bit integers, such as every `_utc_ns` time and `size_bytes`, as decimal strings, and enums by name, such as `"KILL_CLASS_HARD_KILL"`. The schema requires both. It rejects any property the contract does not define.

Beyond the contract's types, the schema checks what a well-formed record needs:

- a run and event ID, at least one station and a range origin;
- SHA-256 digests as 64 lowercase hex characters, and a full git commit SHA;
- latitudes and longitudes in range, and non-negative distances;
- no unset enum values;
- a computed class other than `KILL_CLASS_NO_TEST`, which only an evaluator assigns;
- an override reason that is not empty;
- polygons with at least three vertices.

## C4 measurements

The C4 IDs are those of the JIATF 401 Common Criteria for CUAS Characterization (C4), which group MOPs, KPPs, KSAs and interceptor-specific metrics (INT) under five core capability areas. A run record carries one observation per run. The C4 value itself, such as Pk in percent or CEP, is computed across runs later.

Each measurement carries `criterion_id`, `value`, `sigma` and `unit`. The unit is fixed for each C4 ID and is SI, as a UCUM code; the table's units (km, %, sec, g) are for scoring.

| C4 ID | Kind | Measure | Per-run value | Unit |
|---|---|---|---|---|
| 3.1.2 | MOP | Probability of Kill / Defeat (Pk) | 1 if the kill class in force is a mission, hard or catastrophic kill, 0 if it is no kill; omitted for no-test and undetermined runs | `1` |
| 3.1.3 | MOP | Defeat Range | Distance from the interceptor's launch point to the intercept point, on a defeat | `m` |
| 3.1.4 | MOP | Defeat Engagement Time | Engage command (the trigger controller arming) to closest approach, on a defeat | `s` |
| INT-1 | INT | Interceptor Max Speed | Highest interceptor speed in the run | `m/s` |
| INT-2 | INT | Acceleration / G-Load | Peak interceptor acceleration | `m/s2` |
| INT-6 | INT | Terminal Guidance Accuracy (CEP) | Miss distance at closest approach (`kill_assessment.miss_distance_m`) | `m` |

Other criteria draw on the record's structured content rather than a measurement:

| C4 ID | Measure | Where the record holds it |
|---|---|---|
| INT-3 | Intercept Envelope | `kill_assessment.intercept_point_enu_m`, across runs |
| INT-13 | Engagement Geometry Performance | The target and interceptor tracks in `artifacts` |
| INT-14 | Debris Characterization | `kill_assessment.breakup_piece_count`, the fragments in `artifacts`, and `footprint` |
| 5.7 | Collateral Effects / Damage | `footprint` |
| 11.1 | Public and Blue Force Safety | `footprint` |

The remaining C4 criteria depend on the system under test's own data, such as its ground control station log, which ICS does not record.

A measurement may also carry a validation criterion, `C1` to `C19`, in the unit its definition gives.

## Samples

The 20 records in [`samples/`](samples) are examples, not test results. Every `event_id` is `EXAMPLE`, every `run_id` is `example-01` to `example-20`, and every value is made up.

They cover:

- every kill class, including evaluator overrides to and from `KILL_CLASS_NO_TEST`;
- every flag;
- one to three stations;
- runs with and without contact, a ballistic fit or a footprint.

[`python/ics_golden/run_records.py`](../python/ics_golden/run_records.py) writes them. After a schema or contract change, regenerate them from `python/`:

```sh
PYTHONPATH=gen uv run --locked python -m ics_golden.run_records ../schemas/samples
```

## Checks

[`python/tests/test_run_record_schema.py`](../python/tests/test_run_record_schema.py) runs in the Python toolchain workflow. It checks that:

- the schema is valid Draft 2020-12;
- the committed samples are current, and every one validates;
- the schema's properties are exactly the contract's fields, message by message;
- each seeded defect is rejected:
  - an unknown C4 ID;
  - a wrong unit;
  - a Pk other than 0 or 1;
  - an extra property;
  - a missing run ID;
  - a 64-bit integer written as a number;
  - a malformed digest;
  - a computed `KILL_CLASS_NO_TEST`;
  - an empty override reason;
  - an unset flag.

To validate a record by hand, from `python/`:

```sh
uv run --locked python -c "import json, sys; from jsonschema import Draft202012Validator as V; \
  V(json.load(open('../schemas/run-record.schema.json'))).validate(json.load(open(sys.argv[1])))" RECORD.json
```
