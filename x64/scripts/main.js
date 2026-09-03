import * as api from "api";
import { TabBar } from "./tabbar.js";
const { SHGetSystemImageList, SHGetFileIconIndex, isDarkMode } = api;
const darkMode = isDarkMode();

// ── Window ────────────────────────────────────────────────────────────────
const window = api.CreateWindow({
    className: "TablacusCore",
    text: "Tablacus Core",
    x: 100, y: 100, width: 800, height: 600,
});
window.show();

// ── System ImageList ──────────────────────────────────────────────────────
const sysIL = SHGetSystemImageList("small");
const SFLAGS = api.SHGFI_SYSICONINDEX | api.SHGFI_SMALLICON;
const iBack    = SHGetFileIconIndex("C:\\Windows\\System32\\imageres.dll", SFLAGS).index;
const iForward = SHGetFileIconIndex("C:\\Windows\\System32\\shell32.dll",  SFLAGS).index;
const iUp      = SHGetFileIconIndex("C:\\Windows",                         SFLAGS).index;
const iRefresh = SHGetFileIconIndex("C:\\Windows\\System32",               SFLAGS).index;
const iFolder  = SHGetFileIconIndex("C:\\Users",                           SFLAGS).index;
const iDrive   = SHGetFileIconIndex("C:\\",                                SFLAGS).index;

function menuItem(id, text, iconIndex) {
    return { id, text, iconIndex, imageList: sysIL };
}

function showDropdown(e, buildMenu, onSelect) {
    let buttonId = e.buttonId, x = e.x, y = e.y;
    let rcExclude = { left: e.x, top: e.y, right: e.x, bottom: e.y };
    while (true) {
        const menu = buildMenu(buttonId);
        if (!menu) break;
        const result = menu.trackForToolbar(e.hwnd, x, y, rcExclude, buttonId);
        menu.destroy();
        if (result.switchTo > 0 && result.switchTo !== buttonId) {
            buttonId = result.switchTo; x = result.x; y = result.y;
            rcExclude = result.rcExclude; continue;
        }
        if (result.id > 0) onSelect(result.id, buttonId);
        break;
    }
}

// ── Layout constants ──────────────────────────────────────────────────────
const WIN_W    = 800;
const WIN_H    = 600;
const MENUBAR_H = 22;
const TOOLBAR_H = 26;
const ADDR_H    = 22;
const STATUS_H  = 20;
const STATUS_Y  = WIN_H - STATUS_H;

// ── Menu bar (fixed) ──────────────────────────────────────────────────────
const menubar = window.createElement("TOOLBAR", {
    y: 0, height: MENUBAR_H, width: WIN_W,
    buttonWidth: 0, buttonHeight: 0,
    showArrows: false,
    buttons: [
        { id: 10, text: "ファイル", style: api.BTNS_DROPDOWN },
        { id: 20, text: "編集",     style: api.BTNS_DROPDOWN },
        { id: 30, text: "表示",     style: api.BTNS_DROPDOWN },
        { id: 40, text: "ヘルプ",   style: api.BTNS_DROPDOWN },
    ],
    listeners: {
        dropdown: [(e) => {
            showDropdown(e, (buttonId) => {
                const menu = api.CreatePopupMenu();
                if (buttonId === 10) {
                    menu.append(menuItem(1001, "新しいタブ",   iFolder));
                    menu.append({ separator: true });
                    menu.append(menuItem(1002, "終了",         iDrive));
                } else if (buttonId === 20) {
                    menu.append(menuItem(2001, "コピー",       iFolder));
                    menu.append(menuItem(2002, "貼り付け",     iFolder));
                } else if (buttonId === 30) {
                    menu.append(menuItem(3001, "ツールバー",   iFolder));
                } else if (buttonId === 40) {
                    menu.append(menuItem(4001, "バージョン情報", iFolder));
                } else { menu.destroy(); return null; }
                return menu;
            }, (id) => {
                if (id === 1001) tabbar.addTab("新しいタブ", iFolder);
            });
        }],
    }
});

// ── TabBar (fixed) ────────────────────────────────────────────────────────
const TAB_Y = MENUBAR_H;
const tabbar = new TabBar(window, {
    y: TAB_Y, width: WIN_W,
    imageList: sysIL,
    dark: darkMode,
    listeners: {
        select: [(id) => activateTab(id)],
        close:  [(id) => closeTab(id)],
    },
});

// ── Tab content area geometry (computed after tabbar) ─────────────────────
const CONTENT_Y  = TAB_Y + tabbar.height; // top of per-tab controls
const TOOLBAR_Y  = CONTENT_Y;
const ADDR_Y     = CONTENT_Y + TOOLBAR_H;
const CONTENT_EXP_Y = ADDR_Y + ADDR_H;
const CONTENT_EXP_H = STATUS_Y - CONTENT_EXP_Y;

// ── Status bar (fixed) ────────────────────────────────────────────────────
const stat = window.createElement("STATIC", {
    id: "stat",
    y: STATUS_Y, height: STATUS_H, width: WIN_W,
    listeners: {
        paint: [(e) => {
            const ps = {};
            const hdc = api.BeginPaint(e.hwnd, ps);
            api.DrawText({ hdc, text: stat._text || "Ready",
                rc: ps.rcPaint,
                format: api.DT_LEFT | api.DT_VCENTER | api.DT_SINGLELINE });
            api.EndPaint(e.hwnd, ps);
        }],
    }
});
stat._text = "Ready";

// ── Per-tab content management ────────────────────────────────────────────
// tabContents: Map<tabId, { toolbar, edit, exp }>
const tabContents = new Map();

function showControls(content, visible) {
    const sw = visible ? api.SW_SHOW : api.SW_HIDE;
    if (content.toolbar) api.ShowWindow(content.toolbar.hwnd, sw);
    if (content.edit)    api.ShowWindow(content.edit.hwnd,    sw);
    if (content.exp)     api.ShowWindow(content.exp.hwnd,     sw);
}

function createTabContent(tabId) {
    // ToolBar
    const tb = window.createElement("TOOLBAR", {
        y: TOOLBAR_Y, height: TOOLBAR_H, width: WIN_W,
        buttonWidth: 16, buttonHeight: 16,
        imageList: sysIL,
        buttons: [
            { id: 1, image: iBack,    text: "Back",    style: api.BTNS_WHOLEDROPDOWN },
            { id: 2, image: iForward, text: "Forward", style: api.BTNS_WHOLEDROPDOWN },
            { id: 3, image: iUp,      text: "Up" },
            { separator: true },
            { id: 4, image: iRefresh, text: "Refresh" },
        ],
        listeners: {
            click: [(e) => {
                const c = tabContents.get(tabId);
                if (!c) return;
                if (e.buttonId === 3) c.exp.navigate("..");
                if (e.buttonId === 4) c.exp.navigate(0);
            }],
            dropdown: [(e) => {
                showDropdown(e, (buttonId) => {
                    const menu = api.CreatePopupMenu();
                    if (buttonId === 1) {
                        menu.append(menuItem(101, "← ドキュメント", iFolder));
                        menu.append(menuItem(102, "← デスクトップ", iFolder));
                    } else if (buttonId === 2) {
                        menu.append(menuItem(201, "→ ダウンロード", iFolder));
                    } else { menu.destroy(); return null; }
                    return menu;
                }, (id) => {
                    const c = tabContents.get(tabId);
                    if (!c) return;
                    if (id === 101) c.exp.navigate("shell:Personal");
                    if (id === 102) c.exp.navigate("shell:Desktop");
                    if (id === 201) c.exp.navigate("shell:Downloads");
                });
            }],
        }
    });

    // Address bar
    const edit = window.createElement("EDIT", {
        placeholder: "Path or URL",
        y: ADDR_Y, height: ADDR_H, width: WIN_W,
        listeners: {
            keydown: (e) => {
                if (e.key === "Enter") {
                    const c = tabContents.get(tabId);
                    if (c) c.exp.navigate(e.target.text);
                    return false;
                }
            },
        }
    });
    api.SHAutoComplete(edit.hwnd,
        api.SHACF_FILESYS_DIRS |
        api.SHACF_AUTOSUGGEST_FORCE_ON | api.SHACF_AUTOAPPEND_FORCE_ON);

    // ExplorerBrowser
    const exp = window.createElement("EXPLORER", {
        y: CONTENT_EXP_Y, height: CONTENT_EXP_H, width: WIN_W,
        listeners: {
            navigate: (e) => {
                // Only update UI if this tab is active
                if (tabbar._activeId !== tabId) return;
                const folder = e.target.currentFolder;
                edit.text  = folder.path;
                window.text = folder.name;
                stat._text  = folder.parsingPath;
                tabbar.setLabel(tabId, folder.name || "新しいタブ");
            },
        }
    });

    const content = { toolbar: tb, edit, exp };
    tabContents.set(tabId, content);
    return content;
}

let _prevTabId = null;

function activateTab(id) {
    // Hide previous tab's controls
    if (_prevTabId !== null && _prevTabId !== id) {
        const prev = tabContents.get(_prevTabId);
        if (prev) showControls(prev, false);
    }

    // Show or create current tab's controls
    let content = tabContents.get(id);
    if (!content) {
        content = createTabContent(id);
    }
    showControls(content, true);
    _prevTabId = id;

    // Sync UI
    const folder = content.exp?.currentFolder;
    if (folder) {
        content.edit.text = folder.path;
        window.text = folder.name;
        stat._text  = folder.parsingPath;
    }
}

function closeTab(id) {
    const content = tabContents.get(id);
    if (content) {
        // Controls are not destroyed (Win32 handles lifetime with parent window)
        // Just hide them; GC will clean up JS side
        showControls(content, false);
        tabContents.delete(id);
    }
    tabbar.removeTab(id);
}

// ── Initial tabs ──────────────────────────────────────────────────────────
const tab1 = tabbar.addTab("新しいタブ", iFolder);
// First tab is activated automatically by TabBar's select listener
activateTab(tab1);
