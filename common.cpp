#include <windows.h>
#include "common.h"
#include "api.h"

#if defined(_WINDLL) || defined(_DEBUG)

int		g_nException = 256;
LPCWSTR g_strException = nullptr;
HBRUSH	g_hbrDarkBackground = nullptr;
OPENFILENAME* g_pofn = nullptr;
std::vector<HMODULE>	g_phModule;
BOOL g_bDarkMode = FALSE;
DWORD	g_dwMainThreadId;

extern std::unordered_map<DWORD, HHOOK> g_umCBTHook;
extern JSClassID g_image_class_id;
extern IWICImagingFactory* g_pWICFactory;

LONGLONG teGetStreamPos(IStream* pStream)
{
    ULARGE_INTEGER uliPos;
    LARGE_INTEGER liOffset;
    liOffset.QuadPart = 0;
    pStream->Seek(liOffset, STREAM_SEEK_CUR, &uliPos);
    return uliPos.QuadPart;
}

VOID teCopyStream(IStream* pSrc, IStream* pDst)
{
    LARGE_INTEGER liOffset;
    liOffset.QuadPart = 0;
    LARGE_INTEGER liSrc, liDst;
    liSrc.QuadPart = teGetStreamPos(pSrc);
    liDst.QuadPart = teGetStreamPos(pDst);
    pSrc->Seek(liOffset, STREAM_SEEK_SET, NULL);
    pDst->Seek(liOffset, STREAM_SEEK_SET, NULL);
    ULONG cbRead;
    BYTE pszData[SIZE_BUFF];
    while (SUCCEEDED(pSrc->Read(pszData, SIZE_BUFF, &cbRead)) && cbRead) {
        pDst->Write(pszData, cbRead, NULL);
    }
    pSrc->Seek(liSrc, STREAM_SEEK_SET, NULL);
    pDst->Seek(liDst, STREAM_SEEK_SET, NULL);
}

static void TrimString(std::wstring& s)
{
    size_t start = 0;

    while (start < s.size() && iswspace(s[start])) {
        start++;
    }
    size_t end = s.size();

    while (end > start && iswspace(s[end - 1])) {
        end--;
    }
    s = s.substr(start, end - start);
}

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

    JSContext* ctx = el->ctx;

    if (JS_IsUndefined(e)) {
        e = JS_NewObject(ctx);
    }

    if (JS_IsObject(e)) {
        JS_SetPropertyStr(ctx, e, "target", JS_DupValue(ctx, el->jsThis));
        JS_SetPropertyStr(ctx, e,  "type", JS_NewString(ctx, name));
    }

    JSValue listeners = JS_GetPropertyStr(ctx, el->jsThis, "listeners");

    if (!JS_IsObject(listeners)) {
        JS_FreeValue(ctx, listeners);
        JS_FreeValue(ctx, e);
        return FALSE;
    }

    JSValue handlers = JS_GetPropertyStr(ctx, listeners, name);
    JS_FreeValue(ctx, listeners);

    // click: function() {}
    if (JS_IsFunction(ctx, handlers)) {
        JSValue result = JS_Call(ctx, handlers, el->jsThis, 1, &e);
        JS_FreeValue(ctx, handlers);

        if (!JS_IsUndefined(result)) {
            BOOL b = !JS_ToBool(ctx, result);
            JS_FreeValue(ctx, result);
            JS_FreeValue(ctx, e);
            return b;
        }
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, e);
        return FALSE;
    }

    // click: [fn1, fn2, ...]
    if (JS_IsArray(handlers)) {
        uint32_t len = JS_GetArrayLength( ctx, handlers);
        for (uint32_t i = 0; i < len; i++) {
            JSValue fn = JS_GetPropertyUint32(ctx, handlers, i);
            if (JS_IsFunction(ctx, fn)) {
                JSValue result = JS_Call(ctx, fn, el->jsThis, 1, &e);
                JS_FreeValue(ctx, fn);

                if (!JS_IsUndefined(result)) {
                    BOOL b =!JS_ToBool(ctx, result);
                    JS_FreeValue(ctx, result);
                    JS_FreeValue(ctx, handlers);
                    JS_FreeValue(ctx, e);
                    return b;
                }
                JS_FreeValue(ctx, result);
            } else {
                JS_FreeValue(ctx, fn);
            }
        }
    }
    JS_FreeValue(ctx, handlers);
    JS_FreeValue(ctx, e);
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

BOOL teStartsText(LPCWSTR pszSub, LPCWSTR pszFile)
{
    BOOL bResult = pszFile ? TRUE : FALSE;
    WCHAR wc;
    while (bResult && (wc = *pszSub++)) {
        bResult = towlower(wc) == towlower(*pszFile++);
    }
    return bResult;
}

BOOL tePathMatchSpec1(LPCWSTR pszFile, LPCWSTR pszSpec, WCHAR wSpecEnd)
{
    WCHAR wc = *pszSpec;
    if (wc == wSpecEnd) {
        return !*pszFile;
    }
    if (!*pszFile && wc != '*') {
        return FALSE;
    }
    for (; *pszFile; ++pszFile) {
        wc = *pszSpec++;
        if (wc == '*') {
            wc = towlower(*pszSpec++);
            if (wc == wSpecEnd) {
                return TRUE;
            }
            do {
                if (!*pszFile) {
                    return FALSE;
                }
                if (wc != '*' && wc != '?') {
                    while (towlower(*pszFile) != wc) {
                        if (!*(++pszFile)) {
                            return FALSE;
                        }
                    }
                }
            } while (!tePathMatchSpec1(++pszFile, pszSpec, wSpecEnd));
            return TRUE;
        }
        if (wc != '?') {
            if (wc == wSpecEnd || towlower(*pszFile) != towlower(wc)) {
                return FALSE;
            }
        }
    }
    for (; (wc = *pszSpec) == '*'; pszSpec++);
    return *pszFile == (wc == wSpecEnd ? NULL : wc);
}

BOOL tePathMatchSpec(LPCWSTR pszFile, LPCWSTR pszSpec)
{
    LPWSTR pszSpecEnd;
    if (!pszSpec || !pszSpec[0]) {
        return TRUE;
    }
    if (!pszFile) {
        return FALSE;
    }
    do {
        pszSpecEnd = StrChr(pszSpec, ';');
#ifdef USE_TESTPATHMATCHSPEC
        BOOL b1 = !!tePathMatchSpec1(pszFile, pszSpec, pszSpecEnd ? ';' : NULL);
        BOOL b2 = !!tePathMatchSpec2(pszFile, pszSpec);
        if (b1 != b2) {
            b2 = !!tePathMatchSpec1(pszFile, pszSpec, pszSpecEnd ? ';' : NULL);
        }
#endif
        if (tePathMatchSpec1(pszFile, pszSpec, pszSpecEnd ? ';' : NULL)) {
            return TRUE;
        }
        pszSpec = pszSpecEnd + 1;
    } while (pszSpecEnd);
    return FALSE;
}

BOOL teIsFileSystem(LPOLESTR pszPath)
{
    return tePathMatchSpec(pszPath, L"?:\\*;\\\\*\\*") && !teStartsText(L"\\\\\\", pszPath);
}

BOOL teIsSearchFolder(LPCWSTR lpszPath)
{
    return teStartsText(L"search-ms:", lpszPath);
}

void UnquotePath(std::wstring& path)
{
	{
		// Remove leading/trailing spaces
		TrimString(path);

		// Remove surrounding quotes
		if (path.size() >= 2 &&
			path.front() == L'"' &&
			path.back() == L'"')
		{
			path =
				path.substr(1, path.size() - 2);

			// Trim again after unquoting
			TrimString(path);
		}
	}
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

// CImage

UINT CImage::GetWidth() const
{
    UINT cx = 0;

    if (m_pBitmap) {
        m_pBitmap->GetSize(&cx, nullptr);
    }

    return cx;
}

UINT CImage::GetHeight() const
{
    UINT cy = 0;

    if (m_pBitmap) {
        m_pBitmap->GetSize(nullptr, &cy);
    }

    return cy;
}

HRESULT CImage::LoadFromStream(IStream* pStream, UINT uFrame, BOOL bKeepStream) {
    frame = uFrame;

    LARGE_INTEGER li = {};
    pStream->Seek(li, SEEK_SET, nullptr);

    ComPtr<IWICBitmapDecoder> decoder;

    HRESULT hr = g_pWICFactory->CreateDecoderFromStream(pStream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) {
        return hr;
    }

    decoder->GetFrameCount(&frameCount);

    if (frameCount == 0) {
        return E_FAIL;
    }

    ComPtr<IWICBitmapFrameDecode> frameDecode;

    hr = decoder->GetFrame(uFrame, &frameDecode);
    if (FAILED(hr)) {
        return hr;
    }

    m_pBitmap.Reset();

    hr = g_pWICFactory->CreateBitmapFromSource(frameDecode.Get(), WICBitmapCacheOnDemand, &m_pBitmap);
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<IWICMetadataQueryReader> metadata;

    hr = frameDecode->GetMetadataQueryReader(&metadata);
    if (SUCCEEDED(hr) && metadata) {
        PROPVARIANT propVar;

        PropVariantInit(&propVar);

        if (SUCCEEDED(metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}", &propVar))) {
            VARIANT v;

            VariantInit(&v);
/*          I'll do it later
            if (SUCCEEDED(PropVariantToVariant(&propVar, &v))) {
                int i = GetIntFromVariantClear(&v);

                if (i > 1 && i < 9) {
                    static const int r[] = { 0, 0, 8, 2, 10, 11, 1, 9, 3 };

                    //RotateFlip(r[i], FALSE);
                }
            }
            */
            PropVariantClear(&propVar);
        }
    }

    if (bKeepStream || frameCount > 1 || g_dwMainThreadId != GetCurrentThreadId()) {
        m_pStream.Attach(SHCreateMemStream(nullptr, 0));

        if (m_pStream) {
            teCopyStream(pStream, m_pStream.Get());
            decoder->GetContainerFormat(&sourceFormat);
        }
    }

    return S_OK;
}

HRESULT CImage::LoadFromFile(LPCWSTR pszPath) {
    ComPtr<IStream> stm;

    HRESULT hr = SHCreateStreamOnFileEx(
        pszPath,
        STGM_READ | STGM_SHARE_DENY_NONE,
        FILE_ATTRIBUTE_NORMAL,
        FALSE,
        nullptr,
        &stm);

    if (FAILED(hr)) {
        return hr;
    }

    return LoadFromStream(stm.Get(), 0, FALSE);
}

static JSValue CreateImageObject(
    JSContext* ctx,
    CImage* pImage)
{
    JSValue obj =
        JS_NewObjectClass(
            ctx,
            g_image_class_id);

    JS_SetOpaque(
        obj,
        pImage);

    return obj;
}

// Not static: declared in api.h so it can be called from api.cpp
JSValue Image_fromFile(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    WStrNullable path;

    JS_ToWStrNullable(
        ctx,
        argv[0],
        path);

    if (!path.ptr) {
        return JS_EXCEPTION;
    }

    auto* img = new CImage();

    HRESULT hr = img->LoadFromFile(path.ptr);

    if (FAILED(hr)) {
        delete img;
        return JS_NULL;
    }

    return CreateImageObject(
        ctx,
        img);
}

#endif