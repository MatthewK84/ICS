"""Conan 2 dependencies for the ICS C++ build (ICS-004).

Every dependency is pinned in conan.lock. Add one with `conan lock add` or by
regenerating the lockfile (see cpp/README.md), and record it in the dependency
register (docs/dependency-register.toml).
"""

from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class IcsConan(ConanFile):
    name = "ics"
    settings = "os", "arch", "compiler", "build_type"
    # Header-only packages have one binary for every preset, so Conan builds
    # nothing new for them (ICS-016). None of the libraries throws: real-time
    # code builds without exceptions, and errors come back as values.
    default_options = {
        "fmt/*:header_only": True,
        "spdlog/*:header_only": True,
        "spdlog/*:no_exceptions": True,
        "tomlplusplus/*:exceptions": False,
        "pugixml/*:no_exceptions": True,
    }

    def requirements(self) -> None:
        self.requires("gtest/1.15.0")
        # The runtime for the generated messages in cpp/proto (ICS-011); its
        # version must match the protoc in proto/tools.txt (35.0).
        self.requires("protobuf/7.35.0")
        # Result types for ics::common (ICS-015). 1.2.0 makes tl::expected
        # [[nodiscard]], so an ignored result does not compile.
        self.requires("tl-expected/1.2.0")
        # Structured logging for ics::logging (ICS-016); brings fmt 12.1.0.
        self.requires("spdlog/1.17.0")
        # TOML config files for ics::config (ICS-016).
        self.requires("tomlplusplus/3.4.0")
        # Packet capture from the TAP ports for ics::capture (ICS-020).
        self.requires("libpcap/1.10.6")
        # SHA-256 of each capture file for ics::capture (ICS-020), through the
        # EVP interface, so a FIPS provider can compute it. 3.5 is the current
        # long-term support release.
        self.requires("openssl/3.5.9")
        # Reads the Cursor on Target XML in ics::cot (ICS-022). It reads no
        # DTD and expands no entities beyond XML's own, so a hostile payload
        # cannot reach files or the network or grow without bound.
        self.requires("pugixml/1.16")
        # Streams Lattice's entities over HTTPS for ics::lattice (ICS-023),
        # through OpenSSL, with the server's certificate verified.
        self.requires("libcurl/8.22.0")

    def generate(self) -> None:
        CMakeDeps(self).generate()
        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.generate()
