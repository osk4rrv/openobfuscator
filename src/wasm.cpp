#include "luaobf.h"

#include <emscripten/emscripten.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>

namespace {

thread_local std::string resultBuffer;
thread_local std::string errorBuffer;

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE
const char* oo_obfuscate(const char* source, size_t sourceLength, int language, uint32_t seed, int preset) {
    if (source == nullptr) {
        errorBuffer = "Source pointer is null";
        return nullptr;
    }

    try {
        const std::string_view sourceView(source, sourceLength);
        luaobf::ObfuscationOptions options;
        options.language = language == 0
            ? luaobf::Language::Lua
            : luaobf::Language::JavaScript;
        options.seed = seed;
        options.seedProvided = true;
        options.virtualizeBytecode = true;
        options.luaJitMode = options.language == luaobf::Language::Lua;
        options.preserveOpenObfuscatorStyle = preset >= 1;
        options.addAntiDebug = preset >= 2;

        resultBuffer = luaobf::Obfuscator(options).obfuscate(sourceView);
        errorBuffer.clear();
        return resultBuffer.c_str();
    } catch (const std::exception& error) {
        resultBuffer.clear();
        errorBuffer = error.what();
        return nullptr;
    } catch (...) {
        resultBuffer.clear();
        errorBuffer = "Unknown native engine error";
        return nullptr;
    }
}

EMSCRIPTEN_KEEPALIVE
const char* oo_last_error() {
    return errorBuffer.c_str();
}

} // extern "C"
