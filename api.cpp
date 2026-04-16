#include <windows.h>
#include <vector>

#include "common.h"
#if defined(_WINDLL) || defined(_DEBUG)
#include "api.h"

JSValue g_pMBText;

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

    // fallback: 数値文字列
    return atoi(name);
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

    std::wstring title = L"Window";
    std::wstring className = L"STATIC";

    // ===== Helper: get int property =====
    auto getInt = [&](const char* name, int def) {
        JSValue v = JS_GetPropertyStr(ctx, opts, name);
        int val = def;
        if (!JS_IsUndefined(v)) {
            JS_ToInt32(ctx, &val, v);
        }
        JS_FreeValue(ctx, v);
        return val;
    };

    // ===== Helper: get int64 property =====
    auto getInt64 = [&](const char* name, int64_t def) {
        JSValue v = JS_GetPropertyStr(ctx, opts, name);
        int64_t val = def;
        if (!JS_IsUndefined(v)) {
            JS_ToInt64(ctx, &val, v);
        }
        JS_FreeValue(ctx, v);
        return val;
    };

    // ===== Read options =====
    style = getInt("style", style);
    exStyle = getInt("exStyle", exStyle);

    x = getInt("x", x);
    y = getInt("y", y);
    width = getInt("width", width);
    height = getInt("height", height);

    parent = (HWND)getInt64("parent", 0);
    menu = (HMENU)getInt64("menu", 0);
    instance = (HINSTANCE)getInt64("instance", (int64_t)instance);

    // Read title string
    {
        JSValue v = JS_GetPropertyStr(ctx, opts, "title");
        if (JS_IsString(v)) {
            WStrNullable w;
            JS_ToWStrNullable(ctx, v, w);
            if (w.ptr) {
                title = w.ptr;
            }
        }
        JS_FreeValue(ctx, v);
    }
    // Read class string
    {
        JSValue v = JS_GetPropertyStr(ctx, opts, "className");
        if (JS_IsString(v)) {
            WStrNullable w;
            JS_ToWStrNullable(ctx, v, w);
            if (w.ptr) {
                className = w.ptr;
            }
        }
        JS_FreeValue(ctx, v);
    }

    // ===== Create native window =====
    HWND hwnd = CreateWindowExW(
        exStyle,
        className.c_str(),
        title.c_str(),
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

    // Apply dark mode if supported
    teGetDarkMode();
    teSetDarkMode(hwnd);

    // ===== Create JS object =====
    JSValue obj = JS_NewObject(ctx);

    // Store HWND in JS object
    JS_SetPropertyStr(ctx, obj, "hwnd",
        JS_NewBigInt64(ctx, (int64_t)hwnd));

    // ===== Allocate WindowData =====
    WindowData* data = (WindowData*)js_malloc(ctx, sizeof(WindowData));
    data->ctx = ctx;
    data->jsThis = JS_DupValue(ctx, obj);
    
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
                auto& vec = data->events.map[name];

                // Single function
                if (JS_IsFunction(ctx, val)) {
                    vec.push_back(JS_DupValue(ctx, val));
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

    // ===== Bind HWND <-> WindowData =====
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)data);

    // ===== show(): display the window =====
    JS_SetPropertyStr(ctx, obj, "show",
        JS_NewCFunction(ctx,
            [](JSContext* ctx, JSValueConst this_val,
                int argc, JSValueConst* argv) -> JSValue {

        JSValue v = JS_GetPropertyStr(ctx, this_val, "hwnd");

        int64_t hwnd;
        if (JS_IsBigInt(v)) {
            JS_ToBigInt64(ctx, &hwnd, v);
        }
        else {
            JS_ToInt64(ctx, &hwnd, v);
        }
        JS_FreeValue(ctx, v);

        ShowWindow((HWND)hwnd, SW_SHOW);
        UpdateWindow((HWND)hwnd);

        return JS_UNDEFINED;
    },
            "show", 0)
    );

    return obj;
}

JSValue js_GetModuleFileName(JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    int64_t hModule;
    JS_ToInt64(ctx, &hModule, argv[0]);
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
    JS_ToInt64(ctx, &hwnd, argv[0]);
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
    JS_ToInt64(ctx, &hwnd, argv[0]);
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
    JS_ToInt64(ctx, &hwnd, argv[0]);
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

#endif