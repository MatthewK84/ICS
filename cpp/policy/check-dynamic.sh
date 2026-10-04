#!/usr/bin/env bash
# Run the dynamic CI stages and prove each one catches a seeded defect (ICS-008
# "Done when": seeded defects fail each stage; clean code passes).
#
# Usage: check-dynamic.sh sanitizers [PRESET]
#        check-dynamic.sh fuzz SECONDS
#        check-dynamic.sh coverage
#
#   sanitizers  Builds and tests the ICS code under ASan, UBSan and TSan with
#               GCC 13 and Clang 17; every test must pass. Then each sanitizer's
#               seed in cpp/policy/seeded-runtime must fail with the report
#               named on its first line. PRESET, such as gcc-asan, checks only
#               that one, so CI can run the six side by side.
#   fuzz        Builds the fuzz targets listed in <build>/fuzzers.txt with the
#               clang-fuzz preset and runs each for SECONDS from its seed
#               corpus, as many at once as there are processors; any crash
#               fails. The seeded fuzz target must then crash within
#               min(SECONDS, 120) seconds.
#   coverage    Builds the clang-coverage preset, runs every test and gates the
#               code listed in cpp/policy/coverage-gates.txt: full line and
#               branch coverage, and an assertion density at or above its
#               floor (ICS-015; see .github/scripts/cpp_coverage.py). Each
#               coverage seed must then be rejected.
#
# Crash inputs from real fuzz targets go to $ARTIFACT_DIR (default: a temporary
# folder). Set SKIP_CONAN_INSTALL=1 when dependencies are already installed.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly ROOT
readonly CPP="${ROOT}/cpp"
readonly SEEDS="${CPP}/policy/seeded-runtime"
WORK="$(mktemp -d)"
readonly WORK
readonly ARTIFACTS="${ARTIFACT_DIR:-${WORK}/artifacts}"
readonly COMPILERS=(gcc clang)
readonly SANITIZERS=(asan ubsan tsan)
readonly SANITIZER_SEEDS=(asan_heap_overflow.cpp ubsan_signed_overflow.cpp tsan_data_race.cpp)
FUZZ_JOBS="$(nproc)"
readonly FUZZ_JOBS
readonly FUZZ_SEED="fuzz_planted_bug.cpp"
readonly SEED_FUZZ_BUDGET=120
readonly COVERAGE_PRESET="clang-coverage"
readonly COVERAGE_GATES="${CPP}/policy/coverage-gates.txt"
readonly COVERAGE_SCRIPT="${ROOT}/.github/scripts/cpp_coverage.py"
# cpp/cmake/Reproducible.cmake records ICS sources as build/src/<path under cpp>.
readonly RECORDED_SOURCES="build/src=cpp"
readonly COVERAGE_SEEDS=(coverage_gap.cpp assertion_density_drop.cpp)
# The assertion-density floor each coverage seed is gated at.
readonly COVERAGE_SEED_FLOORS=(0 1)
trap 'rm -rf "${WORK}"' EXIT

fail() {
  echo "::error::$*" >&2
  exit 1
}

usage() {
  fail "usage: check-dynamic.sh sanitizers [PRESET] | check-dynamic.sh fuzz SECONDS | check-dynamic.sh coverage"
}

# Run a command, showing its output only if it fails.
quietly() {
  local log="${WORK}/quietly.log"
  if ! "$@" >"${log}" 2>&1; then
    tail -n 60 "${log}" >&2
    return 1
  fi
}

# Fail unless a seed was rejected with the pattern on its first line.
expect_rejected() {
  local checker="$1" seed="$2" status="$3" output="$4"
  local pattern
  pattern="$(sed -n '1s|^// expect: ||p' "${seed}")"
  [[ -n "${pattern}" ]] || fail "${seed#"${ROOT}/"} has no '// expect:' first line"
  if [[ "${status}" -eq 0 ]]; then
    fail "${checker} accepted the seeded defect in ${seed#"${ROOT}/"}"
  fi
  if ! grep -qE -e "${pattern}" <<<"${output}"; then
    tail -n 40 <<<"${output}" >&2
    fail "${checker} rejected ${seed#"${ROOT}/"}, but not with '${pattern}'"
  fi
  echo "Rejected by ${checker}: ${seed#"${SEEDS}/"}"
}

# Every seed file must be in exactly one of the lists above.
check_seed_list() {
  local listed actual
  listed="$(printf '%s\n' "${SANITIZER_SEEDS[@]}" "${FUZZ_SEED}" "${COVERAGE_SEEDS[@]}" | LC_ALL=C sort)"
  actual="$(find "${SEEDS}" -type f -name '*.cpp' -printf '%f\n' | LC_ALL=C sort)"
  [[ "${listed}" == "${actual}" ]] || fail "check-dynamic.sh must list every seed; listed:"$'\n'"${listed}"$'\n'"found:"$'\n'"${actual}"
}

# Configure one preset with the seeds available.
configure_preset() {
  local preset="$1"
  if [[ "${SKIP_CONAN_INSTALL:-0}" != "1" ]]; then
    quietly "${ROOT}/deploy/toolchain/conan-install.sh" "${preset}"
  fi
  (cd "${CPP}" && quietly cmake --preset "${preset}" -B "${WORK}/${preset}" -DICS_POLICY_SEEDS=ON)
}

# Configure and build one preset with the seeds available.
build_preset() {
  local preset="$1"
  configure_preset "${preset}"
  quietly cmake --build "${WORK}/${preset}" || fail "the ICS code does not build under ${preset}"
}

check_sanitizer() {
  local compiler="$1" index="$2"
  local preset="${compiler}-${SANITIZERS[index]}" seed="${SANITIZER_SEEDS[index]}"
  local output status=0
  build_preset "${preset}"
  quietly ctest --test-dir "${WORK}/${preset}" --output-on-failure || fail "tests fail under ${preset}"
  echo "Clean code: ok under ${preset}"
  quietly cmake --build "${WORK}/${preset}" --target "seed_${seed%.cpp}" || fail "seed_${seed%.cpp} does not build"
  output="$("${WORK}/${preset}/policy/seeded-runtime/seed_${seed%.cpp}" 2>&1)" || status=$?
  expect_rejected "${preset}" "${SEEDS}/${seed}" "${status}" "${output}"
}

# Every sanitizer preset, or only the one named.
check_sanitizers() {
  local only="${1:-}" compiler index checked=0
  for compiler in "${COMPILERS[@]}"; do
    for index in "${!SANITIZERS[@]}"; do
      if [[ -z "${only}" || "${only}" == "${compiler}-${SANITIZERS[index]}" ]]; then
        check_sanitizer "${compiler}" "${index}"
        checked=$((checked + 1))
      fi
    done
  done
  ((checked > 0)) || usage
}

# Fuzz one target for the given time: a fresh working corpus, then the seed corpus if any.
run_fuzzer() {
  local binary="$1" seconds="$2" artifacts="$3" seed_corpus="${4:-}"
  local corpus="${WORK}/corpus/${binary##*/}"
  mkdir -p "${corpus}" "${artifacts}"
  "${binary}" -max_total_time="${seconds}" -timeout=25 -rss_limit_mb=2048 -print_final_stats=1 \
    -artifact_prefix="${artifacts}/${binary##*/}-" "${corpus}" ${seed_corpus:+"${seed_corpus}"}
}

# Wait for one running fuzz target, named in the caller's names array by
# process ID, and fail if it crashed.
reap_fuzzer() {
  local seconds="$1" finished="" status=0
  wait -n -p finished || status=$?
  local name="${names[${finished}]}"
  if ((status != 0)); then
    tail -n 60 "${WORK}/${name}.log" >&2
    fail "${name} crashed; the input is in ${ARTIFACTS}"
  fi
  echo "Fuzzed ${name} for ${seconds} s: no crash"
}

check_fuzzers() {
  local seconds="$1"
  local build="${WORK}/clang-fuzz" binary corpus count=0 running=0 output status=0
  local -A names=()
  local -a targets=()
  configure_preset clang-fuzz
  while read -r binary corpus; do
    if [[ -n "${binary}" ]]; then
      targets+=("${binary##*/}")
    fi
  done <"${build}/fuzzers.txt"
  ((${#targets[@]} > 0)) || fail "no fuzz targets in ${build}/fuzzers.txt"
  quietly cmake --build "${build}" --target "${targets[@]}" seed_fuzz_planted_bug \
    || fail "the fuzz targets do not build under clang-fuzz"
  while read -r binary corpus; do
    [[ -n "${binary}" ]] || continue
    if ((running >= FUZZ_JOBS)); then
      reap_fuzzer "${seconds}"
      running=$((running - 1))
    fi
    run_fuzzer "${binary}" "${seconds}" "${ARTIFACTS}" "${corpus}" >"${WORK}/${binary##*/}.log" 2>&1 &
    names[$!]="${binary##*/}"
    running=$((running + 1))
    count=$((count + 1))
  done <"${build}/fuzzers.txt"
  while ((running > 0)); do
    reap_fuzzer "${seconds}"
    running=$((running - 1))
  done
  echo "Fuzzed ${count} targets, ${FUZZ_JOBS} at a time"
  output="$(run_fuzzer "${build}/policy/seeded-runtime/seed_fuzz_planted_bug" \
    "$((seconds < SEED_FUZZ_BUDGET ? seconds : SEED_FUZZ_BUDGET))" "${WORK}/seed-artifacts" 2>&1)" || status=$?
  expect_rejected "libFuzzer" "${SEEDS}/${FUZZ_SEED}" "${status}" "${output}"
}

# Merge the raw profiles in a folder, then export the coverage of the given
# binaries as JSON.
export_coverage() {
  local profiles="$1" output="$2" binary objects=()
  shift 2
  for binary in "${@:2}"; do
    objects+=(-object "${binary}")
  done
  quietly llvm-profdata-17 merge -sparse -o "${profiles}.profdata" "${profiles}"/*.profraw \
    || fail "no coverage profiles in ${profiles}"
  llvm-cov-17 export -format=text -instr-profile="${profiles}.profdata" "$1" "${objects[@]}" >"${output}" \
    || fail "llvm-cov could not export ${profiles}.profdata"
}

# Gate a coverage export; extra arguments choose the gates.
gate_coverage() {
  local export="$1"
  shift
  python3 "${COVERAGE_SCRIPT}" check "${export}" --root "${ROOT}" --path-map "${RECORDED_SOURCES}" "$@"
}

check_coverage_seed() {
  local index="$1"
  local seed="${COVERAGE_SEEDS[index]}" build="${WORK}/${COVERAGE_PRESET}"
  local target="seed_${seed%.cpp}" profiles="${WORK}/seed-profiles-${index}" output status=0
  quietly cmake --build "${build}" --target "${target}" || fail "${target} does not build"
  quietly env LLVM_PROFILE_FILE="${profiles}/%p.profraw" "${build}/policy/seeded-runtime/${target}" \
    || fail "${target} does not run"
  export_coverage "${profiles}" "${profiles}.json" "${build}/policy/seeded-runtime/${target}"
  output="$(gate_coverage "${profiles}.json" --gate "cpp/policy/seeded-runtime/${seed}" \
    "${COVERAGE_SEED_FLOORS[index]}" 2>&1)" || status=$?
  expect_rejected "the coverage gate" "${SEEDS}/${seed}" "${status}" "${output}"
}

check_coverage() {
  local build="${WORK}/${COVERAGE_PRESET}" binaries=() index
  build_preset "${COVERAGE_PRESET}"
  quietly env LLVM_PROFILE_FILE="${WORK}/profiles/%p-%m.profraw" ctest --test-dir "${build}" --output-on-failure \
    || fail "tests fail under ${COVERAGE_PRESET}"
  mapfile -t binaries < <(ctest --test-dir "${build}" --show-only=json-v1 | python3 "${COVERAGE_SCRIPT}" binaries)
  ((${#binaries[@]} > 0)) || fail "ctest lists no test binaries under ${COVERAGE_PRESET}"
  export_coverage "${WORK}/profiles" "${WORK}/coverage.json" "${binaries[@]}"
  gate_coverage "${WORK}/coverage.json" --gates "${COVERAGE_GATES}" \
    || fail "coverage or assertion density is short of cpp/policy/coverage-gates.txt; see above"
  echo "Clean code: ok under ${COVERAGE_PRESET}"
  for index in "${!COVERAGE_SEEDS[@]}"; do
    check_coverage_seed "${index}"
  done
}

main() {
  local mode="${1:-}"
  check_seed_list
  case "${mode}" in
    sanitizers) check_sanitizers "${2:-}" ;;
    fuzz)
      [[ "${2:-}" =~ ^[1-9][0-9]*$ ]] || usage
      check_fuzzers "$2"
      ;;
    coverage) check_coverage ;;
    *) usage ;;
  esac
  echo "Dynamic checks (${mode}): ok; every seeded defect was caught"
}

main "$@"
