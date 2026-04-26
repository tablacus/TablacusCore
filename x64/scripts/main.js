import * as api from "api";

const window = api.CreateWindow({
    exStyle: 0,
    className: "TablacusCore",
    text: "Tablacus Core",
    x: 100,
    y: 100,
    width: 800,
    height: 600,

    listeners: {
        keydown: [
            (e) => { window.text = ["keyDown", e.key, e.keyCode].join(','); },
        ],
        keyup: [
            (e) => { window.text = ["keyup", e.key, e.keyCode].join(','); 
                return false;
            },
        ],
        mousedown: [
            (e) => { window.text = ["mouseDown", e.button, e.buttons].join(','); },
        ],
        mouseup: [
            (e) => { window.text = ["mouseUp", e.button, e.buttons].join(','); },
        ],
    }
});

window.show();

const edit = window.createElement("EDIT", {
    placeholder: "place holder",
    height: 30,
    width: 300,
    listeners: {
        keyup: [
            (e) => {
                const el = window.getElementById("btn");
                if (el) {
                    el.text = e.target.text;
                }
            },
        ],
        dblclick: (e) => {
            api.MessageBox(null, "Double Click", "OK", api.MB_YESNO, {
                    [api.IDYES]: "ABC",
                    [api.IDNO]: "DEF",
                },
            );
        }
    }
});

const btn = window.createElement("BUTTON", {
    text: "BUTTON",
    id: "btn",
    y:50,
    height: 30,
    width: 300,
    listeners: {
        keydown: [
            (e) => { e.target.text = ["keyDown", e.key, e.keyCode].join(','); },
        ],
        keyup: [
            (e) => {
                e.target.text = ["keyup", e.key, e.keyCode].join(',');
                return false;
            },
        ],
        mousedown: [
            (e) => { e.target.text = ["mouseDown", e.button, e.buttons].join(','); },
        ],
        mouseup: [
            (e) => { e.target.text = ["mouseUp", e.button, e.buttons].join(','); },
        ],
        click: (e) => { api.MessageBox(null, "Click", "OK", api.MB_YESNO, {
                [api.IDYES]: "IDYES",
                [api.IDNO]: "IDNO",
            },
        );}
    }
});

window.text = "Hello, World!";

/*
let r = api.MessageBox(null, ["テスト", window.hwnd].join(":") , "確認", api.MB_YESNO, {
    [api.IDYES]: "IDYES",
    [api.IDNO]: "IDNO",
   }
);
*/