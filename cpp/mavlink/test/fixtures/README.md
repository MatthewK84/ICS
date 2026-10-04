# MAVLink adapter fixtures

Real SITL traffic for `fixture_test.cpp` ([ICS-021](https://github.com/MatthewK84/ICS/issues/21)).

- `crossing.pcap`: part of the TAP capture (`tap.pcap`) of a `crossing` engagement flown on 2026-10-02 by the SITL rig ([`deploy/sitl`](../../../../deploy/sitl/README.md), ICS-018), with PX4 v1.17.0 as MAVLink system 1 and ArduCopter 4.7.1 as system 2. It keeps the packets from the capture's first 6 s, which hold both autopilots starting up, refusing commands and PX4 arming, and from 39.5 s to 42 s, when ArduCopter gets GPS time and arms. Only packets holding at least one message the adapter reads are kept, unchanged, with their time stamps.
- `crossing.tsv`: what the rig's own MAVLink decoder (`python/ics_sitl`) makes of the same packets, one tab-separated line each:
  - `position SYSTEM TIME_BOOT_MS LAT_E7 LON_E7 ALT_MM` for each `GLOBAL_POSITION_INT`;
  - `text SYSTEM TEXT` for each `STATUSTEXT`, with characters outside printable ASCII as `?`;
  - `refused SYSTEM COMMAND RESULT` for each `COMMAND_ACK` that is not `MAV_RESULT_ACCEPTED`.

  Every line was checked against the run's `truth.jsonl` when the fixture was cut.

The nightly [SITL rig workflow](../../../../.github/workflows/sitl.yml) checks whole engagements the same way, with `ics-mavlink-replay` and `python -m ics_sitl compare`.
