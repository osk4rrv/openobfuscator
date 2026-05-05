#include "gui.h"
#include "luaobf.h"

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace luaobf {
namespace {

constexpr int IdInput = 1001;
constexpr int IdInputBrowse = 1002;
constexpr int IdOutput = 1003;
constexpr int IdOutputBrowse = 1004;
constexpr int IdSeed = 1005;
constexpr int IdNumbers = 1006;
constexpr int IdStrings = 1007;
constexpr int IdRename = 1008;
constexpr int IdJunk = 1009;
constexpr int IdAntiDebug = 1010;
constexpr int IdCompress = 1011;
constexpr int IdVm = 1012;
constexpr int IdLuaJit = 1013;
constexpr int IdStyle = 1014;
constexpr int IdFlatten = 1015;
constexpr int IdRun = 1016;
constexpr int IdStatus = 1017;

struct UiState {
    HWND window = nullptr;
    HWND input = nullptr;
    HWND output = nullptr;
    HWND seed = nullptr;
    HWND status = nullptr;
    HFONT font = nullptr;
};

UiState g_ui;

std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    int chars = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(chars), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), chars);
    return out;
}

std::wstring getText(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(len + 1), L'\0');
    GetWindowTextW(hwnd, text.data(), len + 1);
    text.resize(static_cast<size_t>(len));
    return text;
}

void setText(HWND hwnd, const std::wstring& text) {
    SetWindowTextW(hwnd, text.c_str());
}

bool isChecked(int id) {
    return SendMessageW(GetDlgItem(g_ui.window, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

std::string readFile(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Nie mozna otworzyc pliku wejsciowego.");
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void writeFile(const std::wstring& path, const std::string& content) {
    std::ofstream file(std::filesystem::path(path), std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Nie mozna zapisac pliku wyjsciowego.");
    file << content;
}

std::wstring openDialog(bool save) {
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_ui.window;
    ofn.lpstrFilter = L"Lua scripts (*.lua)\0*.lua\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"lua";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (save) {
        ofn.Flags |= OFN_OVERWRITEPROMPT;
        return GetSaveFileNameW(&ofn) ? fileName : L"";
    }
    ofn.Flags |= OFN_FILEMUSTEXIST;
    return GetOpenFileNameW(&ofn) ? fileName : L"";
}

HWND makeControl(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND hwnd = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_ui.window, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)), GetModuleHandleW(nullptr), nullptr);
    if (g_ui.font) SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_ui.font), TRUE);
    return hwnd;
}

HWND makeCheck(const wchar_t* text, int x, int y, int w, int id, bool checked) {
    HWND hwnd = makeControl(L"BUTTON", text, BS_AUTOCHECKBOX | WS_TABSTOP, x, y, w, 24, id);
    SendMessageW(hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return hwnd;
}

void runObfuscation() {
    try {
        std::wstring inPath = getText(g_ui.input);
        std::wstring outPath = getText(g_ui.output);
        if (inPath.empty() || outPath.empty()) {
            MessageBoxW(g_ui.window, L"Wybierz plik wejsciowy i wyjsciowy.", L"LuaObfuscator", MB_ICONWARNING);
            return;
        }

        ObfuscationOptions opts;
        opts.obfuscateNumbers = isChecked(IdNumbers);
        opts.obfuscateStrings = isChecked(IdStrings);
        opts.virtualizeStrings = opts.obfuscateStrings;
        opts.renameIdentifiers = isChecked(IdRename);
        opts.injectJunkCode = isChecked(IdJunk);
        opts.addAntiDebug = isChecked(IdAntiDebug);
        opts.compressWhitespace = isChecked(IdCompress);
        opts.virtualizeBytecode = isChecked(IdVm);
        opts.luaJitMode = isChecked(IdLuaJit);
        opts.preserveOpenObfuscatorStyle = isChecked(IdStyle);
        opts.flattenControlFlow = isChecked(IdFlatten);

        std::wstring seedText = getText(g_ui.seed);
        if (!seedText.empty()) {
            opts.seed = static_cast<uint32_t>(std::stoul(seedText));
        }
        if (!opts.luaJitMode) {
            opts.virtualizeBytecode = false;
        }

        setText(g_ui.status, L"Obfuscating...");
        std::string source = readFile(inPath);
        Obfuscator obfuscator(opts);
        std::string result = obfuscator.obfuscate(source);
        writeFile(outPath, result);

        std::wostringstream ok;
        ok << L"Gotowe: " << result.size() << L" bytes";
        setText(g_ui.status, ok.str());
        MessageBoxW(g_ui.window, L"Obfuscation complete.", L"LuaObfuscator", MB_ICONINFORMATION);
    } catch (const std::exception& ex) {
        std::wstring message = widen(ex.what());
        setText(g_ui.status, L"Blad.");
        MessageBoxW(g_ui.window, message.c_str(), L"LuaObfuscator", MB_ICONERROR);
    }
}

void layout(HWND hwnd) {
    RECT rc {};
    GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left;
    int editWidth = width - 220;
    MoveWindow(g_ui.input, 140, 54, editWidth, 24, TRUE);
    MoveWindow(GetDlgItem(hwnd, IdInputBrowse), width - 70, 54, 48, 24, TRUE);
    MoveWindow(g_ui.output, 140, 88, editWidth, 24, TRUE);
    MoveWindow(GetDlgItem(hwnd, IdOutputBrowse), width - 70, 88, 48, 24, TRUE);
    MoveWindow(g_ui.status, 22, rc.bottom - 42, width - 44, 24, TRUE);
    MoveWindow(GetDlgItem(hwnd, IdRun), width - 180, rc.bottom - 84, 158, 34, TRUE);
}

void createUi(HWND hwnd) {
    g_ui.window = hwnd;
    g_ui.font = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    makeControl(L"STATIC", L"OpenObfuscator LuaJIT VM", SS_LEFT, 22, 16, 320, 26, 0);
    makeControl(L"STATIC", L"Input Lua:", SS_LEFT, 22, 56, 105, 22, 0);
    makeControl(L"STATIC", L"Output Lua:", SS_LEFT, 22, 90, 105, 22, 0);
    g_ui.input = makeControl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 140, 54, 500, 24, IdInput);
    g_ui.output = makeControl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 140, 88, 500, 24, IdOutput);
    makeControl(L"BUTTON", L"...", BS_PUSHBUTTON | WS_TABSTOP, 650, 54, 48, 24, IdInputBrowse);
    makeControl(L"BUTTON", L"...", BS_PUSHBUTTON | WS_TABSTOP, 650, 88, 48, 24, IdOutputBrowse);

    makeCheck(L"Number obfuscation", 22, 136, 190, IdNumbers, true);
    makeCheck(L"String virtualization", 240, 136, 190, IdStrings, true);
    makeCheck(L"Rename local identifiers", 458, 136, 220, IdRename, true);
    makeCheck(L"Junk code injection", 22, 170, 190, IdJunk, true);
    makeCheck(L"Anti-debug guards", 240, 170, 190, IdAntiDebug, true);
    makeCheck(L"Compress output", 458, 170, 190, IdCompress, true);
    makeCheck(L"LuaJIT bytecode VM", 22, 204, 190, IdVm, true);
    makeCheck(L"LuaJIT-only runtime", 240, 204, 190, IdLuaJit, true);
    makeCheck(L"OpenObfuscator style", 458, 204, 220, IdStyle, true);
    makeCheck(L"Control-flow flattening", 22, 238, 200, IdFlatten, false);

    makeControl(L"STATIC", L"Seed:", SS_LEFT, 240, 241, 50, 22, 0);
    g_ui.seed = makeControl(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 292, 238, 138, 24, IdSeed);
    makeControl(L"BUTTON", L"OBFUSCATE", BS_DEFPUSHBUTTON | WS_TABSTOP, 520, 300, 158, 34, IdRun);
    g_ui.status = makeControl(L"STATIC", L"Ready.", SS_LEFT | WS_BORDER, 22, 342, 656, 24, IdStatus);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            createUi(hwnd);
            return 0;
        case WM_SIZE:
            layout(hwnd);
            return 0;
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == IdInputBrowse) {
                std::wstring path = openDialog(false);
                if (!path.empty()) {
                    setText(g_ui.input, path);
                    std::filesystem::path out(path);
                    std::wstring stem = out.stem().wstring();
                    std::wstring ext = out.extension().wstring();
                    if (ext.empty()) ext = L".lua";
                    out.replace_filename(stem + L"_obfuscated" + ext);
                    setText(g_ui.output, out.wstring());
                }
                return 0;
            }
            if (id == IdOutputBrowse) {
                std::wstring path = openDialog(true);
                if (!path.empty()) setText(g_ui.output, path);
                return 0;
            }
            if (id == IdRun) {
                runObfuscation();
                return 0;
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
        }
        case WM_DESTROY:
            if (g_ui.font) {
                DeleteObject(g_ui.font);
                g_ui.font = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}

int runGui() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"LuaObfuscatorGui";

    WNDCLASSEXW wc {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    wc.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIconSm = wc.hIcon;

    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, className, L"OpenObfuscator LuaJIT VM", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 760, 440, nullptr, nullptr, instance, nullptr);
    if (!hwnd) return 1;

    MSG msg {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

}

#else

namespace luaobf {

int runGui() {
    return 1;
}

}

#endif
