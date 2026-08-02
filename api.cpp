#include <windows.h>
#include <windowsx.h>
#include <vector>

#include "common.h"
#if defined(_WINDLL) || defined(_DEBUG)
#include "api.h"

JSValue g_pMBText;
JSClassID g_class_id = 0;
JSClassID g_cfolderitem_class_id = 0;
JSClassID g_image_class_id = 0;
JSClassID g_imagelist_class_id = 0;
static std::unordered_map<std::wstring, UIElement*> g_idMap;
static HWND g_hwndActiveMouse = nullptr;
static POINT g_ptMouseDown = {};
static HWND g_hwndHover = nullptr;

// Custom panel window class name (used instead of "STATIC" to support JS paint handlers)
static const wchar_t* PANEL_CLASS_NAME = L"TablacusCorePanel";

// ImageList wrapper (defined early so TOOLBAR creation code can use it)
struct CImageList {
    HIMAGELIST hIL    = nullptr;
    bool       owned  = true;   // false for system image lists (must not be destroyed)
    ~CImageList() {
        if (owned && hIL) { ImageList_Destroy(hIL); hIL = nullptr; }
    }
};
static BOOL g_bInputChanged = FALSE;
IQueryParser* g_pqp = NULL;

UIElement* get_element(JSValueConst val) {
    return (UIElement*)JS_GetOpaque(val, g_class_id);
}

static void GetSearchArg(
    std::wstring& out,
    const std::wstring& path,
    LPCWSTR pszArg) {
    out.clear();

    if (pszArg == nullptr) {
        return;
    }

    // Find argument position
    size_t pos =
        path.find(pszArg);

    if (pos == std::wstring::npos) {
        return;
    }

    // Extract value part
    out = path.substr(pos + lstrlenW(pszArg));

    // Stop at next '&'
    size_t amp =
        out.find(L'&');

    if (amp != std::wstring::npos) {
        out = out.substr(0, amp);
    }

    // URL decode in-place
    DWORD len = (DWORD)out.size();

    UrlUnescapeW(out.data(), nullptr, &len, URL_UNESCAPE_INPLACE);

    // Empty -> "*"
    if (out.empty()) {
        out = L"*";
    }
}

static IShellItem* JS_ToShellItem(
    JSContext* ctx,
    JSValueConst val)
{
    {
        CFolderItem* fi =
            (CFolderItem*)JS_GetOpaque(
                val,
                g_cfolderitem_class_id);

        if (fi != nullptr && fi->pItem != nullptr) {
            fi->pItem->AddRef();
            return fi->pItem;
        }

        if (JS_IsString(val)) {
            std::wstring path = JS_ToWideString(ctx, val);
            UnquotePath(path);
            if (path.empty()) {
                return nullptr;
            }
            IShellItem* pItem = nullptr;
            if (teIsSearchFolder(path.c_str())) {
                std::wstring path3;
                GetSearchArg(path3, path, L"&crumb=location:");
                ISearchFolderItemFactory* psfif = NULL;
                if SUCCEEDED(CoCreateInstance(CLSID_SearchFolderItemFactory, NULL, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&psfif))) {
                    IShellItem* psi = NULL;
                    if SUCCEEDED(SHCreateItemFromParsingName(path3.c_str(), nullptr, IID_PPV_ARGS(&psi))) {
                        IShellItemArray* psia;
                        if SUCCEEDED(SHCreateShellItemArrayFromShellItem(psi, IID_PPV_ARGS(&psia))) {
                            psfif->SetScope(psia);
                            psia->Release();
                            GetSearchArg(path3, path, L"crumb=");

                            if (!g_pqp) {
                                IQueryParserManager* pqpm = NULL;
                                if SUCCEEDED(CoCreateInstance(__uuidof(QueryParserManager), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pqpm))) {
                                    if SUCCEEDED(pqpm->CreateLoadedParser(L"SystemIndex", MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), IID_PPV_ARGS(&g_pqp))) {
                                        BOOL fUnderstandNQS = FALSE;
                                        BOOL fAutoWildCard = TRUE;
                                        HKEY hKey;
                                        if (RegOpenKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Search\\Preferences", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                                            DWORD dwSize = sizeof(BOOL);
                                            RegQueryValueExA(hKey, "EnableNaturalQuerySyntax", NULL, NULL, (LPBYTE)&fUnderstandNQS, &dwSize);
                                            RegQueryValueExA(hKey, "AutoWildCard", NULL, NULL, (LPBYTE)&fAutoWildCard, &dwSize);
                                            RegCloseKey(hKey);
                                        }
                                        pqpm->InitializeOptions(fUnderstandNQS, fAutoWildCard, g_pqp);
                                        pqpm->SetOption(QPMO_PRELOCALIZED_SCHEMA_BINARY_PATH, FALSE);
                                        for (int i = 0; i < ARRAYSIZE(g_rgGenericProperties); ++i) {
                                            PROPVARIANT propvar;
                                            propvar.pwszVal = const_cast<LPWSTR>(g_rgGenericProperties[i].pszPropertyName);
                                            propvar.vt = VT_LPWSTR;
                                            g_pqp->SetMultiOption(SQMO_DEFAULT_PROPERTY, g_rgGenericProperties[i].pszSemanticType, &propvar);
                                        }
                                    }
                                    pqpm->Release();
                                }
                            }
                            if (g_pqp) {
                                IQuerySolution* pqs = NULL;
                                ICondition* pc, * pc1;
                                if (SUCCEEDED(g_pqp->Parse(path3.c_str(), NULL, &pqs)) && SUCCEEDED(pqs->GetQuery(&pc, NULL))) {
                                    SYSTEMTIME st;
                                    GetLocalTime(&st);
                                    IConditionFactory2* pcf2;
                                    if SUCCEEDED(pqs->QueryInterface(IID_PPV_ARGS(&pcf2))) {
                                        ICondition2* pc2;
                                        if SUCCEEDED(pcf2->ResolveCondition(pc, SQRO_DEFAULT, &st, IID_PPV_ARGS(&pc2))) {
                                            psfif->SetCondition(pc2);
                                            pc2->Release();
                                        }
                                        pcf2->Release();
                                    }
                                    else if SUCCEEDED(pqs->Resolve(pc, SQRO_DONT_SPLIT_WORDS, &st, &pc1)) {
                                        psfif->SetCondition(pc1);
                                        pc1->Release();
                                    }
                                    else {
                                        psfif->SetCondition(pc);
                                    }
                                    pc->Release();
									LPITEMIDLIST pidl;
                                    if SUCCEEDED(psfif->GetIDList(&pidl)) {
										SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&pItem));
										CoTaskMemFree(pidl);
                                    }
                                }
                                SafeRelease(&pqs);
                            }
                            SafeRelease(&psi);
                        }
                    }
                    SafeRelease(&psfif);
                }
                return pItem;
            }

            HRESULT hr = SHCreateItemFromParsingName(
                path.c_str(),
                nullptr,
                IID_PPV_ARGS(&pItem));

            if (FAILED(hr)) {
                return nullptr;
            }

            return pItem;
        }

        return nullptr;
    }
}

static JSValue js_navigate(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    {
        UIElement* el = get_element(this_val);

        if (el == nullptr || el->pSink == nullptr || el->pSink->m_pEB == nullptr) {
            return JS_EXCEPTION;
        }

        if (argc < 1) {
            return JS_ThrowTypeError(
                ctx,
                "path or FolderItem expected");
        }

        IShellItem* pItem = JS_ToShellItem(ctx, argv[0]);

        if (pItem == nullptr) {
            return JS_ThrowTypeError(
                ctx,
                "invalid shell item");
        }

        PIDLIST_ABSOLUTE pidl = nullptr;

        HRESULT hr = SHGetIDListFromObject(
            pItem,
            &pidl);

        pItem->Release();

        if (FAILED(hr) || pidl == nullptr)
        {
            return JS_ThrowInternalError(
                ctx,
                "SHGetIDListFromObject failed");
        }

        hr = el->pSink->m_pEB->BrowseToIDList(
            pidl,
            SBSP_ABSOLUTE);

        CoTaskMemFree(pidl);

        if (FAILED(hr)) {
            return JS_ThrowInternalError(
                ctx,
                "BrowseToIDList failed");
        }

        return JS_UNDEFINED;
    }
}

void ui_element_finalizer(JSRuntime* rt, JSValueConst val)
{
}

IShellItem* GetCurrentFolder(IExplorerBrowser* pEB)
{
    if (pEB == nullptr) {
        return nullptr;
    }

    IShellView* pView = nullptr;

    HRESULT hr = pEB->GetCurrentView(
        IID_PPV_ARGS(&pView));

    if (FAILED(hr) || pView == nullptr) {
        return nullptr;
    }

    IFolderView* pFV = nullptr;

    hr = pView->QueryInterface(IID_PPV_ARGS(&pFV));

    pView->Release();

    if (FAILED(hr) || pFV == nullptr) {
        return nullptr;
    }

    IPersistFolder2* pPF2 = nullptr;

    hr = pFV->GetFolder(
        IID_PPV_ARGS(&pPF2));

    pFV->Release();

    if (FAILED(hr) || pPF2 == nullptr) {
        return nullptr;
    }

    PIDLIST_ABSOLUTE pidl = nullptr;

    hr = pPF2->GetCurFolder(&pidl);

    pPF2->Release();

    if (FAILED(hr) || pidl == nullptr) {
        return nullptr;
    }

    IShellItem* pItem = nullptr;

    hr = SHCreateItemFromIDList(
        pidl,
        IID_PPV_ARGS(&pItem));

    CoTaskMemFree(pidl);

    if (FAILED(hr)) {
        return nullptr;
    }

    return pItem;
}

std::wstring js_read_string(JSContext* ctx, JSValue& opts, LPCSTR name, LPCWSTR def)
{
    std::wstring str = def;
    JSValue v = JS_GetPropertyStr(ctx, opts, name);
    if (JS_IsString(v)) {
        WStrNullable w;
        JS_ToWStrNullable(ctx, v, w);
        if (w.ptr) {
            str = w.ptr;
        }
    }
    JS_FreeValue(ctx, v);
    return str;
}

int teGetModuleFileName(HMODULE hModule, std::wstring& out)
{
    int len = 0;

    for (int nSize = MAX_PATH; nSize < MAX_PATHEX; nSize += MAX_PATH) {
        out.resize(nSize);

        len = GetModuleFileNameW(hModule, &out[0], nSize);

        if (len == 0) {
            out.clear();
            return 0;
        }

        if (len + 1 < nSize) {
            out.resize(len);

            if (!out.empty()) {
                out[0] = towupper(out[0]);
            }
            return len;
        }
    }
    out.clear();
    return 0;
}

int GetConst(JSContext* ctx, JSValueConst api, const char* name, int def = 0)
{
    JSValue val = JS_GetPropertyStr(ctx, api, name);

    if (!JS_IsUndefined(val) && !JS_IsException(val)) {
        int32_t n;
        if (JS_ToInt32(ctx, &n, val) == 0) {
            JS_FreeValue(ctx, val);
            return n;
        }
    }

    JS_FreeValue(ctx, val);

    return atoi(name);
}

// =====  get int property =====
int JS_GetPropertyInt(JSContext* ctx, JSValue opts, const char* name, int def) {
    JSValue v = JS_GetPropertyStr(ctx, opts, name);
    int val = def;
    if (!JS_IsUndefined(v)) {
        JS_ToInt32(ctx, &val, v);
    }
    JS_FreeValue(ctx, v);
    return val;
};

// =====  get int64 property =====
int JS_ToInt64Ex(JSContext* ctx, int64_t* pres, JSValueConst val) {
    if (JS_IsBigInt(val)) {
        return JS_ToBigInt64(ctx, pres, val);
    }
    return JS_ToInt64(ctx, pres, val);
}

int64_t JS_GetPropertyInt64(JSContext* ctx, JSValue opts, const char* name, int64_t def) {
    JSValue v = JS_GetPropertyStr(ctx, opts, name);
    int64_t val = def;
    JS_ToInt64Ex(ctx, &val, v);
    JS_FreeValue(ctx, v);
    return val;
};

LRESULT CommonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_ERASEBKGND:
    {
        // Suppress background erasure if a "paint" handler is registered,
        // so JS has full control over drawing.
        UIElement* el = GetUIElement(hwnd);
        if (el) {
            JSValue listeners = JS_GetPropertyStr(el->ctx, el->jsThis, "listeners");
            if (JS_IsObject(listeners)) {
                JSValue handlers = JS_GetPropertyStr(el->ctx, listeners, "paint");
                JS_FreeValue(el->ctx, listeners);
                bool hasPaint = JS_IsFunction(el->ctx, handlers) || JS_IsArray(handlers);
                JS_FreeValue(el->ctx, handlers);
                if (hasPaint) {
                    return 1;
                }
            } else {
                JS_FreeValue(el->ctx, listeners);
            }
        }
        break;
    }
    case WM_PAINT:
    {
        UIElement* el = GetUIElement(hwnd);
        if (el) {
            char hwndMsg[64];
            snprintf(hwndMsg, sizeof(hwndMsg), "WM_PAINT hwnd=%p\n", hwnd);
            OutputDebugStringA(hwndMsg);

            JSValue listeners = JS_GetPropertyStr(el->ctx, el->jsThis, "listeners");
            if (JS_IsObject(listeners)) {
                JSPropertyEnum* tab = nullptr;
                uint32_t tabLen = 0;
                if (JS_GetOwnPropertyNames(el->ctx, &tab, &tabLen, listeners,
                    JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
                    for (uint32_t i = 0; i < tabLen; i++) {
                        const char* key = JS_AtomToCString(el->ctx, tab[i].atom);
                        char msg[256];
                        snprintf(msg, sizeof(msg), "  listeners key[%u]: %s\n", i, key);
                        OutputDebugStringA(msg);
                        JS_FreeCString(el->ctx, key);
                        JS_FreeAtom(el->ctx, tab[i].atom);
                    }
                    js_free(el->ctx, tab);
                }
                if (tabLen == 0) OutputDebugStringA("  listeners: no keys\n");

                JSValue handlers = JS_GetPropertyStr(el->ctx, listeners, "paint");
                JS_FreeValue(el->ctx, listeners);

                if (JS_IsUndefined(handlers))              OutputDebugStringA("  paint: undefined\n");
                else if (JS_IsFunction(el->ctx, handlers)) OutputDebugStringA("  paint: function\n");
                else if (JS_IsArray(handlers))             OutputDebugStringA("  paint: array\n");
                else                                       OutputDebugStringA("  paint: other\n");

                bool hasPaint = JS_IsFunction(el->ctx, handlers) || JS_IsArray(handlers);
                JS_FreeValue(el->ctx, handlers);

                if (hasPaint) {
                    JSValue e = JS_NewObject(el->ctx);
                    JS_SetPropertyStr(el->ctx, e, "hwnd",
                        JS_NewBigInt64(el->ctx, (int64_t)hwnd));
                    FireEvent(hwnd, "paint", e);
                    ValidateRect(hwnd, nullptr);
                    return 0;
                }
            } else {
                OutputDebugStringA("  listeners: not an object\n");
                JS_FreeValue(el->ctx, listeners);
            }
        } else {
            char hwndMsg[64];
            snprintf(hwndMsg, sizeof(hwndMsg), "WM_PAINT hwnd=%p: no UIElement\n", hwnd);
            OutputDebugStringA(hwndMsg);
        }
        break;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (FireKeyEvent(hwnd, "keydown", wParam)) {
            return 0;
        }
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (FireKeyEvent(hwnd, "keyup", wParam)) {
            return 0;
        }
        break;
    case WM_CHAR:
    case WM_SYSCHAR:
        if (wParam == VK_RETURN) {
            return 0; //Suppress Error Beeps
        }
        break;
    case WM_LBUTTONDOWN:
        g_hwndActiveMouse = hwnd;
        g_ptMouseDown.x = GET_X_LPARAM(lParam);
        g_ptMouseDown.y = GET_Y_LPARAM(lParam);
        SetCapture(hwnd);
        if (FireMouseEvent(hwnd, "mousedown", 0, wParam, lParam)) {
            return 0;
        }
        break;
    case WM_RBUTTONDOWN:
        if (FireMouseEvent(hwnd, "mousedown", 2, wParam, lParam)) {
            return 0;
        }
        break;
    case WM_MBUTTONDOWN:
        if (FireMouseEvent(hwnd, "mousedown", 1, wParam, lParam)) {
            return 0;
        }
        break;
    case WM_XBUTTONDOWN:
    {
        WORD x = GET_XBUTTON_WPARAM(wParam);
        if (FireMouseEvent(hwnd, "mousedown", (x == XBUTTON1) ? 3 : 4, wParam, lParam)) {
            return 0;
        }
        break;
    }
    case WM_LBUTTONUP:
        ReleaseCapture();
        if (FireMouseEvent(hwnd, "mouseup", 0, wParam, lParam)) {
            return 0;
        }
        if (g_hwndActiveMouse == hwnd) {
            g_hwndActiveMouse = nullptr;
            int dx = abs(GET_X_LPARAM(lParam) - g_ptMouseDown.x);
            int dy = abs(GET_Y_LPARAM(lParam) - g_ptMouseDown.y);
            if (dx <= GetSystemMetrics(SM_CXDRAG) && dy <= GetSystemMetrics(SM_CYDRAG)) {
                if (FireMouseEvent(hwnd, "click", 0, wParam, lParam)) {
                    return 0;
                }
            }
        }
        g_hwndActiveMouse = nullptr;
        break;
    case WM_RBUTTONUP:
        if (FireMouseEvent(hwnd, "mouseup", 2, wParam, lParam)) {
            return 0;
        }
        break;
    case WM_MBUTTONUP:
        if (FireMouseEvent(hwnd, "mouseup", 1, wParam, lParam)) {
            return 0;
        }
        break;
    case WM_XBUTTONUP:
    {
        WORD x = GET_XBUTTON_WPARAM(wParam);
        if (FireMouseEvent(hwnd, "mouseup", (x == XBUTTON1) ? 3 : 4, wParam, lParam)) {
            return 0;
        }
        break;
    }
    case WM_LBUTTONDBLCLK:
        if (FireMouseEvent(hwnd, "dblclick", 0, wParam, lParam)) {
            return 0;
        }
        break;
    case WM_MOUSEWHEEL:
        if (FireMouseEvent(hwnd, "wheel", -1, wParam, lParam)) {
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
    {
        if (FireMouseEvent(hwnd, "mousemove", 0, wParam, lParam)) {
            return 0;
        }
        if (g_hwndHover != hwnd) {
            g_hwndHover = hwnd;
            FireMouseEvent(hwnd, "mouseover", 0, wParam, lParam);
        }
        // request leave event
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        TrackMouseEvent(&tme);
        break;
    }
    case WM_MOUSELEAVE:
        if (g_hwndHover == hwnd) {
            g_hwndHover = nullptr;
        }
        if (FireEvent(hwnd, "mouseout", JS_UNDEFINED)) {
            return 0;
        }
        break;
    case WM_COMMAND:
    {
        int code = HIWORD(wParam);
        HWND hCtrl = (HWND)lParam;

        if (code == EN_CHANGE) {
            g_bInputChanged = TRUE;
            if (FireEvent(hCtrl, "input", JS_UNDEFINED)) {
                return 0;
            }
        }
        else if (code == EN_KILLFOCUS) {
            if (g_bInputChanged) {
                g_bInputChanged = FALSE;
                if (FireEvent(hCtrl, "change", JS_UNDEFINED)) {
                    return 0;
                }
            }
        }
        else if (code == 0) {
            // Toolbar button click: lParam == 0, button id in LOWORD(wParam)
            int buttonId = LOWORD(wParam);
            HWND hToolbar = FindWindowExW(hwnd, nullptr, TOOLBARCLASSNAME, nullptr);
            while (hToolbar) {
                UIElement* el = GetUIElement(hToolbar);
                if (el) {
                    JSContext* ctx = el->ctx;

                    // Check if this button has BTNS_DROPDOWN style.
                    // If so (and showArrows is false), fire "dropdown" instead of "click"
                    // so the full button acts like a menu bar item.
                    int btnIndex = (int)SendMessage(hToolbar,
                        TB_COMMANDTOINDEX, buttonId, 0);
                    TBBUTTON tbb{};
                    SendMessage(hToolbar, TB_GETBUTTON, btnIndex, (LPARAM)&tbb);

                    bool isDropdown = (tbb.fsStyle & BTNS_DROPDOWN) &&
                                     !(tbb.fsStyle & BTNS_WHOLEDROPDOWN);

                    if (isDropdown) {
                        // Get button rect in screen coords for menu placement
                        RECT rc{};
                        SendMessage(hToolbar, TB_GETRECT, buttonId, (LPARAM)&rc);
                        MapWindowPoints(hToolbar, HWND_DESKTOP, (POINT*)&rc, 2);

                        JSValue e = JS_NewObject(ctx);
                        JS_SetPropertyStr(ctx, e, "hwnd",
                            JS_NewBigInt64(ctx, (int64_t)hToolbar));
                        JS_SetPropertyStr(ctx, e, "buttonId",
                            JS_NewInt32(ctx, buttonId));
                        JS_SetPropertyStr(ctx, e, "x",
                            JS_NewInt32(ctx, rc.left));
                        JS_SetPropertyStr(ctx, e, "y",
                            JS_NewInt32(ctx, rc.bottom));
                        FireEvent(hToolbar, "dropdown", e);
                    } else {
                        JSValue e = JS_NewObject(ctx);
                        JS_SetPropertyStr(ctx, e, "hwnd",
                            JS_NewBigInt64(ctx, (int64_t)hToolbar));
                        JS_SetPropertyStr(ctx, e, "buttonId",
                            JS_NewInt32(ctx, buttonId));
                        FireEvent(hToolbar, "click", e);
                    }
                    return 0;
                }
                hToolbar = FindWindowExW(hwnd, hToolbar, TOOLBARCLASSNAME, nullptr);
            }
        }
        break;
    }
    case WM_NOTIFY:
    {
        NMHDR* pnm = (NMHDR*)lParam;
        if (pnm->code == TBN_DROPDOWN) {
            NMTOOLBARW* pnmtb = (NMTOOLBARW*)lParam;
            UIElement* el = GetUIElement(pnm->hwndFrom);
            if (el) {
                // Compute screen position of the button for menu placement
                RECT rc{};
                SendMessage(pnm->hwndFrom, TB_GETRECT,
                    pnmtb->iItem, (LPARAM)&rc);
                MapWindowPoints(pnm->hwndFrom, HWND_DESKTOP,
                    (POINT*)&rc, 2);

                JSContext* ctx = el->ctx;
                JSValue e = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, e, "hwnd",
                    JS_NewBigInt64(ctx, (int64_t)pnm->hwndFrom));
                JS_SetPropertyStr(ctx, e, "buttonId",
                    JS_NewInt32(ctx, pnmtb->iItem));
                JS_SetPropertyStr(ctx, e, "x",
                    JS_NewInt32(ctx, rc.left));
                JS_SetPropertyStr(ctx, e, "y",
                    JS_NewInt32(ctx, rc.bottom));
                FireEvent(pnm->hwndFrom, "dropdown", e);

                // Return TBDDRET_TREATPRESSED so the toolbar tracks hot state,
                // enabling seamless switching to adjacent dropdown buttons
                // (like a menu bar) without requiring a new click.
                return TBDDRET_TREATPRESSED;
            }
        }
        break;
    }
    case WM_SIZE:
    {
        UIElement* el = GetUIElement(hwnd);
        if (el && el->pSink && el->pSink->m_pEB) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            el->pSink->m_pEB->SetRect(nullptr, rc);
        }
        FireEvent(hwnd, "Resize", JS_UNDEFINED);
        break;
    }
    case WM_NCDESTROY:
    {
        //SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        UIElement* el = GetUIElement(hwnd);
        if (el) {
            RemoveProp(hwnd, L"UIElement");
            if (el->hwnd == hwnd) {
                if (el->pSink && el->pSink->m_pEB) {
                    //el->pSink->m_pEB->Destroy();
                    SafeRelease(&el->pSink->m_pEB);
                }

                // free JS object
                JS_FreeValue(el->ctx, el->jsThis);
                if (!el->id.empty()) {
                    g_idMap.erase(el->id);
                }
                delete el;
            }
        }
        break;
    }
    }
    return DarkProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK ControlProc(HWND hwnd, UINT msg,
    WPARAM wParam, LPARAM lParam,
    UINT_PTR, DWORD_PTR refData)
{
    LRESULT lResult = CommonProc(hwnd, msg, wParam, lParam);
    if (lResult != 1) {
        return lResult;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void CommonSettings(HWND hwnd, JSContext* ctx, JSValue& obj, JSValue& opts)
{
    // Store HWND in JS object
    JS_SetPropertyStr(ctx, obj, "hwnd",
        JS_NewBigInt64(ctx, (int64_t)hwnd));

    // ===== Allocate UIElement  =====
    UIElement* el = new UIElement();
    el->hwnd = hwnd;
    el->ctx = ctx;
    el->jsThis = JS_DupValue(ctx, obj);
    JS_SetOpaque(obj, el);
    // Associate HWND with UIElement
//    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)el);
    SetProp(hwnd, L"UIElement", el);
    // ID
	el->id = js_read_string(ctx, opts, "id", L"");
    if (!el->id.empty()) {
		g_idMap[el->id] = el;
    }

    JSValue listeners =
        JS_GetPropertyStr(ctx, opts, "listeners");

    if (!JS_IsObject(listeners)) {
        JS_FreeValue(ctx, listeners);
        listeners = JS_NewObject(ctx);
    }

    JS_SetPropertyStr(
        ctx,
        obj,
        "listeners",
        listeners);

    // Debug: log which keys were stored in listeners
    {
        JSPropertyEnum* tab = nullptr;
        uint32_t tabLen = 0;
        JSValue dbgListeners = JS_GetPropertyStr(ctx, obj, "listeners");
        if (JS_GetOwnPropertyNames(ctx, &tab, &tabLen, dbgListeners,
            JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            char header[64];
            snprintf(header, sizeof(header), "CommonSettings hwnd=%p listeners:\n", hwnd);
            OutputDebugStringA(header);
            for (uint32_t i = 0; i < tabLen; i++) {
                const char* key = JS_AtomToCString(ctx, tab[i].atom);
                char msg[256];
                snprintf(msg, sizeof(msg), "  [%u] %s\n", i, key);
                OutputDebugStringA(msg);
                JS_FreeCString(ctx, key);
                JS_FreeAtom(ctx, tab[i].atom);
            }
            if (tabLen == 0) OutputDebugStringA("  (empty)\n");
            js_free(ctx, tab);
        }
        JS_FreeValue(ctx, dbgListeners);
    }

    // ===== show(): display the window =====
    JS_SetPropertyStr(ctx, obj, "show",
        JS_NewCFunction(ctx,
            [](JSContext* ctx, JSValueConst this_val,
                int argc, JSValueConst* argv) -> JSValue {

        UIElement* el = get_element(this_val);
        if (!el) {
            return JS_EXCEPTION;
        }
        ShowWindow(el->hwnd, SW_SHOW);
        UpdateWindow(el->hwnd);
        return JS_UNDEFINED;
    },
            "show", 0)
    );

    JSAtom atom = JS_NewAtom(ctx, "text");

    JS_DefinePropertyGetSet(
        ctx,
        obj,
        atom,

        JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val,
            int argc, JSValueConst* argv) -> JSValue {

        UIElement* el = get_element(this_val);
        if (!el) return JS_EXCEPTION;

        int len = GetWindowTextLengthW(el->hwnd);
        std::wstring text(len + 1, L'\0');

        GetWindowTextW(el->hwnd, text.data(), len + 1);

        return JS_NewString(ctx, WideToUtf8(text.c_str()).c_str());
    }, "get text", 0),

        JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val,
            int argc, JSValueConst* argv) -> JSValue {

        UIElement* el = get_element(this_val);
        if (!el) return JS_EXCEPTION;

        if (argc > 0 && JS_IsString(argv[0])) {
            WStrNullable w;
            JS_ToWStrNullable(ctx, argv[0], w);

            if (w.ptr) {
                SetWindowTextW(el->hwnd, w.ptr);
            }
        }

        return JS_UNDEFINED;
    }, "set text", 1),

        JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE
    );

    JS_FreeAtom(ctx, atom);
}

JSValue js_getElementById(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv)
{
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_NULL;
    }
    std::wstring id = JS_ToWideString(ctx, argv[0]);
    auto it = g_idMap.find(id);

    if (it == g_idMap.end()) {
        return JS_NULL;
    }
    return JS_DupValue(ctx, it->second->jsThis);
}

JSValue js_get_current_folder(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    UIElement* el = get_element(this_val);

    if (el == nullptr || el->pSink == nullptr || el->pSink->m_pEB == nullptr) {
        return JS_EXCEPTION;
    }

    CFolderItem* fi = new CFolderItem();
    fi->pItem = GetCurrentFolder(el->pSink->m_pEB);
    if (fi->pItem == nullptr) {
        delete fi;
        return JS_NULL;
    }
    return NewFolderItem(ctx, fi);
}

JSValue js_createElement(JSContext* ctx, JSValueConst this_val,
    int argc, JSValueConst* argv)
{
    // Get parent element (window or container)
    UIElement* parent = get_element(this_val);
    if (!parent) {
        return JS_EXCEPTION;
    }
    // Get element type (e.g. "BUTTON")
    std::wstring type = JS_ToWideString(ctx, argv[0]);
    if (type.empty()) {
        return JS_EXCEPTION;
    }
    JSValue opts = argc >= 2 ? argv[1] : JS_UNDEFINED;

    HWND hwnd = nullptr;
    // ===== Default values =====
    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD exStyle = 0;
    int x = 0;
    int y = 0;
    int width = 80;
    int height = 20;
    style = JS_GetPropertyInt(ctx, opts, "style", style);
    exStyle = JS_GetPropertyInt(ctx, opts, "exStyle", exStyle);

    x = JS_GetPropertyInt(ctx, opts, "x", x);
    y = JS_GetPropertyInt(ctx, opts, "y", y);
    width = JS_GetPropertyInt(ctx, opts, "width", width);
    height = JS_GetPropertyInt(ctx, opts, "height", height);

    // Read string
    std::wstring text = js_read_string(ctx, opts, "text", L"");
    IExplorerBrowser* pEB = nullptr;

    // --- BUTTON creation ---
    if (lstrcmpi(type.c_str(), L"BUTTON") == 0)
    {
        hwnd = CreateWindowExW(
            0,
            L"BUTTON",
            text.c_str(),
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            x, y, width, height,      // default position/size
            parent->hwnd,
            nullptr,
            GetModuleHandle(nullptr),
            nullptr
        );
    } else if (lstrcmpi(type.c_str(), L"EDIT") == 0) {
        hwnd = CreateWindowExW(
            WS_EX_CLIENTEDGE,              // sunken border
            L"EDIT",
            text.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            x, y, width, height,      // default position/size
            parent->hwnd,
            nullptr,
            GetModuleHandle(nullptr),
            nullptr
        );
		// Placeholder text
        std::wstring placeholder = js_read_string(ctx, opts, "placeholder", L"");
        SendMessage(hwnd, EM_SETCUEBANNER, TRUE, (LPARAM)placeholder.c_str());
    }
    else if (lstrcmpi(type.c_str(), L"STATIC") == 0) {
        // Use a custom window class instead of "STATIC" so that WM_PAINT
        // is delivered to ControlProc and JS paint handlers work correctly.
        hwnd = CreateWindowExW(
            0,
            PANEL_CLASS_NAME,
            text.c_str(),
            WS_CHILD | WS_VISIBLE,
            x, y, width, height,
            parent->hwnd,
            nullptr,
            GetModuleHandle(nullptr),
            nullptr
        );
    }
    else if (lstrcmpi(type.c_str(), L"TOOLBAR") == 0) {
        // Create a Win32 standard toolbar control
        hwnd = CreateWindowExW(
            0,
            TOOLBARCLASSNAME,
            nullptr,
            WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NORESIZE,
            x, y, width, height,
            parent->hwnd,
            nullptr,
            GetModuleHandle(nullptr),
            nullptr
        );

        if (hwnd) {
            // Required: tell the toolbar the size of TBBUTTON
            SendMessage(hwnd, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);

            // Read button size from opts (default 16x16)
            int32_t btnW = (int32_t)JS_GetPropertyInt64(ctx, opts, "buttonWidth",  16);
            int32_t btnH = (int32_t)JS_GetPropertyInt64(ctx, opts, "buttonHeight", 16);
            SendMessage(hwnd, TB_SETBITMAPSIZE, 0, MAKELPARAM(btnW, btnH));
            SendMessage(hwnd, TB_SETBUTTONSIZE,  0, MAKELPARAM(btnW + 7, btnH + 7));

            // Set ImageList from "imageList" JS object if provided
            JSValue imageListJS = JS_GetPropertyStr(ctx, opts, "imageList");
            if (!JS_IsUndefined(imageListJS) && !JS_IsNull(imageListJS)) {
                CImageList* il = static_cast<CImageList*>(
                    JS_GetOpaque(imageListJS, g_imagelist_class_id));
                if (il && il->hIL) {
                    SendMessage(hwnd, TB_SETIMAGELIST, 0, (LPARAM)il->hIL);
                }
            }
            JS_FreeValue(ctx, imageListJS);

            // Build buttons from "buttons" array
            JSValue buttonsJS = JS_GetPropertyStr(ctx, opts, "buttons");
            if (JS_IsArray(buttonsJS)) {
                uint32_t len = 0;
                JSValue lenJS = JS_GetPropertyStr(ctx, buttonsJS, "length");
                JS_ToUint32(ctx, &len, lenJS);
                JS_FreeValue(ctx, lenJS);

                std::vector<TBBUTTON> tbb;

                for (uint32_t i = 0; i < len; i++) {
                    TBBUTTON btn{};
                    JSValue item = JS_GetPropertyUint32(ctx, buttonsJS, i);

                    // Separator?
                    JSValue sepJS = JS_GetPropertyStr(ctx, item, "separator");
                    if (JS_ToBool(ctx, sepJS)) {
                        btn.fsStyle = TBSTYLE_SEP;
                        tbb.push_back(btn);
                        JS_FreeValue(ctx, sepJS);
                        JS_FreeValue(ctx, item);
                        continue;
                    }
                    JS_FreeValue(ctx, sepJS);

                    // id
                    int32_t id = 0;
                    JSValue idJS = JS_GetPropertyStr(ctx, item, "id");
                    JS_ToInt32(ctx, &id, idJS);
                    JS_FreeValue(ctx, idJS);
                    btn.idCommand = id;

                    // image index
                    int32_t imgIdx = I_IMAGENONE;
                    JSValue imgJS = JS_GetPropertyStr(ctx, item, "image");
                    if (!JS_IsUndefined(imgJS)) {
                        JS_ToInt32(ctx, &imgIdx, imgJS);
                    }
                    JS_FreeValue(ctx, imgJS);
                    btn.iBitmap = imgIdx;

                    // text: register via TB_ADDSTRING and use returned index
                    btn.iString = I_IMAGENONE;
                    JSValue txtJS = JS_GetPropertyStr(ctx, item, "text");
                    if (!JS_IsUndefined(txtJS) && !JS_IsNull(txtJS)) {
                        const char* utf8 = JS_ToCString(ctx, txtJS);
                        if (utf8) {
                            std::wstring ws = Utf8ToWide(utf8);
                            JS_FreeCString(ctx, utf8);
                            // TB_ADDSTRING requires double-null terminated string
                            std::vector<wchar_t> buf(ws.size() + 2, 0);
                            wmemcpy(buf.data(), ws.c_str(), ws.size());
                            LRESULT idx = SendMessage(hwnd, TB_ADDSTRING,
                                0, (LPARAM)buf.data());
                            btn.iString = (INT_PTR)idx;
                        }
                    }
                    JS_FreeValue(ctx, txtJS);

                    btn.fsState = TBSTATE_ENABLED;

                    // Allow JS to override button style (e.g. BTNS_DROPDOWN)
                    int32_t btnStyle = BTNS_AUTOSIZE | BTNS_SHOWTEXT;
                    JSValue styleJS = JS_GetPropertyStr(ctx, item, "style");
                    if (!JS_IsUndefined(styleJS)) {
                        int32_t s = 0; JS_ToInt32(ctx, &s, styleJS);
                        btnStyle |= s;
                    }
                    JS_FreeValue(ctx, styleJS);
                    btn.fsStyle = (BYTE)btnStyle;

                    tbb.push_back(btn);
                    JS_FreeValue(ctx, item);
                }

                if (!tbb.empty()) {
                    SendMessage(hwnd, TB_ADDBUTTONS,
                        (WPARAM)tbb.size(), (LPARAM)tbb.data());
                }
            }
            JS_FreeValue(ctx, buttonsJS);

            // showArrows (default: true) controls whether dropdown arrow is drawn.
            // Set TBSTYLE_EX_DRAWDDARROWS only when arrows are wanted.
            JSValue showArrowsJS = JS_GetPropertyStr(ctx, opts, "showArrows");
            bool showArrows = JS_IsUndefined(showArrowsJS)
                ? true
                : (bool)JS_ToBool(ctx, showArrowsJS);
            JS_FreeValue(ctx, showArrowsJS);

            DWORD exStyle = showArrows ? TBSTYLE_EX_DRAWDDARROWS : 0;
            SendMessage(hwnd, TB_SETEXTENDEDSTYLE, 0, exStyle);

            // Auto-size the toolbar
            SendMessage(hwnd, TB_AUTOSIZE, 0, 0);
        }
    }
    else if (PathMatchSpec(type.c_str(), L"Explorer*")) {
        hwnd = CreateWindowExW(
            0,
            L"STATIC",
            text.c_str(),
            WS_CHILD | WS_VISIBLE,
            x, y, width, height,      // default position/size
            parent->hwnd,
            nullptr,
            GetModuleHandle(nullptr),
            nullptr
        );
        HRESULT hr = CoCreateInstance(
            CLSID_ExplorerBrowser,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&pEB)
        );
        SendMessage(hwnd, WM_SETREDRAW, FALSE, 0);
        RECT rc;
        GetClientRect(hwnd, &rc);
        pEB->Initialize(hwnd, &rc, nullptr);
        pEB->SetOptions(EBO_SHOWFRAMES | EBO_ALWAYSNAVIGATE);
        IFolderViewOptions* pOptions;
        if SUCCEEDED(pEB->QueryInterface(IID_PPV_ARGS(&pOptions))) {
            pOptions->SetFolderViewOptions(FVO_VISTALAYOUT, FVO_VISTALAYOUT);
            pOptions->Release();
        }
    } else {
        return JS_EXCEPTION;
    }
    FixChild(parent->hwnd, hwnd);
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessage(hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Create JS object
    JSValue obj = JS_NewObjectClass(ctx, g_class_id);
    CommonSettings(hwnd, ctx, obj, opts);
    // Subclass the control to intercept events
    SetWindowSubclass(hwnd, ControlProc, 0, 0);
    if (pEB) {
        UIElement* el = GetUIElement(hwnd);
        CBrowserSink* pSink = new CBrowserSink(hwnd);
        DWORD cookie = 0;
        pEB->Advise(pSink, &pSink->m_dwEventsCookie);
        pSink->m_pEB = pEB;
        el->pSink = pSink;

        LPITEMIDLIST pidl = nullptr;
        SHParseDisplayName(L"C:\\", nullptr, &pidl, 0, nullptr);
        pEB->BrowseToIDList(pidl, SBSP_ABSOLUTE);
        CoTaskMemFree(pidl);

        JSAtom atom = JS_NewAtom(ctx, "currentFolder");

        JS_DefinePropertyGetSet(
            ctx,
            obj,
            atom,
            JS_NewCFunction(ctx, js_get_current_folder, "get", 0),
            JS_UNDEFINED,
            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE
        );
        JS_FreeAtom(ctx, atom);

        // Method
        JS_SetPropertyStr(
            ctx,
            obj,
            "navigate",

            JS_NewCFunction(
                ctx,
                js_navigate,
                "navigate",
                1
            )
        );
    }
    return obj;
}

JSValue js_CreateWindow(JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    // Validate argument
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "object expected");
    }

    JSValue opts = argv[0];

    // ===== Default values =====
    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD exStyle = 0;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int width = 800;
    int height = 600;

    HWND parent = nullptr;
    HMENU menu = nullptr;
    HINSTANCE instance = GetModuleHandle(nullptr);

    // ===== Read options =====
    style = JS_GetPropertyInt(ctx, opts, "style", style);
    exStyle = JS_GetPropertyInt(ctx, opts, "exStyle", exStyle);

    x = JS_GetPropertyInt(ctx, opts, "x", x);
    y = JS_GetPropertyInt(ctx, opts, "y", y);
    width = JS_GetPropertyInt(ctx, opts, "width", width);
    height = JS_GetPropertyInt(ctx, opts, "height", height);

    parent = (HWND)JS_GetPropertyInt64(ctx, opts, "parent", 0);
    menu = (HMENU)JS_GetPropertyInt64(ctx, opts, "menu", 0);
    instance = (HINSTANCE)JS_GetPropertyInt64(ctx, opts, "instance", (int64_t)instance);

    // Read title string
    std::wstring text = js_read_string(ctx, opts, "text", L"Window");
    // Read class string
    std::wstring className = js_read_string(ctx, opts, "className", L"STATIC");

    // ===== Create native window =====
    HWND hwnd = CreateWindowExW(
        exStyle,
        className.c_str(),
        text.c_str(),
        style,
        x, y, width, height,
        parent,
        menu,
        instance,
        nullptr
    );

    if (!hwnd) {
        return JS_ThrowInternalError(ctx, "CreateWindow failed");
    }
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    SendMessage(hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);

    // Apply dark mode if supported
    teGetDarkMode();
    teSetDarkMode(hwnd);

    // ===== Create JS object =====
    JSValue obj = JS_NewObjectClass(ctx, g_class_id);

    CommonSettings(hwnd, ctx, obj, opts);

    JS_SetPropertyStr(
        ctx,
        obj,
        "createElement",
        JS_NewCFunction(ctx, js_createElement, "createElement", 2)
    );

    JS_SetPropertyStr(
        ctx,
        obj,
        "getElementById",
        JS_NewCFunction(ctx, js_getElementById, "getElementById", 1)
    );
    return obj;
}

JSValue js_GetModuleFileName(JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    int64_t hModule;
    JS_ToInt64Ex(ctx, &hModule, argv[0]);
    std::wstring fileName;
    teGetModuleFileName((HMODULE)hModule, fileName);
    return JS_NewString(ctx, WideToUtf8(fileName.c_str()).c_str());
}

JSValue js_GetModuleHandle(JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    WStrNullable moduleName;
    if (argc > 0) {
		JS_ToWStrNullable(ctx, argv[0], moduleName);
    }
	JSValue result = JS_NewBigInt64(ctx, (int64_t)GetModuleHandle(moduleName.ptr));
	return result;
}

JSValue js_GetCursorPos(JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    POINT pt;
    if (!GetCursorPos(&pt)) {
        return JS_ThrowInternalError(ctx, "GetCursorPos failed: %lu", GetLastError());
    }
    return JS_FromPoint(ctx, pt);
}

JSValue js_MessageBox(JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "MessageBox requires at least 3 arguments");
    }
    int64_t hwnd;
    JS_ToInt64Ex(ctx, &hwnd, argv[0]);
    WStrNullable text;
    JS_ToWStrNullable(ctx, argv[1], text);
    WStrNullable caption;
    JS_ToWStrNullable(ctx, argv[2], caption);
    int type = MB_OK;
    JS_FreeValue(ctx, g_pMBText);
    if (argc >= 3) {
        JS_ToInt32(ctx, &type, argv[3]);
        if (argc >= 4) {
            g_pMBText = JS_DupValue(ctx, argv[4]);
        }
    }
    int result = MessageBox(
        (HWND)hwnd,
        text.ptr,
        caption.ptr,
        type
    );
    return JS_NewInt32(ctx, result);
}

JSValue js_ShowWindow(JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    int64_t hwnd;
    JS_ToInt64Ex(ctx, &hwnd, argv[0]);
    int nCmdShow;
    JS_ToInt32(ctx, &nCmdShow, argv[1]);
    return JS_NewBool(ctx, ShowWindow((HWND)hwnd, nCmdShow));
}

JSValue js_UpdateWindow(JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    int64_t hwnd;
    JS_ToInt64Ex(ctx, &hwnd, argv[0]);
    return JS_NewBool(ctx, UpdateWindow((HWND)hwnd));
}

// Helper: convert a Win32 RECT to a JS object { left, top, right, bottom }
static JSValue RectToJS(JSContext* ctx, const RECT& rc)
{
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "left",   JS_NewInt32(ctx, rc.left));
    JS_SetPropertyStr(ctx, obj, "top",    JS_NewInt32(ctx, rc.top));
    JS_SetPropertyStr(ctx, obj, "right",  JS_NewInt32(ctx, rc.right));
    JS_SetPropertyStr(ctx, obj, "bottom", JS_NewInt32(ctx, rc.bottom));
    return obj;
}

// Helper: read a JS object { left, top, right, bottom } into a Win32 RECT
static void JSToRect(JSContext* ctx, JSValueConst obj, RECT& rc)
{
    JSValue v;
    int32_t n;
    v = JS_GetPropertyStr(ctx, obj, "left");   JS_ToInt32(ctx, &n, v); rc.left   = n; JS_FreeValue(ctx, v);
    v = JS_GetPropertyStr(ctx, obj, "top");    JS_ToInt32(ctx, &n, v); rc.top    = n; JS_FreeValue(ctx, v);
    v = JS_GetPropertyStr(ctx, obj, "right");  JS_ToInt32(ctx, &n, v); rc.right  = n; JS_FreeValue(ctx, v);
    v = JS_GetPropertyStr(ctx, obj, "bottom"); JS_ToInt32(ctx, &n, v); rc.bottom = n; JS_FreeValue(ctx, v);
}

// api.BeginPaint(hwnd, ps) -> HDC (as BigInt)
// Fills ps.rcPaint with the dirty rectangle.
JSValue js_BeginPaint(JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    int64_t hwnd;
    JS_ToInt64Ex(ctx, &hwnd, argv[0]);

    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint((HWND)hwnd, &ps);

    // Write rcPaint back into the JS ps object so JS can read ps.rcPaint
    JSValue rcPaint = RectToJS(ctx, ps.rcPaint);
    JS_SetPropertyStr(ctx, argv[1], "rcPaint", rcPaint);
    JS_SetPropertyStr(ctx, argv[1], "fErase",  JS_NewBool(ctx, ps.fErase));

    // Store the PAINTSTRUCT pointer as a BigInt so EndPaint can retrieve it
    // We store HDC as a BigInt handle returnable to JS
    return JS_NewBigInt64(ctx, (int64_t)hdc);
}

// api.EndPaint(hwnd, ps) -> void
// ps must be the same object passed to BeginPaint (rcPaint is re-read from it).
JSValue js_EndPaint(JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    int64_t hwnd;
    JS_ToInt64Ex(ctx, &hwnd, argv[0]);

    // Reconstruct PAINTSTRUCT from the JS ps object
    PAINTSTRUCT ps{};
    JSValue rcPaintJS = JS_GetPropertyStr(ctx, argv[1], "rcPaint");
    JSToRect(ctx, rcPaintJS, ps.rcPaint);
    JS_FreeValue(ctx, rcPaintJS);

    JSValue fEraseJS = JS_GetPropertyStr(ctx, argv[1], "fErase");
    int fErase = 0;
    JS_ToInt32(ctx, &fErase, fEraseJS);
    JS_FreeValue(ctx, fEraseJS);
    ps.fErase = (BOOL)fErase;

    EndPaint((HWND)hwnd, &ps);
    return JS_UNDEFINED;
}

// api.DrawText({ hdc, text, rc: { left, top, right, bottom }, format }) -> int
JSValue js_DrawText(JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    JSValue opts = argv[0];

    // Read HDC
    int64_t hdc = 0;
    JSValue hdcJS = JS_GetPropertyStr(ctx, opts, "hdc");
    JS_ToInt64Ex(ctx, &hdc, hdcJS);
    JS_FreeValue(ctx, hdcJS);

    // Read text (convert UTF-8 JS string to wide string)
    JSValue textJS = JS_GetPropertyStr(ctx, opts, "text");
    const char* textUtf8 = JS_ToCString(ctx, textJS);
    std::wstring text = textUtf8 ? Utf8ToWide(textUtf8) : std::wstring();
    JS_FreeCString(ctx, textUtf8);
    JS_FreeValue(ctx, textJS);

    // Read rc
    RECT rc{};
    JSValue rcJS = JS_GetPropertyStr(ctx, opts, "rc");
    JSToRect(ctx, rcJS, rc);
    JS_FreeValue(ctx, rcJS);

    // Read format flags (DT_LEFT | DT_TOP etc.)
    int format = DT_LEFT | DT_TOP;
    JSValue fmtJS = JS_GetPropertyStr(ctx, opts, "format");
    if (!JS_IsUndefined(fmtJS)) {
        JS_ToInt32(ctx, &format, fmtJS);
    }
    JS_FreeValue(ctx, fmtJS);

    int result = ::DrawTextW((HDC)hdc, text.c_str(), -1, &rc, format);
    return JS_NewInt32(ctx, result);
}

// ─── Menu API ────────────────────────────────────────────────────────────

// Helper: append one item to hMenu from a JS object
// { id, text, flags?, checked?, separator?, submenu? }
// Forward declaration: defined in the ImageList section below
static HBITMAP WICBitmapToHBITMAP(IWICBitmap* pBitmap);

static HBITMAP AppendMenuItemFromJSEx(JSContext* ctx, HMENU hMenu, JSValueConst item)
{
    JSValue sepJS = JS_GetPropertyStr(ctx, item, "separator");
    if (JS_ToBool(ctx, sepJS)) {
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        JS_FreeValue(ctx, sepJS);
        return nullptr;
    }
    JS_FreeValue(ctx, sepJS);

    // Base flags
    int32_t flags = MF_STRING;
    JSValue flagsJS = JS_GetPropertyStr(ctx, item, "flags");
    if (!JS_IsUndefined(flagsJS)) {
        int32_t f = 0;
        JS_ToInt32(ctx, &f, flagsJS);
        flags |= f;
    }
    JS_FreeValue(ctx, flagsJS);

    // checked?
    JSValue chkJS = JS_GetPropertyStr(ctx, item, "checked");
    if (JS_ToBool(ctx, chkJS)) flags |= MF_CHECKED;
    JS_FreeValue(ctx, chkJS);

    // text
    std::wstring text;
    JSValue txtJS = JS_GetPropertyStr(ctx, item, "text");
    if (!JS_IsUndefined(txtJS)) {
        const char* s = JS_ToCString(ctx, txtJS);
        if (s) { text = Utf8ToWide(s); JS_FreeCString(ctx, s); }
    }
    JS_FreeValue(ctx, txtJS);

    // id
    int32_t id = 0;
    JSValue idJS = JS_GetPropertyStr(ctx, item, "id");
    JS_ToInt32(ctx, &id, idJS);
    JS_FreeValue(ctx, idJS);

    // submenu?
    JSValue subJS = JS_GetPropertyStr(ctx, item, "submenu");
    if (!JS_IsUndefined(subJS) && !JS_IsNull(subJS)) {
        int64_t hSub = 0;
        JS_ToInt64Ex(ctx, &hSub, subJS);
        AppendMenuW(hMenu, flags | MF_POPUP, (UINT_PTR)hSub, text.c_str());
        JS_FreeValue(ctx, subJS);
        goto set_bitmap;
    }
    JS_FreeValue(ctx, subJS);

    AppendMenuW(hMenu, flags, (UINT_PTR)id, text.c_str());

set_bitmap:
    {
        int pos = GetMenuItemCount(hMenu) - 1;

        // Pattern 1: { icon: Image } -> WICBitmap -> HBITMAP via MIIM_BITMAP
        JSValue iconJS = JS_GetPropertyStr(ctx, item, "icon");
        if (!JS_IsUndefined(iconJS) && !JS_IsNull(iconJS)) {
            CImage* img = static_cast<CImage*>(
                JS_GetOpaque(iconJS, g_image_class_id));
            if (img && img->m_pBitmap) {
                HBITMAP hbm = WICBitmapToHBITMAP(img->m_pBitmap.Get());
                if (hbm) {
                    MENUITEMINFOW mii{};
                    mii.cbSize   = sizeof(mii);
                    mii.fMask    = MIIM_BITMAP;
                    mii.hbmpItem = hbm;
                    SetMenuItemInfoW(hMenu, (UINT)pos, TRUE, &mii);
                    JS_FreeValue(ctx, iconJS);
                    return hbm; // caller must track and free
                }
            }
            JS_FreeValue(ctx, iconJS);
            return nullptr;
        }
        JS_FreeValue(ctx, iconJS);

        // Pattern 2: { iconIndex: N, imageList: ilObj }
        // -> ImageList_GetIcon -> HICON via MIIM_ICON
        // HICON set via MIIM_ICON is owned by the menu item; DestroyMenu frees it.
        JSValue ilJS = JS_GetPropertyStr(ctx, item, "imageList");
        JSValue ixJS = JS_GetPropertyStr(ctx, item, "iconIndex");
        if (!JS_IsUndefined(ilJS) && !JS_IsNull(ilJS) &&
            !JS_IsUndefined(ixJS)) {
            CImageList* il = static_cast<CImageList*>(
                JS_GetOpaque(ilJS, g_imagelist_class_id));
            int32_t ix = 0;
            JS_ToInt32(ctx, &ix, ixJS);
            if (il && il->hIL && ix >= 0) {
                HICON hIcon = ImageList_GetIcon(il->hIL, ix, ILD_TRANSPARENT);
                if (hIcon) {
                    MENUITEMINFOW mii{};
                    mii.cbSize    = sizeof(mii);
                    mii.fMask     = MIIM_BITMAP;
                    // Convert HICON to HBITMAP (32bpp with alpha) for MIIM_BITMAP
                    // so the icon alpha channel is preserved on Vista+
                    HDC hdcScreen = GetDC(nullptr);
                    HDC hdcMem    = CreateCompatibleDC(hdcScreen);
                    BITMAPINFO bmi{};
                    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth       = 16;
                    bmi.bmiHeader.biHeight      = -16;
                    bmi.bmiHeader.biPlanes      = 1;
                    bmi.bmiHeader.biBitCount    = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;
                    void* pBits = nullptr;
                    HBITMAP hbm = CreateDIBSection(hdcScreen, &bmi,
                        DIB_RGB_COLORS, &pBits, nullptr, 0);
                    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hbm);
                    DrawIconEx(hdcMem, 0, 0, hIcon, 16, 16,
                        0, nullptr, DI_NORMAL);
                    SelectObject(hdcMem, hOld);
                    DeleteDC(hdcMem);
                    ReleaseDC(nullptr, hdcScreen);
                    DestroyIcon(hIcon);

                    mii.hbmpItem = hbm;
                    SetMenuItemInfoW(hMenu, (UINT)pos, TRUE, &mii);
                    JS_FreeValue(ctx, ilJS);
                    JS_FreeValue(ctx, ixJS);
                    return hbm; // caller must track and free
                }
            }
        }
        JS_FreeValue(ctx, ilJS);
        JS_FreeValue(ctx, ixJS);
    }
    return nullptr;
}

// Thin wrapper to discard the HBITMAP return when tracking is not needed
static void AppendMenuItemFromJS(JSContext* ctx, HMENU hMenu, JSValueConst item)
{
    AppendMenuItemFromJSEx(ctx, hMenu, item);
}

// Collect all HBITMAP handles set on a menu so we can free them on destroy.
// Stored as a JS array of BigInt on the menu object under "_bitmaps".
static void MenuTrackBitmap(JSContext* ctx, JSValueConst menuObj, HBITMAP hbm)
{
    JSValue arr = JS_GetPropertyStr(ctx, menuObj, "_bitmaps");
    if (!JS_IsArray(arr)) {
        JS_FreeValue(ctx, arr);
        arr = JS_NewArray(ctx);
        JS_SetPropertyStr(ctx, menuObj, "_bitmaps", JS_DupValue(ctx, arr));
    }
    JSValue lenJS = JS_GetPropertyStr(ctx, arr, "length");
    uint32_t len = 0; JS_ToUint32(ctx, &len, lenJS); JS_FreeValue(ctx, lenJS);
    JS_SetPropertyUint32(ctx, arr, len, JS_NewBigInt64(ctx, (int64_t)hbm));
    JS_FreeValue(ctx, arr);
}

// Build a JS menu object wrapping an HMENU
static JSValue CreateMenuObject(JSContext* ctx, HMENU hMenu)
{
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "handle",
        JS_NewBigInt64(ctx, (int64_t)hMenu));

    // menu.append(item) or menu.append([item, ...])
    JS_SetPropertyStr(ctx, obj, "append",
        JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val,
            int argc, JSValueConst* argv) -> JSValue
        {
            JSValue hJS = JS_GetPropertyStr(ctx, this_val, "handle");
            int64_t h = 0; JS_ToInt64Ex(ctx, &h, hJS); JS_FreeValue(ctx, hJS);
            HMENU hMenu = (HMENU)h;

            if (JS_IsArray(argv[0])) {
                uint32_t len = 0;
                JSValue lenJS = JS_GetPropertyStr(ctx, argv[0], "length");
                JS_ToUint32(ctx, &len, lenJS); JS_FreeValue(ctx, lenJS);
                for (uint32_t i = 0; i < len; i++) {
                    JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
                    HBITMAP hbm = AppendMenuItemFromJSEx(ctx, hMenu, item);
                    if (hbm) MenuTrackBitmap(ctx, this_val, hbm);
                    JS_FreeValue(ctx, item);
                }
            } else {
                HBITMAP hbm = AppendMenuItemFromJSEx(ctx, hMenu, argv[0]);
                if (hbm) MenuTrackBitmap(ctx, this_val, hbm);
            }
            return JS_UNDEFINED;
        }, "append", 1));

    // menu.track(hwnd, x, y, flags?) -> selected id (0 = cancelled)
    JS_SetPropertyStr(ctx, obj, "track",
        JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val,
            int argc, JSValueConst* argv) -> JSValue
        {
            JSValue hJS = JS_GetPropertyStr(ctx, this_val, "handle");
            int64_t h = 0; JS_ToInt64Ex(ctx, &h, hJS); JS_FreeValue(ctx, hJS);

            int64_t hwnd = 0; JS_ToInt64Ex(ctx, &hwnd, argv[0]);
            int32_t x = 0, y = 0;
            JS_ToInt32(ctx, &x, argv[1]);
            JS_ToInt32(ctx, &y, argv[2]);

            int32_t flags = TPM_LEFTALIGN | TPM_RETURNCMD;
            if (argc >= 4 && !JS_IsUndefined(argv[3])) {
                int32_t f = 0; JS_ToInt32(ctx, &f, argv[3]);
                flags = f | TPM_RETURNCMD;
            }

            int id = TrackPopupMenu((HMENU)h, flags,
                x, y, 0, (HWND)hwnd, nullptr);
            return JS_NewInt32(ctx, id);
        }, "track", 3));

    // menu.trackForToolbar(hToolbar, x, y, rcExclude) -> { id, switchTo }
    // Displays the menu with hot-tracking: when the mouse moves over another
    // dropdown button on the toolbar while the menu is open, the menu closes
    // and switchTo is set to that button's command id so JS can reopen.
    JS_SetPropertyStr(ctx, obj, "trackForToolbar",
        JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val,
            int argc, JSValueConst* argv) -> JSValue
        {
            JSValue hJS = JS_GetPropertyStr(ctx, this_val, "handle");
            int64_t h = 0; JS_ToInt64Ex(ctx, &h, hJS); JS_FreeValue(ctx, hJS);
            HMENU hMenu = (HMENU)h;

            int64_t hToolbar = 0; JS_ToInt64Ex(ctx, &hToolbar, argv[0]);
            int32_t x = 0, y = 0;
            JS_ToInt32(ctx, &x, argv[1]);
            JS_ToInt32(ctx, &y, argv[2]);

            // rcExclude: { left, top, right, bottom } — button rect in screen coords
            RECT rcExclude{};
            if (argc >= 4 && !JS_IsUndefined(argv[3])) {
                JSValue v;
                int32_t n;
                v = JS_GetPropertyStr(ctx, argv[3], "left");   JS_ToInt32(ctx, &n, v); rcExclude.left   = n; JS_FreeValue(ctx, v);
                v = JS_GetPropertyStr(ctx, argv[3], "top");    JS_ToInt32(ctx, &n, v); rcExclude.top    = n; JS_FreeValue(ctx, v);
                v = JS_GetPropertyStr(ctx, argv[3], "right");  JS_ToInt32(ctx, &n, v); rcExclude.right  = n; JS_FreeValue(ctx, v);
                v = JS_GetPropertyStr(ctx, argv[3], "bottom"); JS_ToInt32(ctx, &n, v); rcExclude.bottom = n; JS_FreeValue(ctx, v);
            }

            // State shared with the mouse hook
            struct HotState {
                HWND  hToolbar;
                int   switchTo;    // button id to switch to (-1 = none)
                int   currentId;   // button id currently showing menu
                int   nextX;       // screen x for next menu
                int   nextY;       // screen y (bottom of button) for next menu
                RECT  nextRcExclude; // exclude rect for next menu
            };
            static thread_local HotState g_hs{};
            g_hs = { (HWND)hToolbar, -1, 0, 0, 0, {} };

            // Read currentId from JS argv if provided (argv[4])
            if (argc >= 5 && !JS_IsUndefined(argv[4])) {
                int32_t cid = 0; JS_ToInt32(ctx, &cid, argv[4]);
                g_hs.currentId = cid;
            }

            // Press the current button to show it as active
            if (g_hs.currentId > 0)
                SendMessage((HWND)hToolbar, TB_PRESSBUTTON,
                    g_hs.currentId, MAKELONG(TRUE, 0));

            // WH_MOUSE hook: captures all mouse messages on this thread,
            // including movement over the toolbar while the menu is open.
            HHOOK hHook = SetWindowsHookExW(WH_MOUSE,
                [](int code, WPARAM wp, LPARAM lp) -> LRESULT
                {
                    if (code >= 0 && wp == WM_MOUSEMOVE) {
                        MOUSEHOOKSTRUCT* mhs = (MOUSEHOOKSTRUCT*)lp;
                        POINT pt = mhs->pt; // already screen coordinates

                        // Hit-test toolbar buttons
                        POINT tbPt = pt;
                        ScreenToClient(g_hs.hToolbar, &tbPt);
                        LRESULT idx = SendMessage(g_hs.hToolbar,
                            TB_HITTEST, 0, (LPARAM)&tbPt);

                        if (idx >= 0) {
                            TBBUTTON tbb{};
                            SendMessage(g_hs.hToolbar,
                                TB_GETBUTTON, idx, (LPARAM)&tbb);
                            // Switch only for other dropdown buttons
                            if ((tbb.fsStyle & BTNS_WHOLEDROPDOWN ||
                                 tbb.fsStyle & BTNS_DROPDOWN) &&
                                (tbb.fsState & TBSTATE_ENABLED) &&
                                tbb.idCommand != g_hs.currentId) {
                                g_hs.switchTo = tbb.idCommand;
                                // Get button rect in screen coords for next menu position
                                RECT btnRc{};
                                SendMessage(g_hs.hToolbar, TB_GETRECT,
                                    tbb.idCommand, (LPARAM)&btnRc);
                                MapWindowPoints(g_hs.hToolbar, HWND_DESKTOP,
                                    (POINT*)&btnRc, 2);
                                g_hs.nextX         = btnRc.left;
                                g_hs.nextY         = btnRc.bottom;
                                g_hs.nextRcExclude = btnRc;
                                // Release current button and press the next one
                                SendMessage(g_hs.hToolbar, TB_PRESSBUTTON,
                                    g_hs.currentId, MAKELONG(FALSE, 0));
                                SendMessage(g_hs.hToolbar, TB_PRESSBUTTON,
                                    tbb.idCommand, MAKELONG(TRUE, 0));

                                // Invalidate each button rect individually so
                                // NM_CUSTOMDRAW fires with the correct state.
                                // This prevents the previous button from staying
                                // in a hot/pressed state with light-mode drawing.
                                auto invalidateBtn = [&](int cmdId) {
                                    RECT btnRc{};
                                    SendMessage(g_hs.hToolbar, TB_GETRECT,
                                        cmdId, (LPARAM)&btnRc);
                                    InvalidateRect(g_hs.hToolbar, &btnRc, TRUE);
                                };
                                invalidateBtn(g_hs.currentId);
                                invalidateBtn(tbb.idCommand);
                                UpdateWindow(g_hs.hToolbar);
                                // Close the current menu
                                HWND hMenuWnd = FindWindowW(L"#32768", nullptr);
                                if (hMenuWnd)
                                    PostMessage(hMenuWnd, WM_KEYDOWN, VK_ESCAPE, 0);
                            }
                        }
                    }
                    return CallNextHookEx(nullptr, code, wp, lp);
                },
                nullptr, GetCurrentThreadId());

            TPMPARAMS tpmp{};
            tpmp.cbSize = sizeof(tpmp);
            tpmp.rcExclude = rcExclude;

            int id = TrackPopupMenuEx(hMenu,
                TPM_LEFTALIGN | TPM_RETURNCMD | TPM_LEFTBUTTON,
                x, y, (HWND)hToolbar, &tpmp);

            UnhookWindowsHookEx(hHook);

            // Release the pressed button state after menu closes
            SendMessage((HWND)hToolbar, TB_PRESSBUTTON,
                g_hs.currentId, MAKELONG(FALSE, 0));
            if (g_hs.switchTo > 0)
                SendMessage((HWND)hToolbar, TB_PRESSBUTTON,
                    g_hs.switchTo, MAKELONG(FALSE, 0));

            // Return { id, switchTo, x, y, rcExclude }
            JSValue result = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, result, "id",       JS_NewInt32(ctx, id));
            JS_SetPropertyStr(ctx, result, "switchTo", JS_NewInt32(ctx, g_hs.switchTo));
            JS_SetPropertyStr(ctx, result, "x",        JS_NewInt32(ctx, g_hs.nextX));
            JS_SetPropertyStr(ctx, result, "y",        JS_NewInt32(ctx, g_hs.nextY));
            JSValue rcJS = JS_NewObject(ctx);
            JS_SetPropertyStr(ctx, rcJS, "left",   JS_NewInt32(ctx, g_hs.nextRcExclude.left));
            JS_SetPropertyStr(ctx, rcJS, "top",    JS_NewInt32(ctx, g_hs.nextRcExclude.top));
            JS_SetPropertyStr(ctx, rcJS, "right",  JS_NewInt32(ctx, g_hs.nextRcExclude.right));
            JS_SetPropertyStr(ctx, rcJS, "bottom", JS_NewInt32(ctx, g_hs.nextRcExclude.bottom));
            JS_SetPropertyStr(ctx, result, "rcExclude", rcJS);
            return result;
        }, "trackForToolbar", 4));

    // menu.destroy()
    JS_SetPropertyStr(ctx, obj, "destroy",
        JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val,
            int argc, JSValueConst* argv) -> JSValue
        {
            JSValue hJS = JS_GetPropertyStr(ctx, this_val, "handle");
            int64_t h = 0; JS_ToInt64Ex(ctx, &h, hJS); JS_FreeValue(ctx, hJS);
            DestroyMenu((HMENU)h);

            // Free any bitmaps set via MIIM_BITMAP
            JSValue arr = JS_GetPropertyStr(ctx, this_val, "_bitmaps");
            if (JS_IsArray(arr)) {
                JSValue lenJS = JS_GetPropertyStr(ctx, arr, "length");
                uint32_t len = 0; JS_ToUint32(ctx, &len, lenJS); JS_FreeValue(ctx, lenJS);
                for (uint32_t i = 0; i < len; i++) {
                    JSValue v = JS_GetPropertyUint32(ctx, arr, i);
                    int64_t hbm = 0; JS_ToInt64Ex(ctx, &hbm, v); JS_FreeValue(ctx, v);
                    if (hbm) DeleteObject((HBITMAP)hbm);
                }
            }
            JS_FreeValue(ctx, arr);

            JS_SetPropertyStr(ctx, this_val, "handle", JS_NewInt32(ctx, 0));
            return JS_UNDEFINED;
        }, "destroy", 0));

    // menu.enableItem(id, enabled)
    JS_SetPropertyStr(ctx, obj, "enableItem",
        JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val,
            int argc, JSValueConst* argv) -> JSValue
        {
            JSValue hJS = JS_GetPropertyStr(ctx, this_val, "handle");
            int64_t h = 0; JS_ToInt64Ex(ctx, &h, hJS); JS_FreeValue(ctx, hJS);
            int32_t id = 0; JS_ToInt32(ctx, &id, argv[0]);
            bool enabled = JS_ToBool(ctx, argv[1]);
            EnableMenuItem((HMENU)h, (UINT)id,
                MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
            return JS_UNDEFINED;
        }, "enableItem", 2));

    // menu.checkItem(id, checked)
    JS_SetPropertyStr(ctx, obj, "checkItem",
        JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val,
            int argc, JSValueConst* argv) -> JSValue
        {
            JSValue hJS = JS_GetPropertyStr(ctx, this_val, "handle");
            int64_t h = 0; JS_ToInt64Ex(ctx, &h, hJS); JS_FreeValue(ctx, hJS);
            int32_t id = 0; JS_ToInt32(ctx, &id, argv[0]);
            bool checked = JS_ToBool(ctx, argv[1]);
            CheckMenuItem((HMENU)h, (UINT)id,
                MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
            return JS_UNDEFINED;
        }, "checkItem", 2));

    return obj;
}

// api.CreatePopupMenu() -> menu object
static JSValue js_CreatePopupMenu(JSContext* ctx,
    JSValueConst, int, JSValueConst*)
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return JS_NULL;
    return CreateMenuObject(ctx, hMenu);
}

// api.CreateMenu() -> menu object (for menu bar)
static JSValue js_CreateMenu(JSContext* ctx,
    JSValueConst, int, JSValueConst*)
{
    HMENU hMenu = CreateMenu();
    if (!hMenu) return JS_NULL;
    return CreateMenuObject(ctx, hMenu);
}

// api.SetMenu(hwnd, menu) -> set menu bar on a window
static JSValue js_SetMenu(JSContext* ctx,
    JSValueConst, int argc, JSValueConst* argv)
{
    int64_t hwnd = 0; JS_ToInt64Ex(ctx, &hwnd, argv[0]);
    int64_t hMenu = 0;
    JSValue hJS = JS_GetPropertyStr(ctx, argv[1], "handle");
    JS_ToInt64Ex(ctx, &hMenu, hJS); JS_FreeValue(ctx, hJS);
    return JS_NewBool(ctx, SetMenu((HWND)hwnd, (HMENU)hMenu));
}

// Forward declarations: defined after js_api_funcs
static JSValue js_ImageList_Create(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv);
static JSValue js_SHGetSystemImageList(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv);
static JSValue js_SHGetFileIconIndex(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv);

static const JSCFunctionListEntry js_api_funcs[] = {
    JS_CFUNC_DEF("CreateWindow", 1, js_CreateWindow),
    JS_PROP_INT32_DEF("WS_OVERLAPPEDWINDOW", WS_OVERLAPPEDWINDOW, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("CW_USEDEFAULT", CW_USEDEFAULT, JS_PROP_CONFIGURABLE),

    JS_CFUNC_DEF("GetCursorPos", 0, js_GetCursorPos),
    JS_CFUNC_DEF("GetModuleFileName", 0, js_GetModuleFileName),
    JS_CFUNC_DEF("GetModuleHandle", 0, js_GetModuleHandle),

    JS_CFUNC_DEF("MessageBox", 3, js_MessageBox),
    JS_PROP_INT32_DEF("MB_OK", MB_OK, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_OKCANCEL", MB_OKCANCEL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_ABORTRETRYIGNORE", MB_ABORTRETRYIGNORE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_YESNOCANCEL", MB_YESNOCANCEL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_YESNO", MB_YESNO, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_RETRYCANCEL", MB_RETRYCANCEL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_CANCELTRYCONTINUE", MB_CANCELTRYCONTINUE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_ICONSTOP", MB_ICONSTOP, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_ICONQUESTION", MB_ICONQUESTION, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_ICONEXCLAMATION", MB_ICONEXCLAMATION, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_ICONINFORMATION", MB_ICONINFORMATION, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_USERICON", MB_USERICON, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_DEFBUTTON1", MB_DEFBUTTON1, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_DEFBUTTON2", MB_DEFBUTTON2, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_DEFBUTTON3", MB_DEFBUTTON3, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_DEFBUTTON4", MB_DEFBUTTON4, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_APPLMODAL", MB_APPLMODAL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_SYSTEMMODAL", MB_SYSTEMMODAL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_TASKMODAL", MB_TASKMODAL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_NOFOCUS", MB_NOFOCUS, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_SETFOREGROUND", MB_SETFOREGROUND, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_DEFAULT_DESKTOP_ONLY", MB_DEFAULT_DESKTOP_ONLY, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_RIGHT", MB_RIGHT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MB_RTLREADING", MB_RTLREADING, JS_PROP_CONFIGURABLE),

    JS_PROP_INT32_DEF("IDOK", IDOK, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("IDCANCEL", IDCANCEL, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("IDABORT", IDABORT, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("IDRETRY", IDRETRY, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("IDIGNORE", IDIGNORE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("IDYES", IDYES, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("IDNO", IDNO, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("IDCLOSE", IDCLOSE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("IDHELP", IDHELP, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("IDTRYAGAIN", IDTRYAGAIN, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("IDCONTINUE", IDCONTINUE, JS_PROP_CONFIGURABLE),

    JS_CFUNC_DEF("ShowWindow",  2, js_ShowWindow),
    JS_CFUNC_DEF("UpdateWindow", 1, js_ShowWindow),

    // ImageList API
    JS_CFUNC_DEF("ImageList_Create",      2, js_ImageList_Create),
    JS_CFUNC_DEF("SHGetSystemImageList",  1, js_SHGetSystemImageList),
    JS_CFUNC_DEF("SHGetFileIconIndex",    1, js_SHGetFileIconIndex),

    // SHGFI flags (for SHGetFileIconIndex)
    JS_PROP_INT32_DEF("SHGFI_SYSICONINDEX", SHGFI_SYSICONINDEX, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SHGFI_SMALLICON",    SHGFI_SMALLICON,    JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SHGFI_LARGEICON",    SHGFI_LARGEICON,    JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SHGFI_OPENICON",     SHGFI_OPENICON,     JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SHGFI_OVERLAYINDEX", SHGFI_OVERLAYINDEX, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SHGFI_USEFILEATTRIBUTES", SHGFI_USEFILEATTRIBUTES, JS_PROP_CONFIGURABLE),

    // SHIL values (for SHGetSystemImageList size parameter reference)
    JS_PROP_INT32_DEF("SHIL_LARGE",      SHIL_LARGE,      JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SHIL_SMALL",      SHIL_SMALL,      JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SHIL_EXTRALARGE", SHIL_EXTRALARGE, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("SHIL_JUMBO",      SHIL_JUMBO,      JS_PROP_CONFIGURABLE),

    // Toolbar button style flags (for buttons[].style)
    JS_PROP_INT32_DEF("BTNS_BUTTON",     BTNS_BUTTON,     JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("BTNS_SEP",        BTNS_SEP,        JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("BTNS_CHECK",      BTNS_CHECK,      JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("BTNS_GROUP",      BTNS_GROUP,      JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("BTNS_DROPDOWN",   BTNS_DROPDOWN,   JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("BTNS_AUTOSIZE",   BTNS_AUTOSIZE,   JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("BTNS_NOPREFIX",   BTNS_NOPREFIX,   JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("BTNS_SHOWTEXT",   BTNS_SHOWTEXT,   JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("BTNS_WHOLEDROPDOWN", BTNS_WHOLEDROPDOWN, JS_PROP_CONFIGURABLE),

    // Menu API
    JS_CFUNC_DEF("CreatePopupMenu", 0, js_CreatePopupMenu),
    JS_CFUNC_DEF("CreateMenu",      0, js_CreateMenu),
    JS_CFUNC_DEF("SetMenu",         2, js_SetMenu),

    // Menu item flags
    JS_PROP_INT32_DEF("MF_STRING",    MF_STRING,    JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MF_GRAYED",    MF_GRAYED,    JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MF_DISABLED",  MF_DISABLED,  JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MF_CHECKED",   MF_CHECKED,   JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MF_POPUP",     MF_POPUP,     JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("MF_SEPARATOR", MF_SEPARATOR, JS_PROP_CONFIGURABLE),

    // TrackPopupMenu flags
    JS_PROP_INT32_DEF("TPM_LEFTALIGN",   TPM_LEFTALIGN,   JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TPM_CENTERALIGN", TPM_CENTERALIGN, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TPM_RIGHTALIGN",  TPM_RIGHTALIGN,  JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TPM_TOPALIGN",    TPM_TOPALIGN,    JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TPM_VCENTERALIGN",TPM_VCENTERALIGN,JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TPM_BOTTOMALIGN", TPM_BOTTOMALIGN, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TPM_RIGHTBUTTON", TPM_RIGHTBUTTON, JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("TPM_LEFTBUTTON",  TPM_LEFTBUTTON,  JS_PROP_CONFIGURABLE),

    // Paint API
    JS_CFUNC_DEF("BeginPaint", 2, js_BeginPaint),
    JS_CFUNC_DEF("EndPaint",   2, js_EndPaint),
    JS_CFUNC_DEF("DrawText",   1, js_DrawText),

    // DrawText format flags
    JS_PROP_INT32_DEF("DT_LEFT",        DT_LEFT,        JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DT_CENTER",      DT_CENTER,      JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DT_RIGHT",       DT_RIGHT,       JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DT_TOP",         DT_TOP,         JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DT_VCENTER",     DT_VCENTER,     JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DT_BOTTOM",      DT_BOTTOM,      JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DT_SINGLELINE",  DT_SINGLELINE,  JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DT_WORDBREAK",   DT_WORDBREAK,   JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DT_NOCLIP",      DT_NOCLIP,      JS_PROP_CONFIGURABLE),
    JS_PROP_INT32_DEF("DT_CALCRECT",    DT_CALCRECT,    JS_PROP_CONFIGURABLE),

};

// Wrapper to expose Image_fromFile (defined in common.cpp) as a JS function
static JSValue js_Image_fromFile(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    return Image_fromFile(ctx, this_val, argc, argv);
}

// Build and return a JS object representing the Image class
// with a static fromFile() method
static JSValue CreateImageClass(JSContext* ctx)
{
    JSValue imageClass = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, imageClass, "fromFile",
        JS_NewCFunction(ctx, js_Image_fromFile, "fromFile", 1));

    return imageClass;
}
       
// ─── ImageList JS class ───────────────────────────────────────────────────

static void imagelist_finalizer(JSRuntime* rt, JSValue val)
{
    CImageList* p = static_cast<CImageList*>(
        JS_GetOpaque(val, g_imagelist_class_id));
    delete p;
}

// Helper: convert CImage (WIC IWICBitmap) to a 32bpp HBITMAP with alpha
static HBITMAP WICBitmapToHBITMAP(IWICBitmap* pBitmap)
{
    if (!pBitmap) return nullptr;

    UINT w = 0, h = 0;
    pBitmap->GetSize(&w, &h);

    // Create a 32bpp DIB section
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = (LONG)w;
    bmi.bmiHeader.biHeight      = -(LONG)h; // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!hbm) return nullptr;

    // Copy pixels from WIC into the DIB (convert to PBGRA32 which matches HBITMAP 32bpp)
    ComPtr<IWICImagingFactory> pFactory;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory));

    ComPtr<IWICFormatConverter> pConverter;
    pFactory->CreateFormatConverter(&pConverter);
    pConverter->Initialize(pBitmap,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0,
        WICBitmapPaletteTypeMedianCut);

    UINT stride = w * 4;
    pConverter->CopyPixels(nullptr, stride, stride * h, (BYTE*)pBits);

    return hbm;
}

// imagelist.add(image, maskColor?)
// image     : JS Image object (CImage)
// maskColor : optional COLORREF for mask (e.g. 0x00FF00 for green); omit for alpha
static JSValue js_ImageList_add(JSContext* ctx,
    JSValueConst this_val, int argc, JSValueConst* argv)
{
    CImageList* il = static_cast<CImageList*>(
        JS_GetOpaque(this_val, g_imagelist_class_id));
    if (!il || !il->hIL) return JS_EXCEPTION;

    // First arg: Image object
    CImage* img = static_cast<CImage*>(
        JS_GetOpaque(argv[0], g_image_class_id));
    if (!img || !img->m_pBitmap) return JS_ThrowTypeError(ctx, "Expected Image object");

    HBITMAP hbm = WICBitmapToHBITMAP(img->m_pBitmap.Get());
    if (!hbm) return JS_ThrowInternalError(ctx, "Failed to convert image to HBITMAP");

    int index;
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        // Mask color specified: use ImageList_AddMasked
        int32_t maskColor = 0;
        JS_ToInt32(ctx, &maskColor, argv[1]);
        HBITMAP hMask = nullptr;
        index = ImageList_AddMasked(il->hIL, hbm, (COLORREF)maskColor);
    } else {
        // No mask: use 32bpp alpha channel (PNG transparency)
        index = ImageList_Add(il->hIL, hbm, nullptr);
    }

    DeleteObject(hbm);
    return JS_NewInt32(ctx, index);
}

// imagelist.handle -> BigInt (HIMAGELIST)
static JSValue js_ImageList_getHandle(JSContext* ctx,
    JSValueConst this_val, int argc, JSValueConst* argv)
{
    CImageList* il = static_cast<CImageList*>(
        JS_GetOpaque(this_val, g_imagelist_class_id));
    if (!il) return JS_NULL;
    return JS_NewBigInt64(ctx, (int64_t)il->hIL);
}

// Build a JS ImageList object wrapping a new HIMAGELIST
static JSValue CreateImageListObject(JSContext* ctx, HIMAGELIST hIL)
{
    if (!g_imagelist_class_id) {
        JS_NewClassID(JS_GetRuntime(ctx), &g_imagelist_class_id);
        JSClassDef def{};
        def.class_name = "ImageList";
        def.finalizer  = imagelist_finalizer;
        JS_NewClass(JS_GetRuntime(ctx), g_imagelist_class_id, &def);
    }

    JSValue obj = JS_NewObjectClass(ctx, g_imagelist_class_id);
    CImageList* p = new CImageList();
    p->hIL = hIL;
    JS_SetOpaque(obj, p);

    JS_SetPropertyStr(ctx, obj, "add",
        JS_NewCFunction(ctx, js_ImageList_add, "add", 1));
    JS_SetPropertyStr(ctx, obj, "handle",
        JS_NewCFunction(ctx, js_ImageList_getHandle, "handle", 0));

    return obj;
}

// api.ImageList_Create(cx, cy, flags?)
// flags default: ILC_COLOR32 | ILC_MASK
static JSValue js_ImageList_Create(JSContext* ctx,
    JSValueConst this_val, int argc, JSValueConst* argv)
{
    int32_t cx = 16, cy = 16;
    if (argc >= 1) JS_ToInt32(ctx, &cx, argv[0]);
    if (argc >= 2) JS_ToInt32(ctx, &cy, argv[1]);

    int32_t flags = ILC_COLOR32 | ILC_MASK;
    if (argc >= 3) JS_ToInt32(ctx, &flags, argv[2]);

    HIMAGELIST hIL = ImageList_Create(cx, cy, flags, 0, 4);
    if (!hIL) return JS_NULL;

    return CreateImageListObject(ctx, hIL);
}

// api.SHGetSystemImageList(size?) -> ImageList object (not owned; do not destroy)
// size: "small" (16x16, default) | "large" (32x32) | "extralarge" | "jumbo"
static JSValue js_SHGetSystemImageList(JSContext* ctx,
    JSValueConst, int argc, JSValueConst* argv)
{
    int type = SHIL_SMALL;
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        const char* s = JS_ToCString(ctx, argv[0]);
        if (s) {
            if      (strcmp(s, "large")      == 0) type = SHIL_LARGE;
            else if (strcmp(s, "extralarge") == 0) type = SHIL_EXTRALARGE;
            else if (strcmp(s, "jumbo")      == 0) type = SHIL_JUMBO;
            JS_FreeCString(ctx, s);
        }
    }

    HIMAGELIST hIL = nullptr;
    HRESULT hr = SHGetImageList(type, IID_IImageList, (void**)&hIL);
    if (FAILED(hr) || !hIL) return JS_NULL;

    // Wrap as non-owned so destroy() does not call ImageList_Destroy
    JSValue obj = CreateImageListObject(ctx, hIL);
    CImageList* p = static_cast<CImageList*>(
        JS_GetOpaque(obj, g_imagelist_class_id));
    if (p) p->owned = false;
    return obj;
}

// api.SHGetFileIconIndex(path, flags?) -> { index, overlayIndex }
// Returns the system image list index for a file's icon.
// flags: SHGFI_* values (default: SHGFI_SYSICONINDEX | SHGFI_SMALLICON)
static JSValue js_SHGetFileIconIndex(JSContext* ctx,
    JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 1) return JS_UNDEFINED;

    const char* utf8 = JS_ToCString(ctx, argv[0]);
    if (!utf8) return JS_UNDEFINED;
    std::wstring path = Utf8ToWide(utf8);
    JS_FreeCString(ctx, utf8);

    UINT flags = SHGFI_SYSICONINDEX | SHGFI_SMALLICON;
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        int32_t f = 0; JS_ToInt32(ctx, &f, argv[1]);
        flags = (UINT)f;
    }

    SHFILEINFOW sfi{};
    SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), flags);

    JSValue result = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, result, "index",
        JS_NewInt32(ctx, sfi.iIcon));
    return result;
}

static int js_api_init(JSContext* ctx, JSModuleDef* m)
{
    int ret = JS_SetModuleExportList(
        ctx,
        m,
        js_api_funcs,
        sizeof(js_api_funcs) / sizeof(JSCFunctionListEntry)
    );

    if (ret != 0) {
        return ret;
    }

    // Export the Image class object with its static fromFile() method
    JSValue imageClass = CreateImageClass(ctx);
    ret = JS_SetModuleExport(ctx, m, "Image", imageClass);
    JS_FreeValue(ctx, imageClass);

    return ret;
}

// Window procedure for the custom panel class.
// Unlike STATIC, this class sends WM_PAINT directly to ControlProc
// so JS paint handlers work correctly.
static LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Register the custom panel window class (called once at module init)
static void RegisterPanelClass()
{
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = PanelWndProc;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // No background brush; JS handles painting
    wc.lpszClassName = PANEL_CLASS_NAME;
    RegisterClassExW(&wc);
}

JSModuleDef* js_init_module_api(JSContext* ctx, const char* module_name)
{
    RegisterPanelClass();

    JSModuleDef* m = JS_NewCModule(ctx, module_name, js_api_init);

    JS_AddModuleExportList(
        ctx,
        m,
        js_api_funcs,
        sizeof(js_api_funcs) / sizeof(JSCFunctionListEntry)
    );

    // Declare Image as a named export so it can be imported in JS
    JS_AddModuleExport(ctx, m, "Image");

    return m;
}

static CFolderItem* GetPureFolderItem(JSValueConst val)
{
    return (CFolderItem*)JS_GetOpaque(val, g_cfolderitem_class_id);
}

static CFolderItem* GetFolderItem(JSValueConst val)
{
    CFolderItem* fi = (CFolderItem*)JS_GetOpaque(val, g_cfolderitem_class_id);
    if (!fi->pItem && !fi->utf8path.empty()) {
        if SUCCEEDED(SHCreateItemFromParsingName(
            Utf8ToWide(fi->utf8path.c_str()).c_str(),
            nullptr,
            IID_PPV_ARGS(&fi->pItem))) {
            fi->utf8path = "";
        }
    }
    return fi;
}

void cfolderitem_finalizer(JSRuntime* rt, JSValueConst val)
{
    CFolderItem* fi = (CFolderItem*)JS_GetOpaque(val, g_cfolderitem_class_id);

    if (fi == nullptr) {
        return;
    }

    SafeRelease(&fi->pItem);
    delete fi;
}


void image_finalizer(
    JSRuntime* rt,
    JSValue val)
{
    CImage* p = static_cast<CImage*>(JS_GetOpaque(val, g_image_class_id));
    delete p;
}

static JSValue NewFolderItem(
    JSContext* ctx,
    CFolderItem* fi)
{
    if (fi == nullptr) {
        return JS_NULL;
    }

    JSValue obj = JS_NewObjectClass(ctx, g_cfolderitem_class_id);

    if (JS_IsException(obj)) {
        return obj;
    }

    JS_SetOpaque(obj, fi);

    JSAtom atom = JS_NewAtom(ctx, "name");
    JS_DefinePropertyGetSet(
        ctx,
        obj,
        atom,
        JS_NewCFunction(ctx, js_folderitem_get_name, "get", 0),
        JS_UNDEFINED,
        JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE
    );
    JS_FreeAtom(ctx, atom);

    atom = JS_NewAtom(ctx, "path");
    JS_DefinePropertyGetSet(
        ctx,
        obj,
        atom,
        JS_NewCFunction(ctx, js_folderitem_get_path, "get", 0),
        JS_UNDEFINED,
        JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE
    );
    JS_FreeAtom(ctx, atom);

    atom = JS_NewAtom(ctx, "parsingPath");
    JS_DefinePropertyGetSet(
        ctx,
        obj,
        atom,
        JS_NewCFunction(ctx, js_folderitem_get_parsingPath, "get", 0),
        JS_UNDEFINED,
        JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE
    );
    JS_FreeAtom(ctx, atom);
    return obj;
}


static JSValue js_FolderItem(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "path expected");
    }
    CFolderItem* fi = GetPureFolderItem(this_val);
	if (fi) {
		return JS_DupValue(ctx, this_val);
    }
   fi = new CFolderItem();
    const char* str = JS_ToCString(ctx, argv[0]);
	fi->utf8path = str ? str : "";
    JS_FreeCString(ctx, str);

    if (fi->utf8path.empty()) {
        return JS_EXCEPTION;
    }

    return NewFolderItem(ctx, fi);
}

static JSValue js_folderitem_get_path(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    CFolderItem* fi = GetPureFolderItem(this_val);

    if (fi == nullptr) {
        return JS_EXCEPTION;
    }
    if (!fi->utf8path.empty()) {
        return JS_NewString(ctx, fi->utf8path.c_str());
    }
    if (fi->pItem == nullptr) {
        return JS_EXCEPTION;
    }

    PWSTR psz = nullptr;

    HRESULT hr = fi->pItem->GetDisplayName(
        SIGDN_DESKTOPABSOLUTEEDITING,
        &psz);

    if (FAILED(hr) || psz == nullptr) {
        return JS_NULL;
    }

    JSValue ret = JS_NewString(ctx, WideToUtf8(psz).c_str());

    CoTaskMemFree(psz);

    return ret;
}

static JSValue js_folderitem_get_parsingPath(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    CFolderItem* fi = GetPureFolderItem(this_val);

    if (fi == nullptr) {
        return JS_EXCEPTION;
    }
    if (!fi->utf8path.empty()) {
        return JS_NewString(ctx, fi->utf8path.c_str());
    }
    if (fi->pItem == nullptr) {
        return JS_EXCEPTION;
    }

    PWSTR psz = nullptr;

    HRESULT hr = fi->pItem->GetDisplayName(
        SIGDN_DESKTOPABSOLUTEPARSING,
        &psz);

    if (FAILED(hr) || psz == nullptr) {
        return JS_NULL;
    }

    JSValue ret =
        JS_NewString(ctx, WideToUtf8(psz).c_str());

    CoTaskMemFree(psz);

    return ret;
}

static JSValue js_folderitem_get_name(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    CFolderItem* fi = GetPureFolderItem(this_val);

    if (fi == nullptr) {
        return JS_EXCEPTION;
    }
    if (!fi->utf8path.empty()) {
        return JS_NewString(ctx, fi->utf8path.c_str());
    }
    if (fi->pItem == nullptr) {
        return JS_EXCEPTION;
    }

    PWSTR psz = nullptr;

    HRESULT hr = fi->pItem->GetDisplayName(
        SIGDN_NORMALDISPLAY,
        &psz);

    if (FAILED(hr) || psz == nullptr) {
        return JS_NULL;
    }

    JSValue ret = JS_NewString(ctx, WideToUtf8(psz).c_str());
    CoTaskMemFree(psz);

    return ret;
}

static JSValue js_folderitem_get_isFolder(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    CFolderItem* fi = GetFolderItem(this_val);

    if (fi == nullptr) {
        return JS_EXCEPTION;
    }
    if (fi->pItem == nullptr) {
        return JS_EXCEPTION;
    }

    SFGAOF attr = SFGAO_FOLDER;

    HRESULT hr = fi->pItem->GetAttributes(
        SFGAO_FOLDER,
        &attr);

    if (FAILED(hr)) {
        return JS_FALSE;
    }

    return JS_NewBool(ctx, (attr & SFGAO_FOLDER) != 0);
}

static JSValue js_folderitem_parent(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    CFolderItem* fi = GetFolderItem(this_val);

    if (fi == nullptr || fi->pItem == nullptr) {
        return JS_EXCEPTION;
    }

    CFolderItem* fiParent = new CFolderItem();

    HRESULT hr = fi->pItem->GetParent(&fiParent->pItem);

    if (FAILED(hr) || fiParent->pItem == nullptr) {
        delete fiParent;
        return JS_NULL;
    }

    return NewFolderItem(ctx, fiParent);
}




#endif