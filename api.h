#pragma once

extern "C" {
#include "quickjs.h"
}

JSModuleDef* js_init_module_api(JSContext* ctx, const char* module_name);