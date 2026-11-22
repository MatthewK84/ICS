"""The camera model's parameter file (ICS-042).

A versioned JSON file, checked against schemas/camera-model.schema.json
before it is written and when it is read, one per camera. ICS-058's C++
camera model loads it at startup and records its SHA-256 in every run record
(docs/build-plan.md, "Parameter hand-off").
"""

import json
from dataclasses import dataclass
from datetime import UTC, datetime
from pathlib import Path

import numpy as np
from jsonschema import Draft202012Validator
from jsonschema.exceptions import ValidationError

from ics_models.camera.boresight import Boresight, Camera
from ics_models.camera.fit import CameraFit, Setup, intrinsics_of, intrinsics_vector
from ics_models.camera.model import CameraError

FORMAT: str = "ics-camera-model"
FORMAT_VERSION: int = 1
SCHEMA_PATH: Path = Path(__file__).resolve().parents[3] / "schemas" / "camera-model.schema.json"
INTRINSIC_KEYS: tuple[str, ...] = ("fx_px", "fy_px", "cx_px", "cy_px", "k1", "k2", "k3", "p1", "p2")
BORESIGHT_KEYS: tuple[str, ...] = ("yaw_rad", "pitch_rad", "roll_rad")

type Json = dict[str, Json] | list[Json] | str | int | float | bool | None


@dataclass(frozen=True)
class HeldOut:
    """How many target points and sightings were held out of the fit, and their RMS pixel distances (C3)."""

    points: int
    points_rms_px: float
    sightings: int
    sightings_rms_px: float


@dataclass(frozen=True)
class Provenance:
    """What the model is, which camera, when, from which files, and how it did on what was held out."""

    model_id: str
    station_id: str
    camera: str
    created: datetime
    targets_sha256: str
    sightings_sha256: str
    views: int
    held_out: HeldOut


def validate(document: Json) -> None:
    """CameraError, with the schema's message, for a document the schema refuses."""
    schema = json.loads(SCHEMA_PATH.read_text())
    try:
        Draft202012Validator(schema).validate(document)
    except ValidationError as error:
        raise CameraError("camera-model", error.message) from None


def estimates(keys: tuple[str, ...], values: list[float], sigmas: tuple[float, ...]) -> dict[str, Json]:
    """Each value with its standard deviation, by key."""
    return {key: {"value": value, "sigma": sigma} for key, value, sigma in zip(keys, values, sigmas, strict=True)}


def document(fit: CameraFit, setup: Setup, provenance: Provenance) -> dict[str, Json]:
    """The parameter file's content."""
    camera = fit.camera
    angles = [camera.boresight.yaw, camera.boresight.pitch, camera.boresight.roll]
    bore: dict[str, Json] = estimates(BORESIGHT_KEYS, angles, fit.sigmas[len(INTRINSIC_KEYS) :])
    bore["mirrored"] = camera.boresight.mirrored
    held = provenance.held_out
    intrinsics = estimates(INTRINSIC_KEYS, intrinsics_vector(camera.intrinsics), fit.sigmas[: len(INTRINSIC_KEYS)])
    return {
        "format": FORMAT,
        "format_version": FORMAT_VERSION,
        "model_id": provenance.model_id,
        "station_id": provenance.station_id,
        "camera": provenance.camera,
        "created_utc": provenance.created.astimezone(UTC).isoformat().replace("+00:00", "Z"),
        "image": {"width_px": camera.intrinsics.width, "height_px": camera.intrinsics.height},
        "intrinsics": intrinsics,
        "boresight": bore,
        "fit": {
            "distortion_terms": list(setup.terms),
            "views": provenance.views,
            "points_used": fit.points,
            "rms_px": fit.target_rms_px,
            "points_held_out": held.points,
            "held_out_rms_px": held.points_rms_px,
            "sightings_used": fit.sightings,
            "sighting_rms_px": fit.sighting_rms_px,
            "sightings_held_out": held.sightings,
            "held_out_sighting_rms_px": held.sightings_rms_px,
        },
        "targets_sha256": provenance.targets_sha256,
        "sightings_sha256": provenance.sightings_sha256,
    }


def write_parameters(path: Path, fit: CameraFit, setup: Setup, provenance: Provenance) -> None:
    """Check the parameter file against its schema, then write it."""
    content = document(fit, setup, provenance)
    validate(content)
    path.write_text(json.dumps(content, indent=2) + "\n")


def values_of(content: Json, section: str, keys: tuple[str, ...]) -> list[float]:
    """The values in a section of content the schema has passed."""
    block = content.get(section) if isinstance(content, dict) else None
    values: list[float] = []
    for key in keys:
        entry = block.get(key) if isinstance(block, dict) else None
        value = entry.get("value") if isinstance(entry, dict) else None
        values.append(float(value) if isinstance(value, int | float) else 0.0)
    return values


def camera_from(content: Json) -> Camera:
    """The camera in a parameter file's content. CameraError for content the schema refuses."""
    validate(content)
    # The schema has checked every type; these only tell mypy so.
    image = content.get("image") if isinstance(content, dict) else None
    width = image.get("width_px") if isinstance(image, dict) else None
    height = image.get("height_px") if isinstance(image, dict) else None
    bore = content.get("boresight") if isinstance(content, dict) else None
    mirrored = bore.get("mirrored") if isinstance(bore, dict) else None
    if not (isinstance(width, int) and isinstance(height, int) and isinstance(mirrored, bool)):
        raise CameraError("camera-model", "lacks its image size or mirror flag")
    intrinsics = intrinsics_of(np.array(values_of(content, "intrinsics", INTRINSIC_KEYS)), width, height)
    yaw, pitch, roll = values_of(content, "boresight", BORESIGHT_KEYS)
    return Camera(intrinsics, Boresight(yaw, pitch, roll, mirrored))


def read_parameters(path: Path) -> Camera:
    """The camera in a parameter file. CameraError for a file that is not JSON or that the schema refuses."""
    try:
        content: Json = json.loads(path.read_text())
    except json.JSONDecodeError as error:
        raise CameraError(str(path), f"not JSON: {error.msg}") from None
    return camera_from(content)
