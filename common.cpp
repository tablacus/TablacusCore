#include <windows.h>
#include "common.h"
#if defined(_WINDLL) || defined(_DEBUG)

int		g_nException = 256;
LPCWSTR g_strException = nullptr;
HBRUSH	g_hbrDarkBackground = nullptr;
OPENFILENAME* g_pofn = nullptr;
std::vector<HMODULE>	g_phModule;

extern std::unordered_map<DWORD, HHOOK> g_umCBTHook;

VOID FreeBSTR(BSTR* bstr)
{
    if (*bstr) {
        ::SysFreeString(*bstr);
        *bstr = nullptr;
    }
}

std::wstring Utf8ToWide(const char* utf8)
{
    if (!utf8) {
        return L"";
    }
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &result[0], size);
    result.resize(wcslen(result.c_str()));
    return result;
}

std::string WideToUtf8(const wchar_t* wide)
{
    if (!wide) {
        return "";
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &result[0], size, nullptr, nullptr);
    result.resize(strlen(result.c_str()));
    return result;
}

std::string BSTRToUtf8(BSTR* wide)
{
    if (!wide || !*wide) {
        return "";
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, *wide, -1, nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, *wide, -1, &result[0], size, nullptr, nullptr);
    FreeBSTR(wide);
    result.resize(strlen(result.c_str()));
    return result;
}



std::string LoadFile(const wchar_t* path)
{
    FILE* fp = nullptr;

    if (_wfopen_s(&fp, path, L"rb") != 0 || !fp)
        return "";

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    std::vector<char> buffer(size);
    fread(buffer.data(), 1, size, fp);
    fclose(fp);

    return std::string(buffer.begin(), buffer.end());
}

std::wstring JS_ToWideString(JSContext* ctx, JSValueConst val)
{
    const char* str = JS_ToCString(ctx, val);
    std::wstring wstr = Utf8ToWide(str);
    JS_FreeCString(ctx, str);
    return wstr;
}

bool JS_ToWStrNullable(JSContext* ctx, JSValueConst val, WStrNullable& out)
{
    if (JS_IsNull(val) || JS_IsUndefined(val)) {
        out.ptr = nullptr;
        return true;
    }

    if (!JS_IsString(val)) {
        JS_ThrowTypeError(ctx, "string or null expected");
        return false;
    }

    const char* utf8 = JS_ToCString(ctx, val);
    if (!utf8) {
        return false;
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (len <= 0) {
        JS_FreeCString(ctx, utf8);
        return false;
    }

    out.storage.resize(len - 1);
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &out.storage[0], len);

    JS_FreeCString(ctx, utf8);

    out.ptr = out.storage.c_str();
    return true;
}

JSValue JS_FromPoint(JSContext* ctx, const POINT& pt)
{
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "x", JS_NewInt32(ctx, pt.x));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewInt32(ctx, pt.y));
    return obj;
}

VOID SafeRelease(PVOID ppObj)
{
    try {
        IUnknown** ppunk = static_cast<IUnknown**>(ppObj);
        if (*ppunk) {
            (*ppunk)->Release();
            *ppunk = nullptr;
        }
    }
    catch (...) {
        g_nException = 0;
#ifdef _DEBUG
        g_strException = L"SafeRelease";
#endif
    }
}

VOID teCoTaskMemFree(LPVOID pv)
{
    if (pv) {
        try {
            ::CoTaskMemFree(pv);
        }
        catch (...) {
            g_nException = 0;
#ifdef _DEBUG
            g_strException = L"CoTaskMemFree";
#endif
        }
    }
}

BOOL teIsClan(HWND hwndRoot, HWND hwnd)
{
    while (hwnd != hwndRoot) {
        hwnd = GetParent(hwnd);
        if (!hwnd) {
            return FALSE;
        }
    }
    return TRUE;
}

BOOL teVerifyVersion(DWORD dwMajor, DWORD dwMinor, DWORD dwBuild)
{
    DWORDLONG dwlConditionMask = 0;
    VER_SET_CONDITION(dwlConditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(dwlConditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);
    VER_SET_CONDITION(dwlConditionMask, VER_BUILDNUMBER, VER_GREATER_EQUAL);
    OSVERSIONINFOEX osvi = { sizeof(OSVERSIONINFOEX), dwMajor, dwMinor, dwBuild };
    return VerifyVersionInfo(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_BUILDNUMBER, dwlConditionMask);
}

HMODULE teLoadLibrary(LPCWSTR lpszName)
{
    WCHAR pszPath[MAX_PATHEX];
    ::GetSystemDirectory(pszPath, MAX_PATHEX);
    ::PathCchAppend(pszPath, MAX_PATHEX, lpszName);
    HMODULE hModule = ::GetModuleHandle(pszPath);
    if (!hModule) {
        hModule = ::LoadLibrary(pszPath);
        if (hModule) {
            g_phModule.push_back(hModule);
        }
    }
    return hModule;
}

std::wstring tcPathAppend(LPCWSTR pszPath, LPCWSTR pszFile)
{
	int size = lstrlen(pszPath) + lstrlen(pszFile) + 2;
    std::wstring result(size, 0);
   	lstrcpy(&result[0], pszPath);
    ::PathCchAppend(&result[0], size, pszFile);
    result.resize(lstrlen(result.c_str()));
    return result;
}

VOID teSysFreeString(BSTR* pbs)
{
    if (*pbs) {
        ::SysFreeString(*pbs);
        *pbs = nullptr;
    }
}

BSTR teSysAllocStringLen(const OLECHAR* strIn, UINT uSize)
{
    UINT uOrg = lstrlen(strIn);
    if (strIn && uSize > uOrg) {
        BSTR bs = ::SysAllocStringLen(nullptr, uSize);
        lstrcpy(bs, strIn);
        return bs;
    }
    return ::SysAllocStringLen(strIn, uSize);
}

int teStrCmpIWA(LPCWSTR lpStringW, LPCSTR lpStringA) {
    int wc1 = lpStringW ? tolower(*lpStringW) : NULL;
    int wc2 = lpStringA ? tolower(*lpStringA) : NULL;
    int result = wc1 - wc2;
    if (result || wc1 == NULL || wc2 == NULL) {
        return result;
    }
    for (int i = 1;; ++i) {
        wc1 = tolower(lpStringW[i]);
        wc2 = tolower(lpStringA[i]);
        result = wc1 - wc2;
        if (result || wc1 == NULL || wc2 == NULL) {
            break;
        }
    };
    return result;
}

WindowData* GetWindowData(HWND hwnd)
{
    return (WindowData*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
}

uint32_t JS_GetArrayLength(JSContext* ctx, JSValueConst arr)
{
    uint32_t len = 0;

    JSValue val = JS_GetPropertyStr(ctx, arr, "length");
    if (!JS_IsException(val)) {
        JS_ToUint32(ctx, &len, val);
    }
    JS_FreeValue(ctx, val);

    return len;
}

#endif