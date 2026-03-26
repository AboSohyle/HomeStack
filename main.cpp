#include "main.h"

WCHAR RootPath[MAX_PATH];
HINSTANCE hInst = NULL;
HWND hWindow = NULL;
HANDLE hExitApache = NULL;
HANDLE hExitMaria = NULL;
DWORD ApachePID = 0;
DWORD MariaPID = 0;
HBRUSH hbrDark = NULL;
vector<wstring> SiteList;
WCHAR VersionStr[5][20];
OPTIONS Options = {NORMAL, FALSE, FALSE, TRUE, FALSE, TRUE, TRUE, L""};
BOOL ApacheOk, MariaOk, PhpOk, ComposerOk, PmaOk, NotifyAdded = FALSE;

BOOL CALLBACK ApplyThemeToChild(HWND hChild, LPARAM lParam)
{
  SetWindowTheme(hChild, L"DarkMode_Explorer", NULL);
  SendMessage(hChild, WM_THEMECHANGED, 0, 0);
  InvalidateRect(hChild, NULL, (BOOL)lParam);
  return TRUE;
}

VOID ApplyDarkThemeToApp(HWND hWnd)
{
  BOOL value = TRUE;
  typedef int(WINAPI * fnSetPreferredAppMode)(int);
  DwmSetWindowAttribute(hWnd, 20, &value, sizeof(value));
  HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (hUxtheme)
  {
    auto SetPreferredAppMode = (fnSetPreferredAppMode)(void *)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));

    auto FlushMenuThemes = (void(WINAPI *)())GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136));

    if (SetPreferredAppMode)
      SetPreferredAppMode(2);
    if (FlushMenuThemes)
      FlushMenuThemes();

    FreeLibrary(hUxtheme);
  }
  EnumChildWindows(hWnd, ApplyThemeToChild, (LPARAM)TRUE);
}

VOID BrowseForFolder(HWND hWnd)
{
  IFileOpenDialog *pFileOpen = nullptr;
  HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_PPV_ARGS(&pFileOpen));
  if (SUCCEEDED(hr))
  {
    DWORD dwOptions;
    if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions)))
    {
      pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);
      pFileOpen->SetTitle(L"Select Document Root Folder...");
    }

    if (!Options.Hdoc.empty())
    {
      IShellItem *pDefaultItem = nullptr;
      hr = SHCreateItemFromParsingName(Options.Hdoc.c_str(), NULL, IID_PPV_ARGS(&pDefaultItem));
      if (SUCCEEDED(hr))
      {
        pFileOpen->SetFolder(pDefaultItem);
        pDefaultItem->Release();
      }
    }
    hr = pFileOpen->Show(hWnd);
    if (SUCCEEDED(hr))
    {
      IShellItem *pItem = nullptr;
      hr = pFileOpen->GetResult(&pItem);
      if (SUCCEEDED(hr))
      {
        PWSTR pszFilePath = nullptr;
        hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

        if (SUCCEEDED(hr))
        {
          Options.Hdoc = pszFilePath;
          SendDlgItemMessage(hWnd, IDC_EDIT_DOC_ROOT, WM_SETTEXT, 0, (LPARAM)Options.Hdoc.c_str());
          CoTaskMemFree(pszFilePath);
        }
        pItem->Release();
      }
    }
    pFileOpen->Release();
  }
}

bool UpdatePhpConfig()
{
  const string filename = ".\\config\\php.ini";

  filesystem::path path_obj(RootPath);
  string root = path_obj.string();

  ifstream inFile(filename);
  if (!inFile.is_open())
    return false;

  vector<string> buffer;
  buffer.reserve(200);
  string line;

  while (getline(inFile, line))
  {
    if (line.empty() || line[0] == ';')
    {
      buffer.push_back(move(line));
      continue;
    }
    if (line.find("error_log") != string::npos)
      line = "error_log=\"" + root + "\\logs\\php_error.log\"";

    else if (line.find("include_path") != string::npos)
      line = "include_path=\".;" + root + "\\php\\PEAR\"";

    else if (line.find("extension_dir") != string::npos)
      line = "extension_dir=\"" + root + "\\php\\ext\"";

    else if (line.find("upload_tmp_dir") != string::npos)
      line = "upload_tmp_dir=\"" + root + "\\tmp\"";
    else if (line.find("session.save_path") != string::npos)
      line = "session.save_path=\"" + root + "\\tmp\"";

    buffer.push_back(move(line));
  }
  inFile.close();

  ofstream outFile(filename, ios::trunc);
  ofstream out2File(".\\php\\php.ini", ios::trunc);
  if (!outFile.is_open() || !out2File.is_open())
    return false;

  for (const auto &l : buffer)
  {
    outFile << l << "\n";
    out2File << l << "\n";
  }

  outFile.close();
  out2File.close();

  return true;
}

bool UpdateApacheConfig()
{
  const string filename = ".\\config\\apache.conf";

  filesystem::path path_obj(RootPath);
  string root = path_obj.string();

  replace(root.begin(), root.end(), '\\', '/');

  ifstream inFile(filename);
  if (!inFile.is_open())
    return false;

  vector<string> lines;
  string line;

  while (getline(inFile, line))
  {
    if (line.find("Define MYROOT") == 0)
      line = "Define MYROOT \"" + root + "\"";

    lines.push_back(line);
  }
  inFile.close();

  ofstream outFile(filename, ios::trunc);
  if (!outFile.is_open())
    return false;

  for (const auto &l : lines)
    outFile << l << "\n";

  return true;
}

bool UpdateUserPathVar()
{
  if (!ApacheOk || !PhpOk)
    return false;

  HKEY hKey;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_READ | KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
    return false;

  DWORD dataSize = 0;
  if (RegQueryValueExW(hKey, L"Path", NULL, NULL, NULL, &dataSize) != ERROR_SUCCESS)
  {
    RegCloseKey(hKey);
    return false;
  }

  vector<wchar_t> buffer(dataSize / sizeof(wchar_t) + 1);
  if (RegQueryValueExW(hKey, L"Path", NULL, NULL, (LPBYTE)buffer.data(), &dataSize) != ERROR_SUCCESS)
  {
    RegCloseKey(hKey);
    return false;
  }

  wstring pathStr(buffer.data());
  wstringstream ss(pathStr);
  vector<wstring> segments;
  wstring segment;
  bool php = false, composer = false;
  wstring root = RootPath;
  replace(root.begin(), root.end(), L'/', L'\\');

  wstring phpPath = root + L"\\php";
  wstring composerPath = root + L"\\composer";

  while (getline(ss, segment, L';'))
  {
    if (segment.empty())
      continue;

    wstring lowerLine = segment;
    transform(lowerLine.begin(), lowerLine.end(), lowerLine.begin(), ::towlower);

    if (lowerLine.find(L"homestack\\php") != wstring::npos)
    {
      segment = phpPath;
      php = true;
    }
    else if (lowerLine.find(L"homestack\\composer") != wstring::npos)
    {
      segment = composerPath;
      composer = true;
    }

    segments.push_back(segment);
  }

  if (!php)
    segments.push_back(phpPath);
  if (!composer)
    segments.push_back(composerPath);

  // 5. Rebuild Path string
  wstring updatedPath;
  for (size_t i = 0; i < segments.size(); ++i)
  {
    updatedPath += segments[i];
    if (i < segments.size() - 1)
      updatedPath += L";";
  }

  // 6. Write back as REG_EXPAND_SZ (critical for Path variables)
  LSTATUS status = RegSetValueExW(hKey, L"Path", 0, REG_EXPAND_SZ,
                                  (LPBYTE)updatedPath.c_str(), (DWORD)((updatedPath.length() + 1) * sizeof(wchar_t)));

  RegCloseKey(hKey);
  SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 2000, NULL);

  return (status == ERROR_SUCCESS);
}

BOOL FileExists(LPCWSTR file)
{
  DWORD attrib = GetFileAttributes(file);
  return (attrib != INVALID_FILE_ATTRIBUTES && attrib != FILE_ATTRIBUTE_DIRECTORY);
}

VOID NewJob(PTHREAD_START routine)
{
  HANDLE hand = (HANDLE)_beginthreadex(NULL, 0, routine, NULL, 0, NULL);
  if (hand)
  {
    CloseHandle(hand);
  }
}

VOID InitPathes()
{
  GetModuleFileName(NULL, RootPath, MAX_PATH);
  PathCchRemoveFileSpec(RootPath, MAX_PATH);

  WCHAR file[MAX_PATH];
  StringCchPrintf(file, MAX_PATH, L"%s\\apache\\bin\\httpd.exe", RootPath);
  ApacheOk = FileExists(file);

  StringCchPrintf(file, MAX_PATH, L"%s\\mysql\\bin\\mysqld.exe", RootPath);
  MariaOk = FileExists(file);

  StringCchPrintf(file, MAX_PATH, L"%s\\php\\php.exe", RootPath);
  PhpOk = FileExists(file);

  StringCchPrintf(file, MAX_PATH, L"%s\\composer\\composer.bat", RootPath);
  ComposerOk = FileExists(file);

  StringCchPrintf(file, MAX_PATH, L"%s\\phpMyAdmin\\index.php", RootPath);
  PmaOk = FileExists(file);
}

VOID ClearLogsAndTmpFiles()
{
  WCHAR targetPath[MAX_PATH];

  const WCHAR *logFiles[] = {
      L"logs\\error.log",
      L"logs\\access.log",
      L"logs\\ssl_request.log",
      L"logs\\mysql_error.log",
      L"logs\\php_error.log"};

  // 1. Delete Log Files
  for (const auto &logPath : logFiles)
  {
    if (SUCCEEDED(StringCchPrintf(targetPath, MAX_PATH, L"%s\\%s", RootPath, logPath)))
      DeleteFile(targetPath);
  }

  // 2. Clear tmp directory
  WCHAR tmpSearchPath[MAX_PATH];
  StringCchPrintf(tmpSearchPath, MAX_PATH, L"%s\\tmp\\*", RootPath);

  WIN32_FIND_DATAW ffd;
  HANDLE hFind = FindFirstFile(tmpSearchPath, &ffd);

  if (hFind != INVALID_HANDLE_VALUE)
  {
    do
    {
      if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
      {
        if (SUCCEEDED(StringCchPrintf(targetPath, MAX_PATH, L"%s\\tmp\\%s", RootPath, ffd.cFileName)))
          DeleteFile(targetPath);
      }
    } while (FindNextFile(hFind, &ffd) != 0);
    FindClose(hFind);
  }
}

VOID AppendUserEnvVariable(LPCWSTR name, LPCWSTR newValue)
{
  HKEY hKey;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Environment", 0, NULL,
                      REG_OPTION_NON_VOLATILE, KEY_READ | KEY_SET_VALUE, NULL, &hKey, NULL) != ERROR_SUCCESS)
  {
    return;
  }

  DWORD type = REG_SZ;
  DWORD bytesRequired = 0;

  // 1. Get required buffer size in bytes
  LONG result = RegQueryValueExW(hKey, name, NULL, &type, NULL, &bytesRequired);

  WCHAR *finalBuffer = NULL;
  DWORD newValueLen = lstrlenW(newValue);

  if (result == ERROR_SUCCESS)
  {
    // Calculate: Existing Bytes + Semicolon + NewValue + Null
    DWORD totalBytes = bytesRequired + (DWORD)((newValueLen + 2) * sizeof(WCHAR));
    finalBuffer = (WCHAR *)malloc(totalBytes);

    if (finalBuffer)
    {
      RegQueryValueExW(hKey, name, NULL, &type, (LPBYTE)finalBuffer, &bytesRequired);

      DWORD currentLen = lstrlenW(finalBuffer);
      if (currentLen > 0 && finalBuffer[currentLen - 1] != L';')
      {
        lstrcatW(finalBuffer, L";");
      }
      lstrcatW(finalBuffer, newValue);
    }
  }
  else
  {
    // Variable doesn't exist: Allocate for NewValue + Null
    DWORD totalBytes = (newValueLen + 1) * sizeof(WCHAR);
    finalBuffer = new WCHAR[totalBytes];
    if (finalBuffer)
    {
      lstrcpyW(finalBuffer, newValue);
      type = REG_SZ; // Default for new variables
    }
  }

  // 2. Save and Notify
  if (finalBuffer)
  {
    RegSetValueExW(hKey, name, 0, type, (LPBYTE)finalBuffer, (DWORD)((lstrlenW(finalBuffer) + 1) * sizeof(WCHAR)));
    delete[] finalBuffer;

    DWORD_PTR dwResult;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment", SMTO_ABORTIFHUNG, 5000, &dwResult);
  }

  RegCloseKey(hKey);
}

BOOL StartDefaultEditor(LPCWSTR file)
{
  WCHAR Cmd[MAX_PATH];
  StringCchPrintf(Cmd, MAX_PATH, L"%s\\%s", RootPath, file);

  STARTUPINFO si;
  ZeroMemory(&si, sizeof(STARTUPINFO));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_SHOWNORMAL;

  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

  WCHAR lpCommandLine[MAX_PATH];
  StringCchPrintf(lpCommandLine, MAX_PATH, L"notepad /a \"%s\"", Cmd);

  SetLastError(0);
  if (CreateProcess(NULL, lpCommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
  {
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return TRUE;
  }
  return FALSE;
}

BOOL LoadSiteList()
{
  DWORD attrib = GetFileAttributes(Options.Hdoc.c_str());
  if (attrib == INVALID_FILE_ATTRIBUTES || attrib != FILE_ATTRIBUTE_DIRECTORY)
    return FALSE;

  SiteList.clear();

  WIN32_FIND_DATA ffd;
  WCHAR searchPath[MAX_PATH];

  if (FAILED(StringCchPrintf(searchPath, MAX_PATH, L"%s\\*", Options.Hdoc.c_str())))
    return FALSE;

  HANDLE hFind = FindFirstFile(searchPath, &ffd);
  if (hFind == INVALID_HANDLE_VALUE)
    return FALSE;

  do
  {
    if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && ffd.cFileName[0] != '.')
    {
      SiteList.emplace_back(ffd.cFileName);
    }
  } while (FindNextFile(hFind, &ffd));

  FindClose(hFind);
  return TRUE;
}

VOID NotifyIcon(BOOL show)
{
  NOTIFYICONDATA nid = {};
  nid.cbSize = sizeof(NOTIFYICONDATA);
  nid.uID = 1010;
  nid.hWnd = hWindow;
  nid.uVersion = 4;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid.uCallbackMessage = WM_NOTIFYICON;

  if (!show)
  {
    Shell_NotifyIcon(NIM_DELETE, &nid);
    NotifyAdded = FALSE;
    return;
  }

  if (NotifyAdded)
    Shell_NotifyIcon(NIM_DELETE, &nid);

  wstring msg;
  if (ApachePID && MariaPID)
    msg = L"✔️ Apache running\n✔️ Mariadb running";
  else if (ApachePID && !MariaPID)
    msg = L"✔️ Apache running\n✖️ Mariadb stopped";
  else if (!ApachePID && MariaPID)
    msg = L"✖️ Apache stopped\n✔️ Mariadb running";
  else
    msg = L"✖️ Apache stopped\n✖️ Mariadb stopped";

  UINT icn = (ApachePID && MariaPID) ? IDI_APPICON : (ApachePID || MariaPID ? IDI_APPICON_ONOFF : IDI_APPICON_OFF);
  nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(icn));

  StringCchPrintf(nid.szTip, ARRAYSIZE(nid.szTip), L"Home Stack v1.0\n\n%s", msg.c_str());
  Shell_NotifyIcon(NIM_ADD, &nid);
  NotifyAdded = TRUE;
}

VOID CALLBACK ExitProcessNotify(PVOID lParam, BOOLEAN)
{
  PIDID id = *((PIDID *)lParam);
  SendMessage(hWindow, WM_NOTIFYEXIT, 0, (LPARAM)id);
  delete (PIDID *)lParam;
}

VOID RegisterServiceMonitor(DWORD pid, PIDID type)
{
  HANDLE hProc = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (hProc)
  {
    PIDID *id = new PIDID(type);
    RegisterWaitForSingleObject(&hExitMaria, hProc, ExitProcessNotify, id, INFINITE, WT_EXECUTEONLYONCE);
    CloseHandle(hProc);
  }
}

VOID FindOnlineServices()
{
  if (!ApacheOk && !MariaOk && !PhpOk)
    return;

  HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnapshot == INVALID_HANDLE_VALUE)
    return;

  PROCESSENTRY32 pe32;
  pe32.dwSize = sizeof(PROCESSENTRY32);
  bool apacheFound = false, mariaFound = false;

  if (Process32First(hSnapshot, &pe32))
  {
    do
    {
      if (!apacheFound && _wcsicmp(pe32.szExeFile, L"httpd.exe") == 0)
      {
        ApachePID = pe32.th32ProcessID;
        RegisterServiceMonitor(ApachePID, APACHE);
        apacheFound = true;
      }
      else if (!mariaFound && _wcsicmp(pe32.szExeFile, L"mysqld.exe") == 0)
      {
        MariaPID = pe32.th32ProcessID;
        RegisterServiceMonitor(MariaPID, MARIA);
        mariaFound = true;
      }
    } while ((!apacheFound || !mariaFound) && Process32Next(hSnapshot, &pe32));
  }
  CloseHandle(hSnapshot);
}

VOID OptionsGet()
{
  WCHAR lpCommandline[MAX_PATH];
  StringCchPrintf(lpCommandline, MAX_PATH, L"%s\\config\\self.ini", RootPath);

  if (!FileExists(lpCommandline))
    return;

  WCHAR out_buffer[MAX_PATH];
  GetPrivateProfileString(L"Options", L"StartUp", L"0", out_buffer, 2, lpCommandline);
  Options.StartUp = (STARTUP)wcstol(out_buffer, NULL, 10);
  GetPrivateProfileString(L"Options", L"AutoStartApache", L"0", out_buffer, 2, lpCommandline);
  Options.AutoStartApache = (BOOL)wcstol(out_buffer, NULL, 10);
  GetPrivateProfileString(L"Options", L"AutoStartMaria", L"0", out_buffer, 2, lpCommandline);
  Options.AutoStartMaria = (BOOL)wcstol(out_buffer, NULL, 10);
  GetPrivateProfileString(L"Options", L"TerminateAllOnQiut", L"1", out_buffer, 2, lpCommandline);
  Options.TerminateAllOnQiut = (BOOL)wcstol(out_buffer, NULL, 10);
  GetPrivateProfileString(L"Options", L"ClearAllLogsOnQiut", L"0", out_buffer, 2, lpCommandline);
  Options.ClearAllLogsOnQiut = (BOOL)wcstol(out_buffer, NULL, 10);
  GetPrivateProfileString(L"Options", L"HTTPs", L"1", out_buffer, 2, lpCommandline);
  Options.HTTPs = (BOOL)wcstol(out_buffer, NULL, 10);
  GetPrivateProfileString(L"Options", L"Hdoc", L"", out_buffer, MAX_PATH, lpCommandline);
  Options.Hdoc = out_buffer;
}

VOID OptionsSet()
{
  WCHAR lpCommandline[MAX_PATH];
  StringCchPrintf(lpCommandline, MAX_PATH, L"%s\\config\\self.ini", RootPath);

  WCHAR set[2] = L"0";
  if (Options.StartUp == NORMAL)
    set[0] = L'0';
  else if (Options.StartUp == MINIMIZED)
    set[0] = L'1';
  else
    set[0] = L'2';

  WritePrivateProfileStringW(L"Options", L"StartUp", set, lpCommandline);
  set[0] = (Options.AutoStartApache) ? L'1' : L'0';
  WritePrivateProfileStringW(L"Options", L"AutoStartApache", set, lpCommandline);
  set[0] = (Options.AutoStartMaria) ? L'1' : L'0';
  WritePrivateProfileStringW(L"Options", L"AutoStartMaria", set, lpCommandline);
  set[0] = (Options.TerminateAllOnQiut) ? L'1' : L'0';
  WritePrivateProfileStringW(L"Options", L"TerminateAllOnQiut", set, lpCommandline);
  set[0] = (Options.ClearAllLogsOnQiut) ? L'1' : L'0';
  WritePrivateProfileStringW(L"Options", L"ClearAllLogsOnQiut", set, lpCommandline);
  set[0] = (Options.HTTPs) ? L'1' : L'0';
  WritePrivateProfileStringW(L"Options", L"HTTPs", set, lpCommandline);
  WritePrivateProfileStringW(L"Options", L"Hdoc", Options.Hdoc.c_str(), lpCommandline);
}

VOID MenuSetItemText(HMENU hMenu, UINT uID, LPCWSTR newText)
{
  MENUITEMINFO mii;
  ZeroMemory(&mii, sizeof(MENUITEMINFO));
  mii.cbSize = sizeof(MENUITEMINFO);
  mii.fMask = MIIM_STRING;
  mii.dwTypeData = (LPWSTR)newText;
  SetMenuItemInfo(hMenu, uID, FALSE, &mii);
}

VOID MenuUpdateSiteList(HMENU hPop)
{
  int SiteCount = (int)SiteList.size();
  WCHAR refresh[25];
  StringCchPrintf(refresh, 25, L"Refresh [%d]", SiteCount);
  MenuSetItemText(hPop, IDC_REFRESH, refresh);
  EnableMenuItem(hPop, IDC_REFRESH, MF_BYCOMMAND | ((SiteCount > 0) ? MF_ENABLED : MF_GRAYED));

  for (int i = 0; i < SiteCount; i++)
  {
    UINT id = IDM_ROOTFOLDER + i;
    AppendMenu(hPop, MF_STRING, id, SiteList[i].c_str());
    if (!ApachePID)
      EnableMenuItem(hPop, id, MF_BYCOMMAND | MF_GRAYED);
  }
}

DWORD GetConsoleOutput(LPCWSTR Cmd, LPWSTR Out, int length)
{
  HANDLE hReadPipe = NULL, hWritePipe = NULL;
  SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};

  if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
    return FALSE;

  SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFO si;
  ZeroMemory(&si, sizeof(STARTUPINFO));
  si.cb = sizeof(si);
  si.hStdError = hWritePipe;
  si.hStdOutput = hWritePipe;
  si.dwFlags |= STARTF_USESTDHANDLES;

  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

  WCHAR lpCommandLine[MAX_PATH];
  StringCchPrintf(lpCommandLine, MAX_PATH, L"%s\\%s", RootPath, Cmd);

  if (!CreateProcess(NULL, lpCommandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
  {
    CloseHandle(hReadPipe);
    CloseHandle(hWritePipe);
    return FALSE;
  }

  CloseHandle(hWritePipe);

  DWORD dwBytesRead = 0;
  DWORD totalRead = 0;
  CHAR output[1024];

  while (totalRead < (DWORD)length - 1)
  {
    if (!ReadFile(hReadPipe, output + totalRead, length - 1 - totalRead, &dwBytesRead, NULL) || dwBytesRead == 0)
      break;
    totalRead += dwBytesRead;
  }
  output[totalRead] = '\0';

  WaitForSingleObject(pi.hProcess, 3000);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  CloseHandle(hReadPipe);

  if (!(totalRead > 0))
    return 0;

  int sizeNeeded = MultiByteToWideChar(GetConsoleOutputCP(), 0, output, -1, NULL, 0);

  if (sizeNeeded == 0 || sizeNeeded > length)
    return 0;

  if (MultiByteToWideChar(GetConsoleOutputCP(), 0, output, -1, Out, sizeNeeded) == 0)
    return 0;

  return totalRead;
}

UINT CALLBACK LogFileMonitorThread(LPVOID)
{
  if (!ApacheOk || !MariaOk || !PhpOk)
    return 1;

  wstring logFolder = wstring(RootPath) + L"\\logs";

  HANDLE hNotify = FindFirstChangeNotification(logFolder.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);
  if (hNotify == INVALID_HANDLE_VALUE)
    return 1;

  while (true)
  {
    if (WaitForSingleObject(hNotify, INFINITE) == WAIT_OBJECT_0)
    {
      PostMessage(hWindow, WM_NOTIFYLOGFILES, 0, 0);
      if (!FindNextChangeNotification(hNotify))
        break;
    }
  }
  FindCloseChangeNotification(hNotify);
  return 0;
}

UINT CALLBACK StartApacheThread(LPVOID)
{
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;

  ZeroMemory(&pi, sizeof(pi));

  WCHAR Cmd[1024];

  StringCchPrintf(Cmd, 1024, L"%s\\apache\\bin\\httpd.exe -f %s\\config\\apache.conf", RootPath, RootPath);
  if (CreateProcess(NULL, Cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
  {
    ApachePID = pi.dwProcessId;
    PIDID *idd = new PIDID;
    *idd = APACHE;
    RegisterWaitForSingleObject(&hExitApache, pi.hProcess, ExitProcessNotify, idd, INFINITE, WT_EXECUTEONLYONCE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    PostMessage(hWindow, WM_NOTIFYSTATE, 0, 0);
  }
  return 0;
}

UINT CALLBACK StartMariaThread(LPVOID)
{
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;

  ZeroMemory(&pi, sizeof(pi));

  WCHAR Cmd[1024];

  StringCchPrintf(
      Cmd,
      1024,
      L"\"%s\\mysql\\bin\\mysqld.exe\" --defaults-file=\"%s\\config\\mysql.ini\" --basedir=\"%s\\mysql\" --datadir=\"%s\\mysql\\data\" --tmpdir=\"%s\\tmp\" --plugin-dir=\"%s\\mysql\\lib\\plugin\" --innodb-data-home-dir=\"%s\\mysql\\data\" --innodb-log-group-home-dir=\"%s\\mysql\\data\" --log-error=\"%s\\logs\\mysql_error.log\" --standalone",
      RootPath, RootPath, RootPath, RootPath, RootPath, RootPath, RootPath, RootPath, RootPath);

  if (CreateProcess(NULL, Cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
  {
    MariaPID = pi.dwProcessId;
    PIDID *idd = new PIDID;
    *idd = MARIA;
    RegisterWaitForSingleObject(&hExitMaria, pi.hProcess, ExitProcessNotify, idd, INFINITE, WT_EXECUTEONLYONCE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    PostMessage(hWindow, WM_NOTIFYSTATE, 0, 0);
  }
  return 0;
}

UINT CALLBACK KillMariaThread(LPVOID)
{
  if (!MariaPID)
    return -1;

  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;

  ZeroMemory(&pi, sizeof(pi));

  WCHAR Cmd[1024];

  StringCchPrintf(Cmd, 1024, L"%s\\mysql\\bin\\mariadb-admin.exe shutdown -u root", RootPath);
  if (!CreateProcess(NULL, Cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    goto end;

  WaitForSingleObject(pi.hProcess, 2000);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  PostMessage(hWindow, WM_NOTIFYSTATE, 0, 0);

end:
  return 0;
}

UINT CALLBACK KillApacheThread(LPVOID)
{
  DWORD ret = -1;

  HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, ApachePID);

  if (hProcess)
  {
    if (TerminateProcess(hProcess, 0) != 0)
    {
      WaitForSingleObject(hProcess, 3000);
      WCHAR delfile[MAX_PATH];
      StringCchPrintf(delfile, MAX_PATH, L"%s\\apache\\logs\\httpd.pid", RootPath);
      DeleteFile(delfile);

      ret = 0;
    }
    CloseHandle(hProcess);
  }
  return ret;
}

UINT CALLBACK RestartApacheThread(LPVOID)
{
  DWORD ret = -1;
  if (0 == KillApacheThread(0))
  {
    Sleep(1500);
    ret = StartApacheThread(0);
  }
  return ret;
}

UINT CALLBACK RestartMariaThread(LPVOID)
{
  DWORD ret = -1;
  if (0 == KillMariaThread(0))
  {
    Sleep(1500);
    ret = StartMariaThread(0);
  }
  return ret;
}

UINT CALLBACK GetVersionsThread(LPVOID)
{
  WCHAR buf[1024] = {};
  WCHAR *Ptr = 0, *end = 0;
  HWND hCtrl;
  DWORD bytesRead = 0;

  ZeroMemory(VersionStr, 5 * 20 * sizeof(wchar_t));

  if (ApacheOk)
  {
    GetConsoleOutput(L"apache\\bin\\httpd.exe -v", buf, 1024);
    if ((Ptr = StrStr(buf, L"Apache/")))
    {
      Ptr += 7;
      end = StrStr(Ptr, L" ");
      *end = '\0';
      hCtrl = GetDlgItem(hWindow, IDC_APACHE_V);
      SetWindowText(hCtrl, Ptr);
      StringCbCopy(VersionStr[0], 20, Ptr);
    }
  }

  if (PhpOk)
  {
    GetConsoleOutput(L"php\\php.exe -v", buf, 1024);
    if ((Ptr = StrStr(buf, L"PHP ")))
    {
      Ptr += 4;
      end = StrStr(Ptr, L" ");
      *end = '\0';
      hCtrl = GetDlgItem(hWindow, IDC_PHP_V);
      SetWindowText(hCtrl, Ptr);
      StringCbCopy(VersionStr[1], 20, Ptr);
    }
  }

  if (MariaOk)
  {
    GetConsoleOutput(L"mysql\\bin\\mariadb.exe -V", buf, 1024);
    if ((Ptr = StrStr(buf, L"from ")))
    {
      Ptr += 5;
      end = StrStr(Ptr, L"-");
      *end = '\0';
      hCtrl = GetDlgItem(hWindow, IDC_MARIA_V);
      SetWindowText(hCtrl, Ptr);
      StringCbCopy(VersionStr[2], 20, Ptr);
    }
  }

  if (ComposerOk)
  {
    GetConsoleOutput(L"composer\\composer.bat -V", buf, 1024);
    if ((Ptr = StrStr(buf, L"version ")))
    {
      Ptr += 8;
      end = StrStr(Ptr, L" ");
      *end = '\0';
      hCtrl = GetDlgItem(hWindow, IDC_COMPOSER_V);
      SetWindowText(hCtrl, Ptr);
      StringCbCopy(VersionStr[3], 20, Ptr);
    }
  }

  if (PmaOk)
  {
    StringCchPrintf(buf, MAX_PATH, L"%s\\phpMyAdmin\\package.json", RootPath);

    HANDLE fil = CreateFile(buf, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (fil == INVALID_HANDLE_VALUE)
      return -1;

    CHAR buffer[1024];
    if (!ReadFile(fil, buffer, 1023, &bytesRead, NULL))
    {
      CloseHandle(fil);
      return -1;
    }
    buffer[bytesRead] = '\0';
    CloseHandle(fil);
    CHAR *PtrA, *endA;
    if ((PtrA = StrStrA(buffer, "version\": \"")))
    {
      PtrA += 11;
      endA = StrStrA(PtrA, "\"");
      *endA = '\0';
      hCtrl = GetDlgItem(hWindow, IDC_PMA_V);
      SetWindowTextA(hCtrl, PtrA);
      GetWindowText(hCtrl, VersionStr[4], 20);
    }
  }

  return 0;
}

INT_PTR CALLBACK AcknowledgeProc(HWND hPage, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
  case WM_INITDIALOG:
  {
    ApplyDarkThemeToApp(hPage);
    HFONT fn = (HFONT)SendMessage(hPage, WM_GETFONT, 0, 0);
    HWND h1, h2, h3, h4, h5;
    h1 = CreateWindow(WC_LINK,
                      L"The <A HREF=\"https://www.apachelounge.com/\">Apache Software Foundation:</A> For providing the Apache HTTP Server, which serves as the reliable web gateway for the controller's user interface.",
                      WS_VISIBLE | WS_CHILD, 87, 64, 460, 32, hPage, (HMENU)1200, hInst, NULL);
    h2 = CreateWindow(WC_LINK,
                      L"The <A HREF=\"https://www.apachelounge.com/\">MariaDB Foundation:</A> For MariaDB, which provides the high-performance database used to log device states and home telemetry data.",
                      WS_VISIBLE | WS_CHILD, 87, 120, 460, 32, hPage, (HMENU)1201, hInst, NULL);
    h3 = CreateWindow(WC_LINK,
                      L"The <A HREF=\"https://www.php.net/\">PHP Group:</A> For the PHP scripting language, used to develop the core logic and automation scripts of the stack.",
                      WS_VISIBLE | WS_CHILD, 87, 175, 460, 32, hPage, (HMENU)1202, hInst, NULL);
    h4 = CreateWindow(WC_LINK,
                      L"The <A HREF=\"https://getcomposer.org/\">Composer Team:</A> For Composer, the dependency manager that streamlined the integration of various PHP libraries used in this project.",
                      WS_VISIBLE | WS_CHILD, 87, 230, 460, 32, hPage, (HMENU)1203, hInst, NULL);
    h5 = CreateWindow(WC_LINK,
                      L"The <A HREF=\"https://www.phpmyadmin.net/\">phpMyAdmin Project:</A> For phpMyAdmin, which served as the primary interface for managing and debugging the system's database.",
                      WS_VISIBLE | WS_CHILD, 87, 282, 460, 32, hPage, (HMENU)1204, hInst, NULL);

    SendMessage(h1, WM_SETFONT, (WPARAM)fn, 1);
    SendMessage(h2, WM_SETFONT, (WPARAM)fn, 1);
    SendMessage(h3, WM_SETFONT, (WPARAM)fn, 1);
    SendMessage(h4, WM_SETFONT, (WPARAM)fn, 1);
    SendMessage(h5, WM_SETFONT, (WPARAM)fn, 1);

    return (INT_PTR)TRUE;
  }

  case WM_NOTIFY:
  {
    if (((LPNMHDR)lParam)->code == NM_CLICK)
    {
      PNMLINK pNMLink = (PNMLINK)lParam;
      ShellExecute(NULL, L"open", pNMLink->item.szUrl, NULL, NULL, SW_SHOWNORMAL);
    }
    return (INT_PTR)FALSE;
  }

  case WM_CLOSE:
  {
    EndDialog(hPage, 0);
    return (INT_PTR)TRUE;
  }

  case WM_COMMAND:
  {
    if (LOWORD(wParam) == IDOK)
    {
      EndDialog(hPage, 0);
      return (INT_PTR)TRUE;
    }
    break;
  }

  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLORDLG:
  case WM_CTLCOLORBTN:
  {
    HDC hdc = (HDC)wParam;
    SetTextColor(hdc, RGB(255, 255, 255));
    SetBkMode(hdc, TRANSPARENT);
    return (LRESULT)hbrDark;
  }
  }
  return (INT_PTR)FALSE;
}

INT_PTR CALLBACK AboutProc(HWND hPage, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
  case WM_INITDIALOG:
  {
    ApplyDarkThemeToApp(hPage);
    HWND h1 = CreateWindow(WC_LINK,
                           L"<A HREF=\"https://github.com/AboSohyle/HomeStack/\">repository</A>",
                           WS_VISIBLE | WS_CHILD, 388, 45, 60, 16, hPage, (HMENU)1204, hInst, NULL);
    HWND h2 = CreateWindow(WC_LINK,
                           L"Built with <A HREF=\"https://winlibs.com/\">winlibs</A>.",
                           WS_VISIBLE | WS_CHILD, 14, 238, 278, 16, hPage, (HMENU)1204, hInst, NULL);

    HFONT fn = (HFONT)SendMessage(hPage, WM_GETFONT, 0, 0);
    SendMessage(h1, WM_SETFONT, (WPARAM)fn, 1);
    SendMessage(h2, WM_SETFONT, (WPARAM)fn, 1);
    return (INT_PTR)TRUE;
  }

  case WM_NOTIFY:
  {
    if (((LPNMHDR)lParam)->code == NM_CLICK)
    {
      PNMLINK pNMLink = (PNMLINK)lParam;
      ShellExecute(NULL, L"open", pNMLink->item.szUrl, NULL, NULL, SW_SHOWNORMAL);
    }
    return (INT_PTR)FALSE;
  }

  case WM_CLOSE:
  {
    EndDialog(hPage, 0);
    return (INT_PTR)TRUE;
  }

  case WM_COMMAND:
  {
    if (LOWORD(wParam) == IDOK)
    {
      EndDialog(hPage, 0);
      return (INT_PTR)TRUE;
    }
    break;
  }

  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLORDLG:
  case WM_CTLCOLORBTN:
  {
    HDC hdc = (HDC)wParam;
    SetTextColor(hdc, RGB(255, 255, 255));
    SetBkMode(hdc, TRANSPARENT);
    return (LRESULT)hbrDark;
  }
  }
  return (INT_PTR)FALSE;
}

INT_PTR CALLBACK MainDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
  case WM_INITDIALOG:
  {
    hWindow = hWnd;
    ApplyDarkThemeToApp(hWnd);

    HWND hAni = GetDlgItem(hWnd, IDD_ANIMATION);
    Animate_OpenEx(hAni, hInst, MAKEINTRESOURCE(IDR_AVI));
    SetWindowPos(hAni, NULL, 0, 0, 606, 1, SWP_HIDEWINDOW | SWP_NOMOVE);

    SendDlgItemMessage(hWnd, IDC_ROOTPATH, WM_SETTEXT, 0, (LPARAM)RootPath);
    SendDlgItemMessage(hWnd, IDC_EDIT_DOC_ROOT, WM_SETTEXT, 0, (LPARAM)Options.Hdoc.c_str());
    if (Options.HTTPs)
      SendDlgItemMessage(hWnd, IDC_USEHTTPS, BM_SETCHECK, 1, 0);

    if (Options.TerminateAllOnQiut)
      SendDlgItemMessage(hWnd, IDC_CHECK_EXIT_ALL, BM_SETCHECK, 1, 0);

    if (Options.StartUp == NORMAL)
      CheckRadioButton(hWnd, IDC_RADIO1, IDC_RADIO3, IDC_RADIO1);
    else if (Options.StartUp == MINIMIZED)
      CheckRadioButton(hWnd, IDC_RADIO1, IDC_RADIO3, IDC_RADIO2);
    else
      CheckRadioButton(hWnd, IDC_RADIO1, IDC_RADIO3, IDC_RADIO3);

    if (Options.AutoStartApache)
      SendDlgItemMessage(hWnd, IDC_CHECK_AUTO_APACHE, BM_SETCHECK, 1, 0);

    if (Options.AutoStartMaria)
      SendDlgItemMessage(hWnd, IDC_CHECK_AUTO_MARIA, BM_SETCHECK, 1, 0);

    if (Options.ClearAllLogsOnQiut)
      SendDlgItemMessage(hWnd, IDC_CHECK_CLEAR_LOGS, BM_SETCHECK, 1, 0);

    SendDlgItemMessage(hWnd, IDC_USERPATH, BM_SETCHECK, 1, 0);

    if (ApachePID || MariaPID)
      PostMessage(hWnd, WM_NOTIFYSTATE, 0, 0);

    PostMessage(hWnd, WM_NOTIFYLOGFILES, 0, 0);

    NewJob(LogFileMonitorThread);

    NewJob(GetVersionsThread);

    if (Options.StartUp == MINIMIZED)
      ShowWindow(hWnd, SW_SHOWMINIMIZED);
    else if (Options.StartUp == SYSTRAY)
    {
      ShowWindow(hWnd, SW_SHOWMINIMIZED);
      SetWindowLongPtr(hWnd, GWL_EXSTYLE, GetWindowLongPtr(hWnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
      NotifyIcon(TRUE);
    }

    return (INT_PTR)TRUE;
  }

  case WM_COMMAND:
  {
    if (LOWORD(wParam) >= IDM_ROOTFOLDER && LOWORD(wParam) < (IDM_ROOTFOLDER + SiteList.size()))
    {
      int index = LOWORD(wParam) - IDM_ROOTFOLDER;
      WCHAR path[MAX_PATH];
      StringCchPrintf(path, MAX_PATH, L"%s://localhost/", Options.HTTPs ? L"https" : L"http");
      StringCchCat(path, MAX_PATH, SiteList[index].c_str());
      ShellExecute(NULL, L"open", path, NULL, NULL, SW_SHOWNORMAL);
    }
    else
    {
      switch (LOWORD(wParam))
      {
      case IDM_START_ALL:
      {
        if (ApachePID && MariaPID)
        {
          NewJob(KillApacheThread);
          NewJob(KillMariaThread);
          break;
        }
        else if (ApachePID && !MariaPID)
          NewJob(StartMariaThread);
        else if (!ApachePID && MariaPID)
          NewJob(StartApacheThread);
        else
        {
          NewJob(StartApacheThread);
          NewJob(StartMariaThread);
        }
        break;
      }
      case IDM_RESET_ALL:
      {
        if (ApachePID)
          NewJob(RestartApacheThread);

        if (MariaPID)
          NewJob(RestartMariaThread);

        break;
      }

      case IDC_APACHE_START:
      {
        if (ApachePID)
          NewJob(KillApacheThread);
        else
          NewJob(StartApacheThread);

        break;
      }
      case IDC_MARIA_START:
      {
        if (MariaPID)
          NewJob(KillMariaThread);
        else
          NewJob(StartMariaThread);

        break;
      }
      case IDC_APACHE_RESET:
      {
        NewJob(RestartApacheThread);
        break;
      }
      case IDC_MARIA_RESET:
      {
        NewJob(RestartMariaThread);
        break;
      }

      case IDC_APACHE_ERROR:
      {
        StartDefaultEditor(L"logs\\error.log");
        break;
      }
      case IDC_APACHE_ACCESS:
      {
        StartDefaultEditor(L"logs\\access.log");
        break;
      }
      case IDC_PHP_ERROR:
      {
        StartDefaultEditor(L"logs\\php_error.log");
        break;
      }
      case IDC_MARIA_ERROR:
      {
        StartDefaultEditor(L"logs\\mysql_error.log");
        break;
      }

      case IDC_REFRESH:
      {
        LoadSiteList();
        WCHAR refresh[25];
        StringCchPrintf(refresh, 25, L"Refresh %d", (int)SiteList.size());
        SetDlgItemText(hWnd, IDC_REFRESH, refresh);
        break;
      }
      case IDC_BROWSE:
      {
        if (!Options.Hdoc.empty())
          ShellExecute(NULL, L"open", Options.Hdoc.c_str(), NULL, NULL, SW_SHOWNORMAL);
        break;
      }
      case IDC_PHPMYADMIN:
      {
        WCHAR url[MAX_PATH];
        StringCchPrintf(url, MAX_PATH, L"%s://localhost/phpmyadmin/", Options.HTTPs ? L"https" : L"http");
        ShellExecute(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
        break;
      }
      case IDC_VHOSTS:
      {
        StartDefaultEditor(L"config\\vhosts.conf");
        break;
      }
      case IDC_PHP_INFO:
      {
        WCHAR url[MAX_PATH];
        StringCchPrintf(url, MAX_PATH, L"%s://localhost/phpinfo/", Options.HTTPs ? L"https" : L"http");
        ShellExecute(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL);
        break;
      }
      case IDC_INSTALLCA:
      {
        MessageBox(hWnd, L"Run create_cert.bat to create new certificate then install it useing this tool.", L"Create CA...", 0);
        ShellExecute(NULL, L"open", L"certmgr.msc", NULL, NULL, SW_SHOWNORMAL);
        break;
      }
      case IDC_SET_DOCROOT:
      {
        BrowseForFolder(hWnd);
        break;
      }
      case IDC_THANKS:
      {
        DialogBox(hInst, MAKEINTRESOURCE(IDD_THANKS), hWindow, (DLGPROC)AcknowledgeProc);
        break;
      }
      case IDC_ABOUT:
      {
        DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUT), hWindow, (DLGPROC)AboutProc);
        break;
      }
      case IDC_QUIT:
      {
        if (Options.TerminateAllOnQiut)
        {
          if (ApachePID)
            NewJob(KillApacheThread);
          if (MariaPID)
            NewJob(KillMariaThread);
        }

        Animate_Close(GetDlgItem(hWnd, IDD_ANIMATION));
        if (!IsWindowEnabled(GetDlgItem(hWnd, IDC_CHECK_CLEAR_LOGS)) && Options.ClearAllLogsOnQiut)
          Options.ClearAllLogsOnQiut = 0;

        if (Options.ClearAllLogsOnQiut)
          ClearLogsAndTmpFiles();

        OptionsSet();
        EndDialog(hWnd, 0);
        break;
      }

      case IDC_CHECK_AUTO_APACHE:
      {
        Options.AutoStartApache = (SendDlgItemMessage(hWnd, IDC_CHECK_AUTO_APACHE, BM_GETCHECK, 0, 0) == BST_CHECKED) ? TRUE : FALSE;
        break;
      }
      case IDC_CHECK_AUTO_MARIA:
      {
        Options.AutoStartMaria = (SendDlgItemMessage(hWnd, IDC_CHECK_AUTO_MARIA, BM_GETCHECK, 0, 0) == BST_CHECKED) ? TRUE : FALSE;
        break;
      }
      case IDC_RADIO1:
      {
        if (IsDlgButtonChecked(hWnd, IDC_RADIO1) == BST_CHECKED)
          Options.StartUp = NORMAL;
        break;
      }
      case IDC_RADIO2:
      {
        if (IsDlgButtonChecked(hWnd, IDC_RADIO2) == BST_CHECKED)
          Options.StartUp = MINIMIZED;
        break;
      }
      case IDC_RADIO3:
      {
        if (IsDlgButtonChecked(hWnd, IDC_RADIO3) == BST_CHECKED)
          Options.StartUp = SYSTRAY;
        break;
      }
      case IDC_USEHTTPS:
      {
        Options.HTTPs = (SendDlgItemMessage(hWnd, IDC_USEHTTPS, BM_GETCHECK, 0, 0) == BST_CHECKED) ? TRUE : FALSE;
        break;
      }
      case IDC_CHECK_EXIT_ALL:
      {
        BOOL chk = (SendMessage((HWND)lParam, BM_GETCHECK, 0, 0) == BST_CHECKED) ? TRUE : FALSE;
        Options.TerminateAllOnQiut = chk;
        EnableWindow(GetDlgItem(hWnd, IDC_CHECK_CLEAR_LOGS), chk);
        break;
      }
      case IDC_CHECK_CLEAR_LOGS:
      {
        Options.ClearAllLogsOnQiut = (SendDlgItemMessage(hWnd, IDC_CHECK_CLEAR_LOGS, BM_GETCHECK, 0, 0) == BST_CHECKED) ? TRUE : FALSE;
        break;
      }
      case IDC_USERPATH:
      {
        CheckDlgButton(hWnd, IDC_USERPATH, BST_CHECKED);
        break;
      }
      }
    }
    break;
  }

  case WM_NOTIFYLOGFILES:
  {
    static const wchar_t *logFiles[] = {
        L"\\logs\\access.log",
        L"\\logs\\error.log",
        L"\\logs\\mysql_error.log",
        L"\\logs\\php_error.log"};

    static const int controlIDs[] = {
        IDC_APACHE_ACCESS,
        IDC_APACHE_ERROR,
        IDC_MARIA_ERROR,
        IDC_PHP_ERROR};

    for (int i = 0; i < 4; ++i)
    {
      wstring fullPath = wstring(RootPath) + logFiles[i];
      BOOL exists = FileExists(fullPath.c_str());
      EnableWindow(GetDlgItem(hWnd, controlIDs[i]), exists);
    }
    break;
  }

  case WM_NOTIFYICON:
  {
    if (LOWORD(lParam) == WM_RBUTTONUP)
    {
      HMENU hMenu = LoadMenu(hInst, MAKEINTRESOURCE(IDM_CONTEXTMENU));
      if (hMenu)
      {
        HMENU hSubMenu = GetSubMenu(hMenu, 0);
        if (hSubMenu)
        {
          MenuSetItemText(hSubMenu, IDM_START_ALL, (ApachePID && MariaPID) ? L"Stop all" : L"Start all");
          MenuSetItemText(hSubMenu, IDC_APACHE_START, ApachePID ? L"Stop" : L"Start");
          MenuSetItemText(hSubMenu, IDC_MARIA_START, MariaPID ? L"Stop" : L"Start");

          EnableMenuItem(hSubMenu, IDM_RESET_ALL, MF_BYCOMMAND | ((ApachePID && MariaPID) ? MF_ENABLED : MF_GRAYED));
          EnableMenuItem(hSubMenu, IDC_APACHE_RESET, MF_BYCOMMAND | (ApachePID ? MF_ENABLED : MF_GRAYED));
          EnableMenuItem(hSubMenu, IDC_MARIA_RESET, MF_BYCOMMAND | (MariaPID ? MF_ENABLED : MF_GRAYED));
          EnableMenuItem(hSubMenu, IDC_PHPMYADMIN, MF_BYCOMMAND | ((ApachePID && MariaPID) ? MF_ENABLED : MF_GRAYED));
          EnableMenuItem(hSubMenu, IDC_PHP_INFO, MF_BYCOMMAND | ((ApachePID && MariaPID) ? MF_ENABLED : MF_GRAYED));

          MenuUpdateSiteList(GetSubMenu(hSubMenu, 7));

          SetForegroundWindow(hWnd);
          UINT uFlags = TPM_RIGHTBUTTON;
          if (GetSystemMetrics(SM_MENUDROPALIGNMENT) != 0)
            uFlags |= TPM_RIGHTALIGN;
          else
            uFlags |= TPM_LEFTALIGN;

          POINT pt;
          GetCursorPos(&pt);
          TrackPopupMenuEx(hSubMenu, uFlags, pt.x, pt.y, hWindow, NULL);
        }
        DestroyMenu(hMenu);
      }
    }
    else if (LOWORD(lParam) == WM_LBUTTONUP)
    {
      SetWindowLongPtr(hWnd, GWL_EXSTYLE, GetWindowLongPtr(hWnd, GWL_EXSTYLE) & ~WS_EX_TOOLWINDOW);
      ShowWindow(hWnd, SW_SHOWNORMAL);
      NotifyIcon(FALSE);
      SetForegroundWindow(hWnd);
    }
    return (INT_PTR)TRUE;
  }

  case WM_NOTIFYSTATE:
  {
    HWND hAni = GetDlgItem(hWnd, IDD_ANIMATION);
    BOOL Up = (ApachePID && MariaPID);

    SetWindowPos(hAni, NULL, 0, 0, 0, 0, (Up ? SWP_SHOWWINDOW : SWP_HIDEWINDOW) | SWP_NOMOVE | SWP_NOSIZE);

    if (Up)
      Animate_Play(hAni, 0, -1, -1);
    else
      Animate_Stop(hAni);

    SetWindowText(GetDlgItem(hWnd, IDC_APACHE_START), ApachePID ? L"Stop" : L"Start");
    EnableWindow(GetDlgItem(hWnd, IDC_APACHE_RESET), ApachePID);
    EnableWindow(GetDlgItem(hWnd, IDC_PHP_INFO), ApachePID);
    InvalidateRect(GetDlgItem(hWnd, IDC_APACHE_STATIC), NULL, FALSE);

    SetWindowText(GetDlgItem(hWnd, IDC_MARIA_START), MariaPID ? L"Stop" : L"Start");
    EnableWindow(GetDlgItem(hWnd, IDC_MARIA_RESET), MariaPID);
    InvalidateRect(GetDlgItem(hWnd, IDC_MARIA_STATIC), NULL, FALSE);

    EnableWindow(GetDlgItem(hWnd, IDC_PHPMYADMIN), Up ? PmaOk : FALSE);

    UINT icn = Up ? IDI_APPICON_ON : ((ApachePID || MariaPID) ? IDI_APPICON_ONOFF : IDI_APPICON_OFF);
    HICON hIcon = (HICON)LoadImage(hInst, MAKEINTRESOURCE(icn), IMAGE_ICON, 48, 48, LR_SHARED);
    SendDlgItemMessage(hWnd, IDC_LOGO, STM_SETICON, (WPARAM)hIcon, 0);

    NotifyIcon(NotifyAdded);
    break;
  }

  case WM_SYSCOMMAND:
  {
    if ((wParam & 0xFFF0) == SC_CLOSE)
    {
      ShowWindow(hWnd, SW_HIDE);
      NotifyIcon(TRUE);
      SetWindowLongPtr(hWnd, GWL_EXSTYLE, GetWindowLongPtr(hWnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
    }
    return (INT_PTR)FALSE;
  }

  case WM_CTLCOLORDLG:
  case WM_CTLCOLORBTN:
  case WM_CTLCOLORSTATIC:
  {
    CONST COLORREF red = RGB(218, 59, 47);
    CONST COLORREF green = RGB(104, 218, 61);
    CONST COLORREF txt = RGB(229, 229, 229);

    HDC hdcStatic = (HDC)wParam;
    SetBkMode(hdcStatic, TRANSPARENT);
    SetTextColor(hdcStatic, txt);
    HWND hStatic = (HWND)lParam;

    if (hStatic == GetDlgItem(hWnd, IDC_APACHE_STATIC))
    {
      if (ApachePID)
        SetTextColor(hdcStatic, green);
      else
        SetTextColor(hdcStatic, red);
    }
    else if (hStatic == GetDlgItem(hWnd, IDC_MARIA_STATIC))
    {
      if (MariaPID)
        SetTextColor(hdcStatic, green);
      else
        SetTextColor(hdcStatic, red);
    }
    else if (hStatic == GetDlgItem(hWnd, IDC_APACHE_V))
    {
      if (ApacheOk)
        SetTextColor(hdcStatic, green);
      else
        SetTextColor(hdcStatic, red);
    }
    else if (hStatic == GetDlgItem(hWnd, IDC_MARIA_V))
    {
      if (MariaOk)
        SetTextColor(hdcStatic, green);
      else
        SetTextColor(hdcStatic, red);
    }
    else if (hStatic == GetDlgItem(hWnd, IDC_PHP_V))
    {
      if (PhpOk)
        SetTextColor(hdcStatic, green);
      else
        SetTextColor(hdcStatic, red);
    }
    else if (hStatic == GetDlgItem(hWnd, IDC_COMPOSER_V))
    {
      if (ComposerOk)
        SetTextColor(hdcStatic, green);
      else
        SetTextColor(hdcStatic, red);
    }
    else if (hStatic == GetDlgItem(hWnd, IDC_PMA_V))
    {
      if (PmaOk)
        SetTextColor(hdcStatic, green);
      else
        SetTextColor(hdcStatic, red);
    }

    if (!hbrDark)
      hbrDark = CreateSolidBrush(RGB(44, 44, 44));
    return (LRESULT)hbrDark;
  }

  case WM_NOTIFYEXIT:
  {
    PostMessage(hWindow, WM_NOTIFYSTATE, 0, 0);
    PIDID id = (PIDID)lParam;
    if (id == APACHE)
    {
      UnregisterWait(hExitApache);
      ApachePID = 0;
    }
    else if (id == MARIA)
    {
      UnregisterWait(hExitMaria);
      MariaPID = 0;
    }
    return (INT_PTR)TRUE;
  }
  }
  return (INT_PTR)FALSE;
}

INT WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int)
{
  CONST WCHAR *ClassName = L"AMPCTRLClass";

  HANDLE hMutex = CreateMutex(NULL, FALSE, L"Global\\HomeStack_version_1_0");
  if (GetLastError() == ERROR_ALREADY_EXISTS)
  {
    HWND hWnd = FindWindow(ClassName, NULL);
    if (hWnd)
    {
      hWindow = hWnd;
      SetWindowLongPtr(hWnd, GWL_EXSTYLE, GetWindowLongPtr(hWnd, GWL_EXSTYLE) & ~WS_EX_TOOLWINDOW);
      SendMessage(hWnd, WM_SYSCOMMAND, SC_RESTORE, 0);
      NotifyIcon(FALSE);
      SetForegroundWindow(hWnd);
    }
    else
    {
      MessageBox(NULL, L"Application is already running.", L"Error", MB_ICONERROR);
    }
    if (hMutex)
      CloseHandle(hMutex);
    return -1;
  }

  INITCOMMONCONTROLSEX icc;
  WNDCLASSEX wc;

  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_WIN95_CLASSES;
  InitCommonControlsEx(&icc);

  wc.cbSize = sizeof(wc);
  if (!GetClassInfoEx(NULL, MAKEINTRESOURCE(32770), &wc))
  {
    MessageBox(NULL, L"Error getting class info.", L"Error", MB_ICONERROR | MB_OK);
    return 0;
  }

  wc.hInstance = hInst = hInstance;
  wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(IDI_APPICON));
  wc.hIconSm = wc.hIcon;
  wc.lpszClassName = ClassName;

  if (!RegisterClassEx(&wc))
  {
    MessageBox(NULL, L"Error registering window class.", L"Error", MB_ICONERROR | MB_OK);
    return 0;
  }

  // start up...
  CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

  InitPathes();

  UpdateApacheConfig();
  UpdatePhpConfig();
  UpdateUserPathVar();

  OptionsGet();

  if (Options.Hdoc.empty())
    BrowseForFolder(GetDesktopWindow());

  FindOnlineServices();

  if (ApacheOk && PhpOk && Options.AutoStartApache && !ApachePID)
    NewJob(StartApacheThread);

  if (MariaOk && Options.AutoStartMaria && !MariaPID)
    NewJob(StartMariaThread);

  LoadSiteList();

  DialogBox(hInst, MAKEINTRESOURCE(IDD_APP_DIALOG), NULL, (DLGPROC)MainDlgProc);

  CoUninitialize();

  if (hMutex)
    CloseHandle(hMutex);
  return 0;
}
