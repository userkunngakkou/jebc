// ==========================================================================
// JEBC (JavaScript/TypeScript + Emscripten Bundled Compiler)
//
// Reads .jsbc / .tsbc / .tsxbc files, compiles C code to WASM via emcc,
// bundles JS/TS code via esbuild, and outputs everything into dist/.
// A single-file build tool.
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
#include <vector>
#include <windows.h>

// ==========================================================================
// Output mode enumeration
// ==========================================================================

enum Mode {
    MODE_JS,
    MODE_TS,
    MODE_TSX
};

// ==========================================================================
// Section structure - represents a parsed lang block from the input file
// ==========================================================================

struct Section {
    std::string kind;  // "c", "js", "ts", "tsx"
    std::string body;  // source code inside the block
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
    std::cout << "[JEBC] " << label << std::endl;
    int rc = std::system(wrapped.c_str());
    if (rc != 0) {
        std::string errText;
        if (readFileText(stderrPath, errText) && !errText.empty()) {
            std::cerr << errText << std::endl;
        }
        std::cerr << "[JEBC] command failed: " << cmd << std::endl;
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
    std::regex rx(R"(\blang(?:[.\s]*)(c|js|ts|tsx)\s*\{)");
    std::vector<bool> codeMask = buildCodeMask(text);
    std::size_t searchPos = 0;
    while (searchPos < text.size()) {
        std::smatch m;
        std::string tail = text.substr(searchPos);
        if (!std::regex_search(tail, m, rx)) {
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
// Code generation - WASM glue code and HTML template
// ==========================================================================

/// Generate the WASM loader + glue code.
/// Inserted at the top of the bundle so that C functions are accessible
/// through the 'native' object.
static std::string makeGlueCode() {
    return R"(const native = {};
const sharedBuffer = new SharedArrayBuffer(16 * 1024 * 1024);
const sharedMemory = new WebAssembly.Memory({ initial: 256, maximum: 256, shared: true });
const __jebcImports = {
  env: {
    memory: sharedMemory,
    sharedBuffer
  }
};
const __jebcWasmUrl = new URL('./native.wasm', import.meta.url);
const __jebcWasmResponse = await fetch(__jebcWasmUrl);
if (!__jebcWasmResponse.ok) {
  throw new Error(`native.wasm load failed: ${__jebcWasmResponse.status}`);
}
let __jebcWasm;
try {
  __jebcWasm = await WebAssembly.instantiateStreaming(Promise.resolve(__jebcWasmResponse), __jebcImports);
} catch (e) {
  const __jebcBytes = await __jebcWasmResponse.arrayBuffer();
  __jebcWasm = await WebAssembly.instantiate(__jebcBytes, __jebcImports);
}
Object.assign(native, __jebcWasm.instance.exports);
globalThis.native = native;
globalThis.sharedBuffer = sharedBuffer;
globalThis.sharedMemory = sharedMemory;
)";
}

/// Generate a minimal HTML template (used when dist/index.html doesn't exist)
static std::string makeMinimalIndexHtml() {
    return R"(<!doctype html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>JEBC Output</title>
</head>
<body>
  <script type="module" src="./bundle.js"></script>
</body>
</html>
)";
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
        if (toLower(extensionOf(name)) == wantedExt) {
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
// Main entry point
// ==========================================================================

int main(int argc, char* argv[]) {
    // --- Determine build mode ---
    Mode mode = MODE_JS;
    bool modeFromArgv0 = detectModeByArgv0(argv[0], mode);
    std::string inputFile;

    if (argc >= 2) {
        inputFile = argv[1];
        Mode extMode = MODE_JS;
        if (detectModeByExt(inputFile, extMode)) {
            mode = extMode;
        } else if (!modeFromArgv0) {
            std::cerr << "Unsupported file extension. Use .jsbc, .tsbc, or .tsxbc" << std::endl;
            return 1;
        }
    } else if (modeFromArgv0) {
        inputFile = findInputByMode(mode);
        if (inputFile.empty()) {
            std::cerr << "Input file not specified. Place exactly one ." << modeToScriptKind(mode) << "bc file in current directory or pass an explicit path." << std::endl;
            return 1;
        }
    } else {
        std::cerr << "Usage: jebc <filename.jsbc|filename.tsbc|filename.tsxbc>" << std::endl;
        return 1;
    }

    // --- Read input file ---
    std::string source;
    if (!readFileText(inputFile, source)) {
        std::cerr << "Failed to open input file: " << inputFile << std::endl;
        return 1;
    }

    // --- Parse sections ---
    auto sections = parseSections(source);
    std::unordered_map<std::string, std::string> blocksByKind;
    std::string targetKind = modeToScriptKind(mode);
    for (const auto& sec : sections) {
        blocksByKind[sec.kind] += sec.body + "\n";
    }

    std::string cCode = blocksByKind["c"];
    std::string scriptCode = blocksByKind[targetKind];
    if (scriptCode.empty() && targetKind != "js") {
        scriptCode = blocksByKind["js"];
    }

    if (cCode.empty()) {
        std::cerr << "No lang.c { ... } section found." << std::endl;
        return 1;
    }
    if (scriptCode.empty()) {
        std::cerr << "No lang." << targetKind << " { ... } section found for selected mode." << std::endl;
        return 1;
    }

    // --- Check external tool availability ---
    if (!checkCommandAvailable("emcc")) {
        std::cerr << "emcc was not found in PATH. Install and activate Emscripten, then retry." << std::endl;
        return 1;
    }
    if (!checkCommandAvailable("esbuild")) {
        std::cerr << "esbuild was not found in PATH. Install it (npm i -g esbuild or project-local setup), then retry." << std::endl;
        return 1;
    }

    // --- Prepare build directories ---
    std::string bcDir = ".bc_build";
    std::string distDir = "dist";
    if (!ensureDirectory(bcDir) || !ensureDirectory(distDir)) {
        std::cerr << "Failed to create build directories." << std::endl;
        return 1;
    }

    std::string corePath = joinPath(bcDir, "core.cpp");
    std::string entryPath = joinPath(bcDir, modeToEntryName(mode));
    std::string wasmOut = joinPath(distDir, "native.wasm");
    std::string bundleOut = joinPath(distDir, "bundle.js");
    std::string indexOut = joinPath(distDir, "index.html");
    std::string stderrEmcc = joinPath(bcDir, "emcc.stderr.log");
    std::string stderrEsbuild = joinPath(bcDir, "esbuild.stderr.log");

    // --- Write C code to temporary file ---
    if (!writeFileText(corePath, cCode)) {
        std::cerr << "Failed to write: " << corePath << std::endl;
        return 1;
    }

    // --- Write JS/TS entry with glue code prepended ---
    std::string finalScript = makeGlueCode();
    finalScript += "\n";
    finalScript += scriptCode;
    if (!writeFileText(entryPath, finalScript)) {
        std::cerr << "Failed to write: " << entryPath << std::endl;
        return 1;
    }

    // --- Compile C++ to WASM via emcc ---
    std::string emccCmd =
        "emcc \"" + corePath + "\" -O3 -s WASM=1 -s SIDE_MODULE=1 --no-entry -o \"" + wasmOut + "\"";
    int emccRc = runCommandWithCapturedStderr("Compiling C++...", emccCmd, stderrEmcc);
    if (emccRc != 0) {
        std::cerr << "[JEBC] emcc build failed with exit code: " << emccRc << std::endl;
        return 1;
    }

    // --- Bundle JS/TS via esbuild ---
    std::string esbuildCmd =
        "esbuild \"" + entryPath + "\" --bundle --minify --format=esm --outfile=\"" + bundleOut + "\"";
    int esbuildRc = runCommandWithCapturedStderr("Bundling JS/TS...", esbuildCmd, stderrEsbuild);
    if (esbuildRc != 0) {
        std::cerr << "[JEBC] esbuild bundle failed with exit code: " << esbuildRc << std::endl;
        return 1;
    }

    // --- Generate index.html if it doesn't exist ---
    if (!pathExists(indexOut)) {
        if (!writeFileText(indexOut, makeMinimalIndexHtml())) {
            std::cerr << "[JEBC] Failed to write: " << indexOut << std::endl;
            return 1;
        }
    }

    // --- Done ---
    std::cout << "[JEBC] Build complete: " << wasmOut << " and " << bundleOut << std::endl;
    std::cout << "[JEBC] dist: " << absolutePath(distDir) << std::endl;
    std::cout << "Open this in your browser!" << std::endl;
    return 0;
}
