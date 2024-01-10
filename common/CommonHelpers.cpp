// commonhelpers.cpp - Implementation of common global helper functions
//
// WinDirStat - Directory Statistics
// Copyright (C) 2003-2005 Bernhard Seifert
// Copyright (C) 2004-2024 WinDirStat Team (windirstat.net)
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

#include "stdafx.h"
#include <common/MdExceptions.h>
#include <common/Constants.h>

CStringW MyStrRetToString(const LPITEMIDLIST pidl, const STRRET* strret)
{
    CStringW s;

    switch (strret->uType)
    {
    case STRRET_CSTR:
        {
            s.Format(L"%hs", strret->cStr);
        }
        break;
    case STRRET_OFFSET:
        {
            s.Format(L"%hs", (char*)pidl + strret->uOffset);
        }
        break;
    case STRRET_WSTR:
        {
            s = strret->pOleStr;
        }
        break;
    }

    return s;
}

BOOL ShellExecuteNoThrow(HWND hwnd, LPCWSTR lpVerb, LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShowCmd)
{
    SHELLEXECUTEINFO sei = {
        sizeof(SHELLEXECUTEINFO),
        0,
        hwnd,
        lpVerb,
        lpFile,
        lpParameters,
        lpDirectory,
        nShowCmd,
        nullptr, // hInstApp
        nullptr,
        nullptr,
        nullptr,
        0, // dwHotKey
        {},
        nullptr
    };

    return ::ShellExecuteEx(&sei);
}

BOOL ShellExecuteThrow(HWND hwnd, LPCWSTR lpVerb, LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShowCmd)
{
    CWaitCursor wc;

    BOOL bResult = ShellExecuteNoThrow(hwnd, lpVerb, lpFile, lpParameters, lpDirectory, nShowCmd);
    if (!bResult)
    {
        MdThrowStringExceptionF(L"ShellExecute failed: %1!s!", MdGetWinErrorText(::GetLastError()).GetString());
    }
    return bResult;
}

CStringW GetBaseNameFromPath(LPCWSTR path)
{
    CStringW s  = path;
    const int i = s.ReverseFind(wds::chrBackslash);
    if (i < 0)
    {
        return s;
    }
    return s.Mid(i + 1);
}

CStringW LoadString(UINT resId)
{
    return MAKEINTRESOURCE(resId);
}

CStringW GetAppFileName()
{
    CStringW s;
    VERIFY(::GetModuleFileName(NULL, s.GetBuffer(_MAX_PATH), _MAX_PATH));
    s.ReleaseBuffer();
    return s;
}

CStringW GetAppFolder()
{
    CStringW s  = GetAppFileName();
    const int i = s.ReverseFind(wds::chrBackslash);
    ASSERT(i >= 0);
    s = s.Left(i);
    return s;
}
