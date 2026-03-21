#pragma once
#ifndef UNICODE
#define UNICODE 1
#endif

#ifndef _UNICODE
#define _UNICODE 1
#endif

#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN

#include <Windows.h>
#include <Windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <pathcch.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <strsafe.h>
#include <process.h>
#include <algorithm>
#include <fstream>
#include "resource.h"

using namespace std;

#define WM_NOTIFYICON (WM_USER + 1)
#define WM_NOTIFYSTATE (WM_USER + 2)
#define WM_NOTIFYEXIT (WM_USER + 3)
#define IDM_ROOTFOLDER 11000

typedef unsigned(__stdcall *PTHREAD_START)(void *);

typedef enum
{
  APACHE,
  MARIA
} PIDID;

enum STARTUP
{
  NORMAL,
  MINIMIZED,
  SYSTRAY
};

typedef struct
{
  STARTUP StartUp;
  BOOL AutoStartApache;
  BOOL AutoStartMaria;
  BOOL TerminateAllOnQiut;
  BOOL ClearAllLogsOnQiut;
  BOOL HTTPs;
  BOOL AutoRestartOnChange;
  wstring Hdoc;
} OPTIONS;
