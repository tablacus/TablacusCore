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

// Collection of CFolderItem, exposed to JS as a FolderItems-compatible
// object (Count property + Item(index) method), e.g. for SelectedItems().
struct CFolderItems
{
    std::vector<CFolderItem*> items;

    ~CFolderItems()
    {
        for (auto* fi : items) {
            SafeRelease(&fi->pItem);
            delete fi;
        }
    }
};

JSModuleDef* js_init_module_api(JSContext* ctx, const char* module_name);
LRESULT CommonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
void ui_element_finalizer(JSRuntime* rt, JSValueConst val);
void cfolderitem_finalizer(JSRuntime* rt, JSValueConst val);
void image_finalizer(JSRuntime* rt, JSValueConst val);

static void cfolderitems_finalizer(JSRuntime* rt, JSValueConst val);
static JSValue NewFolderItems(JSContext* ctx, CFolderItems* fis);
static JSValue js_folderitems_get_count(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv);
static JSValue js_folderitems_item(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv);
static JSValue js_get_selected_items(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv);

// Defined in common.cpp; not static so it can be used as the Image.fromFile() implementation
JSValue Image_fromFile(
    JSContext* ctx,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv);

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
