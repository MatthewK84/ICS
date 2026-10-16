"""A star's observed place at a station, through ERFA (ICS-035).

ERFA is the open copy of the IAU's SOFA library. Its atco13 takes a star's
catalog place (ICRS) through proper motion, parallax, light deflection,
aberration, precession, nutation, Earth rotation and refraction to where it is
seen from the site: azimuth from north through east and zenith distance.

It needs UT1, which runs up to 0.9 s from UTC: each night's DUT1 (UT1 - UTC)
comes from IERS Bulletin A, and ICS fetches nothing. Polar motion, under
2.5 µrad, tilts the local vertical just as the pointing model's axis-tilt terms
do; it is left at zero and those terms absorb it. UTC comes through ERFA's own
leap-second table, which ERFA calls dubious some years after its release; no
leap second has been announced since 2016, so a dubious date is accepted, and
pyerfa should be upgraded if one ever is.
"""

import math
from dataclasses import dataclass
from datetime import UTC, datetime

from erfa import ufunc

from ics_models.pointing.model import AltAz, PointingError

# UT1 - UTC is kept within 0.9 s by leap seconds.
MAX_DUT1_S: float = 0.9
MAS_PER_RADIAN: float = 180.0 / math.pi * 3_600_000.0
MAS_PER_ARCSEC: float = 1000.0
HALF_PI: float = math.pi / 2.0


@dataclass(frozen=True)
class Site:
    """Where the station is: geodetic WGS84 latitude and longitude, east positive, and height above the ellipsoid."""

    latitude_deg: float
    longitude_deg: float
    height_m: float


@dataclass(frozen=True)
class Weather:
    """The night's air at the site, and the wavelength the camera sees, for refraction."""

    pressure_hpa: float
    temperature_c: float
    # 0 to 1.
    relative_humidity: float
    wavelength_um: float


@dataclass(frozen=True)
class Conditions:
    """A night at a site: its air, and its DUT1 (UT1 - UTC, in seconds) from IERS Bulletin A."""

    site: Site
    weather: Weather
    dut1_s: float


@dataclass(frozen=True)
class Star:
    """A star's catalog place, as Hipparcos and Gaia give it: ICRS at their epoch.

    pmra is the proper motion in right ascension times cos(declination),
    as catalogs give it.
    """

    star_id: str
    ra_deg: float
    dec_deg: float
    pmra_mas_per_yr: float
    pmdec_mas_per_yr: float
    parallax_mas: float
    radial_velocity_km_s: float


@dataclass(frozen=True)
class PolarMotion:
    """The celestial pole's offset from the Earth's, in radians; zero unless a test sets it."""

    x_rad: float = 0.0
    y_rad: float = 0.0


def utc_julian(when: datetime) -> tuple[float, float]:
    """A UTC time as ERFA's two-part quasi Julian date. PointingError for a naive time or one ERFA refuses."""
    if when.tzinfo is None:
        raise PointingError(when.isoformat(), "a time must say it is UTC")
    utc = when.astimezone(UTC)
    seconds = utc.second + utc.microsecond / 1e6
    day1, day2, status = ufunc.dtf2d("UTC", utc.year, utc.month, utc.day, utc.hour, utc.minute, seconds)
    if int(status) < 0:
        raise PointingError(when.isoformat(), "ERFA refuses the time")
    return float(day1), float(day2)


def check_dut1(dut1_s: float) -> None:
    """PointingError for a DUT1 that is not a number within ±0.9 s."""
    if not (math.isfinite(dut1_s) and abs(dut1_s) <= MAX_DUT1_S):
        raise PointingError("dut1_s", f"{dut1_s} s is not within ±{MAX_DUT1_S} s")


def observed(star: Star, when: datetime, conditions: Conditions, polar: PolarMotion | None = None) -> AltAz:
    """Where star is seen on the night at when: its observed azimuth and elevation, refraction included."""
    return observed_at(star, utc_julian(when), conditions, polar)


def observed_at(
    star: Star, utc: tuple[float, float], conditions: Conditions, polar: PolarMotion | None = None
) -> AltAz:
    """As observed, at a UTC time given as ERFA's two-part quasi Julian date, finer than a microsecond."""
    check_dut1(conditions.dut1_s)
    pole = polar if polar is not None else PolarMotion()
    site, weather = conditions.site, conditions.weather
    dec = math.radians(star.dec_deg)
    result = ufunc.atco13(
        math.radians(star.ra_deg),
        dec,
        star.pmra_mas_per_yr / MAS_PER_RADIAN / math.cos(dec),
        star.pmdec_mas_per_yr / MAS_PER_RADIAN,
        star.parallax_mas / MAS_PER_ARCSEC,
        star.radial_velocity_km_s,
        utc[0],
        utc[1],
        conditions.dut1_s,
        math.radians(site.longitude_deg),
        math.radians(site.latitude_deg),
        site.height_m,
        pole.x_rad,
        pole.y_rad,
        weather.pressure_hpa,
        weather.temperature_c,
        weather.relative_humidity,
        weather.wavelength_um,
    )
    azimuth, zenith_distance = float(result[0]), float(result[1])
    return AltAz(azimuth=azimuth % (2.0 * math.pi), elevation=HALF_PI - zenith_distance)


def catalog_place(seen: AltAz, star_id: str, when: datetime, conditions: Conditions) -> Star:
    """A motionless star that would be seen at seen on the night at when: synthetic nights use it."""
    check_dut1(conditions.dut1_s)
    utc1, utc2 = utc_julian(when)
    site, weather = conditions.site, conditions.weather
    ra, dec, _ = ufunc.atoc13(
        "A",
        seen.azimuth,
        HALF_PI - seen.elevation,
        utc1,
        utc2,
        conditions.dut1_s,
        math.radians(site.longitude_deg),
        math.radians(site.latitude_deg),
        site.height_m,
        0.0,
        0.0,
        weather.pressure_hpa,
        weather.temperature_c,
        weather.relative_humidity,
        weather.wavelength_um,
    )
    return Star(
        star_id=star_id,
        ra_deg=math.degrees(float(ra)),
        dec_deg=math.degrees(float(dec)),
        pmra_mas_per_yr=0.0,
        pmdec_mas_per_yr=0.0,
        parallax_mas=0.0,
        radial_velocity_km_s=0.0,
    )
