# Changelog

## 1.3.0

- Added the production Cloudflare web application and isolated VPS obfuscation service for JavaScript and LuaJIT.
- Added persistent three-success rolling-hour allowances tracked independently by IP and a one-hour cookie.
- Removed application-defined input and output byte caps, with raw source and generated output streaming through temporary files.
- Replaced misleading VM terminology with encoded-loader wording and improved the consent, output, copy, and save interface states.

## 1.2.0

- Added support for JavaScript code.

## 1.1.0

- Added encoded loader format 1 with a unique HALT opcode, strict program termination checks, reconstructed-source length validation, and Adler-32 integrity verification.
- Added safe LuaJIT/`bit` discovery and a stable `@openobfuscator-loader` chunk name while preserving varargs and multiple returns.
- Removed destructive host hook and JIT changes from anti-debug preludes.
- Added explicit seed-presence tracking, strict uint32 CLI/GUI seed parsing, `-s`, `--version`, and improved argument errors.
- Added a C++17 CMake build, install rules, ZIP packaging, unit tests, and an optional LuaJIT end-to-end test.
- Updated user-facing terminology from VM to encoded loader.
