# deploy/capture-bench

The capture bench ([ICS-020](https://github.com/MatthewK84/ICS/issues/20), build-plan item CPP-05) shows that [`ics-capd`](../../cpp/README.md#capture) captures every packet with zero drops, and that every file it writes matches its SHA-256. The [capture bench workflow](../../.github/workflows/capture-bench.yml) runs it every night and on pull requests that change the capture code.

The bench is test equipment. Nothing here ships.

## What the bench is

[`run-bench.sh`](run-bench.sh) puts two network namespaces on a veth pair. In one, `ics-capture-send` sends numbered UDP datagrams at a fixed rate with `sendmmsg`. In the other, `ics-capd` captures the veth end, which stands in for a TAP port, with host time stamps. The capture namespace has no address and no IPv6, and the sender's neighbour entry is static, so nothing else crosses the link: no ARP, no IPv6 neighbour discovery, no replies.

As each file closes, the bench:

1. checks it against its `.sha256` file with `sha256sum --check`;
2. checks with `ics-capture-verify` that it holds the datagrams in order, continuing from the last file, with none missing or repeated;
3. records it in `files.csv` (file, packets, SHA-256) and deletes it, so a long run needs only a few files' worth of disk.

When the sender is done, the bench stops `ics-capd` with SIGTERM, which closes and hashes its last file, and checks that one too. It passes when every hash matched, the files held exactly the datagrams sent, `ics-capd` logged no `drops`, `capture_failed`, `close_failed` or `counters_unavailable`, and it exited 0. Both tools are in [`cpp/testing/capture_bench`](../../cpp/testing/capture_bench).

## The 24 h capture

The issue's "Done when" is a 24 h capture with zero drops. GitHub-hosted jobs stop at 6 h, so CI runs it three ways:

| Run | Datagrams | Rate | Files | Takes |
|---|---|---|---|---|
| Every night: 24 h, accelerated | 172,800,000, a day at 2,000 packets/s | 100,000/s | 24, one every 72 s | about 30 min |
| Pull requests | 12,000,000 | 100,000/s | 24, one every 5 s | 2 min |
| Manual, real time | 2,000/s for 1 to 5 h | 2,000/s | one an hour | up to 5 h |

The accelerated run carries a day's packet count at 50 times the assumed TAP rate (2,000 packets/s on average, bursts up to 20,000), and writes the same 24 files a real day would. The ICS-020 issue closes on it. A real 24 h capture is run on station hardware, below.

## What it does not show

- **Hardware time stamps.** A veth pair has no hardware clock, so the bench uses host time stamps. The service's unit tests check that adapter time stamps are refused on an interface without them, and that TAI − UTC is read from `ptp4l`; the real-hardware run checks the stamps themselves.
- **A real NIC.** The bench's packets never touch a NIC or its driver, whose own ring can overflow. `ics-capd` reports those drops as `interface_dropped`, and the real-hardware run checks them.
- **Wall-clock effects over a day.** Disk filling, log rotation and the like happen over a real day, not 30 minutes.

## Running it

As root, with `ip` installed ([`apt-packages.txt`](apt-packages.txt)) and the tools built (in the `ics-cpp` image, as the workflow does):

```sh
deploy/toolchain/conan-install.sh gcc-release
(cd cpp && cmake --preset gcc-release && cmake --build build/gcc-release --target ics-capd ics-capture-send ics-capture-verify)
build=cpp/build/gcc-release
sudo env BENCH_OUT=/tmp/capture-bench deploy/capture-bench/run-bench.sh "${build}/services/capd/ics-capd" \
  "${build}/testing/capture_bench/ics-capture-send" "${build}/testing/capture_bench/ics-capture-verify" 12000000 100000 5
```

The arguments are the three tools, the datagrams to send, the rate a second and the rotation interval in seconds. `ics-capd` reads its config from `/etc/ics/ics-capd.toml`, so the bench runs it in its own mount namespace with the bench's config mounted over `/etc/ics`; the host's `/etc/ics` is never changed. With `BENCH_OUT` set, it keeps `files.csv`, the sender's count and the `ics-capd` log there. It removes its namespaces and processes when it exits, pass or fail.

## On real hardware

The 24 h capture on a station:

1. **Station.** `ptp4l` disciplining each TAP NIC's hardware clock, with its read-only management socket at the default `/var/run/ptp4l-ro`. `ics-capd` with `/etc/ics/ics-capd.toml` naming the TAP ports, `timestamps = "adapter"`, `rotate_interval_ns` an hour and a `[ptp]` table ([example config](../../cpp/services/capd/ics-capd.toml)), on a disk with room for a day of traffic.
2. **Traffic.** The range's own traffic through the TAP, or a traffic generator on the mirrored link at the expected rate with bursts, for 24 h. A generator that numbers its packets, as `ics-capture-send` does, lets `ics-capture-verify` check that none went missing.
3. **Pass.** Every `counters` line in the `ics-capd` log shows `dropped` and `interface_dropped` 0, there is no `drops`, `capture_failed` or `close_failed` line, `sha256sum --check` passes for every `.sha256` file, and the files cover the whole 24 h, one an hour per port. Spot-check the time stamps against the generator's own log or a PTP-locked reference capture: they should agree to within the NIC's time-stamping accuracy.
