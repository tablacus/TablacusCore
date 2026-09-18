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
// controls once. After that, clientW / clientH track the window's current
// client area and every control is re-measured against them in layout().
const MENUBAR_H = 22;
const TOOLBAR_H = 26;
const ADDR_H    = 22;

let rc = api.GetClientRect(window.hwnd);
let clientW = rc.right - rc.left;
let clientH = rc.bottom - rc.top;

// ── Menu bar (fixed) ──────────────────────────────────────────────────────
const menubar = window.createElement("TOOLBAR", {
    y: 0, height: MENUBAR_H, width: clientW,
    buttonWidth: 0, buttonHeight: 0,
    showArrows: false,
    buttons: [
        { id: 10, text: "File", style: api.BTNS_DROPDOWN },
        { id: 20, text: "Edit",     style: api.BTNS_DROPDOWN },
        { id: 30, text: "View",     style: api.BTNS_DROPDOWN },
        { id: 40, text: "Help",   style: api.BTNS_DROPDOWN },
    ],
    listeners: {
        dropdown: [(e) => {
            showDropdown(e, (buttonId) => {
                const menu = api.CreatePopupMenu();
                if (buttonId === 10) {
                    menu.append(menuItem(1001, "New tab",   iFolder));
                    menu.append({ separator: true });
                    menu.append(menuItem(1002, "Exit",         iDrive));
                } else if (buttonId === 20) {
                    menu.append(menuItem(2001, "Copy",       iFolder));
                    menu.append(menuItem(2002, "Paste",     iFolder));
                } else if (buttonId === 30) {
                    menu.append(menuItem(3001, "Toolbar",   iFolder));
                } else if (buttonId === 40) {
                    menu.append(menuItem(4001, "Version Information", iFolder));
                } else { menu.destroy(); return null; }
                return menu;
            }, (id) => {
                if (id === 1001) {
                    tabbar.addTab("New tab", iFolder);
                    layout();
                }
            });
        }],
    }
});

// ── TabBar (fixed) ────────────────────────────────────────────────────────
const TAB_Y = MENUBAR_H;
const tabbar = new TabBar(window, {
    y: TAB_Y, width: clientW,
    imageList: sysIL,
    dark: darkMode,
    listeners: {
        select: [(id) => activateTab(id)],
        close:  [(id) => closeTab(id)],
    },
});

// ── Tab content area geometry (computed after tabbar) ─────────────────────
// Depends on tabbar.height, which can change (multi-row wrapping) whenever
// the window is resized, so this is a function rather than a fixed const.
function computeGeom() {
    const contentY     = TAB_Y + tabbar.height; // top of per-tab controls
    const toolbarY     = contentY;
    const addrY        = contentY + TOOLBAR_H;
    const contentExpY  = addrY + ADDR_H;
    const contentExpH  = Math.max(0, clientH - contentExpY);
    return { toolbarY, addrY, contentExpY, contentExpH };
}
let { toolbarY: TOOLBAR_Y, addrY: ADDR_Y,
      contentExpY: CONTENT_EXP_Y, contentExpH: CONTENT_EXP_H } = computeGeom();

// ── Per-tab content management ────────────────────────────────────────────
// tabContents: Map<tabId, { toolbar, edit, exp }>
const tabContents = new Map();

function showControls(content) {
    content.toolbar?.show();
    content.edit?.show();
    content.exp?.show();
    api.SendMessage(window.hwnd, api.WM_SETREDRAW, 1, 0);
    api.RedrawWindow(window.hwnd, null, 0, api.RDW_NOERASE | api.RDW_INVALIDATE | api.RDW_ALLCHILDREN);
}

function hideControls(content) {
    api.SendMessage(window.hwnd, api.WM_SETREDRAW, 0, 0);
    content.toolbar?.hide();
    content.edit?.hide();
    content.exp?.hide();
}

function createTabContent(tabId) {
    // ToolBar
    const tb = window.createElement("TOOLBAR", {
        y: TOOLBAR_Y, height: TOOLBAR_H, width: clientW,
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
                        menu.append(menuItem(101, "← Documents", iFolder));
                        menu.append(menuItem(102, "← Desktop", iFolder));
                    } else if (buttonId === 2) {
                        menu.append(menuItem(201, "→ Downloads", iFolder));
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
        placeholder: "Path",
        y: ADDR_Y, height: ADDR_H, width: clientW,
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
        y: CONTENT_EXP_Y, height: CONTENT_EXP_H, width: clientW,
        listeners: {
            navigate: (e) => {
                const folder = e.target.currentFolder;
                tabbar.setLabel(tabId, folder.name);
                edit.text = folder.path;
                // Only update UI if this tab is active
                if (tabbar._activeId === tabId) {
                    window.text = folder.name;
                }
            }
        }
    });

    const content = { toolbar: tb, edit, exp };
    tabContents.set(tabId, content);
    return content;
}

let _prevTabId = null;

function activateTab(id) {
    const prevId = _prevTabId;
    _prevTabId = id;

    // Show or create current tab's controls first
    let content = tabContents.get(id);
    if (!content) {
        content = createTabContent(id);
    }

    // Hide previous tab's controls after a short delay to reduce flicker
    if (prevId !== null && prevId !== id) {
        const prev = tabContents.get(prevId);
        if (prev) {
            hideControls(prev);
        }
    }
    showControls(content);

    // Sync UI
    const folder = content.exp?.currentFolder;
    if (folder) {
        content.edit.text = folder.path;
        window.text = folder.name;
        tabbar.setLabel(id, folder.name);
    }
}

function closeTab(id) {
    const content = tabContents.get(id);
    if (content) {
        // Controls are not destroyed (Win32 handles lifetime with parent window)
        // Just hide them; GC will clean up JS side
        hideControls(content);
        tabContents.delete(id);
    }
    tabbar.removeTab(id);
    layout();
}

// ── Layout (re-fit all controls to the current client size) ───────────────
// Called once at startup and every time the window fires "Resize".
function layout() {
    const rc = api.GetClientRect(window.hwnd);
    clientW = rc.right - rc.left;
    clientH = rc.bottom - rc.top;

    // Menu bar spans the full width, height never changes.
    api.SetWindowPos(menubar.hwnd, 0, 0, clientW, MENUBAR_H);

    // Tab bar spans the full width; its own height can change if the tabs
    // wrap onto more/fewer rows at the new width.
    tabbar.resize(clientW);

    // Everything below the tab bar depends on tabbar.height, so recompute.
    const geom = computeGeom();
    TOOLBAR_Y = geom.toolbarY;
    ADDR_Y = geom.addrY;
    CONTENT_EXP_Y = geom.contentExpY;
    CONTENT_EXP_H = geom.contentExpH;

    // Re-fit every tab's controls (not just the active one) so a hidden
    // tab is already sized correctly the moment it's shown again.
    for (const c of tabContents.values()) {
        c.toolbar && api.SetWindowPos(c.toolbar.hwnd, 0, TOOLBAR_Y, clientW, TOOLBAR_H);
        c.edit    && api.SetWindowPos(c.edit.hwnd,    0, ADDR_Y,    clientW, ADDR_H);
        c.exp     && api.SetWindowPos(c.exp.hwnd,     0, CONTENT_EXP_Y, clientW, CONTENT_EXP_H);
    }
}

window.listeners.Resize = [() => layout()];

// ── Initial tabs ──────────────────────────────────────────────────────────
const tab1 = tabbar.addTab("New tab", iFolder);
// First tab is activated automatically by TabBar's select listener
activateTab(tab1);

// Sync geometry with the window's actual client rect once at startup too
// (covers DPI/border differences from the requested 800x600).
layout();
