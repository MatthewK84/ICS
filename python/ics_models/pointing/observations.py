"""A calibration night's observations, as the capture side writes them (ICS-035).

One CSV row per star on the boresight, with a header naming COLUMNS:

- utc: mid-exposure, ISO 8601 with its offset, for example 2026-10-16T04:12:09.250000Z;
- dut1_s: UT1 - UTC for the night, from IERS Bulletin A;
- the star's catalog place, as Hipparcos and Gaia give it (astrometry.Star);
- encoder_az_rad, encoder_el_rad: the encoder angles at that moment, with the
  star's offset from the boresight already applied by the capture side, which
  knows the camera's plate scale and rotation;
- the night's weather and the camera's wavelength (astrometry.Weather);
- the site (astrometry.Site), the same on every row.

A star below MIN_ELEVATION_RAD is refused: refraction there is too uncertain
for C17.
"""

import csv
import math
from collections.abc import Sequence
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

from ics_models.pointing.astrometry import Conditions, Site, Star, Weather, check_dut1, observed
from ics_models.pointing.fit import Sighting
from ics_models.pointing.model import AltAz, PointingError

MIN_ELEVATION_RAD: float = math.radians(20.0)
STAR_COLUMNS: tuple[str, ...] = (
    "star_id",
    "ra_deg",
    "dec_deg",
    "pmra_mas_per_yr",
    "pmdec_mas_per_yr",
    "parallax_mas",
    "radial_velocity_km_s",
)
COLUMNS: tuple[str, ...] = (
    "utc",
    "dut1_s",
    *STAR_COLUMNS,
    "encoder_az_rad",
    "encoder_el_rad",
    "pressure_hpa",
    "temperature_c",
    "relative_humidity",
    "wavelength_um",
    "site_latitude_deg",
    "site_longitude_deg",
    "site_height_m",
)

type Row = dict[str, str]


@dataclass(frozen=True)
class Observation:
    """One star on the boresight, on a night."""

    utc: datetime
    star: Star
    encoder: AltAz
    conditions: Conditions


def number(row: Row, column: str) -> float:
    """A row's finite number. PointingError naming the column otherwise."""
    text = row.get(column) or ""
    try:
        value = float(text)
    except ValueError:
        raise PointingError(column, f"not a number: {text!r}") from None
    if not math.isfinite(value):
        raise PointingError(column, f"not a finite number: {text!r}")
    return value


def star_from(row: Row) -> Star:
    """The star in a row of a catalog or an observation file."""
    star_id = row.get("star_id") or ""
    if not star_id:
        raise PointingError("star_id", "empty")
    return Star(
        star_id=star_id,
        ra_deg=number(row, "ra_deg"),
        dec_deg=number(row, "dec_deg"),
        pmra_mas_per_yr=number(row, "pmra_mas_per_yr"),
        pmdec_mas_per_yr=number(row, "pmdec_mas_per_yr"),
        parallax_mas=number(row, "parallax_mas"),
        radial_velocity_km_s=number(row, "radial_velocity_km_s"),
    )


def utc_in(row: Row) -> datetime:
    """A row's UTC time, which must give its offset."""
    text = row.get("utc") or ""
    try:
        utc = datetime.fromisoformat(text)
    except ValueError:
        raise PointingError("utc", f"not an ISO 8601 time: {text!r}") from None
    if utc.tzinfo is None:
        raise PointingError("utc", "must give its offset, for example a trailing Z")
    return utc


def conditions_in(row: Row) -> Conditions:
    """A row's site, weather and DUT1."""
    dut1_s = number(row, "dut1_s")
    check_dut1(dut1_s)
    weather = Weather(
        pressure_hpa=number(row, "pressure_hpa"),
        temperature_c=number(row, "temperature_c"),
        relative_humidity=number(row, "relative_humidity"),
        wavelength_um=number(row, "wavelength_um"),
    )
    site = Site(
        latitude_deg=number(row, "site_latitude_deg"),
        longitude_deg=number(row, "site_longitude_deg"),
        height_m=number(row, "site_height_m"),
    )
    return Conditions(site=site, weather=weather, dut1_s=dut1_s)


def observation_from(row: Row) -> Observation:
    """One row. PointingError for anything missing or out of range."""
    encoder = AltAz(azimuth=number(row, "encoder_az_rad"), elevation=number(row, "encoder_el_rad"))
    return Observation(utc=utc_in(row), star=star_from(row), encoder=encoder, conditions=conditions_in(row))


def read_rows(path: Path, columns: Sequence[str]) -> list[Row]:
    """A CSV file's rows. PointingError if its header lacks a column."""
    with path.open(newline="") as file:
        reader = csv.DictReader(file)
        missing = [column for column in columns if column not in (reader.fieldnames or [])]
        if missing:
            raise PointingError(str(path), f"lacks the columns {', '.join(missing)}")
        return list(reader)


def read_observations(path: Path) -> list[Observation]:
    """A night's observations. PointingError naming the line of a bad row, or for rows from two sites."""
    observations: list[Observation] = []
    for line, row in enumerate(read_rows(path, COLUMNS), start=2):
        try:
            observations.append(observation_from(row))
        except PointingError as error:
            raise PointingError(f"{path}:{line}", str(error)) from None
    if len({observation.conditions.site for observation in observations}) > 1:
        raise PointingError(str(path), "holds observations from more than one site")
    return observations


def row_for(observation: Observation) -> list[str]:
    """One observation as a row, in COLUMNS's order."""
    star, conditions = observation.star, observation.conditions
    values = (
        star.ra_deg,
        star.dec_deg,
        star.pmra_mas_per_yr,
        star.pmdec_mas_per_yr,
        star.parallax_mas,
        star.radial_velocity_km_s,
        observation.encoder.azimuth,
        observation.encoder.elevation,
        conditions.weather.pressure_hpa,
        conditions.weather.temperature_c,
        conditions.weather.relative_humidity,
        conditions.weather.wavelength_um,
        conditions.site.latitude_deg,
        conditions.site.longitude_deg,
        conditions.site.height_m,
    )
    utc = observation.utc.isoformat().replace("+00:00", "Z")
    return [utc, repr(conditions.dut1_s), star.star_id, *(repr(value) for value in values)]


def write_observations(path: Path, observations: Sequence[Observation]) -> None:
    """Write observations as read_observations reads them."""
    with path.open("w", newline="") as file:
        writer = csv.writer(file, lineterminator="\n")
        writer.writerow(COLUMNS)
        writer.writerows(row_for(observation) for observation in observations)


def sighting(observation: Observation) -> Sighting:
    """The observation, with the star's observed place worked out. PointingError below MIN_ELEVATION_RAD."""
    sky = observed(observation.star, observation.utc, observation.conditions)
    if sky.elevation < MIN_ELEVATION_RAD:
        raise PointingError(observation.star.star_id, f"below 20° elevation at {observation.utc.isoformat()}")
    return Sighting(encoder=observation.encoder, sky=sky)
