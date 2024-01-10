// item.cpp - Implementation of CItem
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
#include "WinDirStat.h"
#include "DirStatDoc.h" // GetItemColor()
#include "MainFrame.h"
#include <common/CommonHelpers.h>
#include "SelectObject.h"
#include "WorkLimiter.h"
#include "Item.h"

#include <string>
#include <algorithm>
#include <unordered_set>

#include "GlobalHelpers.h"

namespace
{
    CStringW GetFreeSpaceItemName()
    {
        return LoadString(IDS_FREESPACE_ITEM);
    }

    CStringW GetUnknownItemName()
    {
        return LoadString(IDS_UNKNOWN_ITEM);
    }

    constexpr SIZE sizeDeflatePacman = {1, 2};

    // File attribute packing
    constexpr unsigned char INVALID_m_attributes = 0x80;
}

CItem::CItem(ITEMTYPE type, LPCWSTR name, bool dontFollow)
    : m_name(name)
      , m_lastChange{0,0}
      , m_size(0)
      , m_files(0)
      , m_subdirs(0)
      , m_ticksWorked(0)
      , m_readJobs(0)
      , m_type(type)
      , m_readJobDone(false)
      , m_done(false)
      , m_attributes(0)
{
    if (IsType(IT_FILE | IT_FREESPACE | IT_UNKNOWN | IT_MYCOMPUTER) || dontFollow)
    {
        SetReadJobDone();
        m_readJobs = 0;
    }
    else if (IsType(IT_DRIVE | IT_DIRECTORY))
    {
        SetReadJobDone(false);
    }

    if (IsType(IT_DRIVE))
    {
        m_name = FormatVolumeNameOfRootPath(m_name);
    }

    if (IsType(IT_FILE))
    {
        const LPCWSTR ext = wcsrchr(name, L'.');
        if (ext == nullptr)
        {
            static LPCWSTR noext = L".";
            m_extension = noext;
        }
        else
        {
            static std::unordered_set<std::wstring> extcache;
            std::wstring exttoadd(&ext[0]);
            _wcslwr_s(exttoadd.data(), exttoadd.size() + 1);
            const auto cached = extcache.insert(std::move(exttoadd));
            m_extension = cached.first->c_str();
        }
    }
    else
    {
        m_extension = m_name.GetString();
    }
}

CItem::~CItem()
{
    for (int i = 0; i < m_children.GetSize(); i++)
    {
        delete m_children[i];
    }
}

CRect CItem::TmiGetRectangle() const
{
    return m_rect;
}

void CItem::TmiSetRectangle(const CRect& rc)
{
    m_rect = rc;
}

bool CItem::DrawSubitem(int subitem, CDC* pdc, CRect rc, UINT state, int* width, int* focusLeft) const
{
    if (subitem == COL_NAME)
    {
        return CTreeListItem::DrawSubitem(subitem, pdc, rc, state, width, focusLeft);
    }
    if (subitem != COL_SUBTREEPERCENTAGE)
    {
        return false;
    }

    const bool showReadJobs = MustShowReadJobs();

    if (showReadJobs && !GetOptions()->IsPacmanAnimation())
    {
        return false;
    }

    if (showReadJobs && IsDone())
    {
        return false;
    }

    if (width != nullptr)
    {
        *width = GetSubtreePercentageWidth();
        return true;
    }

    DrawSelection(GetTreeListControl(), pdc, rc, state);

    if (showReadJobs)
    {
        rc.DeflateRect(sizeDeflatePacman);
        DrawPacman(pdc, rc, GetTreeListControl()->GetItemSelectionBackgroundColor(this));
    }
    else
    {
        rc.DeflateRect(2, 5);
        for (int i = 0; i < GetIndent(); i++)
        {
            rc.left += rc.Width() / 10;
        }

        DrawPercentage(pdc, rc, GetFraction(), GetPercentageColor());
    }
    return true;
}

CStringW CItem::GetText(int subitem) const
{
    CStringW s;
    switch (subitem)
    {
    case COL_NAME:
        {
            s = m_name;
        }
        break;

    case COL_SUBTREEPERCENTAGE:
        if (IsDone())
        {
            ASSERT(m_readJobs == 0);
            //s = "ok";
        }
        else
        {
            if (m_readJobs == 1)
                VERIFY(s.LoadString(IDS_ONEREADJOB));
            else
                s.FormatMessage(IDS_sREADJOBS, FormatCount(m_readJobs).GetString());
        }
        break;

    case COL_PERCENTAGE:
        if (GetOptions()->IsShowTimeSpent() && MustShowReadJobs() || IsRootItem())
        {
            s.Format(L"[%s s]", FormatMilliseconds(GetTicksWorked()).GetString());
        }
        else
        {
            s.Format(L"%s%%", FormatDouble(GetFraction() * 100).GetString());
        }
        break;

    case COL_SUBTREETOTAL:
        {
            s = FormatBytes(GetSize());
        }
        break;

    case COL_ITEMS:
        if (!IsType(IT_FILE | IT_FREESPACE | IT_UNKNOWN))
        {
            s = FormatCount(GetItemsCount());
        }
        break;

    case COL_FILES:
        if (!IsType(IT_FILE | IT_FREESPACE | IT_UNKNOWN))
        {
            s = FormatCount(GetFilesCount());
        }
        break;

    case COL_SUBDIRS:
        if (!IsType(IT_FILE | IT_FREESPACE | IT_UNKNOWN))
        {
            s = FormatCount(GetSubdirsCount());
        }
        break;

    case COL_LASTCHANGE:
        if (!IsType(IT_FREESPACE | IT_UNKNOWN))
        {
            s = FormatFileTime(m_lastChange);
        }
        break;

    case COL_ATTRIBUTES:
        if (!IsType(IT_FREESPACE | IT_UNKNOWN | IT_MYCOMPUTER))
        {
            s = FormatAttributes(GetAttributes());
        }
        break;

    default:
        {
            ASSERT(0);
        }
        break;
    }
    return s;
}

COLORREF CItem::GetItemTextColor() const
{
    // Get the file/folder attributes
    const DWORD attr = GetAttributes();

    // This happens e.g. on a Unicode-capable FS when using ANSI APIs
    // to list files with ("real") Unicode names
    if (attr == INVALID_FILE_ATTRIBUTES)
    {
        return CTreeListItem::GetItemTextColor();
    }

    // Check for compressed flag
    if (attr & FILE_ATTRIBUTE_COMPRESSED)
    {
        return GetWDSApp()->AltColor();
    }

    if (attr & FILE_ATTRIBUTE_ENCRYPTED)
    {
        return GetWDSApp()->AltEncryptionColor();
    }

    // The rest is not colored
    return CTreeListItem::GetItemTextColor();
}

int CItem::CompareSibling(const CTreeListItem* tlib, int subitem) const
{
    const CItem* other = (CItem*)tlib;

    int r = 0;
    switch (subitem)
    {
    case COL_NAME:
        if (IsType(IT_DRIVE))
        {
            ASSERT(other->IsType(IT_DRIVE));
            r = signum(GetPath().CompareNoCase(other->GetPath()));
        }
        else
        {
            r = signum(m_name.CompareNoCase(other->m_name));
        }
        break;

    case COL_SUBTREEPERCENTAGE:
        if (MustShowReadJobs())
        {
            r = usignum(m_readJobs, other->m_readJobs);
        }
        else
        {
            r = signum(GetFraction() - other->GetFraction());
        }
        break;

    case COL_PERCENTAGE:
        {
            r = signum(GetFraction() - other->GetFraction());
        }
        break;

    case COL_SUBTREETOTAL:
        {
            r = usignum(GetSize(), other->GetSize());
        }
        break;

    case COL_ITEMS:
        {
            r = usignum(GetItemsCount(), other->GetItemsCount());
        }
        break;

    case COL_FILES:
        {
            r = usignum(GetFilesCount(), other->GetFilesCount());
        }
        break;

    case COL_SUBDIRS:
        {
            r = usignum(GetSubdirsCount(), other->GetSubdirsCount());
        }
        break;

    case COL_LASTCHANGE:
        {
            if (m_lastChange < other->m_lastChange)
            {
                return -1;
            }
            if (m_lastChange == other->m_lastChange)
            {
                return 0;
            }
            
            return 1;
        }
        break;
    case COL_ATTRIBUTES:
        {
            r = signum(GetSortAttributes() - other->GetSortAttributes());
        }
        break;

    default:
        {
            ASSERT(false);
        }
        break;
    }
    return r;
}

int CItem::GetChildrenCount() const
{
    return static_cast<int>(m_children.GetSize());
}

CTreeListItem* CItem::GetTreeListChild(int i) const
{
    return m_children[i];
}

short CItem::GetImageToCache() const
{
    // (Caching is done in CTreeListItem)

    if (IsType(IT_MYCOMPUTER))
    {
        return GetMyImageList()->getMyComputerImage();
    }
    if (IsType(IT_FREESPACE))
    {
        return GetMyImageList()->getFreeSpaceImage();
    }
    if (IsType(IT_UNKNOWN))
    {
        return GetMyImageList()->getUnknownImage();
    }

    const CStringW path = GetPath();
    if (IsType(IT_DIRECTORY) && GetWDSApp()->IsVolumeMountPoint(path))
    {
        return GetMyImageList()->getMountPointImage();
    }
    if (IsType(IT_DIRECTORY) && GetWDSApp()->IsFolderJunction(GetAttributes()))
    {
        return GetMyImageList()->getJunctionImage();
    }

    return GetMyImageList()->getFileImage(path);
}

void CItem::DrawAdditionalState(CDC* pdc, const CRect& rcLabel) const
{
    if (!IsRootItem() && this == GetDocument()->GetZoomItem())
    {
        CRect rc = rcLabel;
        rc.InflateRect(1, 0);
        rc.bottom++;

        CSelectStockObject sobrush(pdc, NULL_BRUSH);
        CPen pen(PS_SOLID, 2, GetDocument()->GetZoomColor());
        CSelectObject sopen(pdc, &pen);

        pdc->Rectangle(rc);
    }
}

int CItem::GetSubtreePercentageWidth()
{
    return 105;
}

CItem* CItem::FindCommonAncestor(const CItem* item1, const CItem* item2)
{
    const CItem* parent = item1;
    while (!parent->IsAncestorOf(item2))
    {
        parent = parent->GetParent();
    }
    ASSERT(parent != NULL);
    return const_cast<CItem*>(parent);
}

ULONGLONG CItem::GetProgressRange() const
{
    if (IsType(IT_MYCOMPUTER))
    {
        return GetProgressRangeMyComputer();
    }
    if (IsType(IT_DRIVE))
    {
        return GetProgressRangeDrive();
    }

    ASSERT(FALSE);
    return 0;
}

ULONGLONG CItem::GetProgressPos() const
{
    if (IsType(IT_MYCOMPUTER))
    {
        return GetProgressPosMyComputer();
    }
    if (IsType(IT_DRIVE))
    {
        return GetProgressPosDrive();
    }
    if (IsType(IT_DIRECTORY))
    {
        return m_files + m_subdirs;
    }

    ASSERT(FALSE);
    return 0;
}

const CItem* CItem::UpwardGetRoot() const
{
    if (GetParent() == nullptr)
    {
        return this;
    }

    return GetParent()->UpwardGetRoot();
}

void CItem::UpdateLastChange()
{
    ZeroMemory(&m_lastChange, sizeof(m_lastChange));
    if (IsType(IT_DIRECTORY | IT_FILE))
    {
        FileFindEnhanced finder;
        if (finder.FindFile(GetPath()))
        {
            finder.GetLastWriteTime(&m_lastChange);
            SetAttributes(finder.GetAttributes());
        }
    }
}

CItem* CItem::GetChild(int i) const
{
    return m_children[i];
}

CItem* CItem::GetParent() const
{
    return reinterpret_cast<CItem*>(CTreeListItem::GetParent());
}

int CItem::FindChildIndex(const CItem* child) const
{
    for (int i = 0; i < GetChildrenCount(); i++)
    {
        if (child == m_children[i])
        {
            return i;
        }
    }
    ASSERT(0);
    return 0;
}

void CItem::AddChild(CItem* child)
{
    ASSERT(!IsDone()); // SetDone() computed m_childrenBySize

    // This sequence is essential: First add numbers, then CTreeListControl::OnChildAdded(),
    // because the treelist will display it immediately.
    // If we did it the other way round, CItem::GetFraction() could ASSERT.
    UpwardAddSize(child->GetSize());
    UpwardAddReadJobs(child->GetReadJobs());
    UpwardUpdateLastChange(child->GetLastChange());

    m_children.Add(child);
    child->SetParent(this);

    GetTreeListControl()->OnChildAdded(this, child);
}

void CItem::RemoveChild(const int i)
{
    CItem* child = GetChild(i);
    m_children.RemoveAt(i);
    GetTreeListControl()->OnChildRemoved(this, child);
    delete child;
}

void CItem::RemoveAllChildren()
{
    GetTreeListControl()->OnRemovingAllChildren(this);

    for (int i = 0; i < GetChildrenCount(); i++)
    {
        delete m_children[i];
    }
    m_children.SetSize(0);
}

void CItem::UpwardAddSubdirs(const ULONGLONG dirCount)
{
    m_subdirs += dirCount;
    if (GetParent() != nullptr)
    {
        GetParent()->UpwardAddSubdirs(dirCount);
    }
}

void CItem::UpwardSubtractSubdirs(const ULONGLONG dirCount)
{
    m_subdirs -= dirCount;
    if (GetParent() != nullptr)
    {
        GetParent()->UpwardSubtractSubdirs(dirCount);
    }
}

void CItem::UpwardAddFiles(const ULONGLONG fileCount)
{
    m_files += fileCount;
    if (GetParent() != nullptr)
    {
        GetParent()->UpwardAddFiles(fileCount);
    }
}

void CItem::UpwardSubtractFiles(const ULONGLONG fileCount)
{
    m_files -= fileCount;
    if (GetParent() != nullptr)
    {
        GetParent()->UpwardSubtractFiles(fileCount);
    }
}

void CItem::UpwardAddSize(const ULONGLONG bytes)
{
    m_size += bytes;
    if (GetParent() != nullptr)
    {
        GetParent()->UpwardAddSize(bytes);
    }
}

void CItem::UpwardSubtractSize(const ULONGLONG bytes)
{
    m_size -= bytes;
    if (GetParent() != nullptr)
    {
        GetParent()->UpwardSubtractSize(bytes);
    }
}

void CItem::UpwardAddReadJobs(const ULONGLONG count)
{
    m_readJobs += count;
    if (GetParent() != nullptr)
    {
        GetParent()->UpwardAddReadJobs(count);
    }
}

void CItem::UpwardSubtractReadJobs(const ULONGLONG count)
{
    m_readJobs -= count;
    if (GetParent() != nullptr)
    {
        GetParent()->UpwardSubtractReadJobs(count);
    }
}

// This method increases the last change
void CItem::UpwardUpdateLastChange(const FILETIME& t)
{
    if (m_lastChange < t)
    {
        m_lastChange = t;
        if (GetParent() != nullptr)
        {
            GetParent()->UpwardUpdateLastChange(t);
        }
    }
}

// This method may also decrease the last change
void CItem::UpwardRecalcLastChange()
{
    UpdateLastChange();

    for (int i = 0; i < GetChildrenCount(); i++)
    {
        if (m_lastChange < GetChild(i)->GetLastChange())
        {
            m_lastChange = GetChild(i)->GetLastChange();
        }
    }
    if (GetParent() != nullptr)
    {
        GetParent()->UpwardRecalcLastChange();
    }
}

ULONGLONG CItem::GetSize() const
{
    return m_size;
}

void CItem::SetSize(const ULONGLONG ownSize)
{
    ASSERT(TmiIsLeaf());
    ASSERT(ownSize >= 0);
    m_size = ownSize;
}

ULONGLONG CItem::GetReadJobs() const
{
    return m_readJobs;
}

FILETIME CItem::GetLastChange() const
{
    return m_lastChange;
}

void CItem::SetLastChange(const FILETIME& t)
{
    m_lastChange = t;
}

// Encode the attributes to fit 1 byte
void CItem::SetAttributes(const DWORD attr)
{
    /*
    Bitmask of m_attributes:

    7 6 5 4 3 2 1 0
    ^ ^ ^ ^ ^ ^ ^ ^
    | | | | | | | |__ 1 == R                    (0x01)
    | | | | | | |____ 1 == H                    (0x02)
    | | | | | |______ 1 == S                    (0x04)
    | | | | |________ 1 == A                    (0x08)
    | | | |__________ 1 == Reparse point        (0x10)
    | | |____________ 1 == C                    (0x20)
    | |______________ 1 == E                    (0x40)
    |________________ 1 == invalid attributes   (0x80)
    */

    DWORD ret = attr;

    if (ret == INVALID_FILE_ATTRIBUTES)
    {
        m_attributes = static_cast<unsigned char>(INVALID_m_attributes);
        return;
    }

    ret &= FILE_ATTRIBUTE_READONLY | // Mask out lower 3 bits
    FILE_ATTRIBUTE_HIDDEN |
    FILE_ATTRIBUTE_SYSTEM;

    // Prepend the archive attribute
    ret |= (attr & FILE_ATTRIBUTE_ARCHIVE) >> 2;

    // --> At this point the lower nibble is fully used

    // Now shift the reparse point and compressed attribute into the lower 2 bits of
    // the high nibble.
    ret |= (attr & (FILE_ATTRIBUTE_REPARSE_POINT |
        FILE_ATTRIBUTE_COMPRESSED)) >> 6;

    // Shift the encrypted bit by 8 places
    ret |= (attr & FILE_ATTRIBUTE_ENCRYPTED) >> 8;

    m_attributes = static_cast<unsigned char>(ret);
}

// Decode the attributes encoded by SetAttributes()
DWORD CItem::GetAttributes() const
{
    DWORD ret = m_attributes;

    if (ret & INVALID_m_attributes)
        return INVALID_FILE_ATTRIBUTES;

    ret &= FILE_ATTRIBUTE_READONLY | // Mask out lower 3 bits
    FILE_ATTRIBUTE_HIDDEN |
    FILE_ATTRIBUTE_SYSTEM;

    // FILE_ATTRIBUTE_ARCHIVE
    ret |= (m_attributes & 0x08) << 2;

    // FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_COMPRESSED
    ret |= (m_attributes & 0x30) << 6;

    // FILE_ATTRIBUTE_ENCRYPTED
    ret |= (m_attributes & 0x40) << 8;

    return ret;
}

// Returns a value which resembles sorting of RHSACE considering gaps
int CItem::GetSortAttributes() const
{
    DWORD ret = 0;

    // We want to enforce the order RHSACE with R being the highest priority
    // attribute and E being the lowest priority attribute.
    ret += m_attributes & 0x01 ? 1000000 : 0; // R
    ret += m_attributes & 0x02 ? 100000 : 0;  // H
    ret += m_attributes & 0x04 ? 10000 : 0;   // S
    ret += m_attributes & 0x08 ? 1000 : 0;    // A
    ret += m_attributes & 0x20 ? 100 : 0;     // C
    ret += m_attributes & 0x40 ? 10 : 0;      // E

    return m_attributes & INVALID_m_attributes ? 0 : ret;
}

double CItem::GetFraction() const
{
    if (!GetParent() || GetParent()->GetSize() == 0)
    {
        return 1.0;
    }
    return static_cast<double>(GetSize()) / GetParent()->GetSize();
}

bool CItem::IsRootItem() const
{
    return (m_type & ITF_ROOTITEM) != 0;
}

CStringW CItem::GetPath() const
{
    CStringW path = UpwardGetPathWithoutBackslash();
    if (IsType(IT_DRIVE))
    {
        path += L"\\";
    }
    return path;
}

bool CItem::HasUncPath() const
{
    const CStringW path = GetPath();
    return path.GetLength() >= 2 && path.Left(2) == L"\\\\";
}

CStringW CItem::GetFindPattern() const
{
    CStringW pattern = GetPath();
    if (pattern.Right(1) != wds::chrBackslash)
    {
        pattern += L"\\";
    }
    pattern += L"*.*";
    return pattern;
}

// Returns the path for "Explorer here" or "Command Prompt here"
CStringW CItem::GetFolderPath() const
{
    CStringW path;

    if (IsType(IT_MYCOMPUTER))
    {
        path = GetParseNameOfMyComputer();
    }
    else
    {
        path = GetPath();
        if (IsType(IT_FILE))
        {
            const int i = path.ReverseFind(wds::chrBackslash);
            ASSERT(i != -1);
            path = path.Left(i + 1);
        }
    }
    return path;
}

// returns the path for the mail-report
CStringW CItem::GetReportPath() const
{
    CStringW path = UpwardGetPathWithoutBackslash();
    if (IsType(IT_DRIVE))
    {
        path += L"\\";
    }
    if (IsType(IT_FREESPACE | IT_UNKNOWN))
    {
        path += GetName();
    }

    return path;
}

CStringW CItem::GetName() const
{
    return m_name;
}

CStringW CItem::GetExtension() const
{
    return m_extension;
}

ULONGLONG CItem::GetFilesCount() const
{
    return m_files;
}

ULONGLONG CItem::GetSubdirsCount() const
{
    return m_subdirs;
}

ULONGLONG CItem::GetItemsCount() const
{
    return m_files + m_subdirs;
}

bool CItem::IsReadJobDone() const
{
    return m_readJobDone;
}

void CItem::SetReadJobDone(bool done)
{
    if (!IsReadJobDone() && done)
    {
        UpwardSubtractReadJobs(1);
    }
    else
    {
        UpwardSubtractReadJobs(m_readJobs - 1);
    }
    m_readJobDone = done;
}

void CItem::SetDone()
{
    if (m_done)
    {
        return;
    }

    if (IsType(IT_DRIVE))
    {
        UpdateFreeSpaceItem();

        if (GetDocument()->OptionShowUnknown())
        {
            CItem* unknown = FindUnknownItem();

            ULONGLONG total;
            ULONGLONG free;
            CDirStatApp::getDiskFreeSpace(GetPath(), total, free);

            ULONGLONG unknownspace = total - GetSize();
            if (!GetDocument()->OptionShowFreeSpace())
            {
                unknownspace -= free;
            }
            unknown->SetSize(unknownspace);

            UpwardAddSize(unknownspace);
        }
    }

    // sort by size
    std::sort(m_children.GetData(), m_children.GetData() + m_children.GetSize(),
        [](const CItem* item1, const CItem* item2)
        {
            return item1->GetSize() > item2->GetSize(); // biggest first
        });

    ZeroMemory(&m_rect, sizeof(m_rect));

    m_done = true;
}

ULONGLONG CItem::GetTicksWorked() const
{
    return m_ticksWorked;
}

void CItem::AddTicksWorked(const ULONGLONG more)
{
    m_ticksWorked += more;
}

void CItem::DoSomeWork(CWorkLimiter* limiter)
{
    if (IsDone())
    {
        return;
    }

    StartPacman(true);

    DriveVisualUpdateDuringWork();

    const ULONGLONG start = GetTickCount64();

    if (IsType(IT_DRIVE | IT_DIRECTORY))
    {
        if (!IsReadJobDone())
        {
            ULONGLONG dirCount  = 0;
            ULONGLONG fileCount = 0;

            CList<FILEINFO, FILEINFO> files;

            FileFindEnhanced finder;
            for (BOOL b = finder.FindFile(GetPath()); b; b = finder.FindNextFile())
            {
                DriveVisualUpdateDuringWork();

                if (finder.IsDots())
                {
                    continue;
                }
                if (GetOptions()->IsSkipHidden() && finder.IsHidden())
                {
                    continue;
                }
                if (finder.IsDirectory())
                {
                    dirCount++;
                    AddDirectory(finder);
                }
                else
                {
                    fileCount++;

                    FILEINFO fi;
                    fi.name       = finder.GetFileName();
                    fi.attributes = finder.GetAttributes();
                    // Retrieve file size
                    fi.length = finder.GetCompressedLength();
                    finder.GetLastWriteTime(&fi.lastWriteTime);
                    // (We don't use GetLastWriteTime(CTime&) here, because, if the file has
                    // an invalid timestamp, that function would ASSERT and throw an Exception.)

                    files.AddTail(fi);
                }
            }

            for (POSITION pos = files.GetHeadPosition(); pos != nullptr; files.GetNext(pos))
            {
                const FILEINFO& fi = files.GetAt(pos);
                this->AddFile(fi);
            }

            this->UpwardAddFiles(fileCount);

            UpwardAddSubdirs(dirCount);
            SetReadJobDone();
            AddTicksWorked(GetTickCount64() - start);
        }
        if (IsType(IT_DRIVE))
        {
            UpdateFreeSpaceItem();
        }

        if (limiter->IsDone())
        {
            StartPacman(false);
            return;
        }
    }
    if (IsType(IT_DRIVE | IT_DIRECTORY | IT_MYCOMPUTER))
    {
        ASSERT(IsReadJobDone());
        if (IsDone())
        {
            StartPacman(false);
            return;
        }
        if (GetChildrenCount() == 0)
        {
            SetDone();
            StartPacman(false);
            return;
        }

        const ULONGLONG startChildren = GetTickCount64();
        while (!limiter->IsDone())
        {
            ULONGLONG minticks = ULONGLONG_MAX;
            CItem* minchild    = nullptr;
            for (int i = 0; i < GetChildrenCount(); i++)
            {
                CItem* child = GetChild(i);
                if (child->IsDone())
                {
                    continue;
                }
                if (child->GetTicksWorked() < minticks)
                {
                    minticks = child->GetTicksWorked();
                    minchild = child;
                }
            }
            if (minchild == nullptr)
            {
                SetDone();
                break;
            }
            if (!limiter->IsDone())
            {
                minchild->DoSomeWork(limiter);
            }
        }
        AddTicksWorked(GetTickCount64() - startChildren);
    }
    else
    {
        SetDone();
    }
    StartPacman(false);
}

// Return: false if deleted
bool CItem::StartRefresh()
{
    ASSERT(!IsType(IT_FREESPACE));
    ASSERT(!IsType(IT_UNKNOWN));

    m_ticksWorked = 0;

    // Special case IT_MYCOMPUTER
    if (IsType(IT_MYCOMPUTER))
    {
        ZeroMemory(&m_lastChange, sizeof(m_lastChange));

        for (int i = 0; i < GetChildrenCount(); i++)
        {
            GetChild(i)->StartRefresh();
        }

        return true;
    }
    ASSERT(IsType(IT_FILE | IT_DRIVE | IT_DIRECTORY));

    const bool wasExpanded = IsVisible() && IsExpanded();
    int oldScrollPosition  = 0;
    if (IsVisible())
    {
        oldScrollPosition = GetScrollPosition();
    }

    UncacheImage();

    // Upward clear data
    UpdateLastChange();

    UpwardSetUndone();

    UpwardSubtractReadJobs(GetReadJobs());
    ASSERT(GetReadJobs() == 0);

    if (IsType(IT_FILE))
    {
        GetParent()->UpwardSubtractFiles(1);
    }
    else
    {
        UpwardSubtractFiles(GetFilesCount());
    }
    ASSERT(GetFilesCount() == 0);

    if (IsType(IT_DIRECTORY | IT_DRIVE))
    {
        UpwardSubtractSubdirs(GetSubdirsCount());
    }
    ASSERT(GetSubdirsCount() == 0);

    UpwardSubtractSize(GetSize());
    ASSERT(GetSize() == 0);

    RemoveAllChildren();
    UpwardRecalcLastChange();

    ASSERT(IsType(IT_FILE | IT_DRIVE | IT_DIRECTORY));

    // The item may have been deleted.
    bool deleted = false;
    if (IsType(IT_DRIVE))
    {
        deleted = !DriveExists(GetPath());
    }
    else if (IsType(IT_FILE))
    {
        deleted = !::PathFileExists(GetPath());
    }
    else if (IsType(IT_DIRECTORY))
    {
        deleted = !FolderExists(GetPath());
    }

    if (deleted)
    {
        if (GetParent() == nullptr)
        {
            GetDocument()->UnlinkRoot();
        }
        else
        {
            GetParent()->UpwardRecalcLastChange();
            GetParent()->RemoveChild(GetParent()->FindChildIndex(this)); // --> delete this
        }
        return false;
    }

    // Case IT_FILE
    if (IsType(IT_FILE))
    {
        FileFindEnhanced finder;
        for (BOOL b = finder.FindFile(GetPath()); b; b = finder.FindNextFile())
        {
            if (finder.IsDirectory())
            {
                continue;
            }

            FILEINFO fi;
            fi.name       = finder.GetFileName();
            fi.attributes = finder.GetAttributes();
            fi.length = finder.GetCompressedLength();
            finder.GetLastWriteTime(&fi.lastWriteTime);

            SetLastChange(fi.lastWriteTime);

            UpwardAddSize(fi.length);
            UpwardUpdateLastChange(GetLastChange());
            GetParent()->UpwardAddFiles(1);
        }
        SetDone();
        return true;
    }

    ASSERT(IsType(IT_DRIVE | IT_DIRECTORY));

    if (IsType(IT_DIRECTORY) && !IsRootItem() && GetWDSApp()->IsVolumeMountPoint(GetPath()) && !GetOptions()->IsFollowMountPoints())
    {
        return true;
    }

    if (IsType(IT_DIRECTORY) && !IsRootItem() && GetWDSApp()->IsFolderJunction(GetAttributes()) && !GetOptions()->IsFollowJunctionPoints())
    {
        return true;
    }

    // Initiate re-read
    SetReadJobDone(false);

    // Re-create <free space> and <unknown>
    if (IsType(IT_DRIVE))
    {
        if (GetDocument()->OptionShowFreeSpace())
        {
            CreateFreeSpaceItem();
        }
        if (GetDocument()->OptionShowUnknown())
        {
            CreateUnknownItem();
        }
    }

    {
        CWorkLimiter limiter;
        limiter.Start(0);
        DoSomeWork(&limiter);
    }

    if (wasExpanded)
    {
        GetTreeListControl()->ExpandItem(this);
    }

    if (IsVisible())
    {
        SetScrollPosition(oldScrollPosition);
    }

    return true;
}

void CItem::UpwardSetUndone()
{
    if (IsType(IT_DRIVE) && IsDone() && GetDocument()->OptionShowUnknown())
    {
        int i = 0;
        for (; i < GetChildrenCount(); i++)
        {
            if (GetChild(i)->IsType(IT_UNKNOWN))
            {
                break;
            }
        }
        CItem* unknown = GetChild(i);

        UpwardSubtractSize(unknown->GetSize());

        unknown->SetSize(0);
    }

    m_done = false;

    if (GetParent() != nullptr)
    {
        GetParent()->UpwardSetUndone();
    }
}

void CItem::RefreshRecycler() const
{
    ASSERT(IsType(IT_DRIVE));
    DWORD dummy;
    CStringW system;
    int i        = 0;
    const BOOL b = GetVolumeInformation(GetPath(), nullptr, 0, nullptr, &dummy, &dummy, system.GetBuffer(128), 128);
    system.ReleaseBuffer();
    if (!b)
    {
        VTRACE(L"GetVolumeInformation(%s) failed.", GetPath().GetString());
        return;
    }

    CStringW recycler;
    if (system.CompareNoCase(L"NTFS") == 0)
    {
        recycler = L"recycler";
    }
    else if (system.CompareNoCase(L"FAT32") == 0)
    {
        recycler = L"recycled";
    }
    else
    {
        VTRACE(L"%s: unknown file system type %s", GetPath().GetString(), system.GetString());
        return;
    }

    for (i = 0; i < GetChildrenCount(); i++)
    {
        if (GetChild(i)->GetName().CompareNoCase(recycler) == 0)
        {
            break;
        }
    }
    if (i >= GetChildrenCount())
    {
        VTRACE(L"%s: Recycler(%s) not found.", GetPath().GetString(), recycler.GetString());
        return;
    }

    GetChild(i)->StartRefresh();
}

void CItem::CreateFreeSpaceItem()
{
    ASSERT(IsType(IT_DRIVE));

    UpwardSetUndone();

    ULONGLONG total;
    ULONGLONG free;
    CDirStatApp::getDiskFreeSpace(GetPath(), total, free);

    const auto freespace = new CItem(IT_FREESPACE, GetFreeSpaceItemName());
    freespace->SetSize(free);
    freespace->SetDone();

    AddChild(freespace);
}

CItem* CItem::FindFreeSpaceItem() const
{
    if (const int i = FindFreeSpaceItemIndex(); i < GetChildrenCount())
    {
        return GetChild(i);
    }

    return nullptr;
}

void CItem::UpdateFreeSpaceItem() const
{
    ASSERT(IsType(IT_DRIVE));

    if (!GetDocument()->OptionShowFreeSpace())
    {
        return;
    }

    CItem* freeSpaceItem = FindFreeSpaceItem();
    ASSERT(freeSpaceItem != NULL);

    ULONGLONG total;
    ULONGLONG free;
    CDirStatApp::getDiskFreeSpace(GetPath(), total, free);

    const ULONGLONG before = freeSpaceItem->GetSize();
    const ULONGLONG diff   = free - before;

    freeSpaceItem->UpwardAddSize(diff);

    ASSERT(freeSpaceItem->GetSize() == free);
}

void CItem::RemoveFreeSpaceItem()
{
    ASSERT(IsType(IT_DRIVE));

    UpwardSetUndone();

    const int i = FindFreeSpaceItemIndex();
    ASSERT(i < GetChildrenCount());

    const CItem* freespace = GetChild(i);

    UpwardSubtractSize(freespace->GetSize());

    RemoveChild(i);
}

void CItem::CreateUnknownItem()
{
    ASSERT(IsType(IT_DRIVE));

    UpwardSetUndone();

    const auto unknown = new CItem(IT_UNKNOWN, GetUnknownItemName());
    unknown->SetDone();

    AddChild(unknown);
}

CItem* CItem::FindUnknownItem() const
{
    if (const int i = FindUnknownItemIndex(); i < GetChildrenCount())
    {
        return GetChild(i);
    }

    return nullptr;
}

void CItem::RemoveUnknownItem()
{
    ASSERT(IsType(IT_DRIVE));

    UpwardSetUndone();

    const int i = FindUnknownItemIndex();
    ASSERT(i < GetChildrenCount());

    const CItem* unknown = GetChild(i);

    UpwardSubtractSize(unknown->GetSize());

    RemoveChild(i);
}

CItem* CItem::FindDirectoryByPath(const CStringW& path)
{
    CStringW myPath = GetPath();
    myPath.MakeLower();

    int i = 0;
    while (i < myPath.GetLength() && i < path.GetLength() && myPath[i] == path[i])
    {
        i++;
    }

    if (i < myPath.GetLength())
    {
        return nullptr;
    }

    if (i >= path.GetLength())
    {
        ASSERT(myPath == path);
        return this;
    }

    for (i = 0; i < GetChildrenCount(); i++)
    {
        CItem* item = GetChild(i)->FindDirectoryByPath(path);
        if (item != nullptr)
        {
            return item;
        }
    }

    return nullptr;
}

void CItem::RecurseCollectExtensionData(CExtensionData* ed) const
{
    GetWDSApp()->PeriodicalUpdateRamUsage();

    if (TmiIsLeaf())
    {
        if (IsType(IT_FILE))
        {
            const CStringW & ext = GetExtension();
            SExtensionRecord r;
            if (ed->Lookup(ext, r))
            {
                r.bytes += GetSize();
                r.files++;
            }
            else
            {
                r.bytes = GetSize();
                r.files = 1;
            }
            ed->SetAt(ext, r);
        }
    }
    else
    {
        for (int i = 0; i < GetChildrenCount(); i++)
        {
            GetChild(i)->RecurseCollectExtensionData(ed);
        }
    }
}

ULONGLONG CItem::GetProgressRangeMyComputer() const
{
    ASSERT(IsType(IT_MYCOMPUTER));

    ULONGLONG range = 0;
    for (int i = 0; i < GetChildrenCount(); i++)
    {
        range += GetChild(i)->GetProgressRangeDrive();
    }
    return range;
}

ULONGLONG CItem::GetProgressPosMyComputer() const
{
    ASSERT(IsType(IT_MYCOMPUTER));

    ULONGLONG pos = 0;
    for (int i = 0; i < GetChildrenCount(); i++)
    {
        pos += GetChild(i)->GetProgressPosDrive();
    }
    return pos;
}

ULONGLONG CItem::GetProgressRangeDrive() const
{
    ULONGLONG total;
    ULONGLONG free;
    CDirStatApp::getDiskFreeSpace(GetPath(), total, free);

    total -= free;

    ASSERT(total >= 0);
    return total;
}

ULONGLONG CItem::GetProgressPosDrive() const
{
    ULONGLONG pos = GetSize();

    const CItem* fs = FindFreeSpaceItem();
    if (fs != nullptr)
    {
        pos -= fs->GetSize();
    }

    return pos;
}

COLORREF CItem::GetGraphColor() const
{
    if (IsType(IT_UNKNOWN))
    {
        return RGB(255, 255, 0) | CTreemap::COLORFLAG_LIGHTER;
    }

    if (IsType(IT_FREESPACE))
    {
        return RGB(100, 100, 100) | CTreemap::COLORFLAG_DARKER;
    }

    if (IsType(IT_FILE))
    {
        return GetDocument()->GetCushionColor(GetExtension());
    }

    return RGB(0, 0, 0);
 }

bool CItem::MustShowReadJobs() const
{
    if (GetParent() != nullptr)
    {
        return !GetParent()->IsDone();
    }
    else
    {
        return !IsDone();
    }
}

COLORREF CItem::GetPercentageColor() const
{
    const int i = GetIndent() % GetOptions()->GetTreelistColorCount();
    return GetOptions()->GetTreelistColor(i);
}

int CItem::FindFreeSpaceItemIndex() const
{
    int i = 0;
    for (; i < GetChildrenCount(); i++)
    {
        if (GetChild(i)->IsType(IT_FREESPACE))
        {
            break;
        }
    }
    return i; // maybe == GetChildrenCount() (=> not found)
}

int CItem::FindUnknownItemIndex() const
{
    int i = 0;
    for (; i < GetChildrenCount(); i++)
    {
        if (GetChild(i)->IsType(IT_UNKNOWN))
        {
            break;
        }
    }
    return i; // maybe == GetChildrenCount() (=> not found)
}

CStringW CItem::UpwardGetPathWithoutBackslash() const
{
    CStringW path;
    if (GetParent() != nullptr)
    {
        path = GetParent()->UpwardGetPathWithoutBackslash();
    }

    switch (GetType())
    {
    case IT_MYCOMPUTER:
        {
            // empty
        }
        break;

    case IT_DRIVE:
        {
            // (we don't use our parent's path here.)
            path = PathFromVolumeName(m_name);
        }
        break;

    case IT_DIRECTORY:
        {
            if (!path.IsEmpty())
            {
                path += L"\\";
            }
            path += m_name;
        }
        break;

    case IT_FILE:
        {
            path += L"\\" + m_name;
        }
        break;

    case IT_FREESPACE:
    case IT_UNKNOWN:
        break;

    default:
        {
            ASSERT(0);
        }
    }

    return path;
}

void CItem::AddDirectory(FileFindEnhanced& finder)
{
    bool dontFollow = GetWDSApp()->IsVolumeMountPoint(finder.GetFilePath()) && !GetOptions()->IsFollowMountPoints();

    dontFollow |= GetWDSApp()->IsFolderJunction(finder.GetAttributes()) && !GetOptions()->IsFollowJunctionPoints();

    const auto & child = new CItem(IT_DIRECTORY, finder.GetFileName(), dontFollow);
    FILETIME t;
    finder.GetLastWriteTime(&t);
    child->SetLastChange(t);
    child->SetAttributes(finder.GetAttributes());
    AddChild(child);
}

void CItem::AddFile(const FILEINFO& fi)
{
    const auto & child = new CItem(IT_FILE, fi.name);
    child->SetSize(fi.length);
    child->SetLastChange(fi.lastWriteTime);
    child->SetAttributes(fi.attributes);
    child->SetDone();

    AddChild(child);
}

void CItem::DriveVisualUpdateDuringWork() const
{
    MSG msg;
    while (PeekMessage(&msg, nullptr, WM_PAINT, WM_PAINT, PM_REMOVE))
    {
        DispatchMessage(&msg);
    }

    GetMainFrame()->DrivePacman();
    UpwardDrivePacman();
}

void CItem::UpwardDrivePacman() const
{
    if (!GetOptions()->IsPacmanAnimation())
    {
        return;
    }

    DrivePacman();
    if (GetParent() != nullptr)
    {
        GetParent()->UpwardDrivePacman();
    }
}

void CItem::DrivePacman() const
{
    if (!IsVisible())
    {
        return;
    }

    if (!CTreeListItem::DrivePacman(GetReadJobs()))
    {
        return;
    }

    const int i = GetTreeListControl()->FindTreeItem(this);

    CClientDC dc(GetTreeListControl());
    CRect rc = GetTreeListControl()->GetWholeSubitemRect(i, COL_SUBTREEPERCENTAGE);
    rc.DeflateRect(sizeDeflatePacman);
    DrawPacman(&dc, rc, GetTreeListControl()->GetItemSelectionBackgroundColor(i));
}
