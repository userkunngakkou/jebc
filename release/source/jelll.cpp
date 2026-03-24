// ==========================================================================
// JELLL (JavaScript/TypeScript + Emscripten Bundled Compiler) V2
//
// Reads .jelll / .jsbc / .tsbc / .tsxbc files, compiles C code to WASM
// via emcc, bundles JS/TS/TSX code via esbuild, and outputs dist/.
// Supports lang.c / lang.js / lang.ts / lang.tsx / lang.css / lang.html
// blocks, file inclusion via 'call', and @jelll-sync.
//
// Required external tools: emcc (Emscripten), esbuild
// Target platform: Windows (uses Win32 API)
// ==========================================================================

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>
#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;

// ==========================================================================
// Output mode enumeration
// ==========================================================================

enum Mode {
    MODE_JS,
    MODE_TS,
    MODE_TSX,
    MODE_AUTO
};

// ==========================================================================
// Section structure - represents a parsed lang block from the input file
// ==========================================================================

struct Section {
    std::string kind;  // "c", "js", "ts", "tsx"
    std::string body;  // source code inside the block
};

// ==========================================================================
// @jelll-sync data structures
// ==========================================================================

struct SyncField {
    std::string cType;
    std::string name;
};

struct SyncMethod {
    std::string returnType;
    std::string methodName;
    std::string fullName;
    std::vector<std::pair<std::string, std::string>> extraParams;
};

struct SyncStruct {
    std::string name;
    std::vector<SyncField> fields;
    std::vector<SyncMethod> methods;
};

// ==========================================================================
// String utilities
// ==========================================================================

/// Convert a string to lowercase and return the result
static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// ==========================================================================
// Path utilities (Win32 API based)
// ==========================================================================

/// Check if a character is a path separator (backslash or forward slash)
static bool isSlash(char c) {
    return c == '\\' || c == '/';
}

/// Normalize forward slashes to backslashes
static std::string normalizePath(std::string path) {
    for (char& c : path) {
        if (c == '/') {
            c = '\\';
        }
    }
    return path;
}

/// Join two path components (auto-appends separator if needed)
static std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) {
        return b;
    }
    if (b.empty()) {
        return a;
    }
    if (isSlash(a.back())) {
        return a + b;
    }
    return a + "\\" + b;
}

/// Return the parent directory portion of a path
static std::string parentPath(const std::string& path) {
    std::size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) {
        return "";
    }
    return path.substr(0, pos);
}

/// Extract just the filename from a full path
static std::string filenameOf(const std::string& path) {
    std::size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

/// Get the file extension (including the dot) from a path
static std::string extensionOf(const std::string& path) {
    std::string fn = filenameOf(path);
    std::size_t pos = fn.find_last_of('.');
    if (pos == std::string::npos) {
        return "";
    }
    return fn.substr(pos);
}

/// Convert a path to an absolute path using Win32 GetFullPathNameA
static std::string absolutePath(const std::string& path) {
    char buf[MAX_PATH * 8] = {};
    DWORD len = GetFullPathNameA(path.c_str(), static_cast<DWORD>(sizeof(buf)), buf, nullptr);
    if (len == 0 || len >= sizeof(buf)) {
        return path;
    }
    return std::string(buf, len);
}

// ==========================================================================
// Filesystem checks and operations (Win32)
// ==========================================================================

/// Check if a path exists
static bool pathExists(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES;
}

/// Check if a path is a directory
static bool pathIsDirectory(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

/// Check if a path is a regular file (not a directory)
static bool pathIsRegularFile(const std::string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return false;
    }
    return (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/// Recursively create directories (equivalent to mkdir -p)
static bool ensureDirectory(const std::string& dirPath) {
    if (dirPath.empty()) {
        return true;
    }
    std::string normalized = normalizePath(dirPath);
    if (pathIsDirectory(normalized)) {
        return true;
    }
    std::string parent = parentPath(normalized);
    if (!parent.empty() && !pathIsDirectory(parent)) {
        if (!ensureDirectory(parent)) {
            return false;
        }
    }
    if (CreateDirectoryA(normalized.c_str(), nullptr) != 0) {
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS && pathIsDirectory(normalized);
}

/// List all regular files in the current directory
static std::vector<std::string> listCurrentRegularFiles() {
    std::vector<std::string> files;
    WIN32_FIND_DATAA findData;
    HANDLE h = FindFirstFileA("*", &findData);
    if (h == INVALID_HANDLE_VALUE) {
        return files;
    }
    do {
        std::string name = findData.cFileName;
        if (name == "." || name == "..") {
            continue;
        }
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        files.push_back(name);
    } while (FindNextFileA(h, &findData) != 0);
    FindClose(h);
    return files;
}

// ==========================================================================
// File I/O
// ==========================================================================

/// Read an entire file in binary mode into a string
static bool readFileText(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

/// Write a string to a file (creates parent directories if needed)
static bool writeFileText(const std::string& path, const std::string& text) {
    std::string parent = parentPath(path);
    if (!parent.empty() && !ensureDirectory(parent)) {
        return false;
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << text;
    return static_cast<bool>(out);
}

// ==========================================================================
// External command execution helpers
// ==========================================================================

/// Check if a command exists on PATH (uses the 'where' command)
static bool checkCommandAvailable(const std::string& cmd) {
    std::string probe = "where " + cmd + " >nul 2>nul";
    int rc = std::system(probe.c_str());
    return rc == 0;
}

/// Run an external command, capturing stderr to a file.
/// On failure, prints the captured stderr content and the command line.
static int runCommandWithCapturedStderr(const std::string& label, const std::string& cmd, const std::string& stderrPath) {
    ensureDirectory(parentPath(stderrPath));
    std::ofstream clearFile(stderrPath, std::ios::trunc);
    clearFile.close();
    std::string wrapped = cmd + " 2>\"" + stderrPath + "\"";
    std::cout << "[JELLL] " << label << std::endl;
    int rc = std::system(wrapped.c_str());
    if (rc != 0) {
        std::string errText;
        if (readFileText(stderrPath, errText) && !errText.empty()) {
            std::cerr << errText << std::endl;
        }
        std::cerr << "[JELLL] command failed: " << cmd << std::endl;
    }
    return rc;
}

// ==========================================================================
// Source code analysis - code mask for comment/string literal detection
// ==========================================================================

/// Build a boolean mask over the source where true = "real code" positions.
/// Positions inside comments (// ..., /* ... */) and string literals (' " `)
/// are marked false. Used by parseSections() to avoid matching lang blocks
/// that appear inside comments or string literals.
static std::vector<bool> buildCodeMask(const std::string& src) {
    std::vector<bool> mask(src.size(), true);
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool inTemplate = false;
    bool inLineComment = false;
    bool inBlockComment = false;
    bool escaped = false;

    for (std::size_t i = 0; i < src.size(); ++i) {
        char c = src[i];
        char n = (i + 1 < src.size()) ? src[i + 1] : '\0';
        if (inLineComment) {
            mask[i] = false;
            if (c == '\n') {
                inLineComment = false;
            }
            continue;
        }
        if (inBlockComment) {
            mask[i] = false;
            if (c == '*' && n == '/') {
                if (i + 1 < src.size()) {
                    mask[i + 1] = false;
                }
                inBlockComment = false;
                ++i;
            }
            continue;
        }
        if (inSingleQuote) {
            mask[i] = false;
            if (!escaped && c == '\'') {
                inSingleQuote = false;
            }
            escaped = (!escaped && c == '\\');
            continue;
        }
        if (inDoubleQuote) {
            mask[i] = false;
            if (!escaped && c == '"') {
                inDoubleQuote = false;
            }
            escaped = (!escaped && c == '\\');
            continue;
        }
        if (inTemplate) {
            mask[i] = false;
            if (!escaped && c == '`') {
                inTemplate = false;
            }
            escaped = (!escaped && c == '\\');
            continue;
        }

        escaped = false;
        if (c == '/' && n == '/') {
            mask[i] = false;
            if (i + 1 < src.size()) {
                mask[i + 1] = false;
            }
            inLineComment = true;
            ++i;
            continue;
        }
        if (c == '/' && n == '*') {
            mask[i] = false;
            if (i + 1 < src.size()) {
                mask[i + 1] = false;
            }
            inBlockComment = true;
            ++i;
            continue;
        }
        if (c == '\'') {
            mask[i] = false;
            inSingleQuote = true;
            continue;
        }
        if (c == '"') {
            mask[i] = false;
            inDoubleQuote = true;
            continue;
        }
        if (c == '`') {
            mask[i] = false;
            inTemplate = true;
            continue;
        }
    }

    return mask;
}

// ==========================================================================
// Source code analysis - brace block extraction
// ==========================================================================

/// Extract content between matching '{' and '}' starting at openBracePos.
/// Correctly handles braces inside string literals and comments.
/// On success, sets body to the inner content and endPos to the '}' position.
static bool extractBlock(const std::string& src, std::size_t openBracePos, std::string& body, std::size_t& endPos) {
    if (openBracePos >= src.size() || src[openBracePos] != '{') {
        return false;
    }
    int depth = 0;
    bool inSingleQuote = false;
    bool inDoubleQuote = false;
    bool inTemplate = false;
    bool inLineComment = false;
    bool inBlockComment = false;
    bool escaped = false;
    std::size_t start = openBracePos + 1;

    for (std::size_t i = openBracePos; i < src.size(); ++i) {
        char c = src[i];
        char n = (i + 1 < src.size()) ? src[i + 1] : '\0';
        if (inLineComment) {
            if (c == '\n') {
                inLineComment = false;
            }
            continue;
        }
        if (inBlockComment) {
            if (c == '*' && n == '/') {
                inBlockComment = false;
                ++i;
            }
            continue;
        }
        if (inSingleQuote) {
            if (!escaped && c == '\'') {
                inSingleQuote = false;
            }
            escaped = (!escaped && c == '\\');
            continue;
        }
        if (inDoubleQuote) {
            if (!escaped && c == '"') {
                inDoubleQuote = false;
            }
            escaped = (!escaped && c == '\\');
            continue;
        }
        if (inTemplate) {
            if (!escaped && c == '`') {
                inTemplate = false;
            }
            escaped = (!escaped && c == '\\');
            continue;
        }

        escaped = false;
        if (c == '/' && n == '/') {
            inLineComment = true;
            ++i;
            continue;
        }
        if (c == '/' && n == '*') {
            inBlockComment = true;
            ++i;
            continue;
        }
        if (c == '\'') {
            inSingleQuote = true;
            continue;
        }
        if (c == '"') {
            inDoubleQuote = true;
            continue;
        }
        if (c == '`') {
            inTemplate = true;
            continue;
        }
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                body = src.substr(start, i - start);
                endPos = i;
                return true;
            }
        }
    }
    return false;
}

// ==========================================================================
// Section parser - extracts lang.c / lang.js / lang.ts / lang.tsx blocks
// ==========================================================================

/// Parse all "lang.c { ... }", "lang.js { ... }", etc. blocks from the text.
/// Matches inside comments or string literals are excluded via the code mask.
static std::vector<Section> parseSections(const std::string& text) {
    std::vector<Section> sections;
    std::regex rx(R"(\blang(?:[.\s]*)(css|html|tsx|ts|js|rs|zig|c)\s*\{)");
    std::vector<bool> codeMask = buildCodeMask(text);
    std::size_t searchPos = 0;
    while (searchPos < text.size()) {
        std::smatch m;
        auto searchStart = text.cbegin() + static_cast<std::ptrdiff_t>(searchPos);
        if (!std::regex_search(searchStart, text.cend(), m, rx)) {
            break;
        }
        std::size_t matchAbs = searchPos + static_cast<std::size_t>(m.position(0));
        if (matchAbs >= codeMask.size() || !codeMask[matchAbs]) {
            searchPos = matchAbs + 1;
            continue;
        }
        std::size_t bracePos = matchAbs + static_cast<std::size_t>(m.length(0)) - 1;
        std::string kind;
        if (m[1].matched) {
            kind = toLower(m[1].str());
        } else {
            searchPos = bracePos + 1;
            continue;
        }
        std::string body;
        std::size_t endPos = bracePos;
        if (!extractBlock(text, bracePos, body, endPos)) {
            searchPos = bracePos + 1;
            continue;
        }
        sections.push_back({kind, body});
        searchPos = endPos + 1;
    }
    return sections;
}

// ==========================================================================
// @jelll-sync - struct parsing and code generation
// ==========================================================================

static std::string cTypeToTSType(const std::string& cType) {
    if (cType == "bool") return "boolean";
    return "number";
}

static std::vector<SyncStruct> parseSyncStructs(const std::string& cCode) {
    std::vector<SyncStruct> result;
    std::string marker = "@jelll-sync";
    std::size_t pos = 0;

    while ((pos = cCode.find(marker, pos)) != std::string::npos) {
        pos += marker.size();
        while (pos < cCode.size() && (cCode[pos] == ' ' || cCode[pos] == '\t' ||
                cCode[pos] == '\n' || cCode[pos] == '\r' || cCode[pos] == '*' || cCode[pos] == '/')) {
            ++pos;
        }

        bool isTypedef = false;
        std::string structName;

        if (pos + 15 <= cCode.size() && cCode.substr(pos, 15) == "typedef struct") {
            isTypedef = true;
            pos += 15;
        } else if (pos + 6 <= cCode.size() && cCode.substr(pos, 6) == "struct") {
            pos += 6;
        } else {
            continue;
        }

        while (pos < cCode.size() && (cCode[pos] == ' ' || cCode[pos] == '\t' || cCode[pos] == '\n' || cCode[pos] == '\r')) {
            ++pos;
        }

        if (!isTypedef) {
            std::size_t nameStart = pos;
            while (pos < cCode.size() && (std::isalnum(cCode[pos]) || cCode[pos] == '_')) {
                ++pos;
            }
            structName = cCode.substr(nameStart, pos - nameStart);
        }

        std::size_t bracePos = cCode.find('{', pos);
        if (bracePos == std::string::npos) continue;

        int depth = 1;
        std::size_t closeBrace = bracePos + 1;
        while (closeBrace < cCode.size() && depth > 0) {
            if (cCode[closeBrace] == '{') ++depth;
            else if (cCode[closeBrace] == '}') --depth;
            ++closeBrace;
        }
        if (depth != 0) continue;
        --closeBrace;

        if (isTypedef) {
            std::size_t afterBrace = closeBrace + 1;
            while (afterBrace < cCode.size() && (cCode[afterBrace] == ' ' || cCode[afterBrace] == '\t')) {
                ++afterBrace;
            }
            std::size_t nameStart = afterBrace;
            while (afterBrace < cCode.size() && (std::isalnum(cCode[afterBrace]) || cCode[afterBrace] == '_')) {
                ++afterBrace;
            }
            structName = cCode.substr(nameStart, afterBrace - nameStart);
        }

        if (structName.empty()) continue;

        std::string fieldsStr = cCode.substr(bracePos + 1, closeBrace - bracePos - 1);
        SyncStruct ss;
        ss.name = structName;

        std::istringstream fieldStream(fieldsStr);
        std::string line;
        while (std::getline(fieldStream, line)) {
            std::size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start);
            if (line.empty() || line.substr(0, 2) == "//" || line.substr(0, 2) == "/*") continue;
            std::size_t semi = line.find(';');
            if (semi == std::string::npos) continue;
            line = line.substr(0, semi);
            std::size_t end = line.find_last_not_of(" \t");
            if (end != std::string::npos) line = line.substr(0, end + 1);
            std::size_t lastSpace = line.find_last_of(" \t");
            if (lastSpace == std::string::npos) continue;
            std::string fieldType = line.substr(0, lastSpace);
            std::string fieldName = line.substr(lastSpace + 1);
            start = fieldType.find_first_not_of(" \t");
            if (start != std::string::npos) fieldType = fieldType.substr(start);
            end = fieldType.find_last_not_of(" \t");
            if (end != std::string::npos) fieldType = fieldType.substr(0, end + 1);
            if (!fieldType.empty() && !fieldName.empty()) {
                ss.fields.push_back({fieldType, fieldName});
            }
        }

        if (!ss.fields.empty()) {
            result.push_back(ss);
        }
        pos = closeBrace + 1;
    }

    return result;
}

static void detectSyncMethods(const std::string& cCode, std::vector<SyncStruct>& syncs) {
    for (auto& ss : syncs) {
        std::regex rx("(\\w+)\\s+" + ss.name + "_(\\w+)\\s*\\(\\s*(?:const\\s+)?" + ss.name + "\\s*\\*([^)]*)\\)");
        auto begin = std::sregex_iterator(cCode.begin(), cCode.end(), rx);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            SyncMethod m;
            m.returnType = (*it)[1].str();
            m.methodName = (*it)[2].str();
            m.fullName = ss.name + "_" + m.methodName;
            if (m.returnType == "static" || m.returnType == "struct") continue;

            std::string restParams = (*it)[3].str();
            if (!restParams.empty() && restParams[0] == ',') {
                restParams = restParams.substr(1);
            }
            if (!restParams.empty()) {
                std::istringstream ps(restParams);
                std::string part;
                while (std::getline(ps, part, ',')) {
                    std::size_t s = part.find_first_not_of(" \t\r\n");
                    if (s != std::string::npos) part = part.substr(s);
                    std::size_t e = part.find_last_not_of(" \t\r\n");
                    if (e != std::string::npos) part = part.substr(0, e + 1);
                    if (part.empty()) continue;
                    std::size_t lastSp = part.find_last_of(" \t");
                    if (lastSp != std::string::npos) {
                        std::string pType = part.substr(0, lastSp);
                        std::string pName = part.substr(lastSp + 1);
                        s = pType.find_first_not_of(" \t");
                        if (s != std::string::npos) pType = pType.substr(s);
                        e = pType.find_last_not_of(" \t");
                        if (e != std::string::npos) pType = pType.substr(0, e + 1);
                        m.extraParams.push_back({pType, pName});
                    }
                }
            }

            ss.methods.push_back(m);
        }
    }
}

static std::string generateSyncCCode(const std::vector<SyncStruct>& syncs) {
    std::ostringstream out;
    out << "\n// JELLL @jelll-sync: auto-generated code\n";

    for (const auto& ss : syncs) {
        out << "static " << ss.name << " __jelll_" << ss.name << "_pool[256];\n";
        out << "static int __jelll_" << ss.name << "_count = 0;\n\n";
        out << "extern \"C\" {\n";
        out << "    int __jelll_" << ss.name << "_new() { return __jelll_" << ss.name << "_count++; }\n\n";

        for (const auto& f : ss.fields) {
            out << "    " << f.cType << " __jelll_" << ss.name << "_get_" << f.name << "(int id) {\n";
            out << "        return __jelll_" << ss.name << "_pool[id]." << f.name << ";\n    }\n";
            out << "    void __jelll_" << ss.name << "_set_" << f.name << "(int id, " << f.cType << " v) {\n";
            out << "        __jelll_" << ss.name << "_pool[id]." << f.name << " = v;\n    }\n\n";
        }

        for (const auto& m : ss.methods) {
            out << "    " << m.returnType << " __jelll_" << ss.name << "_call_" << m.methodName << "(int id";
            for (const auto& p : m.extraParams) {
                out << ", " << p.first << " " << p.second;
            }
            out << ") {\n        ";
            if (m.returnType != "void") out << "return ";
            out << m.fullName << "(&__jelll_" << ss.name << "_pool[id]";
            for (const auto& p : m.extraParams) {
                out << ", " << p.second;
            }
            out << ");\n    }\n\n";
        }

        out << "}\n\n";
    }

    return out.str();
}

static std::string generateSyncTSCode(const std::vector<SyncStruct>& syncs) {
    std::ostringstream out;
    out << "// JELLL @jelll-sync: auto-generated classes\n\n";

    for (const auto& ss : syncs) {
        out << "globalThis." << ss.name << " = class " << ss.name << " {\n";
        out << "  __id;\n";
        out << "  constructor() { this.__id = native.__jelll_" << ss.name << "_new(); }\n";
        out << "  get id() { return this.__id; }\n\n";

        for (const auto& f : ss.fields) {
            out << "  get " << f.name << "() { return native.__jelll_" << ss.name << "_get_" << f.name << "(this.__id); }\n";
            out << "  set " << f.name << "(v) { native.__jelll_" << ss.name << "_set_" << f.name << "(this.__id, v); }\n\n";
        }

        for (const auto& m : ss.methods) {
            out << "  " << m.methodName << "(";
            for (std::size_t i = 0; i < m.extraParams.size(); ++i) {
                if (i > 0) out << ", ";
                out << m.extraParams[i].second;
            }
            out << ") {\n    ";
            if (m.returnType != "void") out << "return ";
            out << "native.__jelll_" << ss.name << "_call_" << m.methodName << "(this.__id";
            for (const auto& p : m.extraParams) {
                out << ", " << p.second;
            }
            out << ");\n  }\n\n";
        }

        out << "};\n";
        out << "const " << ss.name << " = globalThis." << ss.name << ";\n\n";
    }

    return out.str();
}

// ==========================================================================
// Code generation - WASM glue code and HTML template
// ==========================================================================

/// Generate the WASM loader + glue code.
static std::string generateWasmInitCode() {
    return R"(const native = {};
const sharedBuffer = new SharedArrayBuffer(16 * 1024 * 1024);
const sharedMemory = new WebAssembly.Memory({ initial: 256, maximum: 256, shared: true });
const __jelllImports = {
  env: {
    memory: sharedMemory,
    sharedBuffer
  }
};
const __jelllWasmUrl = new URL('./native.wasm', import.meta.url);
const __jelllWasmResponse = await fetch(__jelllWasmUrl);
if (!__jelllWasmResponse.ok) {
  throw new Error(`native.wasm load failed: ${__jelllWasmResponse.status}`);
}
let __jelllWasm;
try {
  __jelllWasm = await WebAssembly.instantiateStreaming(Promise.resolve(__jelllWasmResponse), __jelllImports);
} catch (e) {
  const __jelllBytes = await __jelllWasmResponse.arrayBuffer();
  __jelllWasm = await WebAssembly.instantiate(__jelllBytes, __jelllImports);
}
Object.assign(native, __jelllWasm.instance.exports);
globalThis.native = native;
globalThis.sharedBuffer = sharedBuffer;
globalThis.sharedMemory = sharedMemory;
)";
}

/// Generate HTML template with optional CSS and HTML body content
static std::string makeIndexHtml(const std::string& cssCode, const std::string& htmlBody) {
    std::ostringstream out;
    out << "<!doctype html>\n<html lang=\"en\">\n<head>\n";
    out << "  <meta charset=\"UTF-8\" />\n";
    out << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\n";
    out << "  <title>JELLL Output</title>\n";
    if (!cssCode.empty()) {
        out << "  <style>\n" << cssCode << "\n  </style>\n";
    }
    out << "</head>\n<body>\n";
    if (!htmlBody.empty()) {
        out << htmlBody << "\n";
    }
    out << "  <script type=\"module\" src=\"./bundle.js\"></script>\n";
    out << "</body>\n</html>\n";
    return out.str();
}

// ==========================================================================
// Mode detection - determine build mode from executable name or file extension
// ==========================================================================

/// Detect the output mode from argv[0] filename
/// (jsbc.exe -> JS, tsbc.exe -> TS, tsxbc.exe -> TSX)
static bool detectModeByArgv0(const std::string& argv0, Mode& mode) {
    std::string base = toLower(filenameOf(argv0));
    if (base == "jsbc.exe" || base == "jsbc") {
        mode = MODE_JS;
        return true;
    }
    if (base == "tsbc.exe" || base == "tsbc") {
        mode = MODE_TS;
        return true;
    }
    if (base == "tsxbc.exe" || base == "tsxbc") {
        mode = MODE_TSX;
        return true;
    }
    return false;
}

/// Detect the output mode from the input file extension
static bool detectModeByExt(const std::string& input, Mode& mode) {
    std::string ext = toLower(extensionOf(input));
    if (ext == ".jsbc") {
        mode = MODE_JS;
        return true;
    }
    if (ext == ".tsbc") {
        mode = MODE_TS;
        return true;
    }
    if (ext == ".tsxbc") {
        mode = MODE_TSX;
        return true;
    }
    if (ext == ".jelll") {
        mode = MODE_AUTO;
        return true;
    }
    return false;
}

/// Return the script language name for the given mode ("js", "ts", "tsx")
static std::string modeToScriptKind(Mode mode) {
    if (mode == MODE_JS) {
        return "js";
    }
    if (mode == MODE_TS) {
        return "ts";
    }
    return "tsx";
}

/// Return the entry filename for the given mode ("entry.js", etc.)
static std::string modeToEntryName(Mode mode) {
    if (mode == MODE_JS) {
        return "entry.js";
    }
    if (mode == MODE_TS) {
        return "entry.ts";
    }
    return "entry.tsx";
}

/// Search the current directory for an input file matching the given mode.
/// Returns the filename only if exactly one matching file is found.
static std::string findInputByMode(Mode mode) {
    std::string wantedExt = "." + modeToScriptKind(mode) + "bc";
    std::string found;
    std::size_t count = 0;
    for (const auto& name : listCurrentRegularFiles()) {
        std::string ext = toLower(extensionOf(name));
        if (ext == wantedExt || ext == ".jelll") {
            found = name;
            ++count;
        }
    }
    if (count == 1) {
        return found;
    }
    return "";
}

// ==========================================================================
// ES Module support - process import directives and map to .tsx files
// ==========================================================================

static std::string processImports(
    const std::string& scriptCode, 
    const std::string& currentFilePath,
    std::unordered_map<std::string, std::string>& fileToModule,
    std::vector<std::string>& pendingFiles,
    int& modCounter) 
{
    std::string result;
    // Match: import ... from "path.jelll" or import "path.jelll"
    std::regex importRegex(R"((import\s+(?:.*?\s+from\s+)?['"])([^'"]+\.jelll)(['"]))");
    
    std::sregex_iterator words_begin(scriptCode.begin(), scriptCode.end(), importRegex);
    std::sregex_iterator words_end;
    
    std::size_t lastPos = 0;
    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        result += scriptCode.substr(lastPos, match.position() - lastPos);
        result += match[1].str();
        
        std::string relPath = match[2].str();
        std::string absTarget = absolutePath(joinPath(parentPath(currentFilePath), relPath));
        
        if (fileToModule.find(absTarget) == fileToModule.end()) {
            std::string modName = "mod_" + std::to_string(modCounter++) + ".tsx";
            fileToModule[absTarget] = modName;
            pendingFiles.push_back(absTarget);
        }
        
        result += "./" + fileToModule[absTarget];
        result += match[3].str();
        lastPos = match.position() + match.length();
    }
    result += scriptCode.substr(lastPos);
    return result;
}

// ==========================================================================
// Preprocessor - call directive for file inclusion
// ==========================================================================

/// Recursively expand 'call "path"' directives in source text.
/// Detects circular references via the visited set.
static std::string preprocessCalls(const std::string& source, const std::string& basePath,
                                    std::vector<std::string>& visited) {
    std::ostringstream result;
    std::istringstream stream(source);
    std::string line;

    while (std::getline(stream, line)) {
        // Trim leading whitespace for check
        std::size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) {
            result << line << "\n";
            continue;
        }
        std::string trimmed = line.substr(start);

        // Check for: call "path" or call 'path'
        if (trimmed.substr(0, 5) == "call " || trimmed.substr(0, 5) == "call\t") {
            std::size_t q1 = trimmed.find_first_of("\"'", 5);
            if (q1 == std::string::npos) {
                result << line << "\n";
                continue;
            }
            char quote = trimmed[q1];
            std::size_t q2 = trimmed.find(quote, q1 + 1);
            if (q2 == std::string::npos) {
                result << line << "\n";
                continue;
            }
            std::string relPath = trimmed.substr(q1 + 1, q2 - q1 - 1);

            // Resolve path relative to current file's directory
            std::string callPath;
            if (!basePath.empty()) {
                callPath = joinPath(parentPath(basePath), relPath);
            } else {
                callPath = relPath;
            }
            callPath = absolutePath(callPath);

            // Circular reference check
            bool isCircular = false;
            for (const auto& v : visited) {
                if (v == callPath) {
                    std::cerr << "[JELLL] Circular call detected: " << callPath << std::endl;
                    result << "// [JELLL ERROR] Circular call: " << relPath << "\n";
                    isCircular = true;
                    break;
                }
            }
            if (isCircular) {
                continue;
            }

            // Read and recursively process
            std::string callSource;
            if (!readFileText(callPath, callSource)) {
                std::cerr << "[JELLL] Failed to read call target: " << callPath << std::endl;
                result << "// [JELLL ERROR] File not found: " << relPath << "\n";
                continue;
            }

            visited.push_back(callPath);
            result << preprocessCalls(callSource, callPath, visited);
            visited.pop_back();
        } else {
            result << line << "\n";
        }
    }

    return result.str();
}

// ==========================================================================
// Build Execution
// ==========================================================================

static bool runBuild(const std::string& inputFile, Mode mode) {

    // --- Prepare build directories ---
    std::string bcDir = ".bc_build";
    std::string distDir = "dist";
    if (!ensureDirectory(bcDir) || !ensureDirectory(distDir)) {
        std::cerr << "Failed to create build directories." << std::endl;
        return false;
    }

    // --- Process all modules ---
    std::unordered_map<std::string, std::string> fileToModule;
    std::vector<std::string> pendingFiles;
    int modCounter = 1;
    
    std::string absInput = absolutePath(inputFile);
    fileToModule[absInput] = modeToEntryName(mode); // entry.js/ts/tsx
    pendingFiles.push_back(absInput);
    
    std::string globalCCode;
    std::string globalCssCode;
    std::string globalHtmlCode;
    std::string globalRsCode;
    std::string globalZigCode;
    std::unordered_set<std::string> processedSet;

    std::size_t queueIdx = 0;
    while (queueIdx < pendingFiles.size()) {
        std::string currAbs = pendingFiles[queueIdx++];
        
        if (processedSet.count(currAbs)) continue;
        processedSet.insert(currAbs);
        
        std::string source;
        if (!readFileText(currAbs, source)) {
            std::cerr << "Failed to open/read imported file: " << currAbs << std::endl;
            return 1;
        }
        
        std::vector<std::string> visited = { currAbs };
        source = preprocessCalls(source, currAbs, visited);
        
        auto sections = parseSections(source);
        std::unordered_map<std::string, std::string> blocksByKind;
        for (const auto& sec : sections) {
            blocksByKind[sec.kind] += sec.body + "\n";
        }
        
        if (!blocksByKind["c"].empty())    globalCCode    += blocksByKind["c"] + "\n";
        if (!blocksByKind["css"].empty())  globalCssCode  += blocksByKind["css"] + "\n";
        if (!blocksByKind["html"].empty()) globalHtmlCode += blocksByKind["html"] + "\n";
        if (!blocksByKind["rs"].empty())   globalRsCode   += blocksByKind["rs"] + "\n";
        if (!blocksByKind["zig"].empty())  globalZigCode  += blocksByKind["zig"] + "\n";
        
        std::string scriptCode;
        if (mode == MODE_AUTO) {
            for (const auto& sec : sections) {
                if (sec.kind == "js" || sec.kind == "ts" || sec.kind == "tsx") {
                    scriptCode += sec.body + "\n";
                }
            }
        } else {
            std::string targetKind = modeToScriptKind(mode);
            scriptCode = blocksByKind[targetKind];
            if (scriptCode.empty() && targetKind != "js") {
                scriptCode = blocksByKind["js"];
            }
        }
        
        // Process UI imports
        scriptCode = processImports(scriptCode, currAbs, fileToModule, pendingFiles, modCounter);
        
        // Prepend WASM initialization for 100% sync guarantee
        scriptCode = "import \"./wasm_init.tsx\";\n" + scriptCode;
        
        std::string modOutPath = joinPath(bcDir, fileToModule[currAbs]);
        if (!writeFileText(modOutPath, scriptCode)) {
            std::cerr << "Failed to write module: " << modOutPath << std::endl;
            return 1;
        }
    }

    if (globalCCode.empty()) {
        std::cerr << "No lang.c { ... } section found globally." << std::endl;
        return 1;
    }
    
    // --- @jelll-sync: inject synchronized struct code ---
    auto syncStructs = parseSyncStructs(globalCCode);
    std::string syncTS;
    if (!syncStructs.empty()) {
        detectSyncMethods(globalCCode, syncStructs);
        globalCCode += "\n" + generateSyncCCode(syncStructs);
        syncTS = generateSyncTSCode(syncStructs);
        std::cout << "[JELLL] Sync: " << syncStructs.size() << " struct(s) synchronized globally" << std::endl;
    }

    // --- Generate WASM setup code (wasm_init.tsx) ---
    std::string wasmInitContent = generateWasmInitCode();
    if (!syncStructs.empty()) {
        wasmInitContent += "\n" + syncTS;
    }
    if (!writeFileText(joinPath(bcDir, "wasm_init.tsx"), wasmInitContent)) {
        std::cerr << "Failed to write wasm_init.tsx" << std::endl;
        return 1;
    }

    std::string corePath = joinPath(bcDir, "core.cpp");
    std::string entryPath = joinPath(bcDir, modeToEntryName(mode));
    std::string wasmOut = joinPath(distDir, "native.wasm");
    std::string bundleOut = joinPath(distDir, "bundle.js");
    std::string indexOut = joinPath(distDir, "index.html");
    std::string stderrEmcc = joinPath(bcDir, "emcc.stderr.log");
    std::string stderrEsbuild = joinPath(bcDir, "esbuild.stderr.log");

    std::vector<std::string> wasmObjects;

    // --- Write C code to temporary file ---
    if (!globalCCode.empty()) {
        if (!writeFileText(corePath, globalCCode)) {
            std::cerr << "Failed to write: " << corePath << std::endl;
            return false;
        }
        wasmObjects.push_back("\"" + corePath + "\"");
    }

    // --- Compile Rust code ---
    if (!globalRsCode.empty()) {
        if (!checkCommandAvailable("rustc")) {
            std::cerr << "rustc was not found in PATH. Please install Rust (rustup) and add wasm32-unknown-unknown target." << std::endl;
            return false;
        }
        std::string rsPath = joinPath(bcDir, "core.rs");
        if (!writeFileText(rsPath, globalRsCode)) return false;
        std::string rsOut = joinPath(bcDir, "core_rs.o");
        std::string rsCmd = "rustc --target=wasm32-unknown-unknown --emit=obj -O -o \"" + rsOut + "\" \"" + rsPath + "\"";
        if (runCommandWithCapturedStderr("Compiling Rust...", rsCmd, joinPath(bcDir, "rustc.stderr.log")) != 0) return false;
        wasmObjects.push_back("\"" + rsOut + "\"");
    }

    // --- Compile Zig code ---
    if (!globalZigCode.empty()) {
        if (!checkCommandAvailable("zig")) {
            std::cerr << "zig was not found in PATH. Please install Zig compiler." << std::endl;
            return false;
        }
        std::string zigPath = joinPath(bcDir, "core.zig");
        if (!writeFileText(zigPath, globalZigCode)) return false;
        std::string zigOut = joinPath(bcDir, "core_zig.o");
        std::string zigCmd = "zig build-obj -target wasm32-freestanding -O ReleaseFast -fPIE -o \"" + zigOut + "\" \"" + zigPath + "\"";
        if (runCommandWithCapturedStderr("Compiling Zig...", zigCmd, joinPath(bcDir, "zig.stderr.log")) != 0) return false;
        wasmObjects.push_back("\"" + zigOut + "\"");
    }

    // --- Check external tool availability ---
    if (!checkCommandAvailable("emcc")) {
        std::cerr << "emcc was not found in PATH. Install and activate Emscripten, then retry." << std::endl;
        return false;
    }
    if (!checkCommandAvailable("esbuild")) {
        std::cerr << "esbuild was not found in PATH. Install it (npm i -g esbuild or project-local setup), then retry." << std::endl;
        return false;
    }

    // --- Compile C++ and Link WASM via emcc ---
    std::string emccCmd = "emcc";
    for (const auto& obj : wasmObjects) {
        emccCmd += " " + obj;
    }
    emccCmd += " -O3 -s WASM=1 -s SIDE_MODULE=1 --no-entry -o \"" + wasmOut + "\"";
    int emccRc = runCommandWithCapturedStderr("Linking WebAssembly...", emccCmd, stderrEmcc);
    if (emccRc != 0) {
        std::cerr << "[JELLL] emcc build failed with exit code: " << emccRc << std::endl;
        return false;
    }

    // --- Bundle JS/TS via esbuild ---
    std::string esbuildCmd =
        "esbuild \"" + entryPath + "\" --bundle --minify --format=esm --outfile=\"" + bundleOut + "\"";
    int esbuildRc = runCommandWithCapturedStderr("Bundling JS/TS...", esbuildCmd, stderrEsbuild);
    if (esbuildRc != 0) {
        std::cerr << "[JELLL] esbuild bundle failed with exit code: " << esbuildRc << std::endl;
        return false;
    }

    // --- Generate index.html (always regenerate to include latest css/html) ---
    {
        std::string htmlContent = makeIndexHtml(globalCssCode, globalHtmlCode);
        if (!writeFileText(indexOut, htmlContent)) {
            std::cerr << "[JELLL] Failed to write: " << indexOut << std::endl;
            return false;
        }
    }

    // --- Done ---
    std::cout << "[JELLL] Build complete: " << wasmOut << " and " << bundleOut << std::endl;
    std::cout << "[JELLL] dist: " << absolutePath(distDir) << std::endl;
    return true;
}

// ==========================================================================
// Main entry point
// ==========================================================================

int main(int argc, char* argv[]) {
    bool isDev = false;
    std::string inputFile;
    Mode mode = MODE_AUTO;
    int argIdx = 1;

    if (argc > argIdx) {
        std::string firstArg = argv[argIdx];
        if (firstArg == "setup" || firstArg == "--setup") {
            char selfPath[MAX_PATH];
            DWORD pathLen = GetModuleFileNameA(NULL, selfPath, MAX_PATH);
            if (pathLen == 0 || pathLen >= MAX_PATH) {
                std::cerr << "[JELLL] Failed to determine executable path." << std::endl;
                return 1;
            }
            std::string exeDir = parentPath(std::string(selfPath, pathLen));
            
            std::string scriptPath = joinPath(exeDir, "scripts\\postinstall.js");
            if (fs::exists(scriptPath)) {
                std::cout << "[JELLL] Launching environment setup..." << std::endl;
                std::string cmd = "node \"" + scriptPath + "\" --interactive";
                std::system(cmd.c_str());
                return 0;
            }
            
            std::string batchPath = joinPath(exeDir, "setup.bat");
            if (fs::exists(batchPath)) {
                std::system(batchPath.c_str());
                return 0;
            }
            std::cerr << "[JELLL] Setup script or batch file not found at: " << exeDir << std::endl;
            return 1;
        }
    }

    if (argc > argIdx && (std::string(argv[argIdx]) == "dev" || std::string(argv[argIdx]) == "--watch")) {
        isDev = true;
        argIdx++;
    }

    if (argc > argIdx) {
        inputFile = argv[argIdx];
        Mode extMode;
        if (detectModeByExt(inputFile, extMode)) {
            mode = extMode;
        } else if (!detectModeByArgv0(argv[0], mode)) {
            std::cerr << "Unsupported file extension. Use .jelll, .jsbc, .tsbc, or .tsxbc" << std::endl;
            return 1;
        }
    } else {
        bool modeFromArgv0 = detectModeByArgv0(argv[0], mode);
        if (modeFromArgv0) {
            inputFile = findInputByMode(mode);
        } else {
            inputFile = findInputByMode(MODE_AUTO);
            mode = MODE_AUTO;
        }
        if (inputFile.empty()) {
            std::cerr << "Usage: jelll [dev] <filename.jelll>" << std::endl;
            return 1;
        }
    }

    if (!isDev) {
        return runBuild(inputFile, mode) ? 0 : 1;
    }

    std::cout << "[JELLL] Starting Development Mode..." << std::endl;
    if (!runBuild(inputFile, mode)) {
        std::cerr << "[JELLL] Initial build failed, but watching for changes..." << std::endl;
    }

    std::cout << "[JELLL] Starting local server..." << std::endl;
    // Detached background esbuild server
    std::system("start /B cmd /c \"esbuild --servedir=dist >nul 2>nul\"");
    std::cout << "[JELLL] Dev server running at http://127.0.0.1:8000" << std::endl;
    std::cout << "[JELLL] Watching for file changes... (Press Ctrl+C to stop)" << std::endl;

    // Save initial timestamps
    std::unordered_map<std::string, fs::file_time_type> lastWriteTimes;
    auto updateTimestamps = [&]() -> bool {
        bool changed = false;
        for (auto& p : fs::recursive_directory_iterator(".")) {
            if (p.is_regular_file()) {
                std::string ext = toLower(p.path().extension().string());
                if (ext == ".jelll" || ext == ".jsbc" || ext == ".tsbc" || ext == ".tsxbc") {
                    auto pathStr = p.path().string();
                    auto wt = fs::last_write_time(p);
                    if (lastWriteTimes.find(pathStr) == lastWriteTimes.end() || lastWriteTimes[pathStr] != wt) {
                        lastWriteTimes[pathStr] = wt;
                        changed = true;
                    }
                }
            }
        }
        return changed;
    };

    updateTimestamps(); // init

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (updateTimestamps()) {
            std::cout << "\n[JELLL] File change detected! Rebuilding..." << std::endl;
            runBuild(inputFile, mode);
        }
    }

    return 0;
}
