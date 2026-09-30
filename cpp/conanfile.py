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

    def requirements(self) -> None:
        self.requires("gtest/1.15.0")
        # The runtime for the generated messages in cpp/proto (ICS-011); its
        # version must match the protoc in proto/tools.txt (35.0).
        self.requires("protobuf/7.35.0")
        # Result types for ics::common (ICS-015). 1.2.0 makes tl::expected
        # [[nodiscard]], so an ignored result does not compile.
        self.requires("tl-expected/1.2.0")

    def generate(self) -> None:
        CMakeDeps(self).generate()
        toolchain = CMakeToolchain(self)
        toolchain.user_presets_path = False
        toolchain.generate()
