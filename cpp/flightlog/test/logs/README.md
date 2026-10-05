# Onboard sample logs

Logs for the onboard-log importer's tests ([ICS-025](https://github.com/MatthewK84/ICS/issues/25)). `ulog_samples_test.cpp` and `dataflash_samples_test.cpp` check that the readers find what pyulog and pymavlink find in each log (`expected.tsv`); `import_samples_test.cpp` checks that every estimated position and GNSS fix they count becomes a record. The fuzz corpora (`../../fuzz/`) hold short pieces of `px4-crossing.ulg`, `pyulog-sample-px4-events.ulg` and `ardupilot-crossing.bin`.

## From the SITL rig

Cut from the onboard logs of a `crossing` engagement flown on 2026-10-04 by the SITL rig ([`deploy/sitl`](../../../../deploy/sitl/README.md), ICS-018), which saves them as `px4-log/` and `ardupilot-logs/`. PX4 v1.17.0 is MAVLink system 1 and ArduCopter 4.7.1 is system 2.

| File | Cut from | What it holds |
|---|---|---|
| `px4-crossing.ulg` | `px4-log/2026-10-04/23_09_21.ulg` (sha256 `d750c34a9ab07ec4e23c3569edfdaf7b59a597ecabc5345345049a83bef567f6`), its first 12 s | PX4 starting up, taking a mission and arming at 4.1 s. PX4's built-in simulator (SIH) gives its GNSS no UTC time, so the log is untimed. |
| `ardupilot-crossing.bin` | `ardupilot-logs/00000001.BIN` (sha256 `ee6e28d856137a8a5e668192b692464bd6596bad0c059a433cb587aefb9595c8`), 3 s to 13 s and 40 s to 46 s | ArduCopter starting up in GUIDED and getting GPS time at 10 s, then arming and switching to AUTO at 41.5 s |

Each keeps only what the importer reads, with `python -m ics_sitl cut-log` (`python/ics_sitl/logcut.py`): every message that defines the log, such as formats and parameters, then the named topics' samples and the logged text inside the windows, unchanged and in order:

```sh
cd python
TOPICS="--keep vehicle_global_position --keep vehicle_local_position --keep vehicle_attitude \
  --keep vehicle_gps_position --keep sensor_gps --keep vehicle_status"
.venv/bin/python -m ics_sitl cut-log 23_09_21.ulg px4-crossing.ulg --window 0:12 $TOPICS
.venv/bin/python -m ics_sitl cut-log 00000001.BIN ardupilot-crossing.bin --window 3:13 --window 40:46 \
  --keep POS --keep XKF1 --keep ATT --keep GPS --keep GPA --keep MSG --keep MODE --keep ARM
```

## From pyulog

Three logs from the `test/` folder of [pyulog](https://github.com/PX4/pyulog), PX4's ULog parser, at commit `ffbe3755d903e93797a89fb4fce26e0c0420a42f`. They are under pyulog's BSD 3-Clause license ([`pyulog-LICENSE.md`](pyulog-LICENSE.md)).

| File | From | What it tests |
|---|---|---|
| `pyulog-sample-px4-events.ulg` | `sample_px4_events.ulg`, cut to the topics above | A PX4 log with GNSS UTC times. Its simulator's boot clock counts from the Unix epoch. |
| `pyulog-sample-logging-tagged.ulg` | `sample_logging_tagged_and_default_params.ulg`, cut to the topics above | GNSS in the layout before PX4 v1.14 (1e-7 degrees and millimetres), and tagged logged strings |
| `pyulog-sample-appended-multiple.ulg` | `sample_appended_multiple.ulg`, unchanged | Data appended after a crash, at the offsets of the flag-bits message |

The two cuts keep the whole log:

```sh
.venv/bin/python -m ics_sitl cut-log sample_px4_events.ulg pyulog-sample-px4-events.ulg --window 0:4000000000 $TOPICS
.venv/bin/python -m ics_sitl cut-log sample_logging_tagged_and_default_params.ulg pyulog-sample-logging-tagged.ulg \
  --window 0:100000 $TOPICS
```

[`tools.txt`](tools.txt) records the commit and the sha256 of the three source files concatenated in the order of the table.

## `expected.tsv`

What pyulog, at the same commit, and pymavlink 2.4.50 read in each log: one tab-separated line per fact, the file, a key, then values, such as a topic's sample count, the first and last valid positions, and every arming and mode change. Floats are written with Python's `repr`, so the tests compare them exactly. [`expectations.py`](expectations.py) writes it:

```sh
python3 -m venv venv && venv/bin/pip install numpy pymavlink==2.4.50
git clone https://github.com/PX4/pyulog && git -C pyulog checkout ffbe3755d903e93797a89fb4fce26e0c0420a42f
venv/bin/python expectations.py pyulog . > expected.tsv
```
