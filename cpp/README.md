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

Rules (enforced in CI by [ICS-005](https://github.com/MatthewK84/ICS/issues/5) and [ICS-008](https://github.com/MatthewK84/ICS/issues/8); see [Power-of-Ten checks](#power-of-ten-checks)):

- No exceptions on real-time paths; fallible calls return `tl::expected` and are `[[nodiscard]]`.
- No recursion, functions of 50 lines or fewer, no owning raw pointers, no mutable globals, no `NOLINT`.
- No allocation after initialization on real-time paths.
- Mount motion, the sun interlock and triggering live only here, never in the web UI.
- Python parameter files and ONNX models are loaded at startup, and their SHA-256 hashes go into every run record.

## Build

Build inside the `ics-cpp` image (see [deploy/toolchain](../deploy/toolchain/README.md)). Each preset in [`CMakePresets.json`](CMakePresets.json) pairs a compiler, `gcc` or `clang`, with a variant, `debug`, `release`, `asan`, `ubsan` or `tsan`; `clang-fuzz` builds the fuzz targets:

```sh
deploy/toolchain/conan-install.sh gcc-release   # dependencies from cpp/conan.lock
cd cpp
cmake --preset gcc-release
cmake --build --preset gcc-release
ctest --preset gcc-release
```

Builds are reproducible: `deploy/toolchain/check-reproducible.sh gcc` (or `clang`) builds the `toolchain_check` sample twice and requires byte-identical outputs. The flags that make this work are in [`cmake/Reproducible.cmake`](cmake/Reproducible.cmake).

The protobuf messages generated from [`proto/`](../proto/README.md) are the `ics::proto` library in [`proto/`](proto/CMakeLists.txt). Its code is generated and committed, never edited, and it is built without the ICS warning flags, like a dependency.

Dependencies are pinned in `conan.lock`. After changing `conanfile.py`, regenerate it with `conan lock create cpp --profile:all cpp/conan/profiles/gcc13 --lockfile-out cpp/conan.lock` and record the new dependency in the [dependency register](../docs/dependency-register.md).

## Power-of-Ten checks

The C++ rules from the coding-standards table in [docs/build-plan.md](../docs/build-plan.md) are enforced by tools, not review (ICS-005):

| Rule | Enforced by |
|---|---|
| No goto; no recursion; functions of 50 lines or fewer; no owning raw pointers or malloc; `std::span` over pointer arithmetic; no mutable globals | clang-tidy, configured in [`.clang-tidy`](.clang-tidy); every finding is an error |
| `[[nodiscard]]` results are used; no implicit narrowing | `-Wall -Wextra -Wpedantic -Wconversion -Werror` for GCC and Clang, in [`cmake/Warnings.cmake`](cmake/Warnings.cmake) |
| Defects such as out-of-bounds access or uninitialized reads | cppcheck 2.13 (warning, style, performance and portability checks) over the whole build; every finding is an error |
| No suppressions | [`policy/check-policy.sh`](policy/check-policy.sh) rejects `NOLINT`, `cppcheck-suppress`, `diagnostic ignored` pragmas, `-Wno-` flags and any extra `.clang-tidy` file |
| No allocation after initialization | [`ics::testing::NoAllocationScope`](testing/alloc_guard/include/ics/testing/no_allocation_scope.hpp) fails a test that allocates inside it |

Run all of it the way CI does, inside the `ics-cpp` image:

```sh
cpp/policy/check-policy.sh
```

The script also proves each rule still fires: every file in [`policy/seeded/`](policy/seeded) breaks exactly one rule and must be rejected with the diagnostic named on its first line. The seeds build only with `-DICS_POLICY_SEEDS=ON`.

Add tests with `ics_add_gtest(name SOURCES … LIBRARIES …)` from [`cmake/Testing.cmake`](cmake/Testing.cmake). It links GoogleTest and the allocation guard, which replaces the global `operator new` and `delete` so that allocations are counted. Open a `NoAllocationScope` once a component is initialized and run its steady-state path inside it.

Two exceptions are deliberate:

- The allocation hook, [`testing/alloc_guard/src/allocation_hook.cpp`](testing/alloc_guard/src/allocation_hook.cpp), is the only file allowed to call `malloc`; its own `.clang-tidy` turns off `cppcoreguidelines-no-malloc` there and nowhere else. It marks ownership with `gsl::owner`, so `cppcoreguidelines-owning-memory` still applies.
- The `clang-tsan` preset links Clang's shared TSan runtime (`-shared-libsan`), because the static runtime defines `operator new` and `delete` itself and would clash with the allocation hook. GCC's TSan runtime is already shared.

Not enforced here: "no exceptions on real-time paths" needs `-fno-exceptions` on the real-time targets once they exist (ICS-015 onward), and CI does not yet measure C++ test coverage against the Definition of Done's 90%.

## Test stages

The dynamic stages run in CI on every change to `cpp/` (ICS-008), through [`policy/check-dynamic.sh`](policy/check-dynamic.sh):

| Stage | How | Seeded defect it must catch |
|---|---|---|
| AddressSanitizer | `gcc-asan` and `clang-asan` presets; every test must pass | a heap buffer overflow |
| UndefinedBehaviorSanitizer | `gcc-ubsan` and `clang-ubsan` presets, stopping at the first undefined behaviour | a signed integer overflow |
| ThreadSanitizer | `gcc-tsan` and `clang-tsan` presets | a data race |
| libFuzzer | `clang-fuzz` preset (libFuzzer with ASan and UBSan): one minute per fuzz target per change, and an hour per target nightly ([`fuzz-nightly.yml`](../.github/workflows/fuzz-nightly.yml)) | a heap overflow behind a four-byte magic prefix, which must be found within two minutes |

```sh
cpp/policy/check-dynamic.sh sanitizers
cpp/policy/check-dynamic.sh fuzz 60        # seconds per fuzz target
```

The seeded defects live in [`policy/seeded-runtime/`](policy/seeded-runtime), each naming on its first line the report it must produce.

Add a fuzz target with `ics_add_fuzzer(name SOURCES … LIBRARIES … CORPUS folder)` from [`cmake/Fuzzing.cmake`](cmake/Fuzzing.cmake), with a few small inputs in the corpus folder; see [`toolchain_check/fuzz`](toolchain_check/fuzz). Every build compiles fuzz sources, so the warnings, clang-tidy and cppcheck cover them; the `clang-fuzz` preset links them with libFuzzer. A nightly crash fails the run, and the crashing input is uploaded as the `fuzz-crashes` artifact; reproduce it with the fuzz target and the input file as its only argument.

CodeQL analyzes the C++ code too, with the Python and TypeScript code; see [`.github/workflows/codeql.yml`](../.github/workflows/codeql.yml).

Next issue: [ICS-009](https://github.com/MatthewK84/ICS/issues/9) (CI evidence: SBOMs, signing and provenance), then [ICS-015](https://github.com/MatthewK84/ICS/issues/15) (common).
