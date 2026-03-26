#pragma once

#include "resource.h"
#include <windows.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdio>
#include <CommCtrl.h>
#include <locale.h>
#include "api.h"
#include "common.h"
#include "darkmode.h"

//Closed function
typedef BOOL(WINAPI* LPFNRegenerateUserEnvironment)(LPVOID* lpEnvironment, BOOL bUpdate);

#pragma comment(lib, "qjs.lib")
#pragma comment(lib, "qjs-libc.lib")

