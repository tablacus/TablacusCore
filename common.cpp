#include <windows.h>
#include "common.h"
#if defined(_WINDLL) || defined(_DEBUG)

int		g_nException = 256;
LPCWSTR g_strException = nullptr;
HBRUSH	g_hbrDarkBackground = nullptr;
OPENFILENAME* g_pofn = nullptr;
std::vector<HMODULE>	g_phModule;
BOOL g_bDarkMode = FALSE;

extern std::unordered_map<DWORD, HHOOK> g_umCBTHook;

VOID teAdvise(IUnknown* punk, IID diid, IUnknown* punk2, PDWORD pdwCookie)
{
    IConnectionPointContainer* pCPC;
    if (SUCCEEDED(punk->QueryInterface(IID_PPV_ARGS(&pCPC)))) {
        IConnectionPoint* pCP;
        if (SUCCEEDED(pCPC->FindConnectionPoint(diid, &pCP))) {
            pCP->Advise(punk2, pdwCookie);
            pCP->Release();
        }
        pCPC->Release();
    }
}

VOID teUnadviseAndRelease(IUnknown* punk, IID diid, PDWORD pdwCookie)
{
    if (punk) {
        IConnectionPointContainer* pCPC;
        if SUCCEEDED(punk->QueryInterface(IID_PPV_ARGS(&pCPC))) {
            IConnectionPoint* pCP;
            if (SUCCEEDED(pCPC->FindConnectionPoint(diid, &pCP))) {
                pCP->Unadvise(*pdwCookie);
                pCP->Release();
            }
            pCPC->Release();
        }
        punk->Release();
    }
    *pdwCookie = 0;
}

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

UIElement * GetUIElement(HWND hwnd)
{
//    return (UIElement *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	return (UIElement*)GetProp(hwnd, L"UIElement");
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

BOOL FireEvent(HWND hwnd, const char* name, JSValue e)
{
    auto* el = GetUIElement(hwnd);
    if (!el) {
        return FALSE;
    }
    if (JS_IsUndefined(e)) {
        e = JS_NewObject(el->ctx);
    }
    if (JS_IsObject(e)) {
        JS_SetPropertyStr(el->ctx, e, "target", el->jsThis);
        JS_SetPropertyStr(el->ctx, e, "type", JS_NewString(el->ctx, name));
    }
    auto it = el->events.map.find(name);
    if (it == el->events.map.end()) {
        return FALSE;
    }
    for (auto& fn : it->second) {
        JSValue result = JS_Call(el->ctx, fn, el->jsThis, 1, &e);
		if (!JS_IsUndefined(result)) {
            return !JS_ToBool(el->ctx, result);
        }
    }
    return FALSE;
}

JSValue CreateKeyEvent(JSContext* ctx, WPARAM vk)
{
    JSValue e = JS_NewObject(ctx);
    // keyCode
    JS_SetPropertyStr(ctx, e, "keyCode", JS_NewInt32(ctx, (int)vk));

    // key
    wchar_t buf[8];
    GetKeyNameTextW((LONG)(MapVirtualKeyW((UINT)vk, 0) << 16), buf, 8);
    JS_SetPropertyStr(ctx, e, "key", JS_NewString(ctx, WideToUtf8(buf).c_str()));

    JS_SetPropertyStr(ctx, e, "shiftKey", JS_NewBool(ctx, GetKeyState(VK_SHIFT) < 0));
    JS_SetPropertyStr(ctx, e, "ctrlKey", JS_NewBool(ctx, GetKeyState(VK_CONTROL) < 0));
    JS_SetPropertyStr(ctx, e, "altKey", JS_NewBool(ctx, GetKeyState(VK_MENU) < 0));
    JS_SetPropertyStr(ctx, e, "metaKey", JS_NewBool(ctx, GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0));
    return e;
}

BOOL FireKeyEvent(HWND hwnd, const char* name, WPARAM vk)
{
    auto* el = GetUIElement(hwnd);
    if (!el) {
        return FALSE;
    }
    JSValue e = CreateKeyEvent(el->ctx, vk);
    return FireEvent(hwnd, name, e);
}

JSValue CreateMouseEvent(JSContext* ctx, WPARAM wParam)
{
    JSValue e = JS_NewObject(ctx);

    // buttons (Win32 → JS)
    int buttons = 0;
    if (wParam & MK_LBUTTON) {
        buttons |= 1;
    }
    if (wParam & MK_RBUTTON) {
        buttons |= 2;
    }
    if (wParam & MK_MBUTTON) {
        buttons |= 4;
    }
    if (wParam & MK_XBUTTON1) {
        buttons |= 8;
    }
    if (wParam & MK_XBUTTON2) {
        buttons |= 16;
    }
    JS_SetPropertyStr(ctx, e, "buttons", JS_NewInt32(ctx, buttons));
    JS_SetPropertyStr(ctx, e, "shiftKey", JS_NewBool(ctx, (wParam & MK_SHIFT) != 0));
    JS_SetPropertyStr(ctx, e, "ctrlKey", JS_NewBool(ctx, (wParam & MK_CONTROL) != 0));
    JS_SetPropertyStr(ctx, e, "altKey", JS_NewBool(ctx, (wParam & MK_ALT) != 0));
    JS_SetPropertyStr(ctx, e, "metaKey", JS_NewBool(ctx, GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0));
    return e;
}

BOOL FireMouseEvent(HWND hwnd, const char* name, int button, WPARAM wParam, LPARAM lParam)
{
    auto* el = GetUIElement(hwnd);
    if (!el) {
        return FALSE;
    }
    
    JSValue e = CreateMouseEvent(el->ctx, wParam);
    POINT pt = { LOWORD(lParam), HIWORD(lParam) };
	if (button >= 0) { // Button event
        JS_SetPropertyStr(el->ctx, e, "button", JS_NewInt32(el->ctx, button));
    } else { //Wheel event
        JS_SetPropertyStr(el->ctx, e, "deltaY", JS_NewInt32(el->ctx, GET_WHEEL_DELTA_WPARAM(wParam)));
        ::ScreenToClient(hwnd, &pt);
    }
    JS_SetPropertyStr(el->ctx, e, "clientX", JS_NewInt32(el->ctx, pt.x));
    JS_SetPropertyStr(el->ctx, e, "clientY", JS_NewInt32(el->ctx, pt.y));
    ::ClientToScreen(hwnd, &pt);
    JS_SetPropertyStr(el->ctx, e, "screenX", JS_NewInt32(el->ctx, pt.x));
    JS_SetPropertyStr(el->ctx, e, "screenY", JS_NewInt32(el->ctx, pt.y));
    return FireEvent(hwnd, name, e);
}

CBrowserSink::CBrowserSink(HWND hwnd)
{
    refCount = 1;
    m_hwnd = hwnd;
    m_hwndDT = NULL;
    m_hwndDV = NULL;
    m_hwndLV = NULL;
	m_pSV = nullptr;
    m_pdisp = nullptr;
}

CBrowserSink::~CBrowserSink()
{}

HRESULT STDMETHODCALLTYPE CBrowserSink::QueryInterface(REFIID riid, void** ppv)
{
    static const QITAB qit[] =
    {
        QITABENT(CBrowserSink, IDispatch),
        QITABENT(CBrowserSink, IExplorerBrowserEvents),
        { 0 },
#pragma warning( push )
#pragma warning( disable: 4838 )
    }
#pragma warning( pop )
    ;
    return QISearch(this, qit, riid, ppv);
/*    if (ppv == nullptr)
    {
        return E_POINTER;
    }

    if (riid == IID_IUnknown || riid == IID_IExplorerBrowserEvents)
    {
        *ppv = static_cast<IExplorerBrowserEvents*>(this);
        AddRef();
        return S_OK;
    }

    *ppv = nullptr;
    return E_NOINTERFACE;*/
}

ULONG STDMETHODCALLTYPE CBrowserSink::AddRef()
{
    return InterlockedIncrement(&refCount);
}

ULONG STDMETHODCALLTYPE CBrowserSink::Release()
{
    LONG r = InterlockedDecrement(&refCount);

    if (r == 0)
    {
        delete this;
    }

    return r;
}

HRESULT STDMETHODCALLTYPE CBrowserSink::OnNavigationPending(PCIDLIST_ABSOLUTE /*pidlFolder*/)
{
    return S_OK;
}

HRESULT STDMETHODCALLTYPE CBrowserSink::OnViewCreated(IShellView* psv)
{
    if (m_hwnd != nullptr)
    {
        UIElement* el = GetUIElement(m_hwnd);
        if (el != nullptr) {
            SetRedraw(FALSE);
            psv->QueryInterface(IID_PPV_ARGS(&m_pSV));
            GetShellFolderView();
            if (IUnknown_GetWindow(psv, &m_hwndDV) == S_OK) {
                SetProp(m_hwndDV, L"UIElement", el);
                FixChildren(m_hwndDV);
                m_hwndLV = FindWindowExA(m_hwndDV, 0, WC_LISTVIEWA, NULL);
                if (m_hwndLV) {
                    SetProp(m_hwndLV, L"UIElement", el);
				}

                /*                    IFolderView* fv = nullptr;

                                    if (SUCCEEDED(psv->QueryInterface(IID_PPV_ARGS(&fv))))
                                    {
                                        el->folderView = fv;
                                    }*/
            }
        }
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE CBrowserSink::OnNavigationComplete(PCIDLIST_ABSOLUTE /*pidlFolder*/)
{
    if (m_hwnd != nullptr)
    {
        FixChildren(m_hwndDV);
        SetFolderFlags(TRUE);
        SetRedraw(TRUE);
        FireEvent(m_hwnd, "navigate", JS_UNDEFINED);
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE CBrowserSink::OnNavigationFailed(PCIDLIST_ABSOLUTE /*pidlFolder*/)
{
    if (m_hwnd != nullptr)
    {
        FireEvent(m_hwnd, "navigateerror", JS_UNDEFINED);
    }

    return S_OK;
}

VOID CBrowserSink::SetRedraw(BOOL bRedraw)
{
    SendMessage(m_hwnd, WM_SETREDRAW, bRedraw, 0);
}

VOID CBrowserSink::SetFolderFlags(BOOL bGetIconSize)
{
    if (!m_pSV) {
        return;
    }
    DWORD folderFlags = FWF_SHOWSELALWAYS;
    IFolderView2* pFV2;
    if (SUCCEEDED(m_pSV->QueryInterface(IID_PPV_ARGS(&pFV2)))) {
        DWORD dwMask;
        pFV2->GetCurrentFolderFlags(&dwMask);
        dwMask = (dwMask ^ folderFlags) & (~(FWF_NOENUMREFRESH | FWF_USESEARCHFOLDER | FWF_SNAPTOGRID));
        if (dwMask) {
            pFV2->SetCurrentFolderFlags(dwMask, folderFlags);
        }
        SafeRelease(&pFV2);
    }
}

STDMETHODIMP CBrowserSink::GetTypeInfoCount(UINT* pctinfo)
{
    *pctinfo = 0;
    return S_OK;
}

STDMETHODIMP CBrowserSink::GetTypeInfo(UINT iTInfo, LCID lcid, ITypeInfo** ppTInfo)
{
    return E_NOTIMPL;
}

STDMETHODIMP CBrowserSink::GetIDsOfNames(REFIID riid, LPOLESTR* rgszNames, UINT cNames, LCID lcid, DISPID* rgDispId)
{
    return E_NOTIMPL;
}

STDMETHODIMP CBrowserSink::Invoke(DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS* pDispParams, VARIANT* pVarResult, EXCEPINFO* pExcepInfo, UINT* puArgErr)
{
    switch (dispIdMember) {
        case DISPID_SORTDONE://XP-
            return S_OK;
        case DISPID_FILELISTENUMDONE://XP+
            return S_OK;
    }

	return DISP_E_MEMBERNOTFOUND;
}

VOID CBrowserSink::GetShellFolderView()
{
    teUnadviseAndRelease(m_pdisp, DIID_DShellFolderViewEvents, &m_dwCookie);
    if (m_pSV && SUCCEEDED(m_pSV->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&m_pdisp)))) {
        teAdvise(m_pdisp, DIID_DShellFolderViewEvents, static_cast<IDispatch*>(this), &m_dwCookie);
    } else {
        m_pdisp = NULL;
    }
}

#endif