# cpp

Every real-time and production data path: timing, PLI, cameras, mounts, triggering, the processing pipeline, run records and the query API. C++ never does model fitting or exploratory analysis; that belongs in `python/`.

**Language:** C++. **Lead roles:** Systems engineer (timing, PLI, camera I/O), Controls engineer (mount, encoder, tracker, trigger), CV and estimation engineers (registration through footprint).

Planned layout:

```text
cpp/
  common/ logging/ config/ camera_io/ timing/ trigger/ pli/ mount/
  tracker/ encoder/
  endgame/ registration/ detection/ association/ estimation/
  killclass/ footprint/ record/ api/
  services/   ics-timingd ics-plid ics-mountd ics-trigd ics-camd
              ics-api ics-recordd ics-pipeline
```

Rules (enforced in CI by [ICS-005](https://github.com/MatthewK84/ICS/issues/5) and [ICS-008](https://github.com/MatthewK84/ICS/issues/8); see [Power-of-Ten checks](#power-of-ten-checks)):

- No exceptions on real-time paths; fallible calls return `ics::Result` (a `tl::expected`), which is `[[nodiscard]]`. See [Common library](#common-library).
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

## Common library

[`common/`](common) is `ics::common` ([ICS-015](https://github.com/MatthewK84/ICS/issues/15), [ICS-016](https://github.com/MatthewK84/ICS/issues/16)), which every real-time component links:

| Header | Provides |
|---|---|
| [`error.hpp`](common/include/ics/common/error.hpp) | `ics::Error`, one enum of failure codes for all of ICS; `Result<T>` (`tl::expected<T, Error>`), `Status` (`Result<void>`) and `fail(error)`. A result is `[[nodiscard]]`, so ignoring one does not compile. It cannot hold a reference: return a `std::reference_wrapper` |
| [`check.hpp`](common/include/ics/common/check.hpp) | `ics::check(condition)`, the ICS assertion. It returns the condition and, when it is false, writes the caller's file, line and function to stderr without allocating; the caller recovers, normally by returning an error |
| [`static_vector.hpp`](common/include/ics/common/static_vector.hpp), [`ring_buffer.hpp`](common/include/ics/common/ring_buffer.hpp), [`fixed_pool.hpp`](common/include/ics/common/fixed_pool.hpp) | Fixed-capacity containers that never allocate: `StaticVector<T, N>`, the first-in, first-out `RingBuffer<T, N>`, and `FixedPool<T, N>`, whose handles carry a generation so a released one is refused. A full, empty, out-of-range or stale case returns an error; nothing throws or overwrites |
| [`storable.hpp`](common/include/ics/common/storable.hpp) | `Storable`, what the containers need from an element type: default construction, move construction and move assignment that cannot throw |
| [`units.hpp`](common/include/ics/common/units.hpp) | Strong unit types: `Meters`, `Radians` and `Degrees` are a `double` tagged with its unit, so mixing units, or passing a bare `double`, does not compile; `to_radians`, `to_degrees` and `wrap_to_pi` convert. Time is `std::chrono`: `UtcTime` (`sys_time<nanoseconds>`, POSIX-counted UTC as in [docs/frames-and-time.md](../docs/frames-and-time.md)) and `Duration` (`nanoseconds`), with `utc_from_ns` and `to_utc_ns` for `_utc_ns` fields |

How to use them:

- Use `ics::check` for a condition that holds unless there is a bug or an overload the system was sized to avoid, such as a push to a full buffer. An outcome the caller expects, such as a polling reader finding the ring buffer empty, is an ordinary branch.
- The containers are not thread-safe.
- Tests instantiate each container template explicitly (`template class ics::RingBuffer<int, 3>;`), so a member no test calls shows up as uncovered.
- The headers build with `-fno-exceptions`, as real-time targets will; [`common/test/no_exceptions_check.cpp`](common/test/no_exceptions_check.cpp) proves it.

## Logging

[`logging/`](logging) is `ics::logging` (ICS-016). A service makes one `Logger` at start-up, with `Logger::to_stderr(service, threshold)`, and passes it by reference; there is no global logger. Each call writes one JSON line through spdlog to stderr, which journald captures:

```cpp
logger.warn("frame_dropped", {{"camera", "north"}, {"count", 3}});
// {"ts":"2026-10-01T09:53:50.123456789Z","level":"warn","service":"ics-camd","event":"frame_dropped","camera":"north","count":3}
```

- Field values are integers, floating-point numbers, booleans or text. Keys are `lower_snake_case` and must not repeat `ts`, `level`, `service` or `event`; a field with a bad key fails an `ics::check` and is left out.
- Logging formats text and may allocate, so it is not for real-time paths. Real-time code counts or queues what happened, and a non-real-time thread logs it.

## Config

[`config/`](config) is `ics::config` (ICS-016). A service reads its TOML file once at start-up against a schema written as code, and exits when it is invalid, naming every bad field:

```cpp
const auto config = ics::config::read_file(path, &ics::config::read_logging);
if (!config) {
  std::fputs(ics::config::format_errors(path.string(), config.error()).c_str(), stderr);
  return EXIT_FAILURE;
}
// app.toml: log.level: must be one of debug, info, warn, error, not "verbose"
// app.toml: log.colour: is not a known setting
```

- A schema is a function that takes a `Reader&`. Its getters check type and range, and a setting with a unit must carry the unit in its name, as proto fields do: `duration("timeout_ns", …)`, `meters`, `radians`, `degrees`. Every setting is required, and one the schema never reads is reported as unknown, since it is likely a typo.
- Files are written in a TOML subset, defined in [`subset.hpp`](config/include/ics/config/subset.hpp): printable ASCII, tables of bare keys, and settings holding text, decimal numbers, booleans or one-line lists of those. `parse()` checks the subset before toml++ reads the text. Fuzzing found that toml++ 3.4.0 has undefined behaviour on some invalid TOML, such as a non-ASCII character or `=` straight after a table's `[`, and its unreleased main branch and toml11 4.4.0 failed fuzzing too.
- [`logging_config.hpp`](config/include/ics/config/logging_config.hpp) reads the `[log]` table every service has: `level` and `service`.

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

Not enforced here: "no exceptions on real-time paths" needs `-fno-exceptions` on the real-time targets once they exist; the common headers already build that way. Coverage is gated only for the code listed in [`policy/coverage-gates.txt`](policy/coverage-gates.txt) (see [Test stages](#test-stages)), so other code is not yet measured against the Definition of Done's 90%.

## Test stages

The dynamic stages run in CI on every change to `cpp/` (ICS-008), through [`policy/check-dynamic.sh`](policy/check-dynamic.sh):

| Stage | How | Seeded defect it must catch |
|---|---|---|
| AddressSanitizer | `gcc-asan` and `clang-asan` presets; every test must pass | a heap buffer overflow |
| UndefinedBehaviorSanitizer | `gcc-ubsan` and `clang-ubsan` presets, stopping at the first undefined behaviour | a signed integer overflow |
| ThreadSanitizer | `gcc-tsan` and `clang-tsan` presets | a data race |
| libFuzzer | `clang-fuzz` preset (libFuzzer with ASan and UBSan): one minute per fuzz target per change, and an hour per target nightly ([`fuzz-nightly.yml`](../.github/workflows/fuzz-nightly.yml)) | a heap overflow behind a four-byte magic prefix, which must be found within two minutes |
| Coverage (ICS-015) | `clang-coverage` preset (Clang source-based coverage): every line and branch of each path in [`policy/coverage-gates.txt`](policy/coverage-gates.txt) covered, and its assertion density at or above its floor | a branch no test takes, and a function with no `ics::check` under a floor of 1 |

```sh
cpp/policy/check-dynamic.sh sanitizers
cpp/policy/check-dynamic.sh fuzz 60        # seconds per fuzz target
cpp/policy/check-dynamic.sh coverage
```

The assertion density of a gated path is the number of `ics::check` calls in its functions longer than three lines, divided by the number of those functions. Power of Ten rule 5 asks for two per function; container code, with one failure mode per operation, sits near 0.5, and logging and config, where bad input is an expected outcome rather than a bug, sit lower. So each path has a floor that only rises: CI fails below it, and asks for the floor to be raised when the density rises above it. [`.github/scripts/cpp_coverage.py`](../.github/scripts/cpp_coverage.py) reads function extents from `llvm-cov export`, so a template member counts only once some test instantiates it. Add a library to the gates file when it is written, at its measured density. Test and fuzz folders are never gated. Comparisons in gated code are written out rather than defaulted, because Clang 17's coverage miscounts the branches of a defaulted comparison.

The seeded defects live in [`policy/seeded-runtime/`](policy/seeded-runtime), each naming on its first line the report it must produce.

The `asan`, `tsan` and `fuzz` presets link dependencies built with the same sanitizer, from the [`asan`](conan/profiles/asan) and [`tsan`](conan/profiles/tsan) Conan profiles: protobuf and abseil annotate their containers for ASan only when they are built with it, and TSan cannot see synchronization in uninstrumented code. Mixing instrumented and uninstrumented code gives false reports. The `ubsan` presets use the plain debug dependencies, since UBSan checks only the code it instruments. A build folder configured before this change keeps its old toolchain file; configure it again with `cmake --preset <name> --fresh`.

Add a fuzz target with `ics_add_fuzzer(name SOURCES … LIBRARIES … CORPUS folder)` from [`cmake/Fuzzing.cmake`](cmake/Fuzzing.cmake), with a few small inputs in the corpus folder; see [`toolchain_check/fuzz`](toolchain_check/fuzz). Every build compiles fuzz sources, so the warnings, clang-tidy and cppcheck cover them; the `clang-fuzz` preset links them with libFuzzer. A nightly crash fails the run, and the crashing input is uploaded as the `fuzz-crashes` artifact; reproduce it with the fuzz target and the input file as its only argument.

CodeQL analyzes the C++ code too, with the Python and TypeScript code; see [`.github/workflows/codeql.yml`](../.github/workflows/codeql.yml).

Next issue: [ICS-017](https://github.com/MatthewK84/ICS/issues/17) (common frames).
