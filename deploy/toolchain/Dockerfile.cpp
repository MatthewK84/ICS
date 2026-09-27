# syntax=docker/dockerfile:1.7
# ICS C++ toolchain: GCC 13, Clang 17, CMake, Ninja and Conan on Ubuntu 24.04 (ICS-004).
# Build from the repository root:
#   docker build -f deploy/toolchain/Dockerfile.cpp -t ics-cpp .
# Behind a TLS-inspecting proxy, add: --secret id=extra_ca,src=<ca-bundle.pem>
FROM ubuntu:24.04@sha256:008173c23f95b170204355c12626cb5a965d779a7e1283b09e9cffbb1bf33ca3

ARG UBUNTU_SNAPSHOT=20260926T000000Z
COPY deploy/toolchain/install-toolchain.sh deploy/toolchain/apt-packages.txt deploy/toolchain/requirements-build.txt deploy/toolchain/requirements-conan.txt /tmp/toolchain/
RUN --mount=type=secret,id=extra_ca,required=false \
    bash /tmp/toolchain/install-toolchain.sh "${UBUNTU_SNAPSHOT}" && rm -rf /tmp/toolchain

ENV CONAN_HOME=/opt/conan-home \
    SOURCE_DATE_EPOCH=0
LABEL org.opencontainers.image.source="https://github.com/MatthewK84/ICS" \
      org.opencontainers.image.description="ICS C++ toolchain: GCC 13, Clang 17, CMake, Ninja, Conan 2"
WORKDIR /work
