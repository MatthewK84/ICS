# CI evidence

Every merge to `main` publishes a signed SBOM and signed provenance ([ICS-009](https://github.com/MatthewK84/ICS/issues/9)), part of the [DoWI 8430.01](https://www.esd.whs.mil/Portals/54/Documents/DD/issuances/dodi/843001p.pdf) evidence set. The toolchain images get the same when they are published.

| What | Subject | Produced by | Where it is kept |
|---|---|---|---|
| CycloneDX SBOM of the Conan, uv and pnpm lockfiles (plus the toolchain's pip requirements) | `ics-<sha>.tar`, the merged source | [`evidence.yml`](../../.github/workflows/evidence.yml), every merge | The repository's [attestations](https://github.com/MatthewK84/ICS/attestations), permanently; the `evidence-<sha>` workflow artifact for 90 days |
| SLSA v1 provenance: workflow, commit, runner and the pinned tools | `ics-<sha>.tar` | `evidence.yml`, every merge | As above |
| cosign signature, CycloneDX SBOM (Ubuntu and Python packages) and SLSA v1 provenance | `ghcr.io/matthewk84/ics-cpp` and `ics-cuda`, by digest | [`cpp-toolchain.yml`](../../.github/workflows/cpp-toolchain.yml) and [`cuda-toolchain.yml`](../../.github/workflows/cuda-toolchain.yml), when they publish | Next to the image in GHCR |
| cosign signature | `ghcr.io/matthewk84/ics-conan`, the Conan packages of each dependency configuration, by digest | [`conan-deps.yml`](../../.github/workflows/conan-deps.yml), when a configuration's key has no artifact yet | Next to the artifact in GHCR; every job verifies it before restoring the packages |

Each SBOM and provenance is an [in-toto](https://in-toto.io) attestation, signed by [cosign](https://github.com/sigstore/cosign) without a stored key. The workflow's GitHub identity token gets a short-lived certificate from Sigstore's public certificate authority. That certificate names the repository, the workflow file, the ref and the commit, and the signature is recorded in Sigstore's public transparency log. Every job verifies what it signed before it finishes. The per-merge job also proves that a tampered archive fails verification. On pull requests it signs and verifies but publishes nothing.

The SBOM check fails the build if the SBOM lists no Conan, PyPI or npm packages (no Ubuntu or PyPI packages for an image). A scanner that silently stops recognising a lockfile therefore fails the build.

## Verify a merge

Rebuild the archive from the commit, then check both attestations with the GitHub CLI:

```sh
sha=<commit on main>
git archive --format=tar --prefix="ics-${sha}/" -o "ics-${sha}.tar" "${sha}"
for type in https://cyclonedx.org/bom https://slsa.dev/provenance/v1; do
  gh attestation verify "ics-${sha}.tar" --repo MatthewK84/ICS \
    --signer-workflow MatthewK84/ICS/.github/workflows/evidence.yml \
    --source-ref refs/heads/main --source-digest "${sha}" --predicate-type "${type}"
done
```

Add `--format json` to see the SBOM or provenance itself. Or use cosign with the bundles from the `evidence-<sha>` artifact:

```sh
cosign verify-blob-attestation --bundle sbom.sigstore.json --type cyclonedx \
  --certificate-identity "https://github.com/MatthewK84/ICS/.github/workflows/evidence.yml@refs/heads/main" \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com \
  --certificate-github-workflow-sha "${sha}" "ics-${sha}.tar"
```

Use `--type slsaprovenance1` with `provenance.sigstore.json` for the provenance.

## Verify an image

```sh
image=ghcr.io/matthewk84/ics-cpp@sha256:<digest>
identity=(--certificate-identity https://github.com/MatthewK84/ICS/.github/workflows/cpp-toolchain.yml@refs/heads/main
  --certificate-oidc-issuer https://token.actions.githubusercontent.com)
cosign verify "${identity[@]}" "${image}"
cosign verify-attestation --type cyclonedx "${identity[@]}" "${image}"
cosign verify-attestation --type slsaprovenance1 "${identity[@]}" "${image}"
```

For `ics-cuda`, use `cuda-toolchain.yml` in the identity.

## Scripts

- [`tools.txt`](tools.txt) pins Syft, cosign and ORAS by version and sha256.
- [`install-tools.sh BIN_DIR [PINS]`](install-tools.sh) downloads and installs them, and refuses any download whose checksum does not match. Given another pins file, such as [`proto/tools.txt`](../../proto/tools.txt), it installs those tools instead. To upgrade a tool, change its line: the new version, the checksum from the release's checksums file, and the URL.
- [`source-evidence.sh OUT_DIR`](source-evidence.sh) builds, signs and verifies the per-merge evidence.
- [`image-sbom.sh LOCAL_IMAGE NAME OUT_FILE`](image-sbom.sh) writes and checks an image's SBOM. It runs on pull requests too.
- [`attest-image.sh IMAGE TAG SBOM`](attest-image.sh) signs a pushed image by digest, attaches its SBOM and provenance, and verifies all three.
- [`dependency-artifact.sh`](dependency-artifact.sh) `fetch` and `publish` move the Conan packages of a dependency configuration ([`deploy/toolchain/conan-deps.sh`](../toolchain/conan-deps.sh)) to and from GHCR with ORAS, and `point` moves the configuration's `-main` tag to main's current artifact. An artifact counts only when the dependency workflow signed it on main, or, for a pull request's own artifacts, in that pull request; one that is there but does not verify fails the job.
- [`.github/scripts/evidence.py`](../../.github/scripts/evidence.py) writes the provenance and the in-toto statements, checks SBOMs and publishes bundles to the attestation store.
