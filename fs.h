#pragma once
#pragma warning(push)
#pragma warning(disable: 4244)
extern "C" {
#include "quickjs.h"
}
#pragma warning(pop)

// Initializes the native module "fs" which provides readFileSync / writeFileSync
// similar to Node.js's fs module.
// From main.js, it is used as follows:
//
//   import * as fs from "fs";
//   const text = fs.readFileSync("sample.txt", "utf-8");
//   fs.writeFileSync("output.txt", "content to write");
//
JSModuleDef* js_init_module_fs(JSContext* ctx, const char* module_name);
