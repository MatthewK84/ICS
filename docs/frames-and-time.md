# Frames and time

Every ICS component uses the conventions below for positions, heights and time ([ICS-014](https://github.com/MatthewK84/ICS/issues/14), plan ID PRO-03). The protobuf contracts ([`proto/`](../proto/README.md)) encode them in field names and types. The golden vectors in [`golden/frames/`](../golden/frames), generated with GeographicLib, pin them down numerically.

| Quantity | Convention | In the contracts |
|---|---|---|
| Position | WGS84 geodetic: latitude and longitude in degrees, height above the ellipsoid in metres | `GeodeticPoint` |
| Local position, velocity and covariance | The range east-north-up (ENU) frame, with its origin at the defended asset | `EnuVector`, `EnuCovariance` |
| Height | Height above the WGS84 ellipsoid; MSL only where a source reports it, converted with EGM96 | `height_ellipsoid_m` |
| Time | UTC, int64 nanoseconds since the Unix epoch, as POSIX counts them | fields ending `_utc_ns` |

## The ellipsoid

ICS uses the WGS84 ellipsoid:

| Parameter | Value |
|---|---|
| Semi-major axis a | 6,378,137 m |
| Inverse flattening 1/f | 298.257223563 |
| First eccentricity squared e² = f(2 − f) | 0.00669437999014… |

Coordinates are used as the GNSS sources report them, which realize WGS84 to about a centimetre. ICS does not transform between WGS84 realizations or ITRF epochs.

## Geodetic coordinates

- **Latitude** is geodetic, not geocentric: the angle between the equatorial plane and the ellipsoid normal. It is in degrees, from −90 to 90, positive north.
- **Longitude** is in degrees, from −180 to 180, positive east. −180 and 180 are the same meridian.
- **Height** is the height above the WGS84 ellipsoid along its normal, in metres, and negative below it.

## ECEF

Earth-centred, Earth-fixed coordinates are the intermediate step for every conversion; no contract carries them.

- The origin is the Earth's centre of mass.
- Z points to the north pole.
- X points to latitude 0, longitude 0.
- Y completes a right-handed frame, pointing to latitude 0, longitude 90° E.
- The unit is metres.

## The range ENU frame

Local positions, velocities and covariances are in one east-north-up frame per run:

- **Origin:** the defended asset, recorded in each run record as `RunRecord.range_origin`, a geodetic point with its ellipsoid height.
- **Up:** along the ellipsoid normal at the origin. This is not the local vertical of gravity.
- **East and north:** tangent to the origin's parallel and meridian.
- **Handedness:** right-handed, in metres.

This is GeographicLib's `LocalCartesian`. Every `EnuVector` in a run, and every `EnuCovariance`, uses that run's origin.

The frame is a flat plane touching the ellipsoid at the origin, so its "up" is not height above the ground. For example, a point about 20 km north at the origin's ellipsoid height has an up coordinate of −31.4 m (`golden/frames/geodetic-enu.csv`, `range-20km-north`). Use ellipsoid heights for altitude.

One contract field breaks the pattern. `PliRecord.Attitude` keeps MAVLink's convention: the rotation from the vehicle's body frame (forward, right, down) to the north-east-down frame at the vehicle's own position.

## Heights

Inside ICS every height is h, the height above the WGS84 ellipsoid. A source that reports a height above mean sea level (MSL), such as MAVLink's `GLOBAL_POSITION_INT.alt`, is converted where it enters ICS:

h = H + N

- H is the MSL (orthometric) height.
- N is the EGM96 geoid height at the point. It is interpolated cubically on GeographicLib's 5-arcminute `egm96-5` grid.
- That grid is installed in the toolchain images at `/usr/share/GeographicLib/geoids` and pinned by sha256 in [`deploy/toolchain/tools.txt`](../deploy/toolchain/tools.txt).
- Interpolating on the grid stays within 3 mm of the full EGM96 model, with an RMS error of 1 mm; the figures are from the grid file's header.

N ranges from about −107 m to +86 m worldwide. At the made-up range origin used in the examples (40° N, 100° W), it is −25.05 m.

EGM96 is the ICS definition of MSL. A vehicle's own MSL altitude may come from a different geoid model, so prefer a source's ellipsoid height when it reports one; MAVLink's `GPS_RAW_INT.alt_ellipsoid` is an example.

Height above ground needs a terrain model, and is not defined here.

## Time

Every time in ICS is UTC as an int64 count of nanoseconds since 1970-01-01T00:00:00Z. Fields holding a time end in `_utc_ns`; a duration or offset in nanoseconds ends in `_ns`.

- **POSIX counting.** The count runs as POSIX time does: every day has exactly 86,400 seconds, and leap seconds are not counted. A leap second (23:59:60) has no value of its own, so a timestamp inside it cannot be told apart from the second after it. IERS Bulletin C announces leap seconds six months ahead.
- **Range.** An int64 count of nanoseconds covers 1677-09-21 to 2262-04-11.
- **Resolution is not accuracy.** How far a clock can be trusted is `TimeQuality.error_bound_ns`, not the nanosecond resolution.

The other time scales ICS meets convert to UTC where they enter:

| Source | Time scale | Conversion |
|---|---|---|
| PTP grandmaster (ics-timingd, ICS-019) | TAI | UTC = TAI − 37 s, the offset since 2017-01-01; the grandmaster announces it as `currentUtcOffset` |
| GNSS receivers and autopilot logs (ICS-021 to ICS-026) | GPS time | GPS = TAI − 19 s, so UTC = GPS − 18 s since 2017-01-01; the offset changes with each leap second |
| Cameras (ICS-027, ICS-028) | IRIG-B time code, in UTC | Time of year from the code, the year from its control functions, then the measured camera offset (`TimeQuality.CameraOffset`) removed |

The component doing each conversion owns its leap-second offset and must take it from its source (the PTP announce message, or the GNSS navigation message), not from a constant.

## Golden vectors

[`golden/frames/generate.sh`](../golden/frames/generate.sh) runs GeographicLib 2.3's `CartConvert`, `GeoidEval` and `GeoConvert` over the case lists in [`golden/frames/inputs/`](../golden/frames/inputs), in the ics-cpp image:

| File | Conversion | GeographicLib | Printed to |
|---|---|---|---|
| `geodetic-ecef.csv` | Geodetic to ECEF | `CartConvert` | 1 nm |
| `geodetic-enu.csv` | Geodetic to the ENU frame of an origin | `CartConvert -l` | 1 nm |
| `egm96-5.csv` | Geoid height N, and MSL height to ellipsoid height | `GeoidEval -n egm96-5` | 0.1 mm |
| `utm-geodetic.csv` | WGS84 UTM to latitude and longitude ([ICS-024](https://github.com/MatthewK84/ICS/issues/24)) | `GeoConvert -g` | 1e-14 degrees |

The cases cover:

- the made-up range origin and its stations;
- the equator and the poles;
- the southern hemisphere and high latitudes;
- both sides of the antimeridian and ENU cases that cross it;
- points below the ellipsoid and at 12 km;
- EGM96's highest and lowest geoid heights;
- UTM points in zones 1 and 60, in both hemispheres, at the edges of a zone, and at UTM's northern and southern limits.

The C++ frames module, [`cpp/frames`](../cpp/README.md#frames), reproduces every vector within 1 mm ([ICS-017](https://github.com/MatthewK84/ICS/issues/17)). Its geodetic to ECEF and ENU results agree to the vectors' printed nanometre, its geoid heights to their printed 0.1 mm, and its UTM conversion, Karney's sixth-order Krüger series as in GeographicLib, to 1e-12 degrees. CI checks the vectors in three ways:

- **C++ toolchain workflow:** it regenerates them in the image and requires an exact match.
- **Python test** ([`python/tests/test_frame_vectors.py`](../python/tests/test_frame_vectors.py)): it recomputes geodetic to ECEF and ENU in closed form, requires agreement within 1 µm, and checks that h = H + N in every geoid row.
- **C++ test** ([`cpp/frames/test/golden_test.cpp`](../cpp/frames/test/golden_test.cpp)): `ics::frames` reproduces every vector, including the ECEF to geodetic inverse.

To add a case, add a row to an input list, then run `golden/frames/generate.sh` in the ics-cpp image and commit the regenerated files.
