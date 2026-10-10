# deploy/timing-bench

The timing bench ([ICS-019](https://github.com/MatthewK84/ICS/issues/19), build-plan item CPP-03) shows that [`ics-timingd`](../../cpp/README.md#timing) flags holdover within 1 s of GNSS loss. The [timing bench workflow](../../.github/workflows/timing-bench.yml) runs it every night and on pull requests that change the timing code, and fails unless all 20 trials pass.

The bench is test equipment. Nothing here ships.

## What the bench is

[`run-bench.sh`](run-bench.sh) puts two linuxptp 4.0 `ptp4l` instances in their own network namespaces, joined by a veth pair:

| Role | Config | What it does |
|---|---|---|
| Grandmaster | [`grandmaster.cfg`](grandmaster.cfg) | Serves time only, with software time stamps, announcing four times a second. It stands in for a GNSS-disciplined grandmaster |
| Station | [`station.cfg`](station.cfg) | A PTP client that measures its offset from the grandmaster but never adjusts the clock (`free_running`), because both share the host's clock. `ics-timingd` polls its read-only management socket |

The grandmaster announces GNSS lock as clock class 6, time traceable, time source GNSS (0x20). To emulate GNSS loss, `pmc` sets class 7, not time traceable, time source internal oscillator (0xA0), with `SET GRANDMASTER_SETTINGS_NP`. A real grandmaster announces the same change when it enters holdover.

Each trial:

1. waits a random 0 to 0.5 s, so trials fall at different points in the announce and poll cycles;
2. notes the time, then sets the grandmaster's GNSS loss;
3. waits for the first report with `CLOCK_STATE_HOLDOVER` that [`ics-time-watch`](../../cpp/README.md#timing), a gRPC subscriber to `ics-timingd`'s reports, receives, and takes its `time_utc_ns`;
4. sets GNSS lock again, and waits for a report with `CLOCK_STATE_LOCKED`.

The latency is the time from step 2 to that `time_utc_ns`. Both come from the host's clock. A trial passes when the latency is 1 s or less, and the bench passes when all 20 do.

The latency budget is the grandmaster's announce interval (250 ms), which delays the station seeing the new class, plus the poll interval (100 ms), plus a few milliseconds of processing. Local runs measured 31 to 297 ms reading the log, and 6 to 326 ms reading the reports over gRPC (#150).

## What it does not show

- **Time stamping.** Software time stamps on a veth pair say nothing about the offsets and path delays real hardware achieves. The bench tests the state logic, not the accuracy.
- **The grandmaster's own detection.** The bench starts timing when the grandmaster announces the loss. A real grandmaster takes its own time to notice that GNSS has gone and to enter holdover, which depends on the receiver. The real-hardware procedure below measures the whole chain.

## Running it

As root, with `ip`, `ptp4l` and `pmc` installed ([`apt-packages.txt`](apt-packages.txt)) and `ics-timingd` built (in the `ics-cpp` image, as the workflow does):

```sh
deploy/toolchain/conan-install.sh gcc-release
(cd cpp && cmake --preset gcc-release && cmake --build build/gcc-release --target ics-timingd ics-time-watch)
sudo env BENCH_OUT=/tmp/timing-bench deploy/timing-bench/run-bench.sh cpp/build/gcc-release/services/timingd/ics-timingd 20
```

`ics-timingd` reads its config from `/etc/ics/ics-timingd.toml`, so the bench runs it in its own mount namespace with the bench's config mounted over `/etc/ics`; the host's `/etc/ics` is never changed. It prints each trial's latency and the worst one. It takes `ics-time-watch` from `ics-timingd`'s folder. With `BENCH_OUT` set, it keeps `trials.csv` (trial, latency in ns), the reports (`reports.jsonl`) and the `ptp4l`, `ics-timingd` and `ics-time-watch` logs there. It removes its namespaces and processes when it exits, pass or fail.

## On real hardware

The same check on a station, with its grandmaster:

1. **Grandmaster.** A GNSS-disciplined PTP grandmaster that announces clock class 6 when locked and 7 in holdover, and announces at least twice a second (`logAnnounceInterval` −1 or lower). A slower announce interval eats into the 1 s budget one-for-one.
2. **GNSS loss on demand.** An RF switch in the antenna line, or a GNSS simulator, that the station host controls, so the host stamps the moment of loss on the same clock as the log. Pulling the antenna by hand adds the operator's reaction time to the measurement.
3. **Station.** `ptp4l` on the PTP interface with hardware time stamps, in the grandmaster's domain, with its read-only management socket at the default `/var/run/ptp4l-ro`. Then `ics-timingd`, with `/etc/ics/ics-timingd.toml` setting `poll_interval_ns` to 100 ms or less ([example config](../../cpp/services/timingd/ics-timingd.toml)).
4. **Trials.** Wait for `"state":"locked"` in the `ics-timingd` log. Then, 20 times: stamp the time and cut GNSS; record the `ts` of the next `"state":"holdover"` line; restore GNSS and wait for `"state":"locked"`. Write the latencies as `trials.csv`, as the bench does.
5. **Pass.** All 20 latencies at 1 s or less. A capture of the station's PTP port (`tcpdump -i <interface> ether proto 0x88f7`) shows when the first announce with class 7 arrived, which splits each latency into the grandmaster's detection and `ics-timingd`'s.
