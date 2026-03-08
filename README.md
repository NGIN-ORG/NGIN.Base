# NGIN.Base

NGIN.Base is a foundational C++23 library in the NGIN family.

It provides low-level infrastructure that other libraries and applications can build on: containers, memory utilities, async primitives, IO, text, hashing, math, units, and serialization.

The short version is:

- NGIN.Base is not a game engine
- it is not only a utility header dump
- it is the systems-level foundation the rest of the NGIN stack builds on

It is also intended to be useful outside the full NGIN platform.

## What NGIN.Base Is For

NGIN.Base exists for code that needs a strong core library with explicit control over:

- memory and allocation
- containers and views
- async and execution primitives
- IO and filesystem abstractions
- text and formatting utilities
- serialization support
- low-level performance-sensitive building blocks

If you think of the broader NGIN ecosystem as a layered platform, NGIN.Base is the foundation layer.

## What It Provides

NGIN.Base includes facilities such as:

- containers
- memory and allocator utilities
- async/task/fiber infrastructure
- execution helpers
- filesystem and IO abstractions
- serialization support for JSON and XML
- strings, text, hashing, time, math, and units utilities

The library aims to be modern C++ infrastructure with a compiled core where it makes sense and header-first APIs where that keeps usage simple.

## When You Would Use It

You would reach for NGIN.Base when:

- you want foundational C++ infrastructure without pulling in the whole NGIN platform
- you are building other NGIN libraries like `NGIN.Log`, `NGIN.Reflection`, or `NGIN.Core`
- you need systems-oriented utilities with explicit behavior and predictable composition

## Build Targets

NGIN.Base can be consumed in multiple forms:

- `NGIN::Base::Static` with `-DNGIN_BASE_BUILD_STATIC=ON`
- `NGIN::Base::Shared` with `-DNGIN_BASE_BUILD_SHARED=ON`

The convenience alias `NGIN::Base` resolves to the shared target when available, otherwise the static target.

## Build Options

Main CMake options:

- `NGIN_BASE_BUILD_STATIC` default `ON`
- `NGIN_BASE_BUILD_SHARED` default `OFF`
- `NGIN_BASE_BUILD_TESTS` default `ON`
- `NGIN_BASE_BUILD_EXAMPLES` default `ON`
- `NGIN_BASE_BUILD_BENCHMARKS` default `ON`
- `NGIN_BASE_ENABLE_ASAN` default `OFF`
- `NGIN_BASE_ENABLE_TSAN` default `OFF`
- `NGIN_BASE_ENABLE_LTO` default `OFF`
- `NGIN_BASE_STRICT_WARNINGS` default `ON`

## Typical Local Build

```bash
cmake -S . -B build \
  -DNGIN_BASE_BUILD_TESTS=ON \
  -DNGIN_BASE_BUILD_EXAMPLES=ON \
  -DNGIN_BASE_BUILD_BENCHMARKS=OFF

cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Why It Matters In The NGIN Stack

In the broader NGIN platform, NGIN.Base is the library that everything else assumes exists.

That does not make it “special” in the package model, but it does make it foundational in practice:

- `NGIN.Log` builds on it
- `NGIN.Reflection` builds on it
- `NGIN.Core` builds on it
- the native `ngin` CLI uses it for XML parsing and low-level infrastructure

## Read Next

- [Contribution Guide](AGENTS.md)
- [Documentation Index](docs/README.md)
