# Time alignment fixtures

- `crossing-clocks.tsv`: the clock pairs and position times of a whole `crossing` engagement, flown on 2026-10-04 by the SITL rig ([`deploy/sitl`](../../../../deploy/sitl/README.md), ICS-018). The onboard logs in [`cpp/flightlog/test/logs`](../../../flightlog/test/logs/README.md) are cut from the same engagement. PX4 v1.17.0 is MAVLink system 1 and ArduCopter 4.7.1 is system 2. Each line holds what the rig's own MAVLink decoder ([`python/ics_sitl`](../../../../python/ics_sitl)) read from the run's TAP capture (`tap.pcap`, 150 s, sha256 `0a1c33a61d11ac9e530e815bcca045d20e3e8ffe13ecead5c3daee56b778f545`), tab-separated and in capture order:
  - `clock SYSTEM TIME_BOOT_MS TIME_UNIX_USEC` for each `SYSTEM_TIME` that has a UTC time: 150 from PX4 and 146 from ArduCopter;
  - `position SYSTEM TIME_BOOT_MS` for each `GLOBAL_POSITION_INT`: 1492 from PX4 and 1530 from ArduCopter.

  `ics-time-align` reads the same counts from `tap.pcap` through the MAVLink adapter. Each vehicle booted once.

`fixture_test.cpp` runs ICS-026's check on it: with 100 ppm of drift either way put on ArduCopter's boot clock and the middle 60 % of its clock pairs withheld, every position aligns within 1 ms. PX4 SIH's boot clock drifts by about 2.5 % of its own accord, so the check, whose reference takes a clock to have no drift, does not apply to it. The nightly [SITL rig workflow](../../../../.github/workflows/sitl.yml) runs the same check on each engagement it flies.
