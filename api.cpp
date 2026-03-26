#include <windows.h>
#include <vector>

#include "common.h"
#if defined(_WINDLL) || defined(_DEBUG)
#include "api.h"

JSValue g_pMBText;

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

JSValue js_MessageBox(JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv)
{
    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "MessageBox requires at least 3 arguments");
    }
	int64_t hwnd_val;
    JS_ToInt64(ctx, &hwnd_val, argv[0]);
    std::wstring text = Utf8ToWide(JS_ToCString(ctx, argv[1]));
    std::wstring caption = Utf8ToWide(JS_ToCString(ctx, argv[2]));
    int type = MB_OK;
    JS_FreeValue(ctx, g_pMBText);
    if (argc >= 3) {
        JS_ToInt32(ctx, &type, argv[3]);
        if (argc >= 4) {
            g_pMBText = JS_DupValue(ctx, argv[4]);
        }
    }
    int result = MessageBox(
        NULL,
        text.c_str(),
        caption.c_str(),
        type
    );
    return JS_NewInt32(ctx, result);
}

static const JSCFunctionListEntry js_api_funcs[] = {
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