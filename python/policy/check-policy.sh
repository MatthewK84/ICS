#!/usr/bin/env bash
# Enforce the Python Power-of-Ten profile's ban on suppressions and prove that
# every check rejects violations (ICS-006 "Done when": CI fails on an untyped
# or recursive function).
#
# Usage: python/policy/check-policy.sh   (after "uv sync --locked" in python/)
#
#   1. No suppressions: no noqa, "type: ignore", "pragma: no cover" or inline
#      mypy or pyright setting in Python files, and no ruff, mypy, pytest or
#      coverage configuration outside python/pyproject.toml.
#   2. Seeds: every file in python/policy/seeded is rejected, each with the
#      message named on its first line, so a check that silently stops firing
#      fails too.
#
# ruff, mypy, the AST checks and pytest run over the real code as separate CI
# steps; see .github/workflows/python-toolchain.yml.
set -euo pipefail

readonly PYTHON_DIR="$(cd "$(dirname "$0")/.." && pwd)"
readonly SEEDS="${PYTHON_DIR}/policy/seeded"
readonly SUPPRESSION='noqa|type:[[:space:]]*ignore|pragma:[[:space:]]*no[[:space:]]+cover|#[[:space:]]*(mypy|pyright):'
readonly CONFIG_NAMES=(ruff.toml .ruff.toml mypy.ini .mypy.ini setup.cfg tox.ini pytest.ini .coveragerc pyproject.toml)
readonly SCAN_SEEDS=(noqa.py type_ignore.py)
readonly MYPY_SEEDS=(untyped.py)
readonly RUFF_SEEDS=(bare_except.py)
readonly LINT_SEEDS=(recursion.py mutual_recursion.py long_function.py module_state.py)

fail() {
  echo "::error::$*" >&2
  exit 1
}

# Print each suppression marker in the given files or folders as file:line:text.
find_suppressions() {
  grep -rnHE --include='*.py' --include='*.pyi' --exclude-dir='.*' --exclude-dir=seeded \
    -e "${SUPPRESSION}" "$@" || true
}

# Fail unless a seed was rejected with the pattern on its first line.
expect_rejected() {
  local checker="$1" seed="$2" status="$3" output="$4"
  local pattern
  pattern="$(sed -n '1s/^# expect: //p' "${seed}")"
  [[ -n "${pattern}" ]] || fail "${seed#"${PYTHON_DIR}/"} has no '# expect:' first line"
  if [[ "${status}" -eq 0 ]]; then
    fail "${checker} accepted the seeded violation in ${seed#"${PYTHON_DIR}/"}"
  fi
  if ! grep -qE -e "${pattern}" <<<"${output}"; then
    echo "${output}" >&2
    fail "${checker} rejected ${seed#"${PYTHON_DIR}/"}, but not with '${pattern}'"
  fi
  echo "Rejected by ${checker}: ${seed#"${SEEDS}/"}"
}

check_no_suppressions() {
  local found configs name
  local find_names=()
  found="$(find_suppressions "${PYTHON_DIR}")"
  [[ -z "${found}" ]] || fail "suppressions are not allowed (docs/build-plan.md):"$'\n'"${found}"
  for name in "${CONFIG_NAMES[@]}"; do
    find_names+=(-o -name "${name}")
  done
  configs="$(find "${PYTHON_DIR}" -path "${PYTHON_DIR}/.venv" -prune -o \( -false "${find_names[@]}" \) -print \
    | grep -vx "${PYTHON_DIR}/pyproject.toml" || true)"
  [[ -z "${configs}" ]] || fail "tool settings belong in python/pyproject.toml only; found:"$'\n'"${configs}"
  echo "No suppressions: ok"
}

# Every seed file must be in exactly one of the lists above.
check_seed_list() {
  local listed actual
  listed="$(printf '%s\n' "${SCAN_SEEDS[@]}" "${MYPY_SEEDS[@]}" "${RUFF_SEEDS[@]}" "${LINT_SEEDS[@]}" | LC_ALL=C sort)"
  actual="$(find "${SEEDS}" -type f -name '*.py' -printf '%f\n' | LC_ALL=C sort)"
  [[ "${listed}" == "${actual}" ]] || fail "check-policy.sh must list every seed; listed:"$'\n'"${listed}"$'\n'"found:"$'\n'"${actual}"
}

# Run one checker over each seed in a list and require it to reject every one.
check_seeds() {
  local checker="$1"
  shift
  local command=()
  while [[ "$1" != "--" ]]; do
    command+=("$1")
    shift
  done
  shift
  local seed output status
  for seed in "$@"; do
    status=0
    output="$(cd "${PYTHON_DIR}" && uv run --locked "${command[@]}" "${SEEDS}/${seed}" 2>&1)" || status=$?
    expect_rejected "${checker}" "${SEEDS}/${seed}" "${status}" "${output}"
  done
}

check_scan_seeds() {
  local seed output
  for seed in "${SCAN_SEEDS[@]}"; do
    output="$(find_suppressions "${SEEDS}/${seed}")"
    expect_rejected "the suppression scan" "${SEEDS}/${seed}" "$([[ -n "${output}" ]] && echo 1 || echo 0)" "${output}"
  done
}

main() {
  check_no_suppressions
  check_seed_list
  check_scan_seeds
  check_seeds "mypy --strict" mypy -- "${MYPY_SEEDS[@]}"
  check_seeds "ruff" ruff check --no-cache -- "${RUFF_SEEDS[@]}"
  check_seeds "the AST checks" python -m ics_lint -- "${LINT_SEEDS[@]}"
  echo "Power-of-Ten policy: ok; every seeded violation was rejected"
}

main
