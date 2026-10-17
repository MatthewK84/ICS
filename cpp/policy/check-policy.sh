#!/usr/bin/env bash
# Enforce the C++ Power-of-Ten profile and prove that it rejects violations
# (ICS-005 "Done when": CI fails on one seeded violation of each rule).
#
# Usage: check-policy.sh [gcc|clang]
#
#   1. No suppressions: no NOLINT or "diagnostic ignored" pragma in C++
#      files, no -Wno- flag in CMake files, and no .clang-tidy
#      file beyond the two allowed ones.
#   2. Clean code: the ICS code builds with GCC 13 and Clang 17 with every
#      warning an error, and clang-tidy finds nothing.
#   3. Seeds: every file in cpp/policy/seeded is rejected, each with the
#      diagnostic named on its first line, so a rule that silently stops
#      firing fails too.
#   4. JSF AV (ICS-037): the C++ under cpp/policy/jsf-paths.txt keeps the JSF
#      AV rules .github/scripts/cpp_jsf.py and cmake/Jsf.cmake check, and
#      every seed in cpp/policy/seeded/jsf is rejected by them. See
#      docs/cpp-jsf-av.md.
#
# With no argument it checks everything. "gcc" checks the GCC build and its
# seeds; "clang" the Clang build, clang-tidy and their seeds. CI runs "clang"
# (#159): every build already holds GCC to -Werror, the reproducible one in CI.
# Both check the suppressions.
#
# Set SKIP_CONAN_INSTALL=1 when dependencies are already installed.
set -euo pipefail

readonly ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
readonly CPP="${ROOT}/cpp"
readonly SEEDS="${CPP}/policy/seeded"
readonly WORK="$(mktemp -d)"
readonly ALLOWED_TIDY_CONFIGS="cpp/.clang-tidy
cpp/testing/alloc_guard/src/.clang-tidy"
readonly CODE_SUPPRESSION='NOLINT|diagnostic[[:space:]]+ignored'
readonly BUILD_SUPPRESSION='-Wno-'
readonly SCAN_SEEDS=(nolint.cpp pragma_diagnostic.cpp warning_off.cmake)
readonly TIDY_SEEDS=(goto recursion function_size owning_memory malloc pointer_arithmetic mutable_global)
readonly COMPILER_SEEDS=(conversion nodiscard)
readonly ALLOCATION_SEED="post_init_allocation_test.cpp"
readonly JSF_GATE="${ROOT}/.github/scripts/cpp_jsf.py"
readonly JSF_PATHS="${CPP}/policy/jsf-paths.txt"
# Rejected by the JSF gate, in cpp/policy/seeded/jsf.
readonly JSF_SEEDS=(av3_complexity.cpp av9_non_ascii.cpp av11_trigraph.cpp av13_wide_string.cpp av22_stdio.cpp
  av24_abort.cpp av26_pragma.h av33_include_quotes.cpp av35_no_guard.h av41_long_line.cpp
  av42_two_statements.cpp av50_type_name.cpp av52_constant_case.cpp av53_extension.hpp
  av57_access_order.cpp av58_parameters.cpp av59_braces.cpp av61_brace.cpp av99_namespaces.cpp
  av107_block_scope_function.cpp av126_block_comment.cpp av127_commented_code.cpp av133_intro.cpp
  av149_octal.cpp av151_magic_number.cpp av154_bit_field.cpp av157_side_effect.cpp av158_parentheses.cpp
  av160_assignment.cpp av163_unsigned.cpp av170_indirection.cpp av190_continue.cpp av191_break.cpp
  av192_final_else.cpp av200_for.cpp av201_counter.cpp av208_throw.cpp av209_basic_type.cpp)
# Rejected by the JSF warnings in cmake/Jsf.cmake, with either compiler.
readonly JSF_COMPILER_SEEDS=(av185_old_style_cast av202_float_equal)
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
    "${ALLOCATION_SEED}" "${JSF_SEEDS[@]}" "${JSF_COMPILER_SEEDS[@]/%/.cpp}" | LC_ALL=C sort)"
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

# clang-tidy over the Clang build's compile commands.
check_static_analysis() {
  quietly run-clang-tidy-17 -quiet -p "${WORK}/clang" '^(?!.*/(policy/seeded|proto/gen/))' \
    || fail "clang-tidy found violations in the ICS code"
  echo "Clean code: ok (clang-tidy)"
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

check_jsf_compiler_seeds() {
  local compiler="$1" seed output status
  for seed in "${JSF_COMPILER_SEEDS[@]}"; do
    status=0
    output="$(cmake --build "${WORK}/${compiler}" --target "seed_${seed}" 2>&1)" || status=$?
    expect_rejected "${compiler} JSF warnings" "${SEEDS}/jsf/${seed}.cpp" "${status}" "${output}"
  done
}

# The JSF AV code keeps the rules, and every JSF seed is rejected.
check_jsf() {
  local seed output status
  python3 "${JSF_GATE}" check --root "${ROOT}" --build "${WORK}/clang" --paths "${JSF_PATHS}" \
    || fail "the JSF AV code breaks a rule; see docs/cpp-jsf-av.md"
  echo "Clean code: ok (JSF AV)"
  for seed in "${JSF_SEEDS[@]}"; do
    status=0
    output="$(python3 "${JSF_GATE}" check --root "${ROOT}" --build "${WORK}/clang" --files "${SEEDS}/jsf/${seed}" 2>&1)" \
      || status=$?
    expect_rejected "the JSF gate" "${SEEDS}/jsf/${seed}" "${status}" "${output}"
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
  check_jsf_compiler_seeds gcc
}

check_clang() {
  build_clean clang
  check_static_analysis
  check_tidy_seeds
  check_compiler_seeds clang
  check_jsf_compiler_seeds clang
  check_allocation_seed
  check_jsf
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
  echo "Power-of-Ten and JSF AV policy${half:+ (${half})}: ok; every seeded violation was rejected"
}

main "$@"
