#include "luaobf.h"

#include <iostream>
#include <regex>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

luaobf::ObfuscationOptions seeded(uint32_t seed) {
    luaobf::ObfuscationOptions options;
    options.seed = seed;
    options.seedProvided = true;
    return options;
}

luaobf::ObfuscationOptions vmSeeded(uint32_t seed) {
    luaobf::ObfuscationOptions options = seeded(seed);
    options.addAntiDebug = false;
    options.preserveOpenObfuscatorStyle = false;
    return options;
}

bool contains(const std::string& text, const std::string& fragment) {
    return text.find(fragment) != std::string::npos;
}

} // namespace

int main() {
    const std::string source = "return 42\n";

    const std::string zeroA = luaobf::Obfuscator(vmSeeded(0)).obfuscate(source);
    const std::string zeroB = luaobf::Obfuscator(vmSeeded(0)).obfuscate(source);
    const std::string other = luaobf::Obfuscator(vmSeeded(1)).obfuscate(source);
    luaobf::ObfuscationOptions legacyOptions = vmSeeded(123);
    legacyOptions.seedProvided = false;
    const std::string legacyA = luaobf::Obfuscator(legacyOptions).obfuscate(source);
    const std::string legacyB = luaobf::Obfuscator(legacyOptions).obfuscate(source);

    expect(zeroA == zeroB, "an explicitly provided seed 0 must be deterministic");
    expect(zeroA != other, "different seeds must produce different output");
    expect(legacyA == legacyB, "a legacy nonzero seed must remain deterministic without seedProvided");
    expect(contains(zeroA, "OpenObfuscator encoded loader format 1"), "loader format marker is missing");
    expect(contains(zeroA, "integrity:loader"), "loader integrity failure marker is missing");
    expect(contains(zeroA, "integrity:luajit"), "LuaJIT validation marker is missing");
    expect(contains(zeroA, "pcall(require,'bit')"), "safe bit module fallback is missing");
    expect(contains(zeroA, "%65521"), "Adler-32 validation is missing");
    expect(contains(zeroA, "~=10 then error(\"integrity:loader\""), "exact source length validation is missing");
    expect(contains(zeroA, "@openobfuscator-loader"), "encoded loader chunk name is missing");

    std::smatch haltState;
    const bool hasHaltState = std::regex_search(zeroA, haltState,
        std::regex("local (l[[:alnum:]]+)=false\\n"));
    expect(hasHaltState, "HALT state is missing");
    if (hasHaltState) {
        const std::string state = haltState[1].str();
        expect(contains(zeroA, "if " + state + " then error(\"integrity:loader\""),
            "instructions after HALT are not rejected");
        expect(contains(zeroA, "if not " + state + " then error(\"integrity:loader\""),
            "missing HALT is not rejected");
    }

    luaobf::ObfuscationOptions antiDebug = seeded(0);
    antiDebug.luaJitMode = false;
    antiDebug.virtualizeBytecode = false;
    antiDebug.obfuscateStrings = false;
    antiDebug.virtualizeStrings = false;
    antiDebug.obfuscateNumbers = false;
    antiDebug.renameIdentifiers = false;
    antiDebug.injectJunkCode = false;
    antiDebug.compressWhitespace = false;
    antiDebug.preserveOpenObfuscatorStyle = false;
    const std::string antiDebugOutput = luaobf::Obfuscator(antiDebug).obfuscate("return true\n");

    expect(contains(antiDebugOutput, "debug"), "anti-debug prelude was not emitted");
    expect(!contains(antiDebugOutput, "debug.sethook"), "anti-debug prelude must not remove host hooks");
    expect(!contains(antiDebugOutput, "jit.flush"), "anti-debug prelude must not flush host JIT state");
    expect(!contains(antiDebugOutput, "collectgarbage"), "anti-debug prelude must not trigger host GC");

    if (failures != 0) return 1;
    std::cout << "All OpenObfuscator tests passed\n";
    return 0;
}
