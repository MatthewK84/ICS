# deploy

Containers, the Envoy gRPC-Web proxy, systemd units and infrastructure as code for the field server, station computers and CI toolchain images.

**Language:** Dockerfiles, Envoy and systemd configuration, infrastructure as code. **Lead role:** DevSecOps.

Rules:

- Pin every base image and dependency; lockfiles are committed.
- Every build produces a CycloneDX SBOM and signed provenance ([ICS-009](https://github.com/MatthewK84/ICS/issues/9)).
- No credentials, range network addresses or other sensitive configuration in this repository; see [SECURITY.md](../SECURITY.md).

First issue: [ICS-004](https://github.com/MatthewK84/ICS/issues/4) (toolchain containers).
