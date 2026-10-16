"""Star positions through ERFA (ICS-035), checked against SOFA's own test case."""

import math
from datetime import UTC, datetime, timedelta, timezone

import pytest
from erfa import ufunc

from ics_models.pointing.astrometry import (
    MAS_PER_RADIAN,
    Conditions,
    PolarMotion,
    Site,
    Star,
    Weather,
    catalog_place,
    observed,
    observed_at,
    utc_julian,
)
from ics_models.pointing.model import AltAz, PointingError, sky_offset

# SOFA's test of atco13 (t_sofa_c.c, as pyerfa's tests repeat it): a star,
# a site, its air, a time and DUT1, and where the star is seen.
SOFA_RA_RAD: float = 2.71
SOFA_DEC_RAD: float = 0.174
SOFA_PR_RAD_PER_YR: float = 1e-5
SOFA_PD_RAD_PER_YR: float = 5e-6
SOFA_PARALLAX_ARCSEC: float = 0.1
SOFA_RV_KM_S: float = 55.0
SOFA_UTC: tuple[float, float] = (2456384.5, 0.969254051)
SOFA_DUT1_S: float = 0.1550675
SOFA_ELONG_RAD: float = -0.527800806
SOFA_PHI_RAD: float = -1.2345856
SOFA_HEIGHT_M: float = 2738.0
SOFA_POLAR: PolarMotion = PolarMotion(x_rad=2.47230737e-7, y_rad=1.82640464e-6)
SOFA_WEATHER: Weather = Weather(pressure_hpa=731.0, temperature_c=12.8, relative_humidity=0.59, wavelength_um=0.55)
SOFA_AZIMUTH_RAD: float = 0.9251774485485515207e-1
SOFA_ZENITH_RAD: float = 1.407661405256499357
SOFA_TOLERANCE_RAD: float = 1e-12

SITE: Site = Site(latitude_deg=32.38, longitude_deg=-106.48, height_m=1200.0)
WEATHER: Weather = Weather(pressure_hpa=880.0, temperature_c=12.0, relative_humidity=0.3, wavelength_um=0.6)
NIGHT: Conditions = Conditions(site=SITE, weather=WEATHER, dut1_s=0.12)
WHEN: datetime = datetime(2026, 10, 16, 4, 30, tzinfo=UTC)
# ERFA's two directions agree to a few nanoradians, its refraction being
# inverted approximately.
ROUND_TRIP_RAD: float = 1e-8


def sofa_star() -> Star:
    """SOFA's star, as a catalog gives it."""
    return Star(
        star_id="sofa",
        ra_deg=math.degrees(SOFA_RA_RAD),
        dec_deg=math.degrees(SOFA_DEC_RAD),
        pmra_mas_per_yr=SOFA_PR_RAD_PER_YR * math.cos(SOFA_DEC_RAD) * MAS_PER_RADIAN,
        pmdec_mas_per_yr=SOFA_PD_RAD_PER_YR * MAS_PER_RADIAN,
        parallax_mas=SOFA_PARALLAX_ARCSEC * 1000.0,
        radial_velocity_km_s=SOFA_RV_KM_S,
    )


def sofa_conditions() -> Conditions:
    """SOFA's site, air and DUT1."""
    site = Site(latitude_deg=math.degrees(SOFA_PHI_RAD), longitude_deg=math.degrees(SOFA_ELONG_RAD), height_m=2738.0)
    return Conditions(site=site, weather=SOFA_WEATHER, dut1_s=SOFA_DUT1_S)


def test_matches_sofas_test_case() -> None:
    seen = observed_at(sofa_star(), SOFA_UTC, sofa_conditions(), SOFA_POLAR)
    assert seen.azimuth == pytest.approx(SOFA_AZIMUTH_RAD, abs=SOFA_TOLERANCE_RAD)
    assert math.pi / 2 - seen.elevation == pytest.approx(SOFA_ZENITH_RAD, abs=SOFA_TOLERANCE_RAD)


def test_reads_utc_to_the_microsecond() -> None:
    # SOFA's time, to the microsecond: 2013-04-02 23:15:43.550006.
    day1, day2 = utc_julian(datetime(2013, 4, 2, 23, 15, 43, 550006, tzinfo=UTC))
    assert day1 == SOFA_UTC[0]
    assert day2 == pytest.approx(SOFA_UTC[1] - 0.4e-6 / 86400, abs=1e-12)
    # The same instant, given two hours east of Greenwich.
    east = datetime(2013, 4, 3, 1, 15, 43, 550006, tzinfo=timezone(timedelta(hours=2)))
    assert utc_julian(east) == (day1, day2)


def test_refuses_a_time_that_does_not_say_it_is_utc() -> None:
    with pytest.raises(PointingError, match="UTC"):
        utc_julian(datetime(2026, 10, 16, 4, 30))  # A naive time, on purpose.


@pytest.mark.parametrize("dut1_s", [0.95, -1.0, math.nan])
def test_refuses_a_dut1_out_of_range(dut1_s: float) -> None:
    star = catalog_place(AltAz(azimuth=1.0, elevation=0.8), "s", WHEN, NIGHT)
    with pytest.raises(PointingError, match="dut1_s"):
        observed(star, WHEN, Conditions(site=SITE, weather=WEATHER, dut1_s=dut1_s))


def test_a_catalog_place_is_seen_where_it_was_put() -> None:
    for azimuth_deg in (10.0, 135.0, 250.0):
        for elevation_deg in (20.0, 50.0, 85.0):
            seen = AltAz(azimuth=math.radians(azimuth_deg), elevation=math.radians(elevation_deg))
            star = catalog_place(seen, "s", WHEN, NIGHT)
            across, up = sky_offset(observed(star, WHEN, NIGHT), seen)
            assert math.hypot(across, up) < ROUND_TRIP_RAD


def test_refraction_raises_a_star_by_about_a_minute_at_45_degrees() -> None:
    seen = AltAz(azimuth=1.0, elevation=math.radians(45.0))
    star = catalog_place(seen, "s", WHEN, NIGHT)
    airless = Conditions(site=SITE, weather=Weather(0.0, 12.0, 0.3, 0.6), dut1_s=NIGHT.dut1_s)
    lift = observed(star, WHEN, NIGHT).elevation - observed(star, WHEN, airless).elevation
    # About 58 arcseconds at sea level; less at 880 hPa.
    assert math.radians(40 / 3600) < lift < math.radians(55 / 3600)


def test_accepts_a_date_past_erfas_leap_second_table() -> None:
    later = datetime(2040, 1, 1, tzinfo=UTC)
    status = int(ufunc.dtf2d("UTC", 2040, 1, 1, 0, 0, 0.0)[2])
    assert status == 1
    star = catalog_place(AltAz(azimuth=1.0, elevation=0.8), "s", later, NIGHT)
    assert math.isfinite(observed(star, later, NIGHT).elevation)
