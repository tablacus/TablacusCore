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
#include <shobjidl.h>
#include <pathcch.h>
#include <CommCtrl.h>
#include <shdispid.h>
#include <shlwapi.h>
#pragma warning(push)
#pragma warning(disable: 4244)
#include "quickjs.h"
#pragma warning(pop)
#include "quickjs-libc.h"
#include "darkmode.h"

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

class CBrowserSink : public IExplorerBrowserEvents, public IDispatch
{
public:
    LONG refCount;
    HWND m_hwnd;
    HWND m_hwndDV;
    HWND m_hwndLV;
    HWND m_hwndDT;
    IShellView* m_pSV;
    IDispatch* m_pdisp;
    IExplorerBrowser* m_pEB;
    DWORD m_dwCookie;
    DWORD m_dwEventsCookie;

    CBrowserSink(HWND h);
    virtual ~CBrowserSink();

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    //IExplorerBrowserEvents
    HRESULT STDMETHODCALLTYPE OnNavigationPending(PCIDLIST_ABSOLUTE pidlFolder) override;
    HRESULT STDMETHODCALLTYPE OnViewCreated(IShellView* psv) override;
    HRESULT STDMETHODCALLTYPE OnNavigationComplete(PCIDLIST_ABSOLUTE pidlFolder) override;
    HRESULT STDMETHODCALLTYPE OnNavigationFailed(PCIDLIST_ABSOLUTE pidlFolder) override;
    //IDispatch
    STDMETHODIMP GetTypeInfoCount(UINT* pctinfo) override;
    STDMETHODIMP GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo** ppTInfo) override;
    STDMETHODIMP GetIDsOfNames(REFIID riid, LPOLESTR* rgszNames, UINT cNames, LCID lcid, DISPID* rgDispId) override;
    STDMETHODIMP Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS* pDispParams, VARIANT* pVarResult, EXCEPINFO* pExcepInfo, UINT* puArgErr) override;

    VOID SetRedraw(BOOL bRedraw);
    VOID SetFolderFlags(BOOL bGetIconSize);
    VOID GetShellFolderView();
};

struct UIElement  {
    EventMap events;
    JSContext* ctx;
    JSValue jsThis;
    HWND hwnd;
    std::wstring id;
    // --- ExplorerBrowser ---
    CBrowserSink *pSink;
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

#if defined(_WINDLL) || defined(_DEBUG)

#endif



