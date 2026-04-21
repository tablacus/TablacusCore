// TablacusCore.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "TablacusCore.h"

#define MAX_LOADSTRING 100
#if defined(_WINDLL) || defined(_DEBUG)
// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

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
    return nullptr;
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

    // Register the module loader
    JS_SetModuleLoaderFunc(rt, nullptr, js_module_loader, nullptr);

    // Load scripts\main.js
	wchar_t outPath[MAX_PATHEX];
   :: GetModuleFileName(nullptr, outPath, MAX_PATHEX);
    wchar_t* p = wcsrchr(outPath, L'\\');
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

    JSValue val = JS_Eval(g_ctx,
        script.c_str(),
        script.size(),
        utf8Path.c_str(),
        JS_EVAL_TYPE_MODULE);

    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(g_ctx);
        const char* err = JS_ToCString(g_ctx, exc);
        JS_FreeCString(g_ctx, err);
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
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_TABLACUSCORE);
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
    case WM_ERASEBKGND:
        if (g_bDarkMode) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wParam, &rc, g_hbrDarkBackground);
            return 1;
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            // TODO: Add any drawing code that uses hdc here...
            EndPaint(hwnd, &ps);
        }
        break;
    case WM_KEYDOWN:
        if (FireKeyEvent(hwnd, "keydown", wParam)) {
            return 0;
        }
        break;
    case WM_KEYUP:
        if (FireKeyEvent(hwnd, "keyup", wParam)) {
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
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
    case WM_NCDESTROY:
    {
        auto* data = GetWindowData(hwnd);

        if (data) {
            // free event handlers
            for (auto& [name, vec] : data->events.map) {
                for (auto& fn : vec) {
                    JS_FreeValue(data->ctx, fn);
                }
            }

            // free JS object
            JS_FreeValue(data->ctx, data->jsThis);

            delete data;

            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        }
        break;
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