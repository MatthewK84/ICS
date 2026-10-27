#!/usr/bin/env bash
# Enforce the TypeScript Power-of-Ten profile's ban on suppressions and prove
# that every rule rejects a violation (ICS-007 "Done when": CI fails on any,
# !, var or a floating promise).
#
# Usage: web/policy/check-policy.sh   (after "pnpm install --frozen-lockfile" in web/)
#
#   1. No suppressions: no inline eslint configuration, @ts-ignore,
#      @ts-expect-error, @ts-nocheck, prettier-ignore or coverage-ignore
#      comment, and no ESLint, Prettier or TypeScript configuration beyond the
#      allowed files.
#   2. Operator-grade UI: no banned pattern from the UI standard
#      (.claude/skills/operator-grade-ui/SKILL.md, section 9; docs/ui-design.md)
#      in any app's src/: no gradient, blur, large radius or shadow utilities,
#      no indigo or violet, no hard-coded colour outside tokens.css, no emoji
#      and no escape hatch.
#   3. Seeds: every file in web/policy/seeded is rejected by the suppression
#      scan, the UI scan, tsc in strict mode or ESLint, each with the message
#      named on its first line, so a rule that silently stops firing fails too.
#
# Prettier, ESLint, tsc, Vitest and Playwright run over the real code as
# separate CI steps; see .github/workflows/web-toolchain.yml.
set -euo pipefail

readonly WEB_DIR="$(cd "$(dirname "$0")/.." && pwd)"
readonly SEEDS="${WEB_DIR}/policy/seeded"
readonly SUPPRESSION='(//|/\*)[[:space:]]*(eslint([[:space:]]|-disable|-enable)|globals?[[:space:]]|exported[[:space:]])|@ts-(ignore|expect-error|nocheck)|prettier-ignore|(v8|c8|istanbul)[[:space:]]+ignore'
readonly CONFIG_PATTERN='^(eslint\.config\..*|\.eslintrc.*|\.eslintignore|prettier\.config\..*|\.prettierrc.*|\.prettierignore|tsconfig.*\.json)$'
readonly ALLOWED_CONFIGS="./.prettierignore
./eslint.config.js
./policy/seeded/tsconfig.json
./prettier.config.js
./tsconfig.base.json
./tsconfig.json"
# The UI standard's banned patterns, as its section 9 writes them for ripgrep.
readonly UI_BANNED='gradient|backdrop-blur|rounded-(md|lg|xl|2xl|3xl)|shadow-(md|lg|xl|2xl)|(indigo|violet|purple|fuchsia)-[0-9]{2,3}|text-([3-9]xl)|animate-(bounce|pulse)'
readonly UI_COLOUR='#[0-9a-fA-F]{3,8}\b|rgba?\('
readonly UI_EMOJI='[\x{1F300}-\x{1FAFF}\x{2600}-\x{27BF}]'
readonly UI_ESCAPES='@ts-ignore|@ts-expect-error|eslint-disable|: any\b|as any\b'
readonly SCAN_SEEDS=(coverage_ignore.ts)
readonly UI_SEEDS=(ui_gradient.ts ui_colour.ts ui_emoji.ts ui_escape.ts)
readonly TSC_SEEDS=(implicit_any.ts)
readonly ESLINT_SEEDS=(
  any.ts non_null.ts var.ts floating_promise.ts long_function.ts recursion.ts mutual_recursion.ts
  return_type.ts global_write.ts ts_ignore.ts inline_config.ts
)

fail() {
  echo "::error::$*" >&2
  exit 1
}

# Print each suppression comment in the given files or folders as file:line:text.
find_suppressions() {
  grep -rnHE --include='*.ts' --include='*.tsx' --include='*.js' --include='*.mjs' --include='*.cjs' \
    --include='*.html' --exclude-dir=node_modules --exclude-dir=dist --exclude-dir=coverage \
    --exclude-dir=playwright-report --exclude-dir=test-results --exclude-dir=seeded --exclude-dir=gen \
    -e "${SUPPRESSION}" "$@" || true
}

# Print each banned UI pattern in the given files or folders as file:line:text.
# Hard-coded colours are allowed only in tokens.css.
find_ui_violations() {
  local options=(-rnH --exclude-dir=node_modules)
  grep "${options[@]}" -E -e "${UI_BANNED}" "$@" || true
  grep "${options[@]}" --exclude=tokens.css -E -e "${UI_COLOUR}" "$@" || true
  LC_ALL=C.UTF-8 grep "${options[@]}" -P -e "${UI_EMOJI}" "$@" || true
  grep "${options[@]}" -E -e "${UI_ESCAPES}" "$@" || true
}

# Fail unless a seed was rejected with the pattern on its first line.
expect_rejected() {
  local checker="$1" seed="$2" status="$3" output="$4"
  local pattern
  pattern="$(sed -n '1s|^// expect: ||p' "${seed}")"
  [[ -n "${pattern}" ]] || fail "${seed#"${WEB_DIR}/"} has no '// expect:' first line"
  if [[ "${status}" -eq 0 ]]; then
    fail "${checker} accepted the seeded violation in ${seed#"${WEB_DIR}/"}"
  fi
  if ! grep -qE -e "${pattern}" <<<"${output}"; then
    echo "${output}" >&2
    fail "${checker} rejected ${seed#"${WEB_DIR}/"}, but not with '${pattern}'"
  fi
  echo "Rejected by ${checker}: ${seed#"${SEEDS}/"}"
}

check_no_suppressions() {
  local found configs
  found="$(find_suppressions "${WEB_DIR}")"
  [[ -z "${found}" ]] || fail "suppressions are not allowed (docs/build-plan.md):"$'\n'"${found}"
  configs="$(cd "${WEB_DIR}" && find . -path ./node_modules -prune -o -path '*/node_modules' -prune -o -type f -print \
    | awk -F/ -v pattern="${CONFIG_PATTERN}" '$NF ~ pattern' | LC_ALL=C sort)"
  if [[ "${configs}" != "${ALLOWED_CONFIGS}" ]]; then
    fail "only these lint, format and TypeScript settings are allowed:"$'\n'"${ALLOWED_CONFIGS}"$'\n'"found:"$'\n'"${configs}"
  fi
  echo "No suppressions: ok"
}

check_ui() {
  local found
  found="$(find_ui_violations "${WEB_DIR}"/apps/*/src)"
  [[ -z "${found}" ]] || fail "the UI standard bans these (docs/ui-design.md):"$'\n'"${found}"
  echo "Operator-grade UI scan: ok"
}

# Every seed file must be in exactly one of the lists above.
check_seed_list() {
  local listed actual
  listed="$(printf '%s\n' "${SCAN_SEEDS[@]}" "${UI_SEEDS[@]}" "${TSC_SEEDS[@]}" "${ESLINT_SEEDS[@]}" | LC_ALL=C sort)"
  actual="$(find "${SEEDS}" -type f -name '*.ts' -printf '%f\n' | LC_ALL=C sort)"
  [[ "${listed}" == "${actual}" ]] || fail "check-policy.sh must list every seed; listed:"$'\n'"${listed}"$'\n'"found:"$'\n'"${actual}"
}

check_scan_seeds() {
  local seed output
  for seed in "${SCAN_SEEDS[@]}"; do
    output="$(find_suppressions "${SEEDS}/${seed}")"
    expect_rejected "the suppression scan" "${SEEDS}/${seed}" "$([[ -n "${output}" ]] && echo 1 || echo 0)" "${output}"
  done
}

check_ui_seeds() {
  local seed output
  for seed in "${UI_SEEDS[@]}"; do
    output="$(find_ui_violations "${SEEDS}/${seed}")"
    expect_rejected "the UI scan" "${SEEDS}/${seed}" "$([[ -n "${output}" ]] && echo 1 || echo 0)" "${output}"
  done
}

# tsc checks the seeds' project once; each tsc seed must appear in its errors.
check_tsc_seeds() {
  local seed output status=0
  output="$(cd "${WEB_DIR}" && pnpm exec tsc --noEmit -p policy/seeded/tsconfig.json 2>&1)" || status=$?
  for seed in "${TSC_SEEDS[@]}"; do
    expect_rejected "tsc (strict)" "${SEEDS}/${seed}" "${status}" "${output}"
  done
}

check_eslint_seeds() {
  local seed output status
  for seed in "${ESLINT_SEEDS[@]}"; do
    status=0
    output="$(cd "${WEB_DIR}" && pnpm exec eslint --max-warnings 0 --no-ignore "policy/seeded/${seed}" 2>&1)" || status=$?
    expect_rejected "ESLint" "${SEEDS}/${seed}" "${status}" "${output}"
  done
}

main() {
  check_no_suppressions
  check_ui
  check_seed_list
  check_scan_seeds
  check_ui_seeds
  check_tsc_seeds
  check_eslint_seeds
  echo "Power-of-Ten policy: ok; every seeded violation was rejected"
}

main
