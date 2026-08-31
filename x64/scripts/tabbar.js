// tabbar.js - Custom multi-row tab bar
import * as api from "api";

const TAB_H        = 24;
const TAB_PAD      = 8;
const TAB_ICON_W   = 16;
const TAB_ICON_GAP = 4;
const CLOSE_W      = 18;

// Colors: 0xBBGGRR (Win32 COLORREF)
const DARK_CLR = {
    bg:         0x202020,
    tabBg:      0x2D2D2D,
    tabActive:  0x1E1E1E,
    tabHot:     0x3A3A3A,
    text:       0xDDDDDD,
    textActive: 0xFFFFFF,
    border:     0x555555,
    activeLine: 0xFF9E4A,
    closeHot:   0x4444EE,
};
const LIGHT_CLR = {
    bg:         0xF0F0F0,
    tabBg:      0xE1E1E1,
    tabActive:  0xFFFFFF,
    tabHot:     0xD0D0D0,
    text:       0x000000,
    textActive: 0x000000,
    border:     0xAAAAAA,
    activeLine: 0xD70078,
    closeHot:   0x0000CC,
};

export class TabBar {
    constructor(parentWindow, opts = {}) {
        this._tabs      = [];
        this._activeId  = null;
        this._hotIndex  = -1;
        this._hotClose  = -1;
        this._tabRects  = [];
        this._imageList = opts.imageList || null;
        this._dark      = opts.dark || false;
        this._CLR       = this._dark ? DARK_CLR : LIGHT_CLR;
        this._width     = opts.width  || 800;
        this._height    = TAB_H;
        this._y         = opts.y || 0;
        this._nextId    = 1;  // Auto-increment ID counter

        // Merge built-in listeners with user-supplied listeners
        const userListeners = opts.listeners || {};

        this._panel = parentWindow.createElement("PANEL", {
            y: this._y,
            width:  this._width,
            height: TAB_H,
            listeners: {
                paint:      [
                    (e) => this._onPaint(e),
                    ...(userListeners.paint || []),
                ],
                mousedown:  [
                    (e) => this._onMouseDown(e),
                    ...(Array.isArray(userListeners.mousedown)
                        ? userListeners.mousedown
                        : userListeners.mousedown ? [userListeners.mousedown] : []),
                ],
                mousemove:  [
                    (e) => this._onMouseMove(e),
                    ...(Array.isArray(userListeners.mousemove)
                        ? userListeners.mousemove
                        : userListeners.mousemove ? [userListeners.mousemove] : []),
                ],
                mouseleave: [
                    (e) => this._onMouseLeave(e),
                    ...(Array.isArray(userListeners.mouseleave)
                        ? userListeners.mouseleave
                        : userListeners.mouseleave ? [userListeners.mouseleave] : []),
                ],
                // Pass through any other user listeners unchanged
                ...Object.fromEntries(
                    Object.entries(userListeners)
                        .filter(([k]) => !['paint','mousedown','mousemove','mouseleave'].includes(k))
                ),
            }
        });

        // Expose onSelect / onClose via listeners.select / listeners.close
        this._selectListeners = Array.isArray(userListeners.select)
            ? userListeners.select
            : userListeners.select ? [userListeners.select] : [];
        this._closeListeners  = Array.isArray(userListeners.close)
            ? userListeners.close
            : userListeners.close  ? [userListeners.close]  : [];
    }

    // ── Public ────────────────────────────────────────────────────────────

    // addTab(label, iconIndex?, data?) -> id
    addTab(label, iconIndex = -1, data = null) {
        const id = this._nextId++;
        this._tabs.push({ id, label, iconIndex, data });
        if (this._activeId === null) this._activeId = id;
        this._layout();
        this._redraw();
        return id;  // Return assigned id so caller can reference this tab
    }

    removeTab(id) {
        const idx = this._tabs.findIndex(t => t.id === id);
        if (idx < 0) return;
        this._tabs.splice(idx, 1);
        if (this._activeId === id) {
            this._activeId = this._tabs.length
                ? this._tabs[Math.max(0, idx - 1)].id : null;
            this._fireSelect(this._activeId);
        }
        this._layout();
        this._redraw();
    }

    setActive(id) { this._activeId = id; this._redraw(); }

    setLabel(id, label) {
        const t = this._tabs.find(t => t.id === id);
        if (t) { t.label = label; this._layout(); this._redraw(); }
    }

    get hwnd()   { return this._panel.hwnd; }
    get height() { return this._height; }

    // ── Internal event firing ─────────────────────────────────────────────

    _fireSelect(id) {
        for (const fn of this._selectListeners) fn(id);
    }

    _fireClose(id) {
        for (const fn of this._closeListeners) fn(id);
    }

    // ── Layout ────────────────────────────────────────────────────────────

    _measureTab(tab, hdc) {
        const sz = api.GetTextExtent({ hdc, text: tab.label });
        let w = TAB_PAD * 2 + sz.width + CLOSE_W;
        if (tab.iconIndex >= 0) w += TAB_ICON_W + TAB_ICON_GAP;
        return Math.max(w, 60);
    }

    _layout() {
        const hdc  = api.GetDC(this._panel.hwnd);
        const font = api.GetWindowFont(this._panel.hwnd);
        const hOld = api.SelectFont(hdc, font);

        let rowIdx = 0, rowX = 0;
        this._tabRects = [];

        for (let i = 0; i < this._tabs.length; i++) {
            const w = this._measureTab(this._tabs[i], hdc);
            if (rowX + w > this._width && rowX > 0) {
                rowIdx++;
                rowX = 0;
            }
            this._tabRects.push({
                left:   rowX,
                top:    rowIdx * TAB_H,
                right:  rowX + w,
                bottom: rowIdx * TAB_H + TAB_H,
            });
            rowX += w;
        }

        api.SelectFont(hdc, hOld);
        api.ReleaseDC(this._panel.hwnd, hdc);

        this._height = (rowIdx + 1) * TAB_H;
    }

    // ── Paint ─────────────────────────────────────────────────────────────

    _onPaint(e) {
        const ps   = {};
        const hdc  = api.BeginPaint(e.hwnd, ps);
        const font = api.GetWindowFont(e.hwnd);
        const CLR  = this._CLR;
        const W = this._width, H = this._height;

        // Double buffering: draw into an offscreen DC then BitBlt to screen
        const memDC  = api.CreateCompatibleDC(hdc);
        const memBmp = api.CreateCompatibleBitmap(hdc, W, H);
        const hOldBmp = api.SelectObject(memDC, memBmp);
        const hOld    = api.SelectFont(memDC, font);

        api.FillRect({ hdc: memDC, rc: { left:0, top:0, right:W, bottom:H }, color: CLR.bg });
        api.SetBkMode(memDC, api.TRANSPARENT);

        for (let i = 0; i < this._tabs.length; i++) {
            this._drawTab(memDC, i);
        }

        api.DrawLine({ hdc: memDC, x1:0, y1:H-1, x2:W, y2:H-1, color: CLR.border });

        // Blit offscreen buffer to screen in one shot
        api.BitBlt(hdc, 0, 0, W, H, memDC, 0, 0);

        api.SelectFont(memDC, hOld);
        api.SelectObject(memDC, hOldBmp);
        api.DeleteObject(memBmp);
        api.DeleteDC(memDC);

        api.EndPaint(e.hwnd, ps);
    }

    _drawTab(hdc, i) {
        const tab = this._tabs[i];
        const rc  = this._tabRects[i];
        if (!rc) return;
        const CLR      = this._CLR;
        const isActive = tab.id === this._activeId;
        const isHot    = i === this._hotIndex;
        const isClose  = i === this._hotClose;

        let bg = isActive ? CLR.tabActive : isHot ? CLR.tabHot : CLR.tabBg;
        api.FillRect({ hdc, rc, color: bg });

        api.DrawLine({ hdc, x1:rc.left,    y1:rc.top,    x2:rc.right,   y2:rc.top,    color: CLR.border });
        api.DrawLine({ hdc, x1:rc.left,    y1:rc.top,    x2:rc.left,    y2:rc.bottom, color: CLR.border });
        api.DrawLine({ hdc, x1:rc.right-1, y1:rc.top,    x2:rc.right-1, y2:rc.bottom, color: CLR.border });

        if (isActive) {
            api.DrawLine({ hdc, x1:rc.left+1, y1:rc.top,   x2:rc.right-1, y2:rc.top,   color: CLR.activeLine });
            api.DrawLine({ hdc, x1:rc.left+1, y1:rc.top+1, x2:rc.right-1, y2:rc.top+1, color: CLR.activeLine });
        }

        let x = rc.left + TAB_PAD;

        if (tab.iconIndex >= 0 && this._imageList) {
            const iy = rc.top + Math.floor((TAB_H - TAB_ICON_W) / 2);
            api.ImageList_Draw({ imageList: this._imageList, index: tab.iconIndex, hdc, x, y: iy });
            x += TAB_ICON_W + TAB_ICON_GAP;
        }

        api.SetTextColor(hdc, isActive ? CLR.textActive : CLR.text);
        api.DrawText({ hdc, text: tab.label,
            rc: { left: x, top: rc.top, right: rc.right - CLOSE_W - 2, bottom: rc.bottom },
            format: api.DT_LEFT | api.DT_VCENTER | api.DT_SINGLELINE | api.DT_END_ELLIPSIS });

        api.SetTextColor(hdc, isClose ? CLR.closeHot : CLR.text);
        api.DrawText({ hdc, text: "×",
            rc: { left: rc.right - CLOSE_W - 2, top: rc.top, right: rc.right - 2, bottom: rc.bottom },
            format: api.DT_CENTER | api.DT_VCENTER | api.DT_SINGLELINE });
    }

    // ── Mouse ─────────────────────────────────────────────────────────────

    _hit(x, y) {
        for (let i = 0; i < this._tabRects.length; i++) {
            const r = this._tabRects[i];
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
                return { index: i, isClose: x >= r.right - CLOSE_W - 2 };
        }
        return { index: -1, isClose: false };
    }

    _onMouseDown(e) {
        const { index, isClose } = this._hit(e.clientX, e.clientY);
        if (index < 0) return;
        const tab = this._tabs[index];
        if (isClose) {
            // Fire close listeners; if none, remove by default
            if (this._closeListeners.length > 0) {
                this._fireClose(tab.id);
            } else {
                this.removeTab(tab.id);
            }
        } else {
            this._activeId = tab.id;
            this._redraw();
            this._fireSelect(tab.id);
        }
    }

    _onMouseMove(e) {
        const { index, isClose } = this._hit(e.clientX, e.clientY);
        const newClose = isClose ? index : -1;
        if (index !== this._hotIndex || newClose !== this._hotClose) {
            this._hotIndex = index;
            this._hotClose = newClose;
            this._redraw();
        }
    }

    _onMouseLeave(e) {
        this._hotIndex = -1; this._hotClose = -1; this._redraw();
    }

    _redraw() { api.InvalidateRect(this._panel.hwnd); }
}
