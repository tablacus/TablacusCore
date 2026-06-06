#include <windows.h>
#include <vector>

#include "common.h"
#if defined(_WINDLL) || defined(_DEBUG)
#include "api.h"

JSValue g_pMBText;
JSClassID g_class_id;
JSClassID g_cfolderitem_class_id = 0;
static std::unordered_map<std::wstring, UIElement*> g_idMap;
static HWND g_hwndActiveMouse = nullptr;
static POINT g_ptMouseDown = {};
static HWND g_hwndHover = nullptr;
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
                // free event handlers
                for (auto& [name, vec] : el->events.map) {
                    for (auto& fn : vec) {
                        JS_FreeValue(el->ctx, fn);
                    }
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

    // ===== Parse listeners and store directly =====
    JSValue listeners = JS_GetPropertyStr(ctx, opts, "listeners");

    if (JS_IsObject(listeners)) {
        JSPropertyEnum* props;
        uint32_t len;

        if (JS_GetOwnPropertyNames(ctx, &props, &len, listeners,
            JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {

            for (uint32_t i = 0; i < len; i++) {
                const char* name = JS_AtomToCString(ctx, props[i].atom);
                JSValue val = JS_GetProperty(ctx, listeners, props[i].atom);

                // Get event handler list for this event type
                auto& vec = el->events.map[name];

                // Single function
                if (JS_IsFunction(ctx, val)) {
                    vec.push_back(JS_DupValue(el->ctx, val));
                }
                // Array of functions
                else if (JS_IsArray(val)) {
                    uint32_t alen = JS_GetArrayLength(ctx, val);

                    for (uint32_t j = 0; j < alen; j++) {
                        JSValue fn = JS_GetPropertyUint32(ctx, val, j);
                        if (JS_IsFunction(ctx, fn)) {
                            vec.push_back(JS_DupValue(ctx, fn));
                        }
                        JS_FreeValue(ctx, fn);
                    }
                }

                JS_FreeCString(ctx, name);
                JS_FreeValue(ctx, val);
            }
            js_free(ctx, props);
        }
    }
    JS_FreeValue(ctx, listeners);

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
        hwnd = CreateWindowExW(
            0,
            L"STATIC",
            text.c_str(),
            WS_CHILD | WS_VISIBLE | SS_NOTIFY,
            x, y, width, height,      // default position/size
            parent->hwnd,
            nullptr,
            GetModuleHandle(nullptr),
            nullptr
        );
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

    JS_CFUNC_DEF("ShowWindow", 2, js_ShowWindow),
    JS_CFUNC_DEF("UpdateWindow", 1, js_ShowWindow),

};
       
static int js_api_init(JSContext* ctx, JSModuleDef* m)
{
    return JS_SetModuleExportList(
        ctx,
        m,
        js_api_funcs,
        sizeof(js_api_funcs) / sizeof(JSCFunctionListEntry)
    );
}

JSModuleDef* js_init_module_api(JSContext* ctx, const char* module_name)
{
    JSModuleDef* m = JS_NewCModule(ctx, module_name, js_api_init);

    JS_AddModuleExportList(
        ctx,
        m,
        js_api_funcs,
        sizeof(js_api_funcs) / sizeof(JSCFunctionListEntry)
    );

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