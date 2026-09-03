// TablacusCore.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "TablacusCore.h"
#include <pathcch.h>

#define MAX_LOADSTRING 100
#if defined(_WINDLL) || defined(_DEBUG)
// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
std::wstring g_scriptDir;                       // directory containing main.js

// Normalize module name: resolve relative paths against the script directory
static char* js_module_normalize(JSContext* ctx,
    const char* base_name, const char* name, void* opaque)
{
    // "api" is a built-in, pass through as-is
    if (strcmp(name, "api") == 0)
        return js_strdup(ctx, name);

    // Build absolute path: start from base_name's directory
    std::wstring base = Utf8ToWide(base_name);
    std::wstring rel  = Utf8ToWide(name);

    // Get directory of base
    std::wstring dir = base;
    auto slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        dir = dir.substr(0, slash + 1);
    else
        dir = g_scriptDir;

    // Combine and canonicalize
    wchar_t full[MAX_PATH]{};
    PathCchCombineEx(full, MAX_PATH, dir.c_str(), rel.c_str(), PATHCCH_ALLOW_LONG_PATHS);

    std::string utf8 = WideToUtf8(full);
    return js_strdup(ctx, utf8.c_str());
}
IWICImagingFactory* g_pWICFactory = nullptr;

LPFNRegenerateUserEnvironment _RegenerateUserEnvironment = nullptr;

extern HBRUSH	g_hbrDarkBackground;
extern BOOL g_bDarkMode;
extern int g_nException;
extern std::unordered_map<HWND, HWND> g_umDlgProc;
extern LPCWSTR g_strException;

JSContext* g_ctx;

std::unordered_map<DWORD, HHOOK> g_umCBTHook;

#ifdef _WINDLL
HINSTANCE g_hinstDll = nullptr;
#endif

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

JSModuleDef* js_module_loader(JSContext* ctx,
    const char* module_name,
    void* opaque) {
    if (strcmp(module_name, "api") == 0) {
        return js_init_module_api(ctx, module_name);
    }

    // Load JS file modules relative to the scripts directory
    std::wstring wpath = Utf8ToWide(module_name);

    // Read the file
    HANDLE hFile = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return nullptr;

    DWORD size = GetFileSize(hFile, nullptr);
    std::string src(size, '\0');
    DWORD read = 0;
    ReadFile(hFile, src.data(), size, &read, nullptr);
    CloseHandle(hFile);

    // Compile as module
    JSValue func = JS_Eval(ctx, src.c_str(), src.size(),
        module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(func)) return nullptr;

    JSModuleDef* m = (JSModuleDef*)JS_VALUE_GET_PTR(func);
    JS_FreeValue(ctx, func);
    return m;
}

#ifdef _WINDLL
BOOL WINAPI DllMain(HINSTANCE hinstDll, DWORD dwReason, LPVOID lpReserved)
{
    DWORD dwThreadId = GetCurrentThreadId();
    switch (dwReason) {
    case DLL_PROCESS_ATTACH:
        g_hinstDll = hinstDll;
    case DLL_THREAD_ATTACH:
        g_umCBTHook.try_emplace(dwThreadId, SetWindowsHookEx(WH_CBT, (HOOKPROC)CBTProc, nullptr, dwThreadId));
        break;
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
    {
        auto itr = g_umCBTHook.find(dwThreadId);
        if (itr != g_umCBTHook.end()) {
            UnhookWindowsHookEx(itr->second);
            g_umCBTHook.erase(itr);
        }
    }
    break;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void CALLBACK RunDLLW(
    HWND hWnd,
    HINSTANCE hInstance,
    LPWSTR lpCmdLine,
    int nCmdShow)
{
#else
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
#endif
    UNREFERENCED_PARAMETER(lpCmdLine);

    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
    setlocale(LC_ALL, ".UTF8");
    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_TABLACUSCORE, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);
    //Brush
    g_hbrDarkBackground = CreateSolidBrush(TECL_DARKBG);

    HMODULE hDll = LoadLibrary(L"shell32.dll");
//    * (FARPROC*)&_SHRunDialog = GetProcAddress(g_hShell32, MAKEINTRESOURCEA(61));
    *(FARPROC*)&_RegenerateUserEnvironment = GetProcAddress(hDll, "RegenerateUserEnvironment");

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // OleInitialize is required for drag-and-drop support.
    // It calls CoInitializeEx internally so the above call is redundant
    // but harmless; keep it for clarity.
    OleInitialize(nullptr);

    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_pWICFactory));

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow)) {
#ifdef _WINDLL
        return;
#else
        return FALSE;
#endif
    }
    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_TABLACUSCORE));

    JSRuntime* rt = JS_NewRuntime();
    g_ctx = JS_NewContext(rt);
    JS_NewClassID(rt, &g_class_id);
    JSClassDef class_def{};
    class_def.class_name = "UIElement";
    class_def.finalizer = ui_element_finalizer;
    JS_NewClass(rt, g_class_id, &class_def);

    JS_NewClassID(rt, &g_cfolderitem_class_id);
    class_def.class_name = "FolderItem";
    class_def.finalizer = cfolderitem_finalizer;
    JS_NewClass(rt, g_cfolderitem_class_id, &class_def);

    JS_NewClassID(rt, &g_image_class_id);
    class_def.class_name = "Image";
    class_def.finalizer = image_finalizer;
    JS_NewClass(rt, g_image_class_id, &class_def);

    // Register the module loader
    JS_SetModuleLoaderFunc(rt, js_module_normalize, js_module_loader, nullptr);

    // Load scripts\main.js
	wchar_t outPath[MAX_PATHEX];
   :: GetModuleFileName(nullptr, outPath, MAX_PATHEX);
    wchar_t* p = wcsrchr(outPath, L'\\');
    if (p) p[1] = L'\0';
    g_scriptDir = outPath;  // save script directory for module resolution
    wcscat_s(outPath, MAX_PATHEX, L"scripts\\main.js");
    if (p) {
        *(p + 1) = 0;
    }
    wcscat_s(outPath, MAX_PATHEX, L"scripts\\main.js");

    std::string script = ::LoadFile(outPath);

    if (script.empty()) {
#ifdef _WINDLL
        return;
#else
        return 1;
#endif
    }
    std::string utf8Path = WideToUtf8(outPath);

    // Initialize dark mode state before running JS so isDarkMode() returns correctly
    teGetDarkMode();

    JSValue val = JS_Eval(g_ctx,
        script.c_str(),
        script.size(),
        utf8Path.c_str(),
        JS_EVAL_TYPE_MODULE);

    // Execute pending jobs (runs module top-level code)
    JSContext* pctx = nullptr;
    while (JS_ExecutePendingJob(JS_GetRuntime(g_ctx), &pctx) > 0) {}

    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(g_ctx);

        std::string msg;

        // Try direct string conversion
        const char* err = JS_ToCString(g_ctx, exc);
        if (err && strcmp(err, "[uninitialized]") != 0 && strlen(err) > 0) {
            msg += err;
        }
        JS_FreeCString(g_ctx, err);

        // Try stack property
        JSValue stack = JS_GetPropertyStr(g_ctx, exc, "stack");
        if (!JS_IsUndefined(stack)) {
            const char* st = JS_ToCString(g_ctx, stack);
            if (st) { if (!msg.empty()) msg += "\n\n"; msg += st; JS_FreeCString(g_ctx, st); }
        }
        JS_FreeValue(g_ctx, stack);

        // Try message property
        JSValue jmsg = JS_GetPropertyStr(g_ctx, exc, "message");
        if (!JS_IsUndefined(jmsg)) {
            const char* ms = JS_ToCString(g_ctx, jmsg);
            if (ms) { if (!msg.empty()) msg += "\n"; msg += "message: "; msg += ms; JS_FreeCString(g_ctx, ms); }
        }
        JS_FreeValue(g_ctx, jmsg);

        // Fallback: JSON stringify
        if (msg.empty()) {
            JSValue json = JS_JSONStringify(g_ctx, exc, JS_UNDEFINED, JS_UNDEFINED);
            if (!JS_IsException(json)) {
                const char* js = JS_ToCString(g_ctx, json);
                if (js) { msg += js; JS_FreeCString(g_ctx, js); }
                JS_FreeValue(g_ctx, json);
            }
        }

        if (msg.empty()) msg = "(unknown error - exception object has no string representation)";

        MessageBoxA(nullptr, msg.c_str(), "Error", MB_OK | MB_ICONERROR);
        JS_FreeValue(g_ctx, exc);
    }

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    JS_FreeValue(g_ctx, val);
    JS_FreeContext(g_ctx);
    JS_FreeRuntime(rt);
#ifndef _WINDLL
    return (int) msg.wParam;
#endif
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TABLACUSCORE));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = nullptr;
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
#ifdef _DEBUG
    DWORD dwThreadId = GetCurrentThreadId();
    auto itr = g_umCBTHook.find(dwThreadId);
    if (itr == g_umCBTHook.end()) {
        g_umCBTHook[dwThreadId] = SetWindowsHookEx(WH_CBT, (HOOKPROC)CBTProc, nullptr, dwThreadId);
    }
#endif
    teInitDarkMode();
   hInst = hInstance; // Store instance handle in our global variable
   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT lResult = CommonProc(hwnd, message, wParam, lParam);
    if (lResult != 1) {
        return lResult;
    }

    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hwnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hwnd);
                break;
            default:
                return DefWindowProc(hwnd, message, wParam, lParam);
            }
        }
        break;
    case WM_SETTINGCHANGE:
/*        SafeRelease(&g_pqp);
        if (g_pSW) {
            teRegister();
        }*/
        teGetDarkMode();
        teSetDarkMode(hwnd);
        CHAR pszClassA[MAX_CLASS_NAME];
        for (auto itr = g_umDlgProc.begin(); itr != g_umDlgProc.end(); ++itr) {
            GetClassNameA(itr->second, pszClassA, MAX_CLASS_NAME);
            if (::PathMatchSpecA(pszClassA, TOOLTIPS_CLASSA)) {
                SetWindowTheme(itr->second, g_bDarkMode ? L"darkmode_explorer" : L"explorer", nullptr);
            }
        }
        if (_RegenerateUserEnvironment) {
            try {
                if (teStrCmpIWA((LPCWSTR)lParam, "Environment") == 0) {
                    LPVOID lpEnvironment;
                    _RegenerateUserEnvironment(&lpEnvironment, TRUE);
                    //Not permitted to free lpEnvironment!
                    //FreeEnvironmentStrings((LPTSTR)lpEnvironment);
                }
            }
            catch (...) {
                g_nException = 0;
#ifdef _DEBUG
                g_strException = L"RegenerateUserEnvironment";
#endif
            }
        }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

#endif