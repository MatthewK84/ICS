# C++ toolchain images

Pinned build environments for ICS C++ code (ICS-004). CI builds both images and publishes them from `main` to GitHub's container registry, tagged with the commit SHA and `main`. Each published image is signed by digest with cosign and carries a CycloneDX SBOM and SLSA provenance; see [`../evidence/`](../evidence/README.md) to verify them.

| Image | Base | Contents | Used for |
|---|---|---|---|
| `ghcr.io/matthewk84/ics-cpp` | Ubuntu 24.04 | GCC 13.3, Clang 17.0.6 (with sanitizer and libFuzzer runtimes and clang-tidy), cppcheck 2.13, CMake 3.28, Ninja 1.11, Conan 2.27 | Every C++ build and test |
| `ghcr.io/matthewk84/ics-cuda` | NVIDIA CUDA 12.9.1 developer image, Ubuntu 24.04 | CUDA 12.9 plus everything in `ics-cpp` | GPU modules, from ICS-062 |

## How the images are pinned

- **Base images** are pinned by digest in [`Dockerfile.cpp`](Dockerfile.cpp) and [`Dockerfile.cuda`](Dockerfile.cuda).
- **Ubuntu packages** are listed in [`apt-packages.txt`](apt-packages.txt), each with an entry in the [dependency register](../../docs/dependency-register.md). They come from Ubuntu's snapshot service at the date in `UBUNTU_SNAPSHOT`, so a rebuild installs the same versions. Each image lists them in `/usr/local/share/ics-toolchain-packages.txt`.
- **Conan** and its Python dependencies are installed from [`requirements-conan.txt`](requirements-conan.txt), with every file hash-pinned. Conan ships only as source, so its build tool is pinned the same way in [`requirements-build.txt`](requirements-build.txt).

To move to newer packages, change `UBUNTU_SNAPSHOT` (any `YYYYMMDDTHHMMSSZ` date that [snapshot.ubuntu.com](https://snapshot.ubuntu.com) serves). To upgrade Conan, edit `requirements-conan.in` and regenerate:

```sh
uv pip compile requirements-conan.in --generate-hashes --python-version 3.12 \
  --python-platform x86_64-unknown-linux-gnu --no-header -o requirements-conan.txt
```

Use the same command for `requirements-build.in`.

## Build locally

From the repository root:

```sh
docker build -f deploy/toolchain/Dockerfile.cpp -t ics-cpp .
docker build -f deploy/toolchain/Dockerfile.cuda -t ics-cuda .
```

Behind a TLS-inspecting proxy, pass its CA bundle as a build secret: `--secret id=extra_ca,src=<bundle.pem>`. The image then trusts that CA as well.

## Scripts

- [`conan-install.sh PRESET...`](conan-install.sh): installs the Conan dependencies that one or more CMake presets need, from `cpp/conan.lock`. For the `asan`, `tsan` and `fuzz` presets it builds them with the matching sanitizer, from the [`asan`](../../cpp/conan/profiles/asan) and [`tsan`](../../cpp/conan/profiles/tsan) profiles.
- [`check-reproducible.sh gcc|clang`](check-reproducible.sh): builds the `toolchain_check` sample twice in different build folders and fails unless every output is byte-identical. This is the ICS-004 "Done when" test, run in CI for both compilers.
- [`install-toolchain.sh SNAPSHOT_ID`](install-toolchain.sh): used by both Dockerfiles.
