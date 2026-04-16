import * as api from "api";

const window = api.CreateWindow({
    exStyle: 0,
    className: "TablacusCore",
    title: "Tablacus Core",
    x: 100,
    y: 100,
    width: 800,
    height: 600
});

window.show();

let r = api.MessageBox(null, ["テスト", window.hwnd].join(":") , "確認", api.MB_YESNO, {
    [api.IDYES]: "IDYES",
    [api.IDNO]: "IDNO",
   }
);
