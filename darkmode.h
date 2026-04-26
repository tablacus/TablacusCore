#pragma once
#include <windows.h>
#include <windowsx.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <CommCtrl.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <shlobj_core.h>
#pragma warning(push)
#pragma warning(disable: 4244)
#include "quickjs.h"
#pragma warning(pop)
#include "common.h"

//Dark mode
#define TECL_DARKTEXT 0xffffff
#define TECL_DARKTEXT2 0xe0e0e0
#define TECL_DARKBG 0x202020
#define TECL_DARKEDITBG 0x181818
#define TECL_DARKSEL 0x606060

#ifndef WINCOMPATTRDATA
struct WINCOMPATTRDATA
{
	DWORD dwAttrib;
	PVOID pvData;
	SIZE_T cbData;
};
#endif

#ifndef PreferredAppMode
enum PreferredAppMode
{
	APPMODE_DEFAULT = 0,
	APPMODE_ALLOWDARK = 1,
	APPMODE_FORCEDARK = 2,
	APPMODE_FORCELIGHT = 3,
	APPMODE_MAX = 4
};
#endif

typedef BOOL(WINAPI* LPFNSetWindowCompositionAttribute)(HWND hWnd, WINCOMPATTRDATA*);
typedef HRESULT(STDAPICALLTYPE* LPFNDwmSetWindowAttribute)(HWND hwnd, DWORD dwAttribute, LPCVOID pvAttribute, DWORD   cbAttribute);

typedef bool (WINAPI* LPFNAllowDarkModeForApp)(BOOL allow); //Deprecated since 18334
typedef void (WINAPI* LPFNSetPreferredAppMode)(PreferredAppMode appMode);
typedef bool (WINAPI* LPFNAllowDarkModeForWindow)(HWND hwnd, BOOL allow);
typedef bool (WINAPI* LPFNShouldAppsUseDarkMode)();
typedef void (WINAPI* LPFNRefreshImmersiveColorPolicyState)();
typedef bool (WINAPI* LPFNIsDarkModeAllowedForWindow)(HWND hwnd);

VOID teSetDarkMode(HWND hwnd);
VOID teGetDarkMode();
VOID teSetTreeTheme(HWND hwnd, COLORREF cl);
BOOL teIsDarkColor(COLORREF cl);
VOID teFixGroup(LPNMLVCUSTOMDRAW lplvcd, COLORREF clrBk);
LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam);
VOID teInitDarkMode();
VOID FixChildren(HWND hwnd);
VOID FixChild(HWND hwnd, HWND hwnd1);
LRESULT DarkProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
