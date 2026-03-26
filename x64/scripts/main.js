import * as api from "api";

let r = api.MessageBox(null, "テスト" , "確認", api.MB_YESNO, {
    [api.IDYES]: "IDYES",
    [api.IDNO]: "IDNO",
   }
);
