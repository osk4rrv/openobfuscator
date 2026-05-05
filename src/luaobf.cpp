#include "luaobf.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <set>
#include <sstream>

namespace luaobf {

const std::unordered_set<std::string>& Obfuscator::luaKeywords() {
    static const std::unordered_set<std::string> kw = {
        "and",      "break",    "do",       "else",     "elseif",
        "end",      "false",    "for",      "function", "goto",
        "if",       "in",       "local",    "nil",      "not",
        "or",       "repeat",   "return",   "then",     "true",
        "until",    "while"
    };
    return kw;
}

bool Obfuscator::isKeyword(std::string_view word) {
    return luaKeywords().count(std::string(word)) > 0;
}

bool Obfuscator::isBuiltin(std::string_view word) {
    static const std::unordered_set<std::string_view> builtins = {
        "_G", "_VERSION", "assert", "collectgarbage", "dofile", "error",
        "getmetatable", "ipairs", "load", "loadfile", "loadstring",
        "next", "pairs", "pcall", "print", "rawequal", "rawget",
        "rawlen", "rawset", "require", "select", "setmetatable",
        "tonumber", "tostring", "type", "xpcall",
        "string", "table", "math", "io", "os", "debug", "coroutine",
        "bit", "jit", "ffi",
        "char", "byte", "sub", "rep", "find", "match", "gmatch", "gsub",
        "len", "format", "upper", "lower", "reverse",
        "abs", "acos", "asin", "atan", "ceil", "cos", "deg", "exp",
        "floor", "fmod", "frexp", "ldexp", "log", "log10", "max",
        "min", "modf", "pi", "pow", "rad", "random", "randomseed",
        "sin", "sqrt", "tan",
        "insert", "remove", "sort", "concat",
        "open", "read", "write", "close", "flush", "lines",
        "clock", "date", "difftime", "execute", "exit", "getenv",
        "remove", "rename", "setlocale", "time", "tmpname",
        "band", "bor", "bxor", "bnot", "lshift", "rshift", "arshift",
        "toboolean", "tobit", "tohex",
    };
    return builtins.count(word) > 0;
}

Obfuscator::Obfuscator(ObfuscationOptions opts)
    : m_opts(std::move(opts))
{
    if (m_opts.seed == 0) {
        std::random_device rd;
        m_opts.seed = rd();
    }
    m_rng.seed(m_opts.seed);
}

uint8_t Obfuscator::randomByte() {
    return static_cast<uint8_t>(m_rng() & 0xFF);
}

uint32_t Obfuscator::randomU32() {
    return static_cast<uint32_t>(m_rng());
}

uint64_t Obfuscator::randomU64() {
    return m_rng();
}

std::string Obfuscator::randomHex(size_t len) {
    static const char hexChars[] = "0123456789abcdef";
    std::string result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        result += hexChars[m_rng() & 0xF];
    }
    return result;
}

std::string Obfuscator::randomAlphaNum(size_t len) {
    static const char chars[] =
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        result += chars[m_rng() % (sizeof(chars) - 1)];
    }
    return result;
}

static bool isTriviaToken(const Token& tok) {
    return tok.type == TokenType::WHITESPACE ||
           tok.type == TokenType::NEWLINE ||
           tok.type == TokenType::COMMENT_SHORT ||
           tok.type == TokenType::COMMENT_LONG;
}

static size_t previousSignificant(const std::vector<Token>& tokens, size_t index) {
    while (index > 0) {
        --index;
        if (!isTriviaToken(tokens[index])) return index;
    }
    return tokens.size();
}

static size_t nextSignificant(const std::vector<Token>& tokens, size_t index) {
    for (++index; index < tokens.size(); ++index) {
        if (!isTriviaToken(tokens[index])) return index;
    }
    return tokens.size();
}

static std::string luaStringLiteral(std::string_view value) {
    std::ostringstream ss;
    ss << "\"";
    for (unsigned char c : value) {
        switch (c) {
            case '\a': ss << "\\a"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            case '\v': ss << "\\v"; break;
            case '\\': ss << "\\\\"; break;
            case '\"': ss << "\\\""; break;
            default:
                if (c < 32 || c > 126) {
                    ss << "\\" << std::setw(3) << std::setfill('0')
                       << static_cast<int>(c) << std::setfill(' ');
                } else {
                    ss << static_cast<char>(c);
                }
                break;
        }
    }
    ss << "\"";
    return ss.str();
}

static std::string emitNumberList(const std::vector<uint32_t>& data, size_t columns = 28) {
    std::ostringstream ss;
    for (size_t i = 0; i < data.size(); ++i) {
        if (i > 0) ss << ",";
        if (i > 0 && i % columns == 0) ss << "\n";
        ss << data[i];
    }
    return ss.str();
}

static bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

static bool isIdentCont(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

static bool isDigit(char c) {
    return std::isdigit(static_cast<unsigned char>(c));
}

static bool isHexDigit(char c) {
    return std::isxdigit(static_cast<unsigned char>(c));
}

static std::string unescapeString(std::string_view raw) {
    std::string result;
    result.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c == '\\' && i + 1 < raw.size()) {
            char next = raw[++i];
            switch (next) {
                case 'a':  result += '\a'; break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                case 'v':  result += '\v'; break;
                case '\\': result += '\\'; break;
                case '\"': result += '\"'; break;
                case '\'': result += '\''; break;
                case 'z': {
                    while (i + 1 < raw.size() && std::isspace(static_cast<unsigned char>(raw[i + 1])))
                        ++i;
                    break;
                }
                case 'x': {
                    std::string hex;
                    for (size_t j = 0; j < 2 && i + 1 < raw.size() && isHexDigit(raw[i + 1]); ++j)
                        hex += raw[++i];
                    if (!hex.empty()) {
                        result += static_cast<char>(std::stoi(hex, nullptr, 16));
                    }
                    break;
                }
                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9': {
                    std::string dec;
                    dec += next;
                    for (size_t j = 0; j < 2 && i + 1 < raw.size() && std::isdigit(static_cast<unsigned char>(raw[i + 1])); ++j)
                        dec += raw[++i];
                    result += static_cast<char>(std::stoi(dec));
                    break;
                }
                default: {
                    result += '\\';
                    result += next;
                    break;
                }
            }
        } else {
            result += c;
        }
    }
    return result;
}

std::vector<Token> Obfuscator::tokenize(std::string_view source) {
    std::vector<Token> tokens;
    size_t i = 0;
    const size_t len = source.size();
    size_t line = 1;
    size_t col = 1;

    auto advance = [&]() -> char {
        if (i >= len) return '\0';
        col++;
        char c = source[i++];
        if (c == '\n') { line++; col = 1; }
        return c;
    };

    auto peek = [&](size_t ahead = 0) -> char {
        if (i + ahead >= len) return '\0';
        return source[i + ahead];
    };

    auto add = [&](TokenType t, std::string v, size_t l, size_t c) {
        tokens.push_back({t, std::move(v), l, c});
    };

    while (i < len) {
        char c = source[i];
        size_t tokLine = line;
        size_t tokCol = col;

        if (c == '\n') {
            advance();
            add(TokenType::NEWLINE, "\n", tokLine, tokCol);
            continue;
        }
        if (c == '\r' && peek() == '\n') {
            advance();
            advance();
            add(TokenType::NEWLINE, "\n", tokLine, tokCol);
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            std::string ws;
            while (i < len && std::isspace(static_cast<unsigned char>(source[i]))
                   && source[i] != '\n' && source[i] != '\r') {
                ws += advance();
            }
            add(TokenType::WHITESPACE, ws, tokLine, tokCol);
            continue;
        }

        if (c == '-' && peek() == '-') {
            advance(); advance();
            if (peek() == '[' && peek(1) == '[') {
                advance(); advance();
                size_t eqCount = 0;
                while (peek() == '=') { advance(); eqCount++; }
                std::string closeTag = "]";
                for (size_t k = 0; k < eqCount; k++) closeTag += '=';
                closeTag += ']';

                std::string comment = "--[[";
                for (size_t k = 0; k < eqCount; k++) comment += '=';
                comment += '[';

                for (;;) {
                    if (i >= len) break;
                    if (source[i] == ']') {
                        size_t pos = i;
                        std::string tail;
                        size_t cnt = 0;
                        tail += source[pos++];
                        while (pos < len && source[pos] == '=') {
                            tail += source[pos++];
                            cnt++;
                        }
                        if (pos < len && source[pos] == ']') {
                            tail += source[pos];
                            if (cnt == eqCount) {
                                for (size_t k = 0; k < tail.size(); k++) advance();
                                comment += tail;
                                break;
                            }
                        }
                    }
                    comment += advance();
                }
                add(TokenType::COMMENT_LONG, comment, tokLine, tokCol);
            } else {
                std::string comment = "--";
                while (peek() != '\n' && peek() != '\r' && peek() != '\0') {
                    comment += advance();
                }
                add(TokenType::COMMENT_SHORT, comment, tokLine, tokCol);
            }
            continue;
        }

        if (c == '\"') {
            advance();
            std::string raw;
            while (i < len) {
                char ch = source[i];
                if (ch == '\"' && (raw.empty() || raw.back() != '\\')) {
                    advance();
                    break;
                }
                if (ch == '\n' || ch == '\r') break;
                raw += advance();
            }
            std::string unescaped = unescapeString(raw);
            add(TokenType::STRING_DQUOTE, unescaped, tokLine, tokCol);
            continue;
        }

        if (c == '\'') {
            advance();
            std::string raw;
            while (i < len) {
                char ch = source[i];
                if (ch == '\'' && (raw.empty() || raw.back() != '\\')) {
                    advance();
                    break;
                }
                if (ch == '\n' || ch == '\r') break;
                raw += advance();
            }
            std::string unescaped = unescapeString(raw);
            add(TokenType::STRING_SQUOTE, unescaped, tokLine, tokCol);
            continue;
        }

        if (c == '[' && (peek() == '[' || peek() == '=')) {
            advance();
            std::string tag;
            tag += c;
            size_t eqCount = 0;
            while (i < len && source[i] == '=') {
                tag += advance();
                eqCount++;
            }
            if (i < len && source[i] == '[') {
                tag += advance();
                std::string closeTag = "]";
                for (size_t k = 0; k < eqCount; k++) closeTag += '=';
                closeTag += ']';

                std::string content;
                for (;;) {
                    if (i >= len) break;
                    if (source[i] == ']') {
                        size_t pos = i;
                        std::string tail;
                        size_t cnt = 0;
                        tail += source[pos++];
                        while (pos < len && source[pos] == '=') {
                            tail += source[pos++];
                            cnt++;
                        }
                        if (pos < len && source[pos] == ']') {
                            if (cnt == eqCount) {
                                for (size_t k = 0; k < tail.size(); k++) advance();
                                break;
                            }
                        }
                    }
                    content += advance();
                }
                add(TokenType::STRING_LONG, content, tokLine, tokCol);
                continue;
            }
            add(TokenType::OPERATOR, "[", tokLine, tokCol);
            continue;
        }

        if (isIdentStart(c)) {
            std::string ident;
            ident += advance();
            while (i < len && isIdentCont(peek())) {
                ident += advance();
            }
            if (isKeyword(ident)) {
                add(TokenType::KEYWORD, ident, tokLine, tokCol);
            } else {
                add(TokenType::IDENTIFIER, ident, tokLine, tokCol);
            }
            continue;
        }

        if (isDigit(c) || (c == '.' && isDigit(peek()))) {
            std::string num;
            bool isHex = false;
            if (c == '0' && (peek() == 'x' || peek() == 'X')) {
                num += advance();
                num += advance();
                isHex = true;
                while (i < len && isHexDigit(peek()))
                    num += advance();
            } else {
                num += advance();
                while (i < len && isDigit(peek()))
                    num += advance();
                if (peek() == '.') {
                    num += advance();
                    while (i < len && isDigit(peek()))
                        num += advance();
                }
                if (peek() == 'e' || peek() == 'E') {
                    num += advance();
                    if (peek() == '+' || peek() == '-')
                        num += advance();
                    while (i < len && isDigit(peek()))
                        num += advance();
                }
            }
            if (peek() == '.' && num.find('.') == std::string::npos
                && (num.size() < 2 || num[1] != 'x')) {
            }
            add(TokenType::NUMBER, num, tokLine, tokCol);
            continue;
        }

        if (c == '.') {
            if (peek() == '.') {
                advance(); advance();
                if (peek() == '.') {
                    advance();
                    add(TokenType::OPERATOR, "...", tokLine, tokCol);
                } else {
                    add(TokenType::OPERATOR, "..", tokLine, tokCol);
                }
                continue;
            }
            advance();
            add(TokenType::OPERATOR, ".", tokLine, tokCol);
            continue;
        }

        if (c == ':') {
            advance();
            if (peek() == ':') {
                advance();
                add(TokenType::OPERATOR, "::", tokLine, tokCol);
            } else {
                add(TokenType::SEPARATOR, ":", tokLine, tokCol);
            }
            continue;
        }

        const char* multiOps[] = { "<=", ">=", "==", "~=", "//" };
        bool foundMulti = false;
        for (auto& mo : multiOps) {
            if (c == mo[0] && peek() == mo[1]) {
                std::string op; op += c; op += mo[1];
                advance(); advance();
                add(TokenType::OPERATOR, op, tokLine, tokCol);
                foundMulti = true;
                break;
            }
        }
        if (foundMulti) continue;

        static const char* singleOps = "+-*/%^#<>=~(),;{}[]";
        if (std::strchr(singleOps, c)) {
            std::string op(1, c);
            advance();
            add(TokenType::OPERATOR, op, tokLine, tokCol);
            continue;
        }

        std::string unk(1, c);
        advance();
        add(TokenType::UNKNOWN, unk, tokLine, tokCol);
    }

    add(TokenType::EOF_TOKEN, "", line, col);
    return tokens;
}

std::string Obfuscator::generateRandomName(size_t len) {
    std::string name;
    name.reserve(len + 2);
    name += "l";
    name += "abcdefghijklmnopqrstuvwxyz0123456789"[m_rng() % 36];
    for (size_t i = 1; i < len; ++i) {
        name += "abcdefghijklmnopqrstuvwxyz0123456789"[m_rng() % 36];
    }
    return name;
}

std::string Obfuscator::obfuscateNumber(const std::string& numStr) {
    double value;
    auto [ptr, ec] = std::from_chars(numStr.data(), numStr.data() + numStr.size(), value);
    if (ec != std::errc()) return numStr;

    int method = static_cast<int>(m_rng() % 5);
    switch (method) {
        case 0: {
            double a = static_cast<double>(m_rng() % 0xFFFF) + 1.0;
            double b = value * a;
            std::ostringstream ss;
            ss << std::setprecision(12) << "(" << b << "/" << a << ")";
            return ss.str();
        }
        case 1: {
            double a = value + static_cast<double>((m_rng() % 4096) + 1);
            double b = a - value;
            std::ostringstream ss;
            ss << std::setprecision(12) << "(" << a << "-" << b << ")";
            return ss.str();
        }
        case 2: {
            double a = value / 2.0;
            double b = value - a;
            std::ostringstream ss;
            ss << std::setprecision(12) << "(" << a << "+" << b << ")";
            return ss.str();
        }
        case 3: {
            if (value > 0 && value < 0xFFFF) {
                uint64_t v = static_cast<uint64_t>(value);
                uint64_t r = m_rng() % 1000 + 2;
                uint64_t q = v * r;
                std::ostringstream ss;
                ss << "(" << q << "/" << r << ")";
                return ss.str();
            }
            return numStr;
        }
        case 4: {
            if (value > 0 && value < 0xFFFF && value == std::floor(value)) {
                uint64_t v = static_cast<uint64_t>(value);
                uint64_t a = v / 2;
                uint64_t b = v - a;
                std::ostringstream ss;
                ss << "(" << a << "+" << b << ")";
                return ss.str();
            }
            return numStr;
        }
    }
    return numStr;
}

std::string Obfuscator::obfuscateString(const std::string& str, uint8_t key) {
    (void)str;
    (void)key;
    return "";
}

std::string xorEncode(const std::string& data, uint8_t key) {
    std::string result;
    result.reserve(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        result += static_cast<char>(data[i] ^ key);
    }
    return result;
}

std::string toByteLiteral(const std::string& data) {
    std::ostringstream ss;
    for (size_t i = 0; i < data.size(); ++i) {
        if (i > 0) ss << ",";
        ss << static_cast<int>(static_cast<unsigned char>(data[i]));
    }
    return ss.str();
}

std::string base64Encode(const std::vector<uint8_t>& data) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    size_t val = 0;
    int valb = -6;
    for (uint8_t c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            result += table[(val >> valb) & 0x3F];
            valb -= 6;
        }
    }
    if (valb > -6)
        result += table[((val << 8) >> (valb + 8)) & 0x3F];
    while (result.size() % 4)
        result += '=';
    return result;
}

std::string Obfuscator::buildBanner() {
    std::ostringstream ss;
    ss << "-- Obfuscated by OpenObfuscator.us\n"
       << "-- https://github.com/osk4rrv/openobfuscator \n"
       << "-- https://openobfuscator.us/\n";
    if (m_opts.preserveOpenObfuscatorStyle) {
        ss << "-- local 1fh2034b = /245 /354 /451\n"
           << "-- local 13tgf = {/344/245/143/46/256/14/1/145/148/146}\n"
           << "-- local 3r33fd = {...}\n";
    }
    return ss.str();
}

std::string Obfuscator::buildStylePrelude() {
    if (!m_opts.preserveOpenObfuscatorStyle) return "";

    std::ostringstream slash;
    const size_t slashCount = 8 + (m_rng() % 7);
    for (size_t i = 0; i < slashCount; ++i) {
        if (i > 0) slash << " ";
        slash << "/" << (1 + (m_rng() % 511));
    }

    std::ostringstream list;
    const size_t listCount = 9 + (m_rng() % 9);
    for (size_t i = 0; i < listCount; ++i) {
        if (i > 0) list << ",";
        list << "\"/" << (1 + (m_rng() % 511)) << "\"";
    }

    std::ostringstream ss;
    ss << "local _" << randomAlphaNum(7) << "=" << luaStringLiteral(slash.str()) << "\n"
       << "local _" << randomAlphaNum(5) << "={" << list.str() << "}\n"
       << "local _" << randomAlphaNum(6) << "={...}\n";
    return ss.str();
}

std::string Obfuscator::buildAntiDebugPrelude() {
    std::ostringstream ss;
    ss << "pcall(function()"
       << "local a=debug and debug.getinfo and debug.getinfo(1,'S') or nil;"
       << "if a and a.short_src and a.short_src:match('^@') then return end;"
       << "if debug and debug.sethook then debug.sethook(nil) end;"
       << "if jit and jit.flush then jit.flush() end;"
       << "end)\n";
    return ss.str();
}

std::string Obfuscator::buildLuaJitBytecodeVm(std::string_view source) {
    const uint8_t initialKey = static_cast<uint8_t>((randomByte() | 1U) & 0xFFU);
    std::set<int> opSet;
    auto nextOp = [&]() {
        int value = 0;
        do {
            value = 17 + static_cast<int>(m_rng() % 221);
        } while (opSet.count(value));
        opSet.insert(value);
        return value;
    };

    const int opEmit = nextOp();
    const int opMutate = nextOp();
    const int opNoise = nextOp();
    std::vector<uint32_t> program;
    program.reserve(source.size() + (source.size() / 9) + 8);

    uint8_t key = initialKey;
    auto pushWord = [&](int op, int a, int b, int c) {
        uint32_t word =
            (static_cast<uint32_t>(op & 0xFF) << 24) |
            (static_cast<uint32_t>(a & 0xFF) << 16) |
            (static_cast<uint32_t>(b & 0xFF) << 8) |
            static_cast<uint32_t>(c & 0xFF);
        program.push_back(word);
    };

    for (size_t i = 0; i < source.size(); ++i) {
        if ((i % 11) == 0) {
            uint8_t a = randomByte();
            uint8_t c = randomByte();
            pushWord(opMutate, a, randomByte(), c);
            key = static_cast<uint8_t>(((key ^ a) + c) & 0xFFU);
        } else if ((i % 17) == 0) {
            pushWord(opNoise, randomByte(), randomByte(), randomByte());
        }

        uint8_t plain = static_cast<uint8_t>(source[i]);
        uint8_t pad = randomByte();
        uint8_t encoded = static_cast<uint8_t>(plain ^ ((key + pad) & 0xFFU));
        pushWord(opEmit, encoded, pad, randomByte());
        uint32_t ip = static_cast<uint32_t>(program.size());
        key = static_cast<uint8_t>((key + pad + plain + ip) & 0xFFU);
    }

    const std::string vBit = generateRandomName(10);
    const std::string vCode = generateRandomName(10);
    const std::string vOut = generateRandomName(10);
    const std::string vKey = generateRandomName(10);
    const std::string vWord = generateRandomName(10);
    const std::string vOp = generateRandomName(10);
    const std::string vChar = generateRandomName(10);
    const std::string vConcat = generateRandomName(10);
    const std::string vSrc = generateRandomName(10);
    const std::string vLoad = generateRandomName(10);
    const std::string vFn = generateRandomName(10);
    const std::string vErr = generateRandomName(10);

    std::ostringstream ss;
    ss << "return(function(...)\n"
       << "local " << vBit << "=bit\n"
       << "if not(jit and " << vBit << " and " << vBit << ".bxor and " << vBit << ".band and " << vBit << ".rshift)then error(\"integrity:luajit\",0)end\n"
       << "local bxor,band,rshift=" << vBit << ".bxor," << vBit << ".band," << vBit << ".rshift\n"
       << "local " << vChar << "=string.char\n"
       << "local " << vConcat << "=table.concat\n"
       << "local " << vCode << "={\n" << emitNumberList(program) << "\n}\n"
       << "local " << vOut << "={}\n"
       << "local " << vKey << "=" << static_cast<int>(initialKey) << "\n"
       << "for ip=1,#" << vCode << " do\n"
       << "local " << vWord << "=" << vCode << "[ip]\n"
       << "local " << vOp << "=band(rshift(" << vWord << ",24),255)\n"
       << "if " << vOp << "==" << opEmit << " then\n"
       << "local e=band(rshift(" << vWord << ",16),255)\n"
       << "local p=band(rshift(" << vWord << ",8),255)\n"
       << "local c=bxor(e,band(" << vKey << "+p,255))\n"
       << vOut << "[#" << vOut << "+1]=" << vChar << "(c)\n"
       << vKey << "=band(" << vKey << "+p+c+ip,255)\n"
       << "elseif " << vOp << "==" << opMutate << " then\n"
       << "local a=band(rshift(" << vWord << ",16),255)\n"
       << "local c=band(" << vWord << ",255)\n"
       << vKey << "=band(bxor(" << vKey << ",a)+c,255)\n"
       << "elseif " << vOp << "==" << opNoise << " then\n"
       << vKey << "=" << vKey << "\n"
       << "else\n"
       << "error(\"integrity:vm\",0)\n"
       << "end\n"
       << "end\n"
       << "local " << vSrc << "=" << vConcat << "(" << vOut << ")\n"
       << "local " << vLoad << "=loadstring or load\n"
       << "local " << vFn << "," << vErr << "=" << vLoad << "(" << vSrc << ")\n"
       << "if not " << vFn << " then error(\"load:vm \"..tostring(" << vErr << "),0)end\n"
       << "return " << vFn << "(...)\n"
       << "end)(...)\n";
    return ss.str();
}

std::string Obfuscator::generateJunkCode() {
    std::ostringstream ss;
    std::string n = randomHex(4);
    int t = static_cast<int>(m_rng() % 4);
    switch (t) {
        case 0:
            ss << "local _" << n << " = "
               << (m_rng() % 10000) << " + " << (m_rng() % 10000)
               << " * " << (m_rng() % 10000) << " - " << (m_rng() % 10000)
               << " / " << ((m_rng() % 100) + 1);
            break;
        case 1:
            ss << "local _" << n << " = ("
               << (m_rng() % 100) << " > " << (m_rng() % 100)
               << ") and " << (m_rng() % 1000) << " or " << (m_rng() % 1000);
            break;
        case 2:
            ss << "local _" << n << " = {"
               << (m_rng() % 256) << ", " << (m_rng() % 256) << ", "
               << (m_rng() % 256) << ", " << (m_rng() % 256) << ", "
               << (m_rng() % 256) << "}";
            break;
        case 3:
            ss << "local _" << n << " = string.char("
               << (m_rng() % 128) << ", " << (m_rng() % 128) << ", "
               << (m_rng() % 128) << ", " << (m_rng() % 128) << ")";
            break;
    }
    return ss.str();
}

void Obfuscator::passNumberObfuscation(std::vector<Token>& tokens) {
    for (auto& tok : tokens) {
        if (tok.type == TokenType::NUMBER) {
            double v;
            auto [ptr, ec] = std::from_chars(tok.value.data(),
                tok.value.data() + tok.value.size(), v);
            if (ec == std::errc()) {
                tok.value = obfuscateNumber(tok.value);
            }
        }
    }
}

void Obfuscator::passStringObfuscation(std::vector<Token>& tokens) {
    std::vector<std::pair<size_t, std::string>> strReplacements;
    size_t strIndex = 0;

    for (size_t i = 0; i < tokens.size(); ++i) {
        auto& tok = tokens[i];
        if (tok.type == TokenType::STRING_DQUOTE ||
            tok.type == TokenType::STRING_SQUOTE ||
            tok.type == TokenType::STRING_LONG) {
            if (tok.value.empty()) continue;

            uint8_t key = randomByte();
            if (key == 0) key = 0x55;
            std::string encoded = xorEncode(tok.value, key);
            std::string byteList = toByteLiteral(encoded);

            std::string varName = generateRandomName(8);
            std::string decoderVar = generateRandomName(8);

            std::ostringstream replacement;
            replacement << "("
                << "(function()"
                << "local " << decoderVar << "={" << byteList << "};"
                << "local " << varName << "={};"
                << "local b=bit and bit.bxor or function(a,c)local r,p=0,1;while a>0 or c>0 do local x,y=a%2,c%2;if x~=y then r=r+p end;a=(a-x)/2;c=(c-y)/2;p=p*2 end;return r end;"
                << "for i=1,#" << decoderVar << " do "
                << varName << "[i]=string.char(b("
                << decoderVar << "[i]," << static_cast<int>(key) << "))"
                << " end;"
                << "return table.concat(" << varName << ")"
                << " end)()"
                << ")";

            strIndex++;
            tok.value = replacement.str();
            tok.type = TokenType::UNKNOWN;
        }
    }
}

void Obfuscator::passRenameIdentifiers(std::vector<Token>& tokens) {
    std::unordered_map<std::string, std::string> renameMap;
    std::set<std::string> declaredLocals;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type != TokenType::KEYWORD || tokens[i].value != "local") continue;
        size_t j = nextSignificant(tokens, i);
        if (j < tokens.size() && tokens[j].type == TokenType::KEYWORD && tokens[j].value == "function") {
            j = nextSignificant(tokens, j);
            if (j < tokens.size() && tokens[j].type == TokenType::IDENTIFIER && !isBuiltin(tokens[j].value)) {
                declaredLocals.insert(tokens[j].value);
            }
            continue;
        }
        bool expectName = true;
        for (; j < tokens.size(); j = nextSignificant(tokens, j)) {
            const Token& tok = tokens[j];
            if (tok.type == TokenType::OPERATOR && tok.value == "=") break;
            if (tok.type == TokenType::SEPARATOR && tok.value == ":") break;
            if (tok.type == TokenType::OPERATOR && tok.value == ",") {
                expectName = true;
                continue;
            }
            if (expectName && tok.type == TokenType::IDENTIFIER && !isBuiltin(tok.value)) {
                declaredLocals.insert(tok.value);
                expectName = false;
                continue;
            }
            if (!isTriviaToken(tok)) expectName = false;
        }
    }

    std::set<std::string> seenNames;
    for (size_t i = 0; i < tokens.size(); ++i) {
        auto& tok = tokens[i];
        if (tok.type == TokenType::IDENTIFIER &&
            declaredLocals.count(tok.value) &&
            !isKeyword(tok.value) &&
            !isBuiltin(tok.value)) {
            size_t prev = previousSignificant(tokens, i);
            if (prev < tokens.size() && tokens[prev].type == TokenType::OPERATOR &&
                (tokens[prev].value == "." || tokens[prev].value == ":")) {
                continue;
            }
            if (renameMap.find(tok.value) == renameMap.end()) {
                std::string newName;
                do {
                    newName = generateRandomName(8);
                } while (seenNames.count(newName));
                renameMap[tok.value] = newName;
                seenNames.insert(newName);
            }
            tok.value = renameMap[tok.value];
        }
    }
}

void Obfuscator::passInjectJunkCode(std::vector<Token>& tokens) {
    std::vector<size_t> injectPositions;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::NEWLINE) {
            injectPositions.push_back(i);
        }
    }

    size_t junkCount = std::min<size_t>(injectPositions.size() / 8,
        injectPositions.size());
    std::shuffle(injectPositions.begin(), injectPositions.end(), m_rng);

    std::vector<Token> result;
    result.reserve(tokens.size() + junkCount * 4);
    size_t posIdx = 0;

    for (size_t i = 0; i < tokens.size(); ++i) {
        result.push_back(std::move(tokens[i]));
        if (posIdx < junkCount && i == injectPositions[posIdx]) {
            std::string junk = generateJunkCode();
            size_t l = tokens[i].line;
            size_t c = tokens[i].col;
            result.push_back({TokenType::NEWLINE, "\n", l, c});
            result.push_back({TokenType::IDENTIFIER, junk, l, c + 1});
            result.push_back({TokenType::NEWLINE, "\n", l, c + 2});
            posIdx++;
        }
    }
    tokens = std::move(result);
}

void Obfuscator::passFlattenControlFlow(std::vector<Token>& tokens) {
    (void)tokens;
}

void Obfuscator::passAntiDebug(std::vector<Token>& tokens) {
    std::ostringstream antidebug;
    antidebug << "\n"
        << "local _adbg_x" << randomHex(6) << "="
        << "pcall(function()"
        << "local a=debug.getinfo(1,'S');"
        << "if a and a.short_src and a.short_src:match('^@') then return end;"
        << "local f=assert;"
        << "f=nil;"
        << "collectgarbage('collect');"
        << "if f~=nil then while true do end end;"
        << "local s=debug.sethook or rawset;"
        << "if s then s(nil) end;"
        << "local r=debug.getregistry;"
        << "if r and r() then end;"
        << "if jit and jit.flush then jit.flush() end;"
        << "end)\n";

    size_t insertPos = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type == TokenType::NEWLINE) {
            insertPos = i;
            break;
        }
    }

    if (insertPos < tokens.size()) {
        tokens.insert(tokens.begin() + static_cast<ptrdiff_t>(insertPos),
            {TokenType::IDENTIFIER, antidebug.str(), tokens[insertPos].line, 1});
    }
}

void Obfuscator::passCompressWhitespace(std::vector<Token>& tokens) {
    std::vector<Token> result;
    bool prevWasWS = false;
    for (auto& tok : tokens) {
        if (tok.type == TokenType::WHITESPACE) {
            if (prevWasWS) continue;
            prevWasWS = true;
            result.push_back(tok);
        } else if (tok.type == TokenType::COMMENT_SHORT ||
                   tok.type == TokenType::COMMENT_LONG) {
            continue;
        } else {
            prevWasWS = false;
            result.push_back(std::move(tok));
        }
    }
    tokens = std::move(result);
}

std::string Obfuscator::recompose(const std::vector<Token>& tokens) {
    std::ostringstream out;
    for (const auto& tok : tokens) {
        out << tok.value;
    }
    std::string result = out.str();

    std::string cleaned;
    cleaned.reserve(result.size());
    bool prevNL = false;
    for (size_t i = 0; i < result.size(); ++i) {
        if (result[i] == '\n') {
            if (prevNL) continue;
            prevNL = true;
        } else {
            prevNL = false;
        }
        cleaned += result[i];
    }
    return cleaned;
}

std::string Obfuscator::obfuscate(std::string_view source) {
    if (m_opts.luaJitMode && m_opts.virtualizeBytecode) {
        std::string body;
        if (m_opts.preserveOpenObfuscatorStyle) {
            body += buildStylePrelude();
        }
        if (m_opts.addAntiDebug) {
            body += buildAntiDebugPrelude();
        }
        body.append(source.data(), source.size());
        return buildBanner() + buildLuaJitBytecodeVm(body);
    }

    std::vector<Token> tokens = tokenize(source);

    if (m_opts.addAntiDebug) {
        passAntiDebug(tokens);
    }

    if (m_opts.obfuscateStrings || m_opts.virtualizeStrings) {
        passStringObfuscation(tokens);
    }

    if (m_opts.obfuscateNumbers) {
        passNumberObfuscation(tokens);
    }

    if (m_opts.renameIdentifiers) {
        passRenameIdentifiers(tokens);
    }

    if (m_opts.injectJunkCode) {
        passInjectJunkCode(tokens);
    }

    if (m_opts.flattenControlFlow) {
        passFlattenControlFlow(tokens);
    }

    if (m_opts.compressWhitespace) {
        passCompressWhitespace(tokens);
    }

    std::string body = recompose(tokens);
    if (m_opts.preserveOpenObfuscatorStyle) {
        body = buildStylePrelude() + body;
    }
    if (m_opts.luaJitMode && m_opts.virtualizeBytecode) {
        body = buildLuaJitBytecodeVm(body);
    }
    return buildBanner() + body;
}

} // namespace luaobf
