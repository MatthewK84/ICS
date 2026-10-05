"""Write expected.tsv: what pyulog and pymavlink read in the sample logs (ICS-025).

The C++ readers' and importer's tests compare what they read with these lines (README.md). One
tab-separated line per expectation: FILE, KEY, then values. Floats are written with repr, so the
tests can compare float32 and float64 fields exactly.

Usage: python expectations.py PYULOG_CHECKOUT LOGS_FOLDER > expected.tsv
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any  # pyulog and pymavlink have no type hints.

ULOGS = (
    "px4-crossing.ulg",
    "pyulog-sample-px4-events.ulg",
    "pyulog-sample-logging-tagged.ulg",
    "pyulog-sample-appended-multiple.ulg",
)
DATAFLASH = "ardupilot-crossing.bin"
# The lowest GNSS fix type with a position: 2D.
MINIMUM_FIX = 2
GLOBAL_FIELDS = ("timestamp", "lat", "lon", "alt", "alt_ellipsoid", "eph", "epv")
GPS_FIELDS = (
    "timestamp",
    "time_utc_usec",
    "timestamp_time_relative",
    "lat",
    "lon",
    "latitude_deg",
    "longitude_deg",
    "alt_ellipsoid",
    "altitude_ellipsoid_m",
    "fix_type",
    "eph",
    "epv",
)


def line(*values: object) -> None:
    sys.stdout.write("\t".join(str(v) for v in values) + "\n")


def value(v: Any) -> str:
    if hasattr(v, "item"):
        v = v.item()
    if isinstance(v, float):
        return repr(v)
    return str(int(v)) if isinstance(v, (bool, int)) else str(v)


def dataset(log: Any, name: str) -> Any:
    """A topic's instance 0, or None."""
    for data in log.data_list:
        if data.name == name and data.multi_id == 0:
            return data.data
    return None


def rows(data: Any, fields: tuple[str, ...], index: int) -> list[str]:
    return [f"{f}={value(data[f][index])}" for f in fields if f in data]


def global_expectations(name: str, log: Any) -> None:
    g = dataset(log, "vehicle_global_position")
    if g is None:
        return
    flags = g.get("lat_lon_valid", [1] * len(g["timestamp"]))
    valid = [i for i in range(len(g["timestamp"])) if flags[i]]
    line(name, "valid_global", len(valid))
    line(name, "first_global", *rows(g, GLOBAL_FIELDS, valid[0]))
    line(name, "last_global", *rows(g, GLOBAL_FIELDS, valid[-1]))


def gps_expectations(name: str, log: Any) -> None:
    gps = dataset(log, "vehicle_gps_position")
    if gps is None:
        return
    fixed = [i for i in range(len(gps["timestamp"])) if gps["fix_type"][i] >= MINIMUM_FIX]
    timed = [i for i in range(len(gps["timestamp"])) if gps["time_utc_usec"][i] > 0]
    line(name, "fixed_gps", len(fixed))
    line(name, "timed_gps", len(timed))
    line(name, "first_gps", *rows(gps, GPS_FIELDS, fixed[0]))
    line(name, "last_gps", *rows(gps, GPS_FIELDS, fixed[-1]))


def motion_expectations(name: str, log: Any) -> None:
    att = dataset(log, "vehicle_attitude")
    if att is not None:
        line(name, "first_attitude", *rows(att, ("timestamp", "q[0]", "q[1]", "q[2]", "q[3]"), 0))
    local = dataset(log, "vehicle_local_position")
    if local is not None:
        line(name, "first_local", *rows(local, ("timestamp", "vx", "vy", "vz"), 0))


def status_expectations(name: str, log: Any) -> None:
    status = dataset(log, "vehicle_status")
    if status is None:
        return
    previous: tuple[int, int] | None = None
    for i in range(len(status["timestamp"])):
        now = (int(status["arming_state"][i]), int(status["nav_state"][i]))
        if now != previous:
            line(name, "status", status["timestamp"][i], *now)
            previous = now


def string_expectations(name: str, log: Any) -> None:
    tagged = [m for messages in log.logged_messages_tagged.values() for m in messages]
    strings = sorted([*log.logged_messages, *tagged], key=lambda m: m.timestamp)
    line(name, "strings", len(strings))
    if strings:
        line(name, "first_string", strings[0].timestamp, strings[0].message)


def ulog_expectations(ulog: Any, logs: Path, name: str) -> None:
    log = ulog(str(logs / name))
    line(name, "corrupt", int(log.file_corruption))
    line(name, "appended", int(log.has_data_appended))
    line(name, "system_id", value(log.initial_parameters.get("MAV_SYS_ID", -1)))
    for topic in sorted({d.name for d in log.data_list}):
        data = dataset(log, topic)
        if data is not None:
            line(name, "count", topic, len(data["timestamp"]))
    global_expectations(name, log)
    gps_expectations(name, log)
    motion_expectations(name, log)
    status_expectations(name, log)
    string_expectations(name, log)


def dataflash_messages(reader: Any) -> tuple[dict[str, int], dict[str, list[Any]], Any]:
    """Each type's count, the messages the tests look at by type, and the system ID."""
    counts: dict[str, int] = {}
    kept: dict[str, list[Any]] = {t: [] for t in ("POS", "GPS", "MSG", "MODE", "ARM")}
    sysid = None
    while (m := reader.recv_msg()) is not None:
        t = m.get_type()
        counts[t] = counts.get(t, 0) + 1
        if t in kept and (t != "GPS" or getattr(m, "I", 0) == 0):
            kept[t].append(m)
        elif t == "PARM" and m.Name in ("MAV_SYSID", "SYSID_THISMAV") and sysid is None:
            sysid = m.Value
    return counts, kept, sysid


def dataflash_expectations(reader_type: Any, logs: Path, name: str) -> None:
    counts, kept, sysid = dataflash_messages(reader_type(str(logs / name)))
    for t in sorted(counts):
        line(name, "count", t, counts[t])
    line(name, "system_id", value(sysid))
    for which, m in (("first_pos", kept["POS"][0]), ("last_pos", kept["POS"][-1])):
        line(name, which, f"TimeUS={m.TimeUS}", f"Lat={m.Lat!r}", f"Lng={m.Lng!r}", f"Alt={m.Alt!r}")
    timed = [m for m in kept["GPS"] if m.GWk > 0]
    line(name, "fixed_gps", len([m for m in kept["GPS"] if m.Status >= MINIMUM_FIX]))
    line(name, "timed_gps", len(timed))
    for which, m in (("first_timed_gps", timed[0]), ("last_timed_gps", timed[-1])):
        line(
            name,
            which,
            f"TimeUS={m.TimeUS}",
            f"GWk={m.GWk}",
            f"GMS={m.GMS}",
            f"Status={m.Status}",
            f"Lat={m.Lat!r}",
            f"Lng={m.Lng!r}",
            f"Alt={m.Alt!r}",
        )
    line(name, "strings", len(kept["MSG"]))
    line(name, "first_string", kept["MSG"][0].TimeUS, kept["MSG"][0].Message)
    for m in kept["MODE"]:
        line(name, "mode", m.TimeUS, m.ModeNum)
    for m in kept["ARM"]:
        line(name, "arm", m.TimeUS, m.ArmState)


def main(pyulog: str, logs: Path) -> None:
    sys.path.insert(0, pyulog)
    from pymavlink import DFReader  # noqa: PLC0415
    from pyulog import ULog  # noqa: PLC0415

    for name in ULOGS:
        ulog_expectations(ULog, logs, name)
    dataflash_expectations(DFReader.DFReader_binary, logs, DATAFLASH)


if __name__ == "__main__":
    main(sys.argv[1], Path(sys.argv[2]))
