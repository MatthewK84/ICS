#!/usr/bin/env bash
# Enforce the C++ Power-of-Ten profile and prove that it rejects violations
# (ICS-005 "Done when": CI fails on one seeded violation of each rule; ICS-008
# adds cppcheck).
#
# Usage: check-policy.sh [gcc|clang]
#
#   1. No suppressions: no NOLINT, cppcheck-suppress or "diagnostic ignored"
#      pragma in C++ files, no -Wno- flag in CMake files, and no .clang-tidy
#      file beyond the two allowed ones.
#   2. Clean code: the ICS code builds with GCC 13 and Clang 17 with every
#      warning an error, and clang-tidy and cppcheck find nothing.
#   3. Seeds: every file in cpp/policy/seeded is rejected, each with the
#      diagnostic named on its first line, so a rule that silently stops
#      firing fails too.
#
# With no argument it checks everything. "gcc" checks the GCC build and its
# seeds; "clang" the Clang build, clang-tidy, cppcheck and their seeds, so CI
# can run the two side by side. Both check the suppressions.
#
# Set SKIP_CONAN_INSTALL=1 when dependencies are already installed.
set -euo pipefail

readonly ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly CPP="${ROOT}/cpp"
readonly SEEDS="${CPP}/policy/seeded"
readonly WORK="$(mktemp -d)"
readonly ALLOWED_TIDY_CONFIGS="cpp/.clang-tidy
cpp/testing/alloc_guard/src/.clang-tidy"
readonly CODE_SUPPRESSION='NOLINT|cppcheck-suppress|diagnostic[[:space:]]+ignored'
readonly BUILD_SUPPRESSION='-Wno-'
readonly SCAN_SEEDS=(nolint.cpp pragma_diagnostic.cpp warning_off.cmake)
readonly TIDY_SEEDS=(goto recursion function_size owning_memory malloc pointer_arithmetic mutable_global)
readonly COMPILER_SEEDS=(conversion nodiscard)
readonly CPPCHECK_SEEDS=(cppcheck_out_of_bounds.cpp)
# Warnings and style findings are errors; inline suppressions stay disabled.
readonly CPPCHECK_ARGS=(--enable=warning,style,performance,portability --library=googletest --error-exitcode=1 --quiet)
readonly ALLOCATION_SEED="post_init_allocation_test.cpp"
trap 'rm -rf "${WORK}"' EXIT

fail() {
  echo "::error::$*" >&2
  exit 1
}

# Run a command, showing its output only if it fails.
quietly() {
  local log="${WORK}/quietly.log"
  if ! "$@" >"${log}" 2>&1; then
    cat "${log}" >&2
    return 1
  fi
}

# Print each suppression marker in the given files or folders as file:line:text.
find_suppressions() {
  # proto/gen is generated protobuf code (ICS-011), not ICS-authored.
  local skip=(--exclude-dir=build --exclude-dir=seeded --exclude-dir=gen)
  grep -rnHE "${skip[@]}" --include='*.cpp' --include='*.hpp' --include='*.h' --include='*.cu' \
    -e "${CODE_SUPPRESSION}" "$@" || true
  grep -rnHE "${skip[@]}" --include='*.cmake' --include='CMakeLists.txt' --include='CMakePresets.json' \
    -e "${BUILD_SUPPRESSION}" "$@" || true
}

# Fail unless a seed was rejected with the pattern on its first line.
expect_rejected() {
  local checker="$1" seed="$2" status="$3" output="$4"
  local pattern
  pattern="$(sed -n '1s/^\(\/\/\|#\) expect: //p' "${seed}")"
  [[ -n "${pattern}" ]] || fail "${seed#"${ROOT}/"} has no 'expect:' first line"
  if [[ "${status}" -eq 0 ]]; then
    fail "${checker} accepted the seeded violation in ${seed#"${ROOT}/"}"
  fi
  if ! grep -qE -e "${pattern}" <<<"${output}"; then
    echo "${output}" >&2
    fail "${checker} rejected ${seed#"${ROOT}/"}, but not with '${pattern}'"
  fi
  echo "Rejected by ${checker}: ${seed#"${SEEDS}/"}"
}

check_no_suppressions() {
  local found configs
  found="$(find_suppressions "${CPP}")"
  [[ -z "${found}" ]] || fail "suppressions are not allowed (docs/build-plan.md):"$'\n'"${found}"
  configs="$(cd "${ROOT}" && find cpp -name .clang-tidy -not -path 'cpp/build/*' | LC_ALL=C sort)"
  if [[ "${configs}" != "${ALLOWED_TIDY_CONFIGS}" ]]; then
    fail "only these .clang-tidy files are allowed:"$'\n'"${ALLOWED_TIDY_CONFIGS}"$'\n'"found:"$'\n'"${configs}"
  fi
  echo "No suppressions: ok"
}

# Every seed file must be in exactly one of the lists above.
check_seed_list() {
  local listed actual
  listed="$(printf '%s\n' "${SCAN_SEEDS[@]}" "${TIDY_SEEDS[@]/%/.cpp}" "${COMPILER_SEEDS[@]/%/.cpp}" \
    "${CPPCHECK_SEEDS[@]}" "${ALLOCATION_SEED}" | LC_ALL=C sort)"
  actual="$(cd "${SEEDS}" && find . -type f ! -name CMakeLists.txt -printf '%f\n' | LC_ALL=C sort)"
  [[ "${listed}" == "${actual}" ]] || fail "check-policy.sh must list every seed; listed:"$'\n'"${listed}"$'\n'"found:"$'\n'"${actual}"
}

# Configure a debug build of the ICS code and the seeds.
configure() {
  local compiler="$1"
  if [[ "${SKIP_CONAN_INSTALL:-0}" != "1" ]]; then
    quietly "${ROOT}/deploy/toolchain/conan-install.sh" "${compiler}-debug"
  fi
  (cd "${CPP}" && quietly cmake --preset "${compiler}-debug" -B "${WORK}/${compiler}" -DICS_POLICY_SEEDS=ON)
}

# The ICS code builds with the compiler given, every warning an error.
build_clean() {
  local compiler="$1"
  configure "${compiler}"
  quietly cmake --build "${WORK}/${compiler}" || fail "the ICS code does not build cleanly with ${compiler}"
  echo "Clean code: ok (${compiler} with warnings as errors)"
}

# clang-tidy and cppcheck over the Clang build's compile commands.
check_static_analysis() {
  quietly run-clang-tidy-17 -quiet -p "${WORK}/clang" '^(?!.*/(policy/seeded|proto/gen/))' \
    || fail "clang-tidy found violations in the ICS code"
  quietly cppcheck "${CPPCHECK_ARGS[@]}" --project="${WORK}/clang/compile_commands.json" \
    -i "${CPP}/policy/seeded" -i "${CPP}/policy/seeded-runtime" -i "${CPP}/proto/gen" -i "${WORK}/clang/proto/gen" \
    || fail "cppcheck found defects in the ICS code"
  echo "Clean code: ok (clang-tidy, cppcheck)"
}

check_scan_seeds() {
  local seed output
  for seed in "${SCAN_SEEDS[@]}"; do
    output="$(find_suppressions "${SEEDS}/${seed}")"
    expect_rejected "the suppression scan" "${SEEDS}/${seed}" "$([[ -n "${output}" ]] && echo 1 || echo 0)" "${output}"
  done
}

check_tidy_seeds() {
  local seed output status
  for seed in "${TIDY_SEEDS[@]}"; do
    status=0
    output="$(clang-tidy-17 --quiet -p "${WORK}/clang" "${SEEDS}/${seed}.cpp" 2>&1)" || status=$?
    expect_rejected "clang-tidy" "${SEEDS}/${seed}.cpp" "${status}" "${output}"
  done
}

check_compiler_seeds() {
  local compiler="$1" seed output status
  for seed in "${COMPILER_SEEDS[@]}"; do
    status=0
    output="$(cmake --build "${WORK}/${compiler}" --target "seed_${seed}" 2>&1)" || status=$?
    expect_rejected "${compiler} warnings" "${SEEDS}/${seed}.cpp" "${status}" "${output}"
  done
}

check_cppcheck_seeds() {
  local seed output status
  for seed in "${CPPCHECK_SEEDS[@]}"; do
    status=0
    output="$(cppcheck "${CPPCHECK_ARGS[@]}" --std=c++20 "${SEEDS}/${seed}" 2>&1)" || status=$?
    expect_rejected "cppcheck" "${SEEDS}/${seed}" "${status}" "${output}"
  done
}

check_allocation_seed() {
  local output status=0
  quietly cmake --build "${WORK}/clang" --target seed_post_init_allocation || fail "the allocation seed does not build"
  output="$("${WORK}/clang/policy/seeded/seed_post_init_allocation" 2>&1)" || status=$?
  expect_rejected "the allocation guard" "${SEEDS}/${ALLOCATION_SEED}" "${status}" "${output}"
}

check_gcc() {
  build_clean gcc
  check_compiler_seeds gcc
}

check_clang() {
  build_clean clang
  check_static_analysis
  check_tidy_seeds
  check_compiler_seeds clang
  check_cppcheck_seeds
  check_allocation_seed
}

main() {
  local half="${1:-}"
  [[ "${half}" =~ ^(gcc|clang|)$ ]] || fail "usage: check-policy.sh [gcc|clang]"
  check_no_suppressions
  check_seed_list
  check_scan_seeds
  if [[ "${half}" != "clang" ]]; then
    check_gcc
  fi
  if [[ "${half}" != "gcc" ]]; then
    check_clang
  fi
  echo "Power-of-Ten policy${half:+ (${half})}: ok; every seeded violation was rejected"
}

main "$@"
