#!/usr/bin/env bash
# Run the dynamic CI stages and prove each one catches a seeded defect (ICS-008
# "Done when": seeded defects fail each stage; clean code passes).
#
# Usage: check-dynamic.sh sanitizers
#        check-dynamic.sh fuzz SECONDS
#
#   sanitizers  Builds and tests the ICS code under ASan, UBSan and TSan with
#               GCC 13 and Clang 17; every test must pass. Then each sanitizer's
#               seed in cpp/policy/seeded-runtime must fail with the report
#               named on its first line.
#   fuzz        Builds the clang-fuzz preset and runs every fuzz target listed
#               in <build>/fuzzers.txt for SECONDS from its seed corpus; any
#               crash fails. The seeded fuzz target must then crash within
#               min(SECONDS, 120) seconds.
#
# Crash inputs from real fuzz targets go to $ARTIFACT_DIR (default: a temporary
# folder). Set SKIP_CONAN_INSTALL=1 when dependencies are already installed.
set -euo pipefail

readonly ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly CPP="${ROOT}/cpp"
readonly SEEDS="${CPP}/policy/seeded-runtime"
readonly WORK="$(mktemp -d)"
readonly ARTIFACTS="${ARTIFACT_DIR:-${WORK}/artifacts}"
readonly COMPILERS=(gcc clang)
readonly SANITIZERS=(asan ubsan tsan)
readonly SANITIZER_SEEDS=(asan_heap_overflow.cpp ubsan_signed_overflow.cpp tsan_data_race.cpp)
readonly FUZZ_SEED="fuzz_planted_bug.cpp"
readonly SEED_FUZZ_BUDGET=120
trap 'rm -rf "${WORK}"' EXIT

fail() {
  echo "::error::$*" >&2
  exit 1
}

usage() {
  fail "usage: check-dynamic.sh sanitizers | check-dynamic.sh fuzz SECONDS"
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
  listed="$(printf '%s\n' "${SANITIZER_SEEDS[@]}" "${FUZZ_SEED}" | LC_ALL=C sort)"
  actual="$(find "${SEEDS}" -type f -name '*.cpp' -printf '%f\n' | LC_ALL=C sort)"
  [[ "${listed}" == "${actual}" ]] || fail "check-dynamic.sh must list every seed; listed:"$'\n'"${listed}"$'\n'"found:"$'\n'"${actual}"
}

# Configure and build one preset with the seeds available.
build_preset() {
  local preset="$1"
  if [[ "${SKIP_CONAN_INSTALL:-0}" != "1" ]]; then
    quietly "${ROOT}/deploy/toolchain/conan-install.sh" "${preset}"
  fi
  (cd "${CPP}" && quietly cmake --preset "${preset}" -B "${WORK}/${preset}" -DICS_POLICY_SEEDS=ON)
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

check_sanitizers() {
  local compiler index
  for compiler in "${COMPILERS[@]}"; do
    for index in "${!SANITIZERS[@]}"; do
      check_sanitizer "${compiler}" "${index}"
    done
  done
}

# Fuzz one target for the given time: a fresh working corpus, then the seed corpus if any.
run_fuzzer() {
  local binary="$1" seconds="$2" artifacts="$3" seed_corpus="${4:-}"
  local corpus="${WORK}/corpus/${binary##*/}"
  mkdir -p "${corpus}" "${artifacts}"
  "${binary}" -max_total_time="${seconds}" -timeout=25 -rss_limit_mb=2048 -print_final_stats=1 \
    -artifact_prefix="${artifacts}/${binary##*/}-" "${corpus}" ${seed_corpus:+"${seed_corpus}"}
}

check_fuzzers() {
  local seconds="$1"
  local build="${WORK}/clang-fuzz" binary corpus count=0 output status=0
  build_preset clang-fuzz
  while read -r binary corpus; do
    [[ -n "${binary}" ]] || continue
    quietly run_fuzzer "${binary}" "${seconds}" "${ARTIFACTS}" "${corpus}" \
      || fail "${binary##*/} crashed; the input is in ${ARTIFACTS}"
    echo "Fuzzed ${binary##*/} for ${seconds} s: no crash"
    count=$((count + 1))
  done <"${build}/fuzzers.txt"
  ((count > 0)) || fail "no fuzz targets in ${build}/fuzzers.txt"
  quietly cmake --build "${build}" --target seed_fuzz_planted_bug || fail "the seeded fuzz target does not build"
  output="$(run_fuzzer "${build}/policy/seeded-runtime/seed_fuzz_planted_bug" \
    "$((seconds < SEED_FUZZ_BUDGET ? seconds : SEED_FUZZ_BUDGET))" "${WORK}/seed-artifacts" 2>&1)" || status=$?
  expect_rejected "libFuzzer" "${SEEDS}/${FUZZ_SEED}" "${status}" "${output}"
}

main() {
  local mode="${1:-}"
  check_seed_list
  case "${mode}" in
    sanitizers) check_sanitizers ;;
    fuzz)
      [[ "${2:-}" =~ ^[1-9][0-9]*$ ]] || usage
      check_fuzzers "$2"
      ;;
    *) usage ;;
  esac
  echo "Dynamic checks (${mode}): ok; every seeded defect was caught"
}

main "$@"
