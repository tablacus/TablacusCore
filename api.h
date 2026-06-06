#pragma once
#pragma warning(push)
#pragma warning(disable: 4244)
extern "C" {
#include "quickjs.h"
}
#pragma warning(pop)
#include "darkmode.h"

const struct {
    LPCWSTR pszPropertyName;
    LPCWSTR pszSemanticType;
} g_rgGenericProperties[] =
{
    { L"System.Generic.String",          L"System.StructuredQueryType.String" },
    { L"System.Generic.Integer",         L"System.StructuredQueryType.Integer" },
    { L"System.Generic.DateTime",        L"System.StructuredQueryType.DateTime" },
    { L"System.Generic.Boolean",         L"System.StructuredQueryType.Boolean" },
    { L"System.Generic.FloatingPoint",   L"System.StructuredQueryType.FloatingPoint" }
};
struct CFolderItem
{
    IShellItem* pItem;
    std::string utf8path;

    CFolderItem()
    {
        pItem = nullptr;
    }
};

JSModuleDef* js_init_module_api(JSContext* ctx, const char* module_name);
LRESULT CommonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
void ui_element_finalizer(JSRuntime* rt, JSValueConst val);
void cfolderitem_finalizer(JSRuntime* rt, JSValueConst val);
static JSValue js_folderitem_get_name(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv);
static JSValue js_folderitem_get_path(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv);
static JSValue js_folderitem_get_parsingPath(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv);
static JSValue NewFolderItem(
    JSContext* ctx,
    CFolderItem* fi);
