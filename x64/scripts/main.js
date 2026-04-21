import * as api from "api";

const window = api.CreateWindow({
    exStyle: 0,
    className: "TablacusCore",
    title: "Tablacus Core",
    x: 100,
    y: 100,
    width: 800,
    height: 600,

    listeners: {
        keydown: [
            (e) => { window.title = ["keyDown", e.key, e.keyCode].join(','); },
        ],
        keyup: [
            (e) => { window.title = ["keyup", e.key, e.keyCode].join(','); 
                return false;
            },
        ],
        mousedown: [
            (e) => { window.title = ["mouseDown", e.button, e.buttons].join(','); },
        ],
    }
});

window.show();
window.title = "Hello, World!";
/*
let r = api.MessageBox(null, ["テスト", window.hwnd].join(":") , "確認", api.MB_YESNO, {
    [api.IDYES]: "IDYES",
    [api.IDNO]: "IDNO",
   }
);
*/