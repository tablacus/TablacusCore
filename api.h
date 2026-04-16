#pragma once

extern "C" {
#include "quickjs.h"
}
#include "darkmode.h"

JSModuleDef* js_init_module_api(JSContext* ctx, const char* module_name);