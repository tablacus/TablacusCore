// fs.cpp : A native module providing readFileSync / writeFileSync 
//          similar to Node.js's fs module.
//
// Supported encodings: "utf8"/"utf-8" (default), "utf16le"/"ucs2",
//                      "latin1"/"binary"/"ascii"
// * Since this project does not have Buffer / TypedArray, unlike Node.js,
//   readFileSync returns a string (interpreted as utf-8) even if the encoding is omitted.
//   If you want to handle binary data as-is, specify "binary" for the encoding.
//
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

#include "common.h"
#if defined(_WINDLL) || defined(_DEBUG)
#include "fs.h"

namespace {

// Normalizes Node.js encoding names. Returns an empty string if unsupported.
std::string NormalizeEncoding(const std::string& enc)
{
    std::string s = enc;
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });

    if (s.empty() || s == "utf8" || s == "utf-8") return "utf8";
    if (s == "utf16le" || s == "utf-16le" || s == "ucs2" || s == "ucs-2") return "utf16le";
    if (s == "latin1" || s == "binary" || s == "ascii") return "binary";
    return "";
}

// Interprets the 2nd and 3rd arguments of readFileSync/writeFileSync (string or {encoding, flag} object).
bool ParseEncodingArg(JSContext* ctx, JSValueConst arg, std::string& encoding, std::string& flag)
{
    encoding = "utf8";
    flag.clear();

    if (JS_IsUndefined(arg) || JS_IsNull(arg)) {
        return true;
    }

    if (JS_IsString(arg)) {
        const char* s = JS_ToCString(ctx, arg);
        std::string norm = NormalizeEncoding(s ? s : "");
        JS_FreeCString(ctx, s);
        if (norm.empty()) {
            JS_ThrowTypeError(ctx, "Unknown encoding");
            return false;
        }
        encoding = norm;
        return true;
    }

    if (JS_IsObject(arg)) {
        JSValue encVal = JS_GetPropertyStr(ctx, arg, "encoding");
        if (JS_IsString(encVal)) {
            const char* s = JS_ToCString(ctx, encVal);
            std::string norm = NormalizeEncoding(s ? s : "");
            JS_FreeCString(ctx, s);
            if (norm.empty()) {
                JS_FreeValue(ctx, encVal);
                JS_ThrowTypeError(ctx, "Unknown encoding");
                return false;
            }
            encoding = norm;
        }
        JS_FreeValue(ctx, encVal);

        JSValue flagVal = JS_GetPropertyStr(ctx, arg, "flag");
        if (JS_IsString(flagVal)) {
            const char* s = JS_ToCString(ctx, flagVal);
            flag = s ? s : "";
            JS_FreeCString(ctx, s);
        }
        JS_FreeValue(ctx, flagVal);
        return true;
    }

    JS_ThrowTypeError(ctx, "options must be a string or an object");
    return false;
}

} // namespace

// fs.readFileSync(path, encoding = "utf-8")
static JSValue js_fs_readFileSync(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "The \"path\" argument must be specified");
    }

    std::wstring wpath = JS_ToWideString(ctx, argv[0]);

    std::string encoding, flag;
    if (argc >= 2 && !ParseEncodingArg(ctx, argv[1], encoding, flag)) {
        return JS_EXCEPTION;
    }

    HANDLE hFile = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return JS_ThrowPlainError(ctx, "ENOENT: no such file or directory, open '%s'",
            WideToUtf8(wpath.c_str()).c_str());
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(hFile, &size)) {
        CloseHandle(hFile);
        return JS_ThrowPlainError(ctx, "failed to get file size of '%s'",
            WideToUtf8(wpath.c_str()).c_str());
    }

    std::string raw;
    raw.resize((size_t)size.QuadPart);

    BOOL ok = TRUE;
    LONGLONG remaining = size.QuadPart;
    char* p = raw.data();
    while (remaining > 0 && ok) {
        DWORD chunk = (DWORD)min(remaining, (LONGLONG)(1 << 30) /* Read in 1GB chunks */);
        DWORD read = 0;
        ok = ReadFile(hFile, p, chunk, &read, nullptr);
        p += read;
        remaining -= read;
        if (read == 0) break;
    }
    CloseHandle(hFile);

    if (!ok || remaining != 0) {
        return JS_ThrowPlainError(ctx, "failed to read file '%s'", WideToUtf8(wpath.c_str()).c_str());
    }

    if (encoding == "utf16le") {
        return JS_NewStringUTF16(ctx, (const uint16_t*)raw.data(), raw.size() / 2);
    }

    if (encoding == "binary") {
        // Equivalent to Node.js "latin1"/"binary": 1 byte = 1 code point (0-255)
        std::vector<uint16_t> wide(raw.size());
        for (size_t i = 0; i < raw.size(); ++i) {
            wide[i] = (uint8_t)raw[i];
        }
        return JS_NewStringUTF16(ctx, wide.data(), wide.size());
    }

    // "utf8" (default value)
    return JS_NewStringLen(ctx, raw.data(), raw.size());
}

// fs.writeFileSync(path, data, encoding = "utf-8")
static JSValue js_fs_writeFileSync(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "writeFileSync requires \"path\" and \"data\" arguments");
    }

    if (!JS_IsString(argv[1])) {
        return JS_ThrowTypeError(ctx, "\"data\" must be a string (Buffer/TypedArray is not supported)");
    }

    std::wstring wpath = JS_ToWideString(ctx, argv[0]);

    std::string encoding, flag;
    if (argc >= 3 && !ParseEncodingArg(ctx, argv[2], encoding, flag)) {
        return JS_EXCEPTION;
    }
    if (encoding.empty()) encoding = "utf8";

    // Convert JS string to a byte sequence of the specified encoding
    std::string bytes;
    if (encoding == "utf16le") {
        size_t len16 = 0;
        const uint16_t* buf16 = JS_ToCStringLenUTF16(ctx, &len16, argv[1]);
        if (!buf16) return JS_EXCEPTION;
        bytes.assign((const char*)buf16, len16 * sizeof(uint16_t));
        JS_FreeCStringUTF16(ctx, buf16);
    } else if (encoding == "binary") {
        size_t len16 = 0;
        const uint16_t* buf16 = JS_ToCStringLenUTF16(ctx, &len16, argv[1]);
        if (!buf16) return JS_EXCEPTION;
        bytes.resize(len16);
        for (size_t i = 0; i < len16; ++i) bytes[i] = (char)(uint8_t)buf16[i];
        JS_FreeCStringUTF16(ctx, buf16);
    } else {
        size_t len8 = 0;
        const char* buf8 = JS_ToCStringLen(ctx, &len8, argv[1]);
        if (!buf8) return JS_EXCEPTION;
        bytes.assign(buf8, len8);
        JS_FreeCString(ctx, buf8);
    }

    // flag: append if "a"/"a+", overwrite otherwise (default). Equivalent to Node.js options.flag.
    bool append = (flag == "a" || flag == "a+");
    HANDLE hFile;
    if (append) {
        hFile = CreateFileW(wpath.c_str(), FILE_APPEND_DATA, 0, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    } else {
        hFile = CreateFileW(wpath.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }

    if (hFile == INVALID_HANDLE_VALUE) {
        return JS_ThrowPlainError(ctx, "failed to open '%s' for writing", WideToUtf8(wpath.c_str()).c_str());
    }

    BOOL ok = TRUE;
    const char* p = bytes.data();
    size_t remaining = bytes.size();
    while (remaining > 0 && ok) {
        DWORD chunk = (DWORD)min(remaining, (size_t)(1 << 30) /* Write in 1GB chunks */);
        DWORD written = 0;
        ok = WriteFile(hFile, p, chunk, &written, nullptr);
        p += written;
        remaining -= written;
        if (written == 0) break;
    }
    CloseHandle(hFile);

    if (!ok || remaining != 0) {
        return JS_ThrowPlainError(ctx, "failed to write file '%s'", WideToUtf8(wpath.c_str()).c_str());
    }

    return JS_UNDEFINED;
}

static const JSCFunctionListEntry js_fs_funcs[] = {
    JS_CFUNC_DEF("readFileSync",  2, js_fs_readFileSync),
    JS_CFUNC_DEF("writeFileSync", 3, js_fs_writeFileSync),
};

static int js_fs_init(JSContext* ctx, JSModuleDef* m)
{
    return JS_SetModuleExportList(
        ctx, m, js_fs_funcs, sizeof(js_fs_funcs) / sizeof(JSCFunctionListEntry));
}

JSModuleDef* js_init_module_fs(JSContext* ctx, const char* module_name)
{
    JSModuleDef* m = JS_NewCModule(ctx, module_name, js_fs_init);
    if (!m) return nullptr;

    JS_AddModuleExportList(
        ctx, m, js_fs_funcs, sizeof(js_fs_funcs) / sizeof(JSCFunctionListEntry));

    return m;
}

#endif