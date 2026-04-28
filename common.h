#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <pathcch.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shlobj_core.h>
#include <pathcch.h>
#pragma warning(push)
#pragma warning(disable: 4244)
#include "quickjs.h"
#pragma warning(pop)
#include "quickjs-libc.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "Pathcch.lib")

#define WINDOW_CLASS			L"TablacusExplorerLite"
#define WINDOW_CLASS2			L"TablacusExplorerLite2"
#define PATH_BLANK				L"about:blank"
#define MAX_LOADSTRING			100
#define MAX_HISTORY				32
#define MAX_CSIDL				0x3e
#define CSIDL_LIBRARY			0x3e
#define CSIDL_USER				0x3f
#define CSIDL_RESULTSFOLDER		0x40
#define CSIDL_UNAVAILABLE		0x41
#define MAX_CSIDL2				0x42

#define SHGDN_FORPARSINGEX	0x80000000

#define MAX_PATHEX 32768

std::wstring Utf8ToWide(const char* utf8);
std::string  WideToUtf8(const wchar_t* wide);
std::string LoadFile(const wchar_t* path);

VOID FreeBSTR(BSTR* bstr);

struct WStrNullable {
    std::wstring storage;
    LPCWSTR ptr = nullptr;
};

struct EventMap {
    std::unordered_map<std::string, std::vector<JSValue>> map;
};

struct UIElement  {
    EventMap events;
    JSContext* ctx;
    JSValue jsThis;
    HWND hwnd;
    std::wstring id;
    IExplorerBrowser* pEB;
//    BOOL defaultPrevented;
};

bool JS_ToWStrNullable(JSContext* ctx, JSValueConst val, WStrNullable& out);
JSValue JS_FromPoint(JSContext* ctx, const POINT& pt);

VOID SafeRelease(PVOID ppObj);
VOID teCoTaskMemFree(LPVOID pv);
BOOL teIsClan(HWND hwndRoot, HWND hwnd);
BOOL teVerifyVersion(DWORD dwMajor, DWORD dwMinor, DWORD dwBuild);
HMODULE teLoadLibrary(LPCWSTR lpszName);
std::wstring tcPathAppend(LPCWSTR pszPath, LPCWSTR pszFile);
VOID teSysFreeString(BSTR* pbs);
BSTR teSysAllocStringLen(const OLECHAR* strIn, UINT uSize);
int teStrCmpIWA(LPCWSTR lpStringW, LPCSTR lpStringA);
UIElement * GetUIElement (HWND hwnd);
uint32_t JS_GetArrayLength(JSContext* ctx, JSValueConst arr);
BOOL FireEvent(HWND hwnd, const char* name, JSValue e);
BOOL FireKeyEvent(HWND hwnd, const char* name, WPARAM vk);
BOOL FireMouseEvent(HWND hwnd, const char* name, int button, WPARAM wParam, LPARAM lParam);
std::wstring JS_ToWideString(JSContext* ctx, JSValueConst val);

#if !defined(_WINDLL) && !defined(_DEBUG)
#pragma comment(linker, "/entry:\"wWinMain\"")
#endif

#pragma execution_character_set("utf-8")
#define CP_TE CP_UTF8

