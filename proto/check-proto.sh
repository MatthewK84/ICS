#!/usr/bin/env bash
# Check the protobuf contracts and their generated code (ICS-011), then prove
# each check still rejects a seeded defect.
#
# Usage: proto/check-proto.sh [AGAINST]
#
#   1. buf format and buf lint (STANDARD and COMMENTS rules) pass on proto/.
#   2. buf breaking finds no breaking change against proto/ at the git commit
#      AGAINST (default: origin/main). Skipped when AGAINST has no module yet.
#   3. buf generate reproduces the committed C++, Python and TypeScript code
#      exactly (cpp/proto/gen, python/gen, web/packages/ics-proto/src/gen).
#   4. Seeds: buf lint rejects proto/policy/seeded/lint_violation.proto, buf
#      breaking rejects a deleted field, and the comparison in step 3 rejects
#      an edited generated file.
#
# Needs buf and protoc from proto/tools.txt on the PATH and web/node_modules
# installed (pnpm install --frozen-lockfile), run from any folder.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
readonly ROOT
readonly AGAINST="${1:-origin/main}"
readonly TEMPLATE="proto/buf.gen.yaml"
readonly GENERATED=(cpp/proto/gen python/gen web/packages/ics-proto/src/gen)
readonly LINT_SEED="${ROOT}/proto/policy/seeded/lint_violation.proto"
WORK="$(mktemp -d)"
readonly WORK
trap 'rm -rf "${WORK}"' EXIT

fail() {
  echo "::error::$*" >&2
  exit 1
}

# expect_rejected CHECK PATTERN OUTPUT STATUS: a seeded defect must fail CHECK
# with PATTERN in its output.
expect_rejected() {
  local check="$1" pattern="$2" output="$3" status="$4"
  [[ "${status}" -ne 0 ]] || fail "${check} accepted its seeded defect"
  if ! grep -qE -e "${pattern}" <<<"${output}"; then
    echo "${output}" >&2
    fail "${check} rejected its seeded defect, but not with '${pattern}'"
  fi
  echo "Rejected by ${check}: seeded defect (${pattern})"
}

check_lint() {
  buf format --diff --exit-code proto || fail "run 'buf format -w proto' to format the .proto files"
  buf lint proto || fail "buf lint found problems"
  echo "Format and lint: ok"
}

check_breaking() {
  if ! git -C "${ROOT}" cat-file -e "${AGAINST}:proto/buf.yaml" 2>/dev/null; then
    echo "Breaking changes: ${AGAINST} has no protobuf module yet; nothing to compare"
    return
  fi
  git -C "${ROOT}" archive "${AGAINST}" proto | tar -x -C "${WORK}/against" --strip-components=1
  buf breaking proto --against "${WORK}/against" || fail "buf breaking found a breaking change against ${AGAINST}"
  echo "Breaking changes: none against ${AGAINST}"
}

# Compare the code generated into $WORK/fresh with the committed code in $1.
# Python byte-code caches are not generated code.
compare_generated() {
  local committed="$1" dir
  for dir in "${GENERATED[@]}"; do
    diff -r --exclude=__pycache__ "${WORK}/fresh/${dir}" "${committed}/${dir}" || return 1
  done
}

check_generated() {
  (cd "${ROOT}" && buf generate proto --template "${TEMPLATE}" --output "${WORK}/fresh")
  compare_generated "${ROOT}" || fail "generated code is stale; run: buf generate proto --template ${TEMPLATE}"
  echo "Generated code: matches buf generate for C++, Python and TypeScript"
}

check_seeds() {
  local output status copy="${WORK}/seeded"
  mkdir -p "${copy}"
  cp -r "${ROOT}/proto/." "${copy}/proto"
  cp "${LINT_SEED}" "${copy}/proto/ics/v1/"
  status=0 && output="$(buf lint --error-format=json "${copy}/proto" 2>&1)" || status=$?
  expect_rejected "buf lint" "$(sed -n '1s|^// expect: ||p' "${LINT_SEED}")" "${output}" "${status}"
  rm "${copy}/proto/ics/v1/$(basename "${LINT_SEED}")"
  sed -i '/repeated Flag flags = 14;/d' "${copy}/proto/ics/v1/run_record.proto"
  status=0 && output="$(buf breaking --error-format=json "${copy}/proto" --against "${ROOT}/proto" 2>&1)" || status=$?
  expect_rejected "buf breaking" "FIELD_NO_DELETE" "${output}" "${status}"
  cp -r "${WORK}/fresh" "${copy}/committed"
  echo "// edited by hand" >>"${copy}/committed/python/gen/ics/v1/run_record_pb2.py"
  status=0 && output="$(compare_generated "${copy}/committed" 2>&1)" || status=$?
  expect_rejected "the generated-code comparison" "edited by hand" "${output}" "${status}"
}

main() {
  mkdir -p "${WORK}/against"
  check_lint
  check_breaking
  check_generated
  check_seeds
  echo "Protobuf: ok; every seeded defect was rejected"
}

main
