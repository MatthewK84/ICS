"""The pointing model's files and command line (ICS-035): observations, plans, parameter files and golden vectors."""

import csv
import json
import math
from dataclasses import replace
from datetime import UTC, datetime, timedelta
from pathlib import Path

import numpy as np
import pytest

from ics_golden import pointing as golden
from ics_models.pointing.__main__ import EXIT_REFUSED, main
from ics_models.pointing.astrometry import Conditions, Site, Star, Weather, catalog_place
from ics_models.pointing.fit import FitResult
from ics_models.pointing.model import AltAz, PointingError, sky_offset, to_encoder, to_sky
from ics_models.pointing.observations import COLUMNS, Observation, read_observations, sighting, write_observations
from ics_models.pointing.parameters import Provenance, model_from, read_parameters, sha256_of, write_parameters
from ics_models.pointing.plan import plan_night, read_catalog, separation, write_plan
from ics_models.pointing.synth import Night, synthetic_night

REPO: Path = Path(__file__).resolve().parents[2]
GOLDEN: Path = REPO / "golden" / "pointing"
MICRORADIAN: float = 1e-6
SITE: Site = Site(latitude_deg=32.38, longitude_deg=-106.48, height_m=1200.0)
CONDITIONS: Conditions = Conditions(
    site=SITE,
    weather=Weather(pressure_hpa=880.0, temperature_c=12.0, relative_humidity=0.3, wavelength_um=0.6),
    dut1_s=0.12,
)
START: datetime = datetime(2026, 10, 16, 3, 0, tzinfo=UTC)
NIGHT: Night = Night(conditions=CONDITIONS, start=START, hours=6.0, noise_rad=3 * MICRORADIAN)
PLANNED: int = 30
CLI_PLANNED: int = 12
STARS: int = 60
HELD_OUT: int = 15
C17_URAD: float = 10.0
ROUND_TRIP_RAD: float = 1e-12


def observations(count: int = STARS) -> list[Observation]:
    """A synthetic night through the golden model."""
    return synthetic_night(np.random.default_rng(35), NIGHT, golden.golden_model(), count)


def write_rows(path: Path, rows: list[dict[str, str]], columns: tuple[str, ...] = COLUMNS) -> None:
    """Write rows under a header."""
    with path.open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=columns, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def rows_of(path: Path) -> list[dict[str, str]]:
    """A CSV file's rows."""
    with path.open(newline="") as file:
        return list(csv.DictReader(file))


def test_observations_round_trip(tmp_path: Path) -> None:
    written = observations(5)
    write_observations(tmp_path / "night.csv", written)
    assert read_observations(tmp_path / "night.csv") == written


@pytest.mark.parametrize(
    ("column", "text", "message"),
    [
        ("ra_deg", "east", "ra_deg: not a number"),
        ("dec_deg", "nan", "dec_deg: not a finite number"),
        ("star_id", "", "star_id: empty"),
        ("utc", "2026-10-16T03:00:00", "utc: must give its offset"),
        ("utc", "tonight", "utc: not an ISO 8601 time"),
        ("dut1_s", "", "dut1_s: not a number"),
        ("dut1_s", "1.2", "dut1_s: 1.2 s is not within"),
    ],
)
def test_names_the_line_of_a_bad_observation(tmp_path: Path, column: str, text: str, message: str) -> None:
    write_observations(tmp_path / "night.csv", observations(3))
    rows = rows_of(tmp_path / "night.csv")
    rows[1][column] = text
    write_rows(tmp_path / "bad.csv", rows)
    with pytest.raises(PointingError, match=f"bad.csv:3: {message}"):
        read_observations(tmp_path / "bad.csv")


def test_refuses_a_file_without_a_column_or_from_two_sites(tmp_path: Path) -> None:
    write_observations(tmp_path / "night.csv", observations(3))
    rows = rows_of(tmp_path / "night.csv")
    short = tuple(column for column in COLUMNS if column != "dut1_s")
    write_rows(tmp_path / "short.csv", [{k: v for k, v in row.items() if k != "dut1_s"} for row in rows], short)
    with pytest.raises(PointingError, match="lacks the columns dut1_s"):
        read_observations(tmp_path / "short.csv")
    rows[2]["site_height_m"] = "1300.0"
    write_rows(tmp_path / "two.csv", rows)
    with pytest.raises(PointingError, match="more than one site"):
        read_observations(tmp_path / "two.csv")


def test_refuses_a_star_below_20_degrees() -> None:
    low = AltAz(azimuth=1.0, elevation=math.radians(15.0))
    star = catalog_place(low, "low", START, CONDITIONS)
    observation = Observation(utc=START, star=star, encoder=low, conditions=CONDITIONS)
    with pytest.raises(PointingError, match="below 20°"):
        sighting(observation)


def catalog(count: int) -> list[Star]:
    """Stars spread over the whole sky, motionless."""
    rng = np.random.default_rng(93)
    return [
        Star(f"hip-{i}", float(rng.uniform(0, 360)), math.degrees(math.asin(rng.uniform(-1, 1))), 0.0, 0.0, 0.0, 0.0)
        for i in range(count)
    ]


def test_plans_stars_spread_across_the_sky(tmp_path: Path) -> None:
    end = START + timedelta(hours=6)
    plan = plan_night(catalog(300), CONDITIONS, START, end, PLANNED)
    assert len(plan) == PLANNED
    assert len({planned.star.star_id for planned in plan}) == PLANNED
    assert all(START <= planned.utc <= end for planned in plan)
    assert all(math.radians(20) <= planned.sky.elevation <= math.radians(85) for planned in plan)
    closest = min(separation(a.sky, b.sky) for i, a in enumerate(plan) for b in plan[i + 1 :])
    assert closest > math.radians(5)
    write_plan(tmp_path / "plan.csv", plan)
    assert len(rows_of(tmp_path / "plan.csv")) == PLANNED


def test_leaves_out_a_slot_without_a_star() -> None:
    plan = plan_night(catalog(1), CONDITIONS, START, START + timedelta(hours=6), 10)
    assert len(plan) <= 1
    with pytest.raises(PointingError, match="ends after it starts"):
        plan_night(catalog(5), CONDITIONS, START, START, 10)


def test_reads_a_catalog_and_names_a_bad_line(tmp_path: Path) -> None:
    columns = (
        "star_id",
        "ra_deg",
        "dec_deg",
        "pmra_mas_per_yr",
        "pmdec_mas_per_yr",
        "parallax_mas",
        "radial_velocity_km_s",
    )
    good = dict.fromkeys(columns, "0.0") | {"star_id": "a"}
    write_rows(tmp_path / "catalog.csv", [good], columns)
    assert read_catalog(tmp_path / "catalog.csv")[0].star_id == "a"
    write_rows(tmp_path / "bad.csv", [good, good | {"ra_deg": "?"}], columns)
    with pytest.raises(PointingError, match=r"bad\.csv:3: ra_deg"):
        read_catalog(tmp_path / "bad.csv")


def provenance() -> Provenance:
    """A test's provenance."""
    return Provenance("night-1", "station-1", START, SITE, "0" * 64, 15, 4 * MICRORADIAN)


def test_parameter_files_round_trip_and_are_checked(tmp_path: Path) -> None:
    model = golden.golden_model()
    result = FitResult(model=model, sigmas=(1e-6,) * len(model.terms), rms_rad=4e-6, used=45, rejected=(3,))
    write_parameters(tmp_path / "model.json", result, provenance())
    assert read_parameters(tmp_path / "model.json") == model
    content = json.loads((tmp_path / "model.json").read_text())
    assert content["fit"] == {
        "stars_used": 45,
        "stars_rejected": 1,
        "rms_urad": pytest.approx(4.0),
        "stars_held_out": 15,
        "held_out_rms_urad": pytest.approx(4.0),
    }
    content["terms"][0]["name"] = "XX"
    with pytest.raises(PointingError, match="pointing-model: 'XX' is not one of"):
        model_from(content)
    (tmp_path / "broken.json").write_text("{")
    with pytest.raises(PointingError, match="not JSON"):
        read_parameters(tmp_path / "broken.json")


def test_the_golden_files_are_the_reference_models(tmp_path: Path) -> None:
    golden.write(tmp_path)
    for name in ("model.json", "vectors.csv"):
        assert (tmp_path / name).read_text() == (GOLDEN / name).read_text(), name
    model = read_parameters(GOLDEN / "model.json")
    for row in rows_of(GOLDEN / "vectors.csv"):
        encoder = AltAz(float(row["encoder_az_rad"]), float(row["encoder_el_rad"]))
        sky = AltAz(float(row["sky_az_rad"]), float(row["sky_el_rad"]))
        assert to_sky(model, encoder) == sky
        across, up = sky_offset(to_encoder(model, sky), encoder)
        assert math.hypot(across, up) < ROUND_TRIP_RAD
    assert golden.main([]) == golden.EXIT_USAGE
    assert golden.main([str(tmp_path / "again")]) == 0


def test_fits_a_night_from_the_command_line(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    write_observations(tmp_path / "night.csv", observations())
    out = tmp_path / "model.json"
    every = ",".join(golden.golden_model().terms)
    arguments = ["fit", str(tmp_path / "night.csv"), "--station", "station-1", "--model-id", "night-1"]
    code = main([*arguments, "--terms", every, "--out", str(out)])
    assert code == 0
    content = json.loads(out.read_text())
    assert content["observations_sha256"] == sha256_of(tmp_path / "night.csv")
    assert content["fit"]["stars_held_out"] == HELD_OUT
    assert content["fit"]["held_out_rms_urad"] < C17_URAD
    assert "C17" in capsys.readouterr().out


def test_plans_a_night_from_the_command_line(tmp_path: Path) -> None:
    columns = (
        "star_id",
        "ra_deg",
        "dec_deg",
        "pmra_mas_per_yr",
        "pmdec_mas_per_yr",
        "parallax_mas",
        "radial_velocity_km_s",
    )
    rows = [
        {"star_id": star.star_id, "ra_deg": str(star.ra_deg), "dec_deg": str(star.dec_deg)}
        | dict.fromkeys(columns[3:], "0.0")
        for star in catalog(100)
    ]
    write_rows(tmp_path / "catalog.csv", rows, columns)
    arguments = [
        "plan",
        str(tmp_path / "catalog.csv"),
        "--site",
        "32.38,-106.48,1200",
        "--start",
        "2026-10-16T03:00:00Z",
    ]
    arguments += [
        "--end",
        "2026-10-16T09:00:00Z",
        "--count",
        str(CLI_PLANNED),
        "--dut1",
        "0.12",
        "--out",
        str(tmp_path / "plan.csv"),
    ]
    assert main(arguments) == 0
    assert len(rows_of(tmp_path / "plan.csv")) == CLI_PLANNED


@pytest.mark.parametrize(
    "arguments",
    [
        ["fit", "missing.csv", "--station", "s", "--model-id", "m", "--out", "model.json"],
        ["plan", "missing.csv", "--site", "1,2", "--start", "x", "--end", "x", "--dut1", "0", "--out", "p.csv"],
        [
            "plan",
            "missing.csv",
            "--site",
            "1,2,3",
            "--start",
            "2026-10-16T03:00:00",
            "--end",
            "x",
            "--dut1",
            "0",
            "--out",
            "p",
        ],
    ],
)
def test_refuses_bad_input_with_exit_2(
    tmp_path: Path, arguments: list[str], capsys: pytest.CaptureFixture[str]
) -> None:
    assert main([str(tmp_path / a) if a.endswith((".csv", ".json")) else a for a in arguments]) == EXIT_REFUSED
    assert capsys.readouterr().err


def test_refuses_a_night_without_observations(tmp_path: Path) -> None:
    write_observations(tmp_path / "empty.csv", [])
    out = str(tmp_path / "model.json")
    assert main(["fit", str(tmp_path / "empty.csv"), "--station", "s", "--model-id", "m", "--out", out]) == EXIT_REFUSED


def test_keeps_the_site_of_every_observation() -> None:
    moved = replace(observations(1)[0], conditions=replace(CONDITIONS, site=replace(SITE, height_m=1.0)))
    assert moved.conditions.site.height_m == 1.0
