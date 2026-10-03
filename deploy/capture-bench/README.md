# deploy/capture-bench

The capture bench ([ICS-020](https://github.com/MatthewK84/ICS/issues/20), build-plan item CPP-05) checks the "Done when" for [`ics-capd`](../../cpp/README.md#capture): a 24 h capture with zero drops. The [capture bench workflow](../../.github/workflows/capture-bench.yml) runs an accelerated version every night and a short one on pull requests that change the capture code.

The bench is test equipment. Nothing here ships.

## What the bench is

[`run-bench.sh`](run-bench.sh) puts two network namespaces on a veth pair:

| End | Runs |
|---|---|
| Load | [`ics-loadgen`](../../cpp/tools/loadgen/main.cpp), which sends a fixed number of 64-byte UDP datagrams at a fixed rate |
| Capture | `ics-capd`, capturing its end of the pair as it would a TAP port, with host time stamps (a veth has no hardware clock) |

The link carries nothing but the load. IPv6 is off, and the load end's neighbour entry is static, so there is no ARP. The datagrams go to an address the capture end does not hold, so nothing answers them.

While the load runs, the bench checks each file `ics-capd` closes against its `.sha256` with `sha256sum`, adds up the packets and drops the `file_closed` log line reports, and deletes the file, so the disk holds only a few files at a time. At the end it stops `ics-capd`, which closes the last file. The bench passes when:

- every file matches its `.sha256`;
- `ics-capd` counted no drops, in the ring or at the interface;
- the files hold exactly as many packets as `ics-loadgen` sent, so none was lost or added;
- `ics-capd` logged no error and stopped cleanly.

## Why accelerated

The issue asks for 24 h. A hosted CI job stops at 6 h, so the nightly run compresses the day:

- **The same packets.** A TAP port carries about 2,000 packets a second (task-card assumption 1), so a day is 172,800,000. The soak sends that many at 100,000 a second, which takes about 29 minutes.
- **The same files.** `ics-capd`'s hourly rotation makes 24 files a day. The soak rotates every 72 s, and so makes the same 24.
- **A harder load.** 100,000 packets a second is 50 times the expected rate, so the ring and the writer have less slack than a station gives them.

What compression cannot show is a defect that appears only with wall-clock time, such as a slow leak. The real-time procedure below covers that.

## Running it

As root, with `ip` installed ([`apt-packages.txt`](apt-packages.txt)) and `ics-capd` and `ics-loadgen` built:

```sh
deploy/toolchain/conan-install.sh gcc-release
(cd cpp && cmake --preset gcc-release && cmake --build build/gcc-release --target ics-capd ics-loadgen)
sudo env BENCH_OUT=/tmp/capture-bench deploy/capture-bench/run-bench.sh \
  cpp/build/gcc-release/services/capd/ics-capd cpp/build/gcc-release/tools/loadgen/ics-loadgen \
  172800000 100000 72
```

The arguments are the datagrams to send, the rate, and the seconds per file. It prints a summary. With `BENCH_OUT` set, it keeps the summary, `files.csv` (each file's packets and drops) and the `ics-capd` log there. It removes its namespaces and processes when it exits, pass or fail.

`ics-capd` reads `/etc/ics/ics-capd.toml`, so the bench runs it in its own mount namespace with the bench's config mounted over `/etc/ics`; the host's `/etc/ics` is never changed. The workflow's manual run takes the same three values, so it can also run in real time (2,000 a second) for up to 5.5 hours.

## On a station: the 24 h acceptance run

1. **TAP.** Connect the TAP's monitor port to the station's capture NIC. Check the NIC has hardware time stamps: `ethtool -T <interface>` should list `hardware-receive` and `HWTSTAMP_FILTER_ALL`.
2. **Time.** Run `ptp4l` on the PTP interface so that it disciplines the capture NIC's hardware clock. With a different NIC, run `phc2sys` to slave its clock to the PTP one. `ics-capd` reads TAI−UTC from `ptp4l` at start.
3. **Config.** Write `/etc/ics/ics-capd.toml` from the [example](../../cpp/services/capd/ics-capd.toml), with `timestamps = "adapter"` and an `output_dir` on a disk with room for a day: the TAP's daily volume plus 16 bytes a packet.
4. **Run.** Start `ics-capd` with `CAP_NET_RAW` and `CAP_NET_ADMIN`; hardware time stamps need the second. Leave it for 24 h under normal range traffic.
5. **Pass.**
   - Each hour's `file_closed` line shows `"dropped":0,"interface_dropped":0` and is logged at `info`.
   - `sha256sum -c` passes for every `.sha256` in `output_dir`.
   - The NIC's own counters show no receive drops over the day (`ethtool -S <interface>`, before and after).
