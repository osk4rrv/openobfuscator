#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cstdint>
#include <random>
#include <functional>

namespace luaobf {

enum class TokenType : uint8_t {
    WHITESPACE,
    NEWLINE,
    COMMENT_SHORT,
    COMMENT_LONG,
    KEYWORD,
    IDENTIFIER,
    NUMBER,
    STRING_DQUOTE,
    STRING_SQUOTE,
    STRING_LONG,
    OPERATOR,
    SEPARATOR,
    EOF_TOKEN,
    UNKNOWN
};

struct Token {
    TokenType type;
    std::string value;
    size_t line;
    size_t col;
};

struct ObfuscationOptions {
    bool obfuscateNumbers = true;
    bool obfuscateStrings = true;
    bool renameIdentifiers = true;
    bool injectJunkCode = true;
    bool flattenControlFlow = false;
    bool addAntiDebug = true;
    bool virtualizeStrings = true;
    bool virtualizeBytecode = true;
    bool luaJitMode = true;
    bool preserveOpenObfuscatorStyle = true;
    bool compressWhitespace = true;
    uint32_t seed = 0;
};

class Obfuscator {
public:
    explicit Obfuscator(ObfuscationOptions opts = {});

    std::string obfuscate(std::string_view source);

private:
    ObfuscationOptions m_opts;
    std::mt19937_64 m_rng;

    std::vector<Token> tokenize(std::string_view source);
    std::string recompose(const std::vector<Token>& tokens);

    void passNumberObfuscation(std::vector<Token>& tokens);
    void passStringObfuscation(std::vector<Token>& tokens);
    void passRenameIdentifiers(std::vector<Token>& tokens);
    void passInjectJunkCode(std::vector<Token>& tokens);
    void passFlattenControlFlow(std::vector<Token>& tokens);
    void passAntiDebug(std::vector<Token>& tokens);
    void passCompressWhitespace(std::vector<Token>& tokens);

    std::string obfuscateNumber(const std::string& numStr);
    std::string obfuscateString(const std::string& str, uint8_t key);
    std::string buildBanner();
    std::string buildStylePrelude();
    std::string buildAntiDebugPrelude();
    std::string buildLuaJitBytecodeVm(std::string_view source);
    std::string generateRandomName(size_t len);
    std::string generateJunkCode();

    uint8_t randomByte();
    uint32_t randomU32();
    uint64_t randomU64();
    std::string randomHex(size_t len);
    std::string randomAlphaNum(size_t len);

    static const std::unordered_set<std::string>& luaKeywords();
    static bool isKeyword(std::string_view word);
    static bool isBuiltin(std::string_view word);
};

std::string base64Encode(const std::vector<uint8_t>& data);
std::string xorEncode(const std::string& data, uint8_t key);
std::string toByteLiteral(const std::string& data);

} // namespace luaobf
