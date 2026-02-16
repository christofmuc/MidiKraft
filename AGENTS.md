# Repository Guidelines

## Project Structure & Module Organization
The repository builds a set of static C++ libraries grouped under `base/`, with optional modules in `librarian/` and `database/`. Each module follows a consistent `include/` and `src/` split; headers define the public API while implementations live alongside. Top-level `CMakeLists.txt` controls which modules compile, so keep new code localized within the relevant module directory and expose only stable headers through `include/`.

## Build, Test, and Development Commands
Use CMake 3.14+ for configuration. Typical workflow:
`cmake -S . -B build -DMIDIKRAFT_BUILD_LIBRARIAN=ON -DMIDIKRAFT_BUILD_DATABASE=ON` — generate a full workspace with optional modules.
`cmake --build build --target midikraft-base` — compile the core library (replace target to build others).
`cmake --build build --config Release` — produce an optimized build for integration testing.
When integrating into a host app, add the resulting `build/` products via your parent project’s CMake.

## Coding Style & Naming Conventions
Follow the existing JUCE-friendly C++ style: tabs for primary indentation, braces on the same line, and `#pragma once` headers. Keep namespaces lowercase (`midikraft`), classes and structs in PascalCase, member functions camelCase, and constants in ALL_CAPS. Prefer standard library types, lean on `std::vector`/`std::array`, and reuse helper utilities in `include/`. Run `clang-format` (JUCE style) before submitting if available in your toolchain.

## Testing Guidelines
Automated tests are not yet present; new features should arrive with Catch2 or JUCE unit tests under a `tests/` subdirectory in the relevant module. Name tests after the class under test (e.g., `PatchTests.cpp`) and wire them into CMake via `add_executable` + `add_test`. Until CI is in place, document manual verification steps—especially hardware or Sysex round-trips—in your pull request.

## Commit & Pull Request Guidelines
Recent history favors concise, sentence-style commit subjects (imperative verbs welcome) with context in the body when needed. Keep commits focused and sign them if required by your workflow. Pull requests should summarize the change, note module touchpoints, link related issues, and include screenshots or logs when behavior affects device interactions. Mention any required hardware setup so reviewers can reproduce your validation steps.
