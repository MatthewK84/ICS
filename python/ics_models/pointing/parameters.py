"""The pointing model's parameter file (ICS-035).

A versioned JSON file, checked against schemas/pointing-model.schema.json
before it is written and when it is read. ICS-036's C++ encoder module loads
it at startup and records its SHA-256 in every run record (docs/build-plan.md,
"Parameter hand-off").
"""

import hashlib
import json
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path

from jsonschema import Draft202012Validator
from jsonschema.exceptions import ValidationError

from ics_models.pointing.astrometry import Site
from ics_models.pointing.fit import FitResult
from ics_models.pointing.model import PointingError, PointingModel, Term

FORMAT: str = "ics-pointing-model"
FORMAT_VERSION: int = 1
SCHEMA_PATH: Path = Path(__file__).resolve().parents[3] / "schemas" / "pointing-model.schema.json"
MICRORADIANS: float = 1e6

type Json = dict[str, Json] | list[Json] | str | int | float | bool | None


@dataclass(frozen=True)
class Provenance:
    """What the model is, where, when, from which observations, and how it did on the stars held out."""

    model_id: str
    station_id: str
    created: datetime
    site: Site
    observations_sha256: str
    held_out: int
    held_out_rms_rad: float


def sha256_of(path: Path) -> str:
    """A file's SHA-256, in hexadecimal."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(document: Json) -> None:
    """PointingError, with the schema's message, for a document the schema refuses."""
    schema = json.loads(SCHEMA_PATH.read_text())
    try:
        Draft202012Validator(schema).validate(document)
    except ValidationError as error:
        raise PointingError("pointing-model", error.message) from None


def document(result: FitResult, provenance: Provenance) -> dict[str, Json]:
    """The parameter file's content."""
    terms: list[Json] = [
        {"name": str(term), "value_rad": value, "sigma_rad": sigma}
        for term, value, sigma in zip(result.model.terms, result.model.coefficients, result.sigmas, strict=True)
    ]
    created = provenance.created.astimezone(UTC).isoformat().replace("+00:00", "Z")
    return {
        "format": FORMAT,
        "format_version": FORMAT_VERSION,
        "model_id": provenance.model_id,
        "station_id": provenance.station_id,
        "created_utc": created,
        "site": {
            "latitude_deg": provenance.site.latitude_deg,
            "longitude_deg": provenance.site.longitude_deg,
            "height_m": provenance.site.height_m,
        },
        "terms": terms,
        "fit": {
            "stars_used": result.used,
            "stars_rejected": len(result.rejected),
            "rms_urad": result.rms_rad * MICRORADIANS,
            "stars_held_out": provenance.held_out,
            "held_out_rms_urad": provenance.held_out_rms_rad * MICRORADIANS,
        },
        "observations_sha256": provenance.observations_sha256,
    }


def write_parameters(path: Path, result: FitResult, provenance: Provenance) -> None:
    """Check the parameter file against its schema, then write it."""
    content = document(result, provenance)
    validate(content)
    path.write_text(json.dumps(content, indent=2) + "\n")


def model_from(content: Json) -> PointingModel:
    """The model in a parameter file's content. PointingError for content the schema refuses."""
    validate(content)
    # The schema has checked every type; these only tell mypy so.
    entries = content.get("terms") if isinstance(content, dict) else None
    terms: list[Term] = []
    values: list[float] = []
    for entry in entries if isinstance(entries, list) else []:
        name = entry.get("name") if isinstance(entry, dict) else None
        value = entry.get("value_rad") if isinstance(entry, dict) else None
        if isinstance(name, str) and isinstance(value, int | float):
            terms.append(Term(name))
            values.append(float(value))
    return PointingModel(terms=tuple(terms), coefficients=tuple(values))


def read_parameters(path: Path) -> PointingModel:
    """The model in a parameter file. PointingError for a file that is not JSON or that the schema refuses."""
    try:
        content: Json = json.loads(path.read_text())
    except json.JSONDecodeError as error:
        raise PointingError(str(path), f"not JSON: {error.msg}") from None
    return model_from(content)
