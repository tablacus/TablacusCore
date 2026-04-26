#pragma once
#pragma warning(push)
#pragma warning(disable: 4244)
extern "C" {
#include "quickjs.h"
}
#pragma warning(pop)
#include "darkmode.h"

JSModuleDef* js_init_module_api(JSContext* ctx, const char* module_name);
LRESULT CommonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
