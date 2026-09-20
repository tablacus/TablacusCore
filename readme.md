使い方(main.js側)
js
import * as fs from "fs";

let text = fs.readFileSync("sample.txt", "utf-8");
console.log(text);

fs.writeFileSync("output.txt", "書き出したい内容");

実装の要点

対応エンコーディング: "utf8"/"utf-8"(既定)、"utf16le"/"ucs2"、"latin1"/"binary"/"ascii"
writeFileSync は第3引数に文字列のほか { encoding, flag } オブジェクトも受け付け、flag: "a" で追記もできます(Node.jsの options 形式に寄せています)
内部は CreateFileW/ReadFile/WriteFile で実装し、JS_NewStringUTF16 / JS_ToCStringLenUTF16(quickjs-ng提供API)を使ってUTF-16⇔バイト列の変換を正確に行っています
エラー時は Node.js 風に ENOENT: no such file or directory, open '...' のようなメッセージで例外を投げます
制約
このプロジェクトには Buffer/TypedArray が無いため、readFileSync はエンコーディング省略時も文字列(UTF-8扱い)を返します。Node.jsのようにバイナリのBufferを返すことはできません(必要であれば "binary" エンコーディングで1バイト=1文字コードとして読み書き可能です)。
