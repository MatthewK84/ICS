#!/usr/bin/env bash
# Check the protobuf contracts and their generated code (ICS-011), then prove
# each check still rejects a seeded defect.
#
# Usage: proto/check-proto.sh [AGAINST]
#
#   1. buf format and buf lint (STANDARD and COMMENTS rules) pass on proto/.
#      The files copied from other projects into proto/third_party keep their
#      authors' style: buf lints them with its MINIMAL rules only, and does
#      not format them.
#   2. buf breaking finds no breaking change against proto/ at the git commit
#      AGAINST (default: origin/main). Skipped when AGAINST has no module yet.
#   3. buf generate reproduces the committed C++, Python and TypeScript code
#      exactly (cpp/proto/gen, python/gen, web/packages/ics-proto/src/gen),
#      with the C++ of proto/third_party (ICS-024) beside the ICS C++.
#   4. Each folder in proto/third_party holds exactly the .proto files that
#      proto/third_party/tools.txt pins by sha256.
#   5. Seeds: buf lint rejects proto/policy/seeded/lint_violation.proto, buf
#      breaking rejects a deleted field, the comparison in step 3 rejects an
#      edited generated file, and the check in step 4 an edited copied file.
#
# Needs buf and protoc from proto/tools.txt on the PATH and web/node_modules
# installed (pnpm install --frozen-lockfile), run from any folder.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
readonly ROOT
readonly AGAINST="${1:-origin/main}"
readonly TEMPLATE="proto/buf.gen.yaml"
readonly THIRD_PARTY_TEMPLATE="proto/buf.gen.third_party.yaml"
# The folders copied from other projects, each under proto/third_party.
readonly THIRD_PARTY=(proto/third_party/sapient_msg)
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

# --exclude-path for each folder copied from another project.
third_party_excludes() {
  local dir
  for dir in "${THIRD_PARTY[@]}"; do
    printf -- '--exclude-path\n%s\n' "${dir}"
  done
}

check_lint() {
  local excludes=()
  mapfile -t excludes < <(third_party_excludes)
  buf format --diff --exit-code proto "${excludes[@]}" \
    || fail "run 'buf format -w proto ${excludes[*]}' to format the .proto files"
  buf lint proto || fail "buf lint found problems"
  echo "Format and lint: ok"
}

check_breaking() {
  if ! git -C "${ROOT}" cat-file -e "${AGAINST}:proto/buf.yaml" 2>/dev/null; then
    echo "Breaking changes: ${AGAINST} has no protobuf module yet; nothing to compare"
    return
  fi
  git -C "${ROOT}" archive "${AGAINST}" proto | tar -x -C "${WORK}/against" --strip-components=1
  # A base from before proto/third_party gets this tree's copy and buf.yaml, so
  # both have the same modules and the copied files compare with themselves.
  if [[ ! -d "${WORK}/against/third_party" ]]; then
    cp -r "${ROOT}/proto/third_party" "${WORK}/against/"
    cp "${ROOT}/proto/buf.yaml" "${WORK}/against/buf.yaml"
  fi
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

# The ICS code first: its template's clean empties the output folders.
generate() {
  local output="$1" excludes=() paths=() dir
  mapfile -t excludes < <(third_party_excludes)
  for dir in "${THIRD_PARTY[@]}"; do
    paths+=(--path "${dir}")
  done
  (cd "${ROOT}" && buf generate proto --template "${TEMPLATE}" "${excludes[@]}" --output "${output}")
  (cd "${ROOT}" && buf generate proto --template "${THIRD_PARTY_TEMPLATE}" "${paths[@]}" --output "${output}")
}

check_generated() {
  generate "${WORK}/fresh"
  compare_generated "${ROOT}" \
    || fail "generated code is stale; run the two buf generate commands in ${THIRD_PARTY_TEMPLATE}"
  echo "Generated code: matches buf generate for C++, Python and TypeScript, and for proto/third_party in C++"
}

# The sha256 of the .proto files in a folder, concatenated in path order.
proto_digest() {
  local dir="$1"
  (cd "$(dirname "${dir}")" && find "$(basename "${dir}")" -name '*.proto' -print0 | LC_ALL=C sort -z \
    | xargs -0 cat | sha256sum | cut -d ' ' -f 1)
}

# Each folder named in a third_party tools.txt must hold the files it pins.
verify_copies() {
  local third_party="$1" name commit sha url actual
  while read -r name commit sha url; do
    actual="$(proto_digest "${third_party}/${name}")"
    if [[ "${actual}" != "${sha}" ]]; then
      echo "proto/third_party/${name} is not the copy of ${url} at ${commit}: sha256 ${actual}, not ${sha}"
      return 1
    fi
  done < <(grep -Ev '^(#|$)' "${third_party}/tools.txt")
}

check_copies() {
  verify_copies "${ROOT}/proto/third_party" || fail "copied .proto files were changed; see proto/third_party/README.md"
  echo "Copied files: match proto/third_party/tools.txt"
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
  echo "// edited by hand" >>"${copy}/proto/third_party/sapient_msg/proto_options.proto"
  status=0 && output="$(verify_copies "${copy}/proto/third_party" 2>&1)" || status=$?
  expect_rejected "the copied-file check" "is not the copy of" "${output}" "${status}"
}

main() {
  mkdir -p "${WORK}/against"
  check_lint
  check_breaking
  check_generated
  check_copies
  check_seeds
  echo "Protobuf: ok; every seeded defect was rejected"
}

main
