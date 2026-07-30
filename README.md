# OpenObfuscator

OpenObfuscator 1.1.0 is a dependency-free C++17 Lua/LuaJIT source obfuscator. Its default output uses a LuaJIT source VM wrapper while preserving the behavior, varargs, and multiple return values of the input chunk.

## Build and test

A C++17 compiler and CMake 3.16 or newer are required. No external C++ dependencies are used.

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DOPENOBFUSCATOR_REQUIRE_LUAJIT_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The C++ unit and CLI contract tests are always registered when `BUILD_TESTING` is enabled. If a `luajit` executable is found during configuration, CTest also registers an end-to-end test that obfuscates and executes `tests/fixture.lua`, compares its behavior with the source, and runs `tests/test_integrity.lua` against unknown opcodes, invalid HALT flow, payload corruption, and length mismatches. Set `OPENOBFUSCATOR_REQUIRE_LUAJIT_TESTS=ON` for release builds so configuration fails instead of skipping the runtime test when LuaJIT is unavailable.

Install or create the ZIP package with:

```sh
cmake --install build --prefix install
cmake --build build --target package
```

On Windows, the executable includes the native GUI and links the required Win32 libraries.

## CLI

```text
openobfuscator [options] <input.lua> [output.lua]
```

If no output file is specified, generated Lua is written to stdout. Main options:

- `-o <file>`: write to a specific output file.
- `-s <n>`, `--seed <n>`: use a reproducible uint32 seed, including an explicit seed of `0`.
- `--no-numbers`, `--no-strings`, `--no-rename`, `--no-junk`: disable individual transformations.
- `--no-antidebug`, `--no-compress`, `--no-style`: disable the corresponding prelude/output features.
- `--no-vm`: disable the LuaJIT source VM wrapper (the flag name is retained for compatibility).
- `--no-luajit`: produce output that does not require LuaJIT; this also disables the source VM.
- `--flatten`: enable the existing control-flow flattening option.
- `--gui`: open the Windows graphical interface.
- `--version`: print `1.1.0`.
- `-h`, `--help`: show all options.

Seeds are parsed as complete decimal values in the range `0` through `4294967295`; partial, negative, and overflowing values are rejected.

## Source VM format 1

Version 1.1 introduces source VM format 1. The wrapper stores an encoded instruction stream as 32-bit words with randomized opcodes for source-byte emission, key mutation, noise, and a unique HALT operation. At runtime it requires exactly one effective HALT at the end of the stream. A missing or duplicate HALT, an instruction after HALT, or an unknown opcode raises `integrity:vm`.

Before loading the reconstructed chunk, the wrapper checks its exact byte length and Adler-32 checksum. It first uses the global `bit` module or safely attempts `require('bit')`, then verifies that the runtime is LuaJIT. The source is loaded with the chunk name `@openobfuscator-vm`.

### Limitations

- This is a **source VM**, not a Lua bytecode VM. It decodes and interprets an internal 32-bit instruction stream whose payload reconstructs Lua source.
- The original source exists in plaintext in process memory immediately before `loadstring`/`load`; this is obfuscation, not encryption or a secure execution boundary.
- Source VM output requires LuaJIT and its `bit` API. Use `--no-vm` or `--no-luajit` when that requirement is unsuitable.
- Obfuscation cannot prevent a determined runtime observer from recovering code.

## License and credits

OpenObfuscator is distributed under the MIT License. Original author: [@osk4rrv](https://github.com/osk4rrv).
