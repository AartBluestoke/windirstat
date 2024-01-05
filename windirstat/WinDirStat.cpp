// windirstat.cpp - Implementation of CDirstatApp and some globals
//
// WinDirStat - Directory Statistics
// Copyright (C) 2003-2005 Bernhard Seifert
// Copyright (C) 2004-2024 WinDirStat Team (windirstat.net)
// Copyright (C) 2010 Chris Wimmer
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
//

#include "stdafx.h"
#include "WinDirStat.h"
#include <common/MdExceptions.h>
#include <common/CommonHelpers.h>
#include "MainFrame.h"
#include "selectdrivesdlg.h"
#include "AboutDlg.h"
#include "DirStatDoc.h"
#include "graphview.h"
#include "osspecific.h"
#include "GlobalHelpers.h"
#include "WorkLimiter.h"
#pragma warning(push)
#pragma warning(disable : 4091)
#include <Dbghelp.h> // for mini dumps

#include "SmartPointer.h"
#pragma warning(pop)

#ifdef _DEBUG
#   include <common/tracer.cpp>
#endif

CMainFrame* GetMainFrame()
{
    // Not: return (CMainFrame *)AfxGetMainWnd();
    // because CWinApp::m_pMainWnd is set too late.
    return CMainFrame::GetTheFrame();
}

CDirstatApp* GetWDSApp()
{
    return static_cast<CDirstatApp*>(AfxGetApp());
}

CStringW GetAuthorEmail()
{
    return L"team" L"\x40" L"windirstat.net"; // FIXME into common string file
}

CStringW GetWinDirStatHomepage()
{
    return L"windirstat.net"; // FIXME into common string file
}

CMyImageList* GetMyImageList()
{
    return GetWDSApp()->GetMyImageList();
}


// CDirstatApp

BEGIN_MESSAGE_MAP(CDirstatApp, CWinApp)
    ON_COMMAND(ID_APP_ABOUT, OnAppAbout)
    ON_COMMAND(ID_FILE_OPEN, OnFileOpen)
    ON_COMMAND(ID_RUN_ELEVATED, OnRunElevated)
    ON_UPDATE_COMMAND_UI(ID_RUN_ELEVATED, OnUpdateRunElevated)
    ON_COMMAND(ID_HELP_MANUAL, OnHelpManual)
END_MESSAGE_MAP()


CDirstatApp _theApp;

CDirstatApp::CDirstatApp()
    : m_pDocTemplate(nullptr)
      , m_langid(0)
      , m_workingSet(0)
      , m_pageFaults(0)
      , m_lastPeriodicalRamUsageUpdate(GetTickCount64())
      , m_altColor(GetAlternativeColor(RGB(0x00, 0x00, 0xFF), L"AltColor"))
      , m_altEncryptionColor(GetAlternativeColor(RGB(0x00, 0x80, 0x00), L"AltEncryptionColor"))
#   ifdef VTRACE_TO_CONSOLE
      , m_vtrace_console(new CWDSTracerConsole())
#   endif // VTRACE_TO_CONSOLE
{
#   ifdef _DEBUG
    TestScanResourceDllName();
#   endif
}

CMyImageList* CDirstatApp::GetMyImageList()
{
    m_myImageList.initialize();
    return &m_myImageList;
}

void CDirstatApp::UpdateRamUsage()
{
    CWinThread::OnIdle(0);
}

void CDirstatApp::PeriodicalUpdateRamUsage()
{
    if (GetTickCount64() - m_lastPeriodicalRamUsageUpdate > 1200)
    {
        UpdateRamUsage();
        m_lastPeriodicalRamUsageUpdate = GetTickCount64();
    }
}

CStringW CDirstatApp::FindResourceDllPathByLangid(LANGID& langid)
{
    return FindAuxiliaryFileByLangid(
        wds::strLangPrefix
        , wds::strLangSuffix
        , langid
        , true
    );
}

CStringW CDirstatApp::FindHelpfilePathByLangid(LANGID langid)
{
    CStringW s;
    if (langid == GetBuiltInLanguage())
    {
        // The English help file is named windirstat.chm.
        s = GetAppFolder() + L"\\windirstat.chm";
        if (::PathFileExists(s))
        {
            return s;
        }
    }

    // Help files for other languages are named wdshxxxx.chm (xxxx = LANGID).
    s = FindAuxiliaryFileByLangid(L"wdsh", L".chm", langid, false);
    if (!s.IsEmpty())
    {
        return s;
    }

    // Else, try windirstat.chm again.
    s = GetAppFolder() + L"\\windirstat.chm";
    if (::PathFileExists(s))
    {
        return s;
    }

    // Not found.
    return wds::strEmpty;
}

void CDirstatApp::GetAvailableResourceDllLangids(CArray<LANGID, LANGID>& arr)
{
    arr.RemoveAll();

    CFileFind finder;
    BOOL b = finder.FindFile(GetAppFolder() + L"\\wdsr*" STR_LANG_SUFFIX);
    while (b)
    {
        b = finder.FindNextFile();
        if (finder.IsDirectory())
        {
            continue;
        }

        LANGID langid;
        if (ScanResourceDllName(finder.GetFileName(), langid) && IsCorrectResourceDll(finder.GetFilePath()))
        {
            arr.Add(langid);
        }
    }
}

void CDirstatApp::RestartApplication()
{
    // First, try to create the suspended process
    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    const BOOL success = CreateProcess(GetAppFileName(), nullptr, nullptr, nullptr, false, CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
    if (!success)
    {
        CStringW s;
        s.FormatMessage(IDS_CREATEPROCESSsFAILEDs, GetAppFileName().GetString(), MdGetWinErrorText(::GetLastError()).GetString());
        AfxMessageBox(s);
        return;
    }

    // We _send_ the WM_CLOSE here to ensure that all CPersistence-Settings
    // like column widths an so on are saved before the new instance is resumed.
    // This will post a WM_QUIT message.
    (void)GetMainFrame()->SendMessage(WM_CLOSE);

    const DWORD dw = ::ResumeThread(pi.hThread);
    if (dw != 1)
    {
        VTRACE(L"ResumeThread() didn't return 1");
    }

    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
}

bool CDirstatApp::getDiskFreeSpace(LPCWSTR pszRootPath, ULONGLONG& total, ULONGLONG& unused)
{
    ULARGE_INTEGER u64total = {0};
    ULARGE_INTEGER u64free = {0};

    const BOOL b = GetDiskFreeSpaceEx(pszRootPath, nullptr, &u64total, &u64free);
    if (!b)
    {
        VTRACE(L"GetDiskFreeSpaceEx(%s) failed.", pszRootPath);
    }

    // FIXME: need to retrieve total via IOCTL_DISK_GET_PARTITION_INFO instead
    total  = u64total.QuadPart;
    unused = u64free.QuadPart;

    // Race condition ...
    ASSERT(unused <= total);
    return FALSE != b;
}

bool CDirstatApp::ScanResourceDllName(LPCWSTR name, LANGID& langid)
{
    return ScanAuxiliaryFileName(
        wds::strLangPrefix
        , wds::strLangSuffix
        , name
        , langid
    );
}

// suffix contains the dot (e.g. ".chm")
bool CDirstatApp::ScanAuxiliaryFileName(LPCWSTR prefix, LPCWSTR suffix, LPCWSTR name, LANGID& langid)
{
    using wds::iLangCodeLength;
    ASSERT(wcslen(prefix) == wcslen(wds::strLangPrefix)); // FIXME: Also add .chm part or split
    ASSERT(wcslen(suffix) == wcslen(wds::strLangSuffix)); // FIXME: see above

    CStringW s(name); // [prefix][lngcode].[suffix]
    s.MakeLower();
    if (s.Left(static_cast<int>(wcslen(prefix))) != prefix)
    {
        return false;
    }
    s = s.Mid(static_cast<int>(wcslen(prefix))); // remove prefix from the front -> [lngcode].[suffix]

    if (s.GetLength() != iLangCodeLength + static_cast<int>(wcslen(suffix)))
    {
        return false;
    }

    if (s.Mid(iLangCodeLength) != CStringW(suffix).MakeLower())
    {
        return false;
    }

    s = s.Left(iLangCodeLength); // retain the language code -> [lngcode]

    for (int i = 0; i < iLangCodeLength; i++)
    {
        if (!_istxdigit(s[i]))
        {
            return false;
        }
    }

    int id;
    VERIFY(1 == _stscanf_s(s, L"%04x", &id));
    langid = static_cast<LANGID>(id);

    return true;
}

#ifdef _DEBUG
void CDirstatApp::TestScanResourceDllName()
{
    LANGID id;
    ASSERT(!ScanResourceDllName(wds::strEmpty, id));
    ASSERT(!ScanResourceDllName(STR_RESOURCE_PREFIX STR_LANG_SUFFIX, id));
    ASSERT(!ScanResourceDllName(STR_RESOURCE_PREFIX L"123" STR_LANG_SUFFIX, id));
    ASSERT(!ScanResourceDllName(STR_RESOURCE_PREFIX L"12345" STR_LANG_SUFFIX, id));
    ASSERT(!ScanResourceDllName(STR_RESOURCE_PREFIX L"1234.exe", id));
    ASSERT(ScanResourceDllName (STR_RESOURCE_PREFIX L"0123" STR_LANG_SUFFIX, id));
    ASSERT(id == 0x0123);
    ASSERT(ScanResourceDllName (CStringW(STR_RESOURCE_PREFIX L"a13F" STR_LANG_SUFFIX).MakeUpper(), id));
    ASSERT(id == 0xa13f);
}
#endif

CStringW CDirstatApp::FindAuxiliaryFileByLangid(LPCWSTR prefix, LPCWSTR suffix, LANGID& langid, bool checkResource)
{
    CStringW number;
    number.Format(L"%04x", langid);

    CStringW exactName;
    exactName.Format(L"%s%s%s", prefix, number.GetString(), suffix);

    CStringW exactPath = GetAppFolder() + L"\\" + exactName;
    if (::PathFileExists(exactPath) && (!checkResource || IsCorrectResourceDll(exactPath)))
    {
        return exactPath;
    }

    CStringW search;
    search.Format(L"%s*%s", prefix, suffix);

    CFileFind finder;
    BOOL b = finder.FindFile(GetAppFolder() + L"\\" + search);
    while (b)
    {
        b = finder.FindNextFile();
        if (finder.IsDirectory())
        {
            continue;
        }

        LANGID id;
        if (!ScanAuxiliaryFileName(prefix, suffix, finder.GetFileName(), id))
        {
            continue;
        }

        if (PRIMARYLANGID(id) == PRIMARYLANGID(langid) && (!checkResource || IsCorrectResourceDll(finder.GetFilePath())))
        {
            langid = id;
            return finder.GetFilePath();
        }
    }

    return wds::strEmpty;
}

bool CDirstatApp::IsCorrectResourceDll(LPCWSTR path)
{
    const HMODULE module = ::LoadLibraryEx(path, nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (module == nullptr)
    {
        return false;
    }

    // TODO/FIXME: introduce some method of checking the resource version

    const CStringW reference = LoadString(IDS_RESOURCEVERSION);

    const int bufsize = reference.GetLength() * 2;
    CStringW s;
    const int r = LoadString(module, IDS_RESOURCEVERSION, s.GetBuffer(bufsize), bufsize);
    s.ReleaseBuffer();

    FreeLibrary(module);

    if (r == 0 || s != reference)
    {
        return false;
    }

    return true;
}

void CDirstatApp::ReReadMountPoints()
{
    m_mountPoints.Initialize();
}

bool CDirstatApp::IsVolumeMountPoint(const CStringW& path)
{
    return m_mountPoints.IsVolumeMountPoint(path);
}

bool CDirstatApp::IsFolderJunction(DWORD attr)
{
    return m_mountPoints.IsFolderJunction(attr);
}

// Get the alternative colors for compressed and encrypted files/folders.
// This function uses either the value defined in the Explorer configuration
// or the default color values.
COLORREF CDirstatApp::GetAlternativeColor(COLORREF clrDefault, LPCWSTR which)
{
    COLORREF x;
    DWORD cbValue = sizeof(x);
    CRegKey key;

    // Open the explorer key
    key.Open(HKEY_CURRENT_USER, wds::strExplorerKey, KEY_READ);

    // Try to read the REG_BINARY value
    if (ERROR_SUCCESS == key.QueryBinaryValue(which, &x, &cbValue))
    {
        // Return the read value upon success
        return x;
    }
    else
    {
        // Return the default upon failure
        return clrDefault;
    }
}

COLORREF CDirstatApp::AltColor() const
{
    // Return property value
    return m_altColor;
}

COLORREF CDirstatApp::AltEncryptionColor() const
{
    // Return property value
    return m_altEncryptionColor;
}

CStringW CDirstatApp::GetCurrentProcessMemoryInfo()
{
    UpdateMemoryInfo();

    if (m_workingSet == 0)
    {
        return wds::strEmpty;
    }

    const CStringW n = PadWidthBlanks(FormatBytes(m_workingSet), 11);

    CStringW s;
    s.FormatMessage(IDS_RAMUSAGEs, n.GetString());

    return s;
}

bool CDirstatApp::UpdateMemoryInfo()
{
    PROCESS_MEMORY_COUNTERS pmc;
    ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);

    if (!::GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        return false;
    }

    m_workingSet = pmc.WorkingSetSize;

    bool ret = false;
    if (pmc.PageFaultCount > m_pageFaults + 500)
    {
        ret = true;
    }

    m_pageFaults = pmc.PageFaultCount;

    return ret;
}

LANGID CDirstatApp::GetBuiltInLanguage()
{
    return MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
}

BOOL CDirstatApp::InitInstance()
{
    Inherited::InitInstance();

    // Initialize visual controls
    constexpr INITCOMMONCONTROLSEX ctrls = { sizeof(INITCOMMONCONTROLSEX) , ICC_STANDARD_CLASSES };
    (void)::InitCommonControlsEx(&ctrls);
    (void)::OleInitialize(nullptr);
    (void)::AfxOleInit();
    (void)::AfxEnableControlContainer();
    (void)::AfxInitRichEdit2();

    Inherited::EnableHtmlHelp();

    Inherited::SetRegistryKey(L"Seifert");
    Inherited::LoadStdProfileSettings(4);

    m_langid = GetBuiltInLanguage();

    LANGID langid = CLanguageOptions::GetLanguage();
    if (langid != GetBuiltInLanguage())
    {
        const CStringW resourceDllPath = FindResourceDllPathByLangid(langid);
        if (!resourceDllPath.IsEmpty())
        {
            // Load language resource DLL
            if (const HINSTANCE dll = ::LoadLibrary(resourceDllPath))
            {
                // Set default module handle for loading of resources
                AfxSetResourceHandle(dll);
                m_langid = langid;
            }
            else
            {
                VTRACE(L"LoadLibrary(%s) failed: %u", resourceDllPath.GetString(), ::GetLastError());
            }
        }
        // else: We use our built-in English resources.

        CLanguageOptions::SetLanguage(m_langid);
    }

    GetOptions()->LoadFromRegistry();

    m_pDocTemplate = new CSingleDocTemplate(
        IDR_MAINFRAME,
        RUNTIME_CLASS(CDirstatDoc),
        RUNTIME_CLASS(CMainFrame),
        RUNTIME_CLASS(CGraphView));
    if (!m_pDocTemplate)
    {
        return FALSE;
    }
    AddDocTemplate(m_pDocTemplate);

    CCommandLineInfo cmdInfo;
    ParseCommandLine(cmdInfo);
    if (cmdInfo.m_nShellCommand == CCommandLineInfo::FileOpen)
    {
        // Use the default a new document since the shell processor will fault
        // interpreting the complex configuration string we pass as a document name
        CCommandLineInfo cmdAlt;
        ProcessShellCommand(cmdAlt);
    }
    else
    {
        if (!ProcessShellCommand(cmdInfo))
            return FALSE;
    }

    FileIconInit(TRUE);

    GetMainFrame()->InitialShowWindow();
    m_pMainWnd->UpdateWindow();

    // When called by setup.exe, WinDirStat remained in the
    // background, so force it to the foreground
    m_pMainWnd->BringWindowToTop();
    m_pMainWnd->SetForegroundWindow();

    // Attempt to enable backup / restore privileges if running as admin
    if (IsAdmin())
    {
        if (!EnableReadPrivileges())
        {
            VTRACE(L"Failed to enable additional privileges.");
        }
    }

    if (cmdInfo.m_nShellCommand == CCommandLineInfo::FileOpen)
    {
        // Terminate parent process that called us
        int token = 0;
        const DWORD parent = wcstoul(cmdInfo.m_strFileName.Tokenize(L"|", token), nullptr, 10);
        cmdInfo.m_strFileName = cmdInfo.m_strFileName.Right(cmdInfo.m_strFileName.GetLength() - token);
        SmartPointer<HANDLE> handle(CloseHandle, OpenProcess(PROCESS_TERMINATE, FALSE, parent));
        if (handle != nullptr)
        {
            TerminateProcess(handle, 0);
        }
        
        m_pDocTemplate->OpenDocumentFile(cmdInfo.m_strFileName, true);
    }
    else
    {
        OnFileOpen();
    }

    return TRUE;
}

int CDirstatApp::ExitInstance()
{
    return Inherited::ExitInstance();
}

LANGID CDirstatApp::GetLangid() const
{
    return m_langid;
}

LANGID CDirstatApp::GetEffectiveLangid()
{
    if (GetOptions()->IsUseWdsLocale())
    {
        return GetLangid();
    }
    else
    {
        return ::GetUserDefaultLangID();
    }
}

void CDirstatApp::OnAppAbout()
{
    StartAboutDialog();
}

void CDirstatApp::OnFileOpen()
{
    CSelectDrivesDlg dlg;
    if (IDOK == dlg.DoModal())
    {
        const CStringW path = CDirstatDoc::EncodeSelection(static_cast<RADIO>(dlg.m_radio), dlg.m_folderName, dlg.m_drives);
        m_pDocTemplate->OpenDocumentFile(path, true);
    }
}

void CDirstatApp::OnUpdateRunElevated(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!IsAdmin());
}

void CDirstatApp::OnRunElevated()
{
    // For the configuration to launch, include the parent process so we can
    // terminate it once launched from the child process
    const CStringW sAppName = GetAppFileName();
    CStringW launchConfig;
    launchConfig.Format(L"%lu|%s", GetCurrentProcessId(), GetDocument()->GetPathName().GetString());

    SHELLEXECUTEINFO shellInfo;
    ZeroMemory(&shellInfo, sizeof(shellInfo));
    shellInfo.cbSize = sizeof(shellInfo);
    shellInfo.fMask  = SEE_MASK_DEFAULT;
    shellInfo.lpFile = sAppName;
    shellInfo.lpVerb = L"runas";
    shellInfo.nShow  = SW_NORMAL;
    shellInfo.lpParameters = launchConfig.GetString();
    
    if (!::ShellExecuteEx(&shellInfo))
    {
        VTRACE(L"ShellExecuteEx failed to elevate %d", GetLastError());
    }
}

BOOL CDirstatApp::OnIdle(LONG lCount)
{
    bool more = false;

    CDirstatDoc* doc = GetDocument();
    CWorkLimiter limiter;
    limiter.Start(600);
    if (doc && !doc->Work(&limiter))
    {
        more = true;
    }

    if (Inherited::OnIdle(lCount))
    {
        more = true;
    }

    return more;
}

void CDirstatApp::OnHelpManual()
{
    // FIXME: open browser, point to Wiki (via windirstat.net short link), based on current language
    DoContextHelp(IDH_StartPage);
}

void CDirstatApp::DoContextHelp(DWORD)
{
    CStringW msg;
    msg.FormatMessage(IDS_HELPFILEsCOULDNOTBEFOUND, L"windirstat.chm");
    AfxMessageBox(msg);
}
