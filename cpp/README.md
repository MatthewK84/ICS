# cpp

Every real-time and production data path: timing, PLI, cameras, mounts, triggering, the processing pipeline, run records and the query API. C++ never does model fitting or exploratory analysis; that belongs in `python/`.

**Language:** C++. **Lead roles:** Systems engineer (timing, PLI, camera I/O), Controls engineer (mount, encoder, tracker, trigger), CV and estimation engineers (registration through footprint).

Planned layout:

```text
cpp/
  common/ camera_io/ timing/ trigger/ pli/ mount/ tracker/ encoder/
  endgame/ registration/ detection/ association/ estimation/
  killclass/ footprint/ record/ api/
  services/   ics-timingd ics-plid ics-mountd ics-trigd ics-camd
              ics-api ics-recordd ics-pipeline
```

Rules (enforced in CI by [ICS-005](https://github.com/MatthewK84/ICS/issues/5) and [ICS-008](https://github.com/MatthewK84/ICS/issues/8)):

- No exceptions on real-time paths; fallible calls return `tl::expected` and are `[[nodiscard]]`.
- No recursion, functions of 50 lines or fewer, no owning raw pointers, no mutable globals, no `NOLINT`.
- No allocation after initialization on real-time paths.
- Mount motion, the sun interlock and triggering live only here, never in the web UI.
- Python parameter files and ONNX models are loaded at startup, and their SHA-256 hashes go into every run record.

## Build

Build inside the `ics-cpp` image (see [deploy/toolchain](../deploy/toolchain/README.md)). Each preset in [`CMakePresets.json`](CMakePresets.json) pairs a compiler, `gcc` or `clang`, with a variant, `debug`, `release`, `asan` or `tsan`:

```sh
deploy/toolchain/conan-install.sh gcc-release   # dependencies from cpp/conan.lock
cd cpp
cmake --preset gcc-release
cmake --build --preset gcc-release
ctest --preset gcc-release
```

Builds are reproducible: `deploy/toolchain/check-reproducible.sh gcc` (or `clang`) builds the `toolchain_check` sample twice and requires byte-identical outputs. The flags that make this work are in [`cmake/Reproducible.cmake`](cmake/Reproducible.cmake).

Dependencies are pinned in `conan.lock`. After changing `conanfile.py`, regenerate it with `conan lock create cpp --profile:all cpp/conan/profiles/gcc13 --lockfile-out cpp/conan.lock` and record the new dependency in the register (ICS-010).

Next issue: [ICS-005](https://github.com/MatthewK84/ICS/issues/5) (C++ coding rules in CI), then [ICS-015](https://github.com/MatthewK84/ICS/issues/15) (common).
