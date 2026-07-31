#include "luaobf.h"
#include "gui.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

static void printUsage(const char* prog) {
    std::cout << "OpenObfuscator " << luaobf::Version << " - Lua/LuaJIT and JavaScript source obfuscator\n"
              << "Usage: " << prog << " [options] <input.lua|input.js> [output]\n\n"
              << "Options:\n"
              << "  -o <file>         Output file (default: stdout)\n"
              << "  -l, --language <language>\n"
              << "                    Input language: lua, javascript, js, or auto\n"
              << "                    (default: infer from .lua/.js/.cjs)\n"
              << "  -s, --seed <n>    Set uint32 random seed for reproducible output\n"
              << "  --no-numbers      Disable number obfuscation\n"
              << "  --no-strings      Disable string obfuscation\n"
              << "  --no-rename       Disable identifier renaming\n"
              << "  --no-junk         Disable junk code injection\n"
              << "  --no-antidebug    Disable anti-debug code\n"
              << "  --no-compress     Keep whitespace and comments\n"
              << "  --no-vm           Disable the encoded source loader wrapper\n"
              << "  --no-luajit       Disable LuaJIT-only output for Lua input\n"
              << "  --no-style        Disable OpenObfuscator.us style banner/prelude\n"
              << "  --flatten         Enable control flow flattening\n"
              << "  --gui             Open graphical interface\n"
              << "  --version         Show version\n"
              << "  -h, --help        Show this help\n"
              << std::endl;
}

static bool parseUint32(std::string_view text, uint32_t& value) {
    if (text.empty()) return false;
    uint32_t parsed = 0;
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc() || result.ptr != end) return false;
    value = parsed;
    return true;
}

static std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

static bool parseLanguage(std::string_view text, std::optional<luaobf::Language>& language) {
    const std::string value = lowercase(std::string(text));
    if (value == "auto") {
        language.reset();
        return true;
    }
    if (value == "lua" || value == "luajit") {
        language = luaobf::Language::Lua;
        return true;
    }
    if (value == "javascript" || value == "js") {
        language = luaobf::Language::JavaScript;
        return true;
    }
    return false;
}

static luaobf::Language inferLanguage(const std::string& path) {
    const std::string extension = lowercase(fs::path(path).extension().string());
    if (extension == ".js" || extension == ".cjs") {
        return luaobf::Language::JavaScript;
    }
    return luaobf::Language::Lua;
}

static std::string readFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open input file: " << path << std::endl;
        std::exit(1);
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

static void writeFile(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open output file: " << path << std::endl;
        std::exit(1);
    }
    file << content;
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
#ifdef _WIN32
        return luaobf::runGui();
#else
        printUsage(argv[0]);
        return 0;
#endif
    }

    luaobf::ObfuscationOptions opts;
    std::optional<luaobf::Language> requestedLanguage;
    std::string inputFile;
    std::string outputFile;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        if (arg == "--version") {
            std::cout << "OpenObfuscator " << luaobf::Version << '\n';
            return 0;
        }
        if (arg == "--gui") {
            return luaobf::runGui();
        }
        if (arg == "-l" || arg == "--language") {
            if (i + 1 >= argc) {
                std::cerr << "Error: Missing value for " << arg << ".\n";
                return 1;
            }
            const std::string languageText = argv[++i];
            if (!parseLanguage(languageText, requestedLanguage)) {
                std::cerr << "Error: Unsupported language: " << languageText
                          << ". Expected lua, javascript, js, or auto.\n";
                return 1;
            }
        } else if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "Error: Missing value for -o.\n";
                return 1;
            }
            outputFile = argv[++i];
        } else if (arg == "-s" || arg == "--seed") {
            if (i + 1 >= argc) {
                std::cerr << "Error: Missing value for " << arg << ".\n";
                return 1;
            }
            const std::string seedText = argv[++i];
            if (!parseUint32(seedText, opts.seed)) {
                std::cerr << "Error: Invalid uint32 seed: " << seedText << '\n';
                return 1;
            }
            opts.seedProvided = true;
        } else if (arg == "--no-numbers") {
            opts.obfuscateNumbers = false;
        } else if (arg == "--no-strings") {
            opts.obfuscateStrings = false;
            opts.virtualizeStrings = false;
        } else if (arg == "--no-rename") {
            opts.renameIdentifiers = false;
        } else if (arg == "--no-junk") {
            opts.injectJunkCode = false;
        } else if (arg == "--no-antidebug") {
            opts.addAntiDebug = false;
        } else if (arg == "--no-compress") {
            opts.compressWhitespace = false;
        } else if (arg == "--no-vm") {
            opts.virtualizeBytecode = false;
        } else if (arg == "--no-luajit") {
            opts.luaJitMode = false;
        } else if (arg == "--no-style") {
            opts.preserveOpenObfuscatorStyle = false;
        } else if (arg == "--flatten") {
            opts.flattenControlFlow = true;
        } else if (!arg.empty() && arg[0] != '-') {
            if (inputFile.empty()) {
                inputFile = arg;
            } else if (outputFile.empty()) {
                outputFile = arg;
            } else {
                std::cerr << "Error: Unexpected argument: " << arg << '\n';
                return 1;
            }
        } else {
            std::cerr << "Error: Unknown option: " << arg << '\n';
            return 1;
        }
    }

    if (inputFile.empty()) {
        std::cerr << "Error: Missing input file.\n";
        printUsage(argv[0]);
        return 1;
    }

    if (!fs::exists(inputFile)) {
        std::cerr << "Error: Input file not found: " << inputFile << std::endl;
        return 1;
    }

    if (lowercase(fs::path(inputFile).extension().string()) == ".mjs") {
        std::cerr << "Error: ECMAScript modules (.mjs) are not supported by the JavaScript encoded source loader.\n";
        return 1;
    }

    opts.language = requestedLanguage.value_or(inferLanguage(inputFile));
    if (opts.language == luaobf::Language::Lua && !opts.luaJitMode) {
        opts.virtualizeBytecode = false;
    }

    const std::string source = readFile(inputFile);
    luaobf::Obfuscator obfuscator(opts);
    const std::string result = obfuscator.obfuscate(source);

    if (!outputFile.empty()) {
        writeFile(outputFile, result);
        std::cout << "Obfuscated: " << inputFile << " -> " << outputFile
                  << " (" << result.size() << " bytes)" << std::endl;
    } else {
        std::cout << result;
    }

    return 0;
}
