#pragma once
#pragma warning(push)
#pragma warning(disable: 4244)
extern "C" {
#include "quickjs.h"
}
#pragma warning(pop)
#include "darkmode.h"

struct CFolderItem
{
    IShellItem* pItem;

    CFolderItem()
    {
        pItem = nullptr;
    }
};

JSModuleDef* js_init_module_api(JSContext* ctx, const char* module_name);
LRESULT CommonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
void ui_element_finalizer(JSRuntime* rt, JSValueConst val);
void cfolderitem_finalizer(JSRuntime* rt, JSValueConst val);
static JSValue js_folderitem_get_path(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv);
static JSValue NewFolderItem(
    JSContext* ctx,
    IShellItem* pItem);