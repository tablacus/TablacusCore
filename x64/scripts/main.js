import * as api from "api";
const { SHGetSystemImageList, SHGetFileIconIndex } = api;

// ── Window ────────────────────────────────────────────────────────────────
const window = api.CreateWindow({
    className: "TablacusCore",
    text: "Tablacus Core",
    x: 100, y: 100, width: 800, height: 600,
});
window.show();

// ── System ImageList (small 16x16) ────────────────────────────────────────
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

// Helper: show a dropdown with hot-tracking support
function showDropdown(e, buildMenu, onSelect) {
    let buttonId  = e.buttonId;
    let x         = e.x;
    let y         = e.y;
    let rcExclude = { left: e.x, top: e.y, right: e.x, bottom: e.y };

    while (true) {
        const menu = buildMenu(buttonId);
        if (!menu) break;

        const result = menu.trackForToolbar(
            e.hwnd, x, y, rcExclude, buttonId
        );
        menu.destroy();

        if (result.switchTo > 0 && result.switchTo !== buttonId) {
            buttonId  = result.switchTo;
            x         = result.x;
            y         = result.y;
            rcExclude = result.rcExclude;
            continue;
        }

        if (result.id > 0) onSelect(result.id, buttonId);
        break;
    }
}

// ── Menu bar (toolbar without arrows) ────────────────────────────────────
const MENUBAR_H = 22;
const menubar = window.createElement("TOOLBAR", {
    y: 0, height: MENUBAR_H, width: 800,
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
            showDropdown(e,
                (buttonId) => {
                    const menu = api.CreatePopupMenu();
                    if (buttonId === 10) {
                        menu.append(menuItem(1001, "新しいウィンドウ", iFolder));
                        menu.append({ separator: true });
                        menu.append(menuItem(1002, "終了", iDrive));
                    } else if (buttonId === 20) {
                        menu.append(menuItem(2001, "コピー",     iFolder));
                        menu.append(menuItem(2002, "貼り付け",   iFolder));
                        menu.append({ separator: true });
                        menu.append(menuItem(2003, "すべて選択", iFolder));
                    } else if (buttonId === 30) {
                        menu.append(menuItem(3001, "ツールバー", iFolder));
                        menu.append(menuItem(3002, "ステータスバー", iFolder));
                    } else if (buttonId === 40) {
                        menu.append(menuItem(4001, "バージョン情報", iFolder));
                    } else {
                        menu.destroy();
                        return null;
                    }
                    return menu;
                },
                (id, buttonId) => {
                    if (id === 1002) api.PostQuitMessage(0);
                    window.text = "Menu: " + id;
                }
            );
        }],
    }
});

// ── ToolBar ───────────────────────────────────────────────────────────────
const TOOLBAR_H = 26;
const tb = window.createElement("TOOLBAR", {
    y: MENUBAR_H, height: TOOLBAR_H, width: 800,
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
            if (e.buttonId === 3) exp.navigate("..");
            if (e.buttonId === 4) exp.navigate(0);
        }],
        dropdown: [(e) => {
            showDropdown(e,
                (buttonId) => {
                    const menu = api.CreatePopupMenu();
                    if (buttonId === 1) {
                        menu.append(menuItem(101, "← ドキュメント", iFolder));
                        menu.append(menuItem(102, "← デスクトップ", iFolder));
                        menu.append(menuItem(103, "← C:\\",         iDrive));
                    } else if (buttonId === 2) {
                        menu.append(menuItem(201, "→ ダウンロード", iFolder));
                        menu.append(menuItem(202, "→ ピクチャ",     iFolder));
                    } else {
                        menu.destroy();
                        return null;
                    }
                    return menu;
                },
                (id) => {
                    if (id === 101) exp.navigate("shell:Personal");
                    if (id === 102) exp.navigate("shell:Desktop");
                    if (id === 103) exp.navigate("C:\\");
                    if (id === 201) exp.navigate("shell:Downloads");
                    if (id === 202) exp.navigate("shell:My Pictures");
                }
            );
        }],
    }
});

// ── Address bar ──────────────────────────────────────────────────────────
const ADDR_Y = MENUBAR_H + TOOLBAR_H;
const ADDR_H = 22;
const edit = window.createElement("EDIT", {
    placeholder: "Path or URL",
    y: ADDR_Y, height: ADDR_H, width: 800,
    listeners: {
        keydown: (e) => {
            if (e.key === "Enter") {
                exp.navigate(e.target.text);
                return false;
            }
        },
    }
});

// ── Status bar ────────────────────────────────────────────────────────────
const STATUS_H = 20;
const stat = window.createElement("STATIC", {
    id: "stat",
    y: ADDR_Y + ADDR_H, height: STATUS_H, width: 800,
    listeners: {
        paint: [(e) => {
            const ps = {};
            const hdc = api.BeginPaint(e.hwnd, ps);
            api.DrawText({
                hdc,
                text: stat._text || "Ready",
                rc: ps.rcPaint,
                format: api.DT_LEFT | api.DT_VCENTER | api.DT_SINGLELINE,
            });
            api.EndPaint(e.hwnd, ps);
        }],
    }
});
stat._text = "Ready";

// ── ExplorerBrowser ───────────────────────────────────────────────────────
const EXP_Y = ADDR_Y + ADDR_H + STATUS_H;
const exp = window.createElement("ExplorerBrowser", {
    y: EXP_Y, height: 600 - EXP_Y, width: 800,
    listeners: {
        navigate: (e) => {
            const folder = e.target.currentFolder;
            edit.text   = folder.path;
            window.text = folder.name;
            stat._text  = folder.parsingPath;
        },
    }
});
