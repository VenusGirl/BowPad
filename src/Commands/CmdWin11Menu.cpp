// This file is part of BowPad.
//
// Copyright (C) 2025 - Stefan Kueng
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// See <http://www.gnu.org/licenses/> for a copy of the full license text
//
#include "stdafx.h"
#include "CmdWin11Menu.h"
#include "PackageRegistration.h"
#include "PathUtils.h"
#include "UnicodeUtils.h"
#include "ResString.h"
#include "BowPad.h"
#include <future>

bool CCmdRegisterWin11ContextMenu::Execute()
{
    if (::GetSystemMetrics(SM_CLEANBOOT) > 0)
    {
        return false; // we would get an exception HRESULT 0x8007043c (ERROR_NOT_SAFEBOOT_SERVICE).
    }

    // check if we're running on windows 11
    PWSTR        pszPath = nullptr;
    std::wstring sysPath;
    if (SHGetKnownFolderPath(FOLDERID_System, KF_FLAG_CREATE, nullptr, &pszPath) == S_OK)
    {
        sysPath = pszPath;
        CoTaskMemFree(pszPath);
    }
    auto             explorerVersion = CPathUtils::GetVersionFromFile(sysPath + L"\\shell32.dll");
    std::vector<int> versions;
    stringtok(versions, explorerVersion, true, L".");
    bool isWin11OrLater = versions.size() > 3 && versions[2] >= 22000;
    if (isWin11OrLater)
    {
        CRegStdDWORD noContextMenuHKCU(L"Software\\BowPad\\NoWin11ContextMenu", 0, true, HKEY_CURRENT_USER);
        noContextMenuHKCU.removeValue();

        auto future = std::async([&]() {
            try
            {
                auto                extPath  = CPathUtils::GetModuleDir(nullptr);
                auto                msixPath = extPath + L"\\package.msix";
                PackageRegistration registrator(extPath, msixPath, L"2BD6356E-3263-4AA6-A5FC-C48280BE5EDD");
                return registrator.RegisterForCurrentUser();
            }
            catch (const std::exception& ex)
            {
                return CUnicodeUtils::StdGetUnicode(ex.what());
            }
        });
        auto ret    = future.get();

        if (ret.empty())
        {
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, 0, 0);
            ResString sInfo(g_hRes, IDS_WIN11_CONTEXTMENU_REGISTERED);
            MessageBox(GetHwnd(), sInfo, L"BowPad", MB_ICONINFORMATION);
            return true;
        }
        else
            MessageBoxW(GetHwnd(), ret.c_str(), L"BowPad", MB_ICONERROR);
    }
    return false;
}

bool CCmdUnRegisterWin11ContextMenu::Execute()
{
    if (::GetSystemMetrics(SM_CLEANBOOT) > 0)
    {
        return false; // we would get an exception HRESULT 0x8007043c (ERROR_NOT_SAFEBOOT_SERVICE).
    }

    // check if we're running on windows 11
    PWSTR        pszPath = nullptr;
    std::wstring sysPath;
    if (SHGetKnownFolderPath(FOLDERID_System, KF_FLAG_CREATE, nullptr, &pszPath) == S_OK)
    {
        sysPath = pszPath;
        CoTaskMemFree(pszPath);
    }
    auto             explorerVersion = CPathUtils::GetVersionFromFile(sysPath + L"\\shell32.dll");
    std::vector<int> versions;
    stringtok(versions, explorerVersion, true, L".");
    bool isWin11OrLater = versions.size() > 3 && versions[2] >= 22000;
    if (isWin11OrLater)
    {
        CRegStdDWORD noContextMenuHKCU(L"Software\\BowPad\\NoWin11ContextMenu", 0, true, HKEY_CURRENT_USER);
        noContextMenuHKCU = 1;

        auto future       = std::async([&]() {
                try
                {
                    auto                extPath  = CPathUtils::GetModuleDir(nullptr);
                    auto                msixPath = extPath + L"\\package.msix";
                    PackageRegistration registrator(extPath, msixPath, L"2BD6356E-3263-4AA6-A5FC-C48280BE5EDD");
                    return registrator.UnregisterForCurrentUser();
                }
                catch (const std::exception& ex)
                {
                    return CUnicodeUtils::StdGetUnicode(ex.what());
                } });
        auto ret          = future.get();

        if (ret.empty())
        {
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, 0, 0);
            ResString sInfo(g_hRes, IDS_WIN11_CONTEXTMENU_UNREGISTERED);
            MessageBox(GetHwnd(), sInfo, L"BowPad", MB_ICONINFORMATION);
            return true;
        }
        else
            MessageBoxW(GetHwnd(), ret.c_str(), L"BowPad", MB_ICONERROR);
    }
    return false;
}
