# NGIN.Base
NGIN.Base is a modern C++23 library providing high-performance abstractions for application development.   It includes alternatives to the STL components, as well as utilities for benchmarking, testing, and building scientific or performance-critical software.

## Library Modes

NGIN.Base can now be consumed in multiple forms depending on project needs:

- Static archive (`NGIN::Base::Static`) built by default (`-DNGIN_BASE_BUILD_STATIC=ON`).
- Shared library (`NGIN::Base::Shared`) enabled via `-DNGIN_BASE_BUILD_SHARED=ON`.

The convenience alias `NGIN::Base` resolves to the shared target when available, otherwise the static library. This allows existing build scripts to continue linking against `NGIN::Base` while opting into compiled variants as requirements grow (threading, IO, etc.).
