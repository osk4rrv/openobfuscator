# OpenObfuscator

OpenObfuscator 1.2.0 is a dependency-free C++17 source obfuscator for Lua/LuaJIT and JavaScript. Both languages use the same native source-VM encoder: randomized 32-bit instructions reconstruct the protected source only at runtime, with strict HALT, byte-length, and Adler-32 integrity checks.

## Supported runtimes

- **Lua:** LuaJIT with the `bit` API when the source VM is enabled. Use `--no-vm` or `--no-luajit` for the legacy Lua token-transform pipeline.
- **JavaScript:** modern browsers and Node.js. The generated wrapper supports CommonJS modules and classic scripts that do not depend on persistent top-level `let`/`const` bindings. ECMAScript modules are not supported by the source-VM runtime.

The input language is inferred from `.lua`, `.js`, or `.cjs`. ECMAScript module files (`.mjs`) are rejected because the source-VM runtime is not module-aware. Use `--language` to override detection for supported script syntax.

## Build and test

A C++17 compiler and CMake 3.16 or newer are required. No external C++ dependencies are used.

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DOPENOBFUSCATOR_REQUIRE_LUAJIT_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The C++ unit and CLI contract tests are registered when `BUILD_TESTING` is enabled. If `luajit` is found, CTest also registers the LuaJIT end-to-end and source-VM integrity checks. Set `OPENOBFUSCATOR_REQUIRE_LUAJIT_TESTS=ON` for release builds so configuration fails when LuaJIT is unavailable.

Install or create the ZIP package with:

```sh
cmake --install build --prefix install
cmake --build build --target package
```

On Windows, the executable includes the native GUI. The GUI detects the language from the selected file extension.

## CLI

```text
openobfuscator [options] <input.lua|input.js> [output]
```

If no output file is specified, generated code is written to stdout. Main options:

- `-o <file>`: write to a specific output file.
- `-l <language>`, `--language <language>`: select `lua`, `javascript`, `js`, or `auto`.
- `-s <n>`, `--seed <n>`: use a reproducible uint32 seed, including an explicit seed of `0`.
- `--no-numbers`, `--no-strings`, `--no-rename`, `--no-junk`: disable Lua token transformations.
- `--no-antidebug`, `--no-compress`, `--no-style`: disable the corresponding Lua prelude/output features.
- `--no-vm`: disable the source-VM wrapper.
- `--no-luajit`: produce Lua output that does not require LuaJIT; JavaScript output is unaffected.
- `--flatten`: enable the existing Lua control-flow flattening option.
- `--gui`: open the Windows graphical interface.
- `--version`: print `1.2.0`.
- `-h`, `--help`: show all options.

Seeds are parsed as complete decimal values from `0` through `4294967295`; partial, negative, and overflowing values are rejected.

## Source VM format 1

The source VM stores an encoded instruction stream as 32-bit words with randomized opcodes for source-byte emission, key mutation, noise, and HALT. At runtime, the language-specific wrapper rejects missing, duplicate, or misplaced HALT instructions and unknown opcodes. It validates the reconstructed byte length and Adler-32 checksum before executing the source.

Lua output discovers and verifies LuaJIT's `bit` API before loading the reconstructed chunk as `@openobfuscator-vm`. JavaScript output reconstructs UTF-8 bytes and executes CommonJS source in a module-compatible function scope or classic-script source through global indirect evaluation.

### Limitations

- This is a **source VM**, not a Lua or JavaScript bytecode VM.
- The original source exists in plaintext in process memory immediately before execution. This is obfuscation, not encryption or a secure execution boundary.
- ECMAScript module semantics, including `import`, `export`, top-level `await`, `import.meta`, and module-level `this`, are not supported by the JavaScript source VM.
- Classic-script top-level `let` and `const` bindings created by the wrapper do not persist for later scripts; use CommonJS or avoid depending on persistent global lexical bindings.
- JavaScript source-VM output requires runtime string code generation (`Function`/`eval`) and will not run under policies that disable it, such as a CSP without `unsafe-eval` or Node.js with `--disallow-code-generation-from-strings`.
- Obfuscation cannot prevent a determined runtime observer from recovering code.

## Library API

The existing `luaobf::Obfuscator` API remains source compatible. Set `ObfuscationOptions::language` to `Language::Lua` or `Language::JavaScript` before calling `obfuscate()`.

## License and credits

OpenObfuscator is distributed under the MIT License. Original author: [@osk4rrv](https://github.com/osk4rrv).
