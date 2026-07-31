# OpenObfuscator

OpenObfuscator 1.3.0 is a dependency-free C++17 source obfuscator for Lua/LuaJIT and JavaScript. By default it encodes source bytes as randomized 32-bit values and emits a language-specific loader. The loader verifies its termination marker, byte length, and Adler-32 checksum before running the reconstructed source.

## Supported runtimes

- **Lua:** LuaJIT with the `bit` API when the encoded source loader is enabled. Use `--no-vm` or `--no-luajit` for the legacy Lua token-transform pipeline.
- **JavaScript:** modern browsers and Node.js. The generated wrapper supports CommonJS modules and classic scripts that do not depend on persistent top-level `let`/`const` bindings. ECMAScript modules are not supported by the encoded loader runtime.

The input language is inferred from `.lua`, `.js`, or `.cjs`. ECMAScript module files (`.mjs`) are rejected because the encoded loader runtime is not module-aware. Use `--language` to override detection for supported script syntax.

## Build and test

A C++17 compiler and CMake 3.16 or newer are required. No external C++ dependencies are used.

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DOPENOBFUSCATOR_REQUIRE_LUAJIT_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The C++ unit and CLI contract tests are registered when `BUILD_TESTING` is enabled. If `luajit` is found, CTest also registers the LuaJIT end-to-end and encoded loader integrity checks. Set `OPENOBFUSCATOR_REQUIRE_LUAJIT_TESTS=ON` for release builds so configuration fails when LuaJIT is unavailable.

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
- `--no-vm`: disable the encoded loader wrapper.
- `--no-luajit`: produce Lua output that does not require LuaJIT; JavaScript output is unaffected.
- `--flatten`: enable the existing Lua control-flow flattening option.
- `--gui`: open the Windows graphical interface.
- `--version`: print `1.3.0`.
- `-h`, `--help`: show all options.

Seeds are parsed as complete decimal values from `0` through `4294967295`; partial, negative, and overflowing values are rejected.

## Encoded loader format 1

The loader stores source bytes as randomized 32-bit values mixed with key changes and noise. At runtime, the language-specific wrapper rejects missing or misplaced termination markers and unknown values. It validates the reconstructed byte length and Adler-32 checksum before executing the source.

Lua output discovers and verifies LuaJIT's `bit` API before loading the reconstructed chunk as `@openobfuscator-loader`. JavaScript output reconstructs UTF-8 bytes and executes CommonJS source in a module-compatible function scope or classic-script source through global indirect evaluation.

### Limitations

- This is an **encoded source loader**, not Lua or JavaScript bytecode.
- The original source exists in plaintext in process memory immediately before execution. This is obfuscation, not encryption or a secure execution boundary.
- ECMAScript module semantics, including `import`, `export`, top-level `await`, `import.meta`, and module-level `this`, are not supported by the JavaScript encoded source loader.
- Classic-script top-level `let` and `const` bindings created by the wrapper do not persist for later scripts; use CommonJS or avoid depending on persistent global lexical bindings.
- JavaScript encoded loader output requires runtime string code generation (`Function`/`eval`) and will not run under policies that disable it, such as a CSP without `unsafe-eval` or Node.js with `--disallow-code-generation-from-strings`.
- Obfuscation cannot prevent a determined runtime observer from recovering code.

## Library API

The existing `luaobf::Obfuscator` API remains source compatible. Set `ObfuscationOptions::language` to `Language::Lua` or `Language::JavaScript` before calling `obfuscate()`.

## License and credits

OpenObfuscator is distributed under the MIT License. Original author: [@osk4rrv](https://github.com/osk4rrv).
