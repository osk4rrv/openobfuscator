#include "luaobf.h"
#include "gui.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

static void printUsage(const char* prog) {
    std::cout << "LuaObfuscator - Military Grade Lua/LuaJIT Obfuscator\n"
              << "Usage: " << prog << " [options] <input.lua> [output.lua]\n\n"
              << "Options:\n"
              << "  -o <file>         Output file (default: stdout)\n"
              << "  -s <seed>         Random seed for reproducible output\n"
              << "  --no-numbers      Disable number obfuscation\n"
              << "  --no-strings      Disable string obfuscation\n"
              << "  --no-rename       Disable identifier renaming\n"
              << "  --no-junk         Disable junk code injection\n"
              << "  --no-antidebug    Disable anti-debug code\n"
              << "  --no-compress     Keep whitespace and comments\n"
              << "  --no-vm           Disable LuaJIT VM bytecode wrapper\n"
              << "  --no-luajit       Disable LuaJIT-only output requirement\n"
              << "  --no-style        Disable OpenObfuscator.us style banner/prelude\n"
              << "  --flatten         Enable control flow flattening\n"
              << "  --seed <n>        Set random seed\n"
              << "  --gui             Open graphical interface\n"
              << "  -h, --help        Show this help\n"
              << std::endl;
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
        return luaobf::runGui();
    }

    luaobf::ObfuscationOptions opts;
    std::string inputFile;
    std::string outputFile;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--gui") {
            return luaobf::runGui();
        } else if (arg == "-o" && i + 1 < argc) {
            outputFile = argv[++i];
        } else if (arg == "--seed" && i + 1 < argc) {
            opts.seed = static_cast<uint32_t>(std::stoul(argv[++i]));
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
            opts.virtualizeBytecode = false;
        } else if (arg == "--no-style") {
            opts.preserveOpenObfuscatorStyle = false;
        } else if (arg == "--flatten") {
            opts.flattenControlFlow = true;
        } else if (!arg.empty() && arg[0] != '-') {
            if (inputFile.empty()) {
                inputFile = arg;
            } else if (outputFile.empty()) {
                outputFile = arg;
            }
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }

    if (inputFile.empty()) {
        printUsage(argv[0]);
        return 1;
    }

    if (!fs::exists(inputFile)) {
        std::cerr << "Error: Input file not found: " << inputFile << std::endl;
        return 1;
    }

    std::string source = readFile(inputFile);

    luaobf::Obfuscator obfuscator(opts);
    std::string result = obfuscator.obfuscate(source);

    if (!outputFile.empty()) {
        writeFile(outputFile, result);
        std::cout << "Obfuscated: " << inputFile << " -> " << outputFile
                  << " (" << result.size() << " bytes)" << std::endl;
    } else {
        std::cout << result;
    }

    return 0;
}
