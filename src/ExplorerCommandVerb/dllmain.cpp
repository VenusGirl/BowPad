// dllmain.cpp : Defines the entry point for the DLL application.
#include "stdafx.h"

#include <comutil.h>
#include <wrl/module.h>
#include <wrl/implements.h>
#include <wrl/client.h>
#include <shobjidl_core.h>
#include <wil\resource.h>
#include <shellapi.h>
#include <Shlobj.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cwctype>
#include <thread>
#include <Shlwapi.h>
#include "../../ext/sktoolslib/PackageRegistration.h"
#include "../../ext/sktoolslib/PathUtils.h"
#include "../../ext/sktoolslib/StringUtils.h"
#include "../../ext/sktoolslib/Registry.h"

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "comsupp.lib")

using namespace Microsoft::WRL;

void          EnsureRegistrationOnCurrentUser();

HMODULE       hDll = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule,
                      DWORD   ulReasonForCall,
                      LPVOID /*lpReserved*/)
{
    hDll = hModule;
    switch (ulReasonForCall)
    {
        case DLL_PROCESS_ATTACH:
            EnsureRegistrationOnCurrentUser();
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

void RegisterForCurrentUserWorker()
{
    if (::GetSystemMetrics(SM_CLEANBOOT) > 0)
    {
        return; // we would get an exception HRESULT 0x8007043c (ERROR_NOT_SAFEBOOT_SERVICE).
    }
    auto extPath  = CPathUtils::GetModuleDir(hDll);
    auto msixPath = extPath + L"\\package.msix";
    try
    {
        PackageRegistration registrator(extPath, msixPath, L"2BD6356E-3263-4AA6-A5FC-C48280BE5EDD");

        // check the registry DWORD value HKCU\Software\BowPad\NoWin11ContextMenu and HKLM\Software\BowPad\NoWin11ContextMenu
        // and if any of those is set to 1, then we remove any existing package and return
        CRegStdDWORD        noContextMenuHKCU(L"Software\\BowPad\\NoWin11ContextMenu", 0, true, HKEY_CURRENT_USER);
        CRegStdDWORD        noContextMenuHKLM(L"Software\\BowPad\\NoWin11ContextMenu", 0, true, HKEY_LOCAL_MACHINE);
        std::wstring        err;
        if (noContextMenuHKCU || noContextMenuHKLM)
            err=registrator.UnregisterForCurrentUser();
        else
            err=registrator.RegisterForCurrentUser();
        if (!err.empty())
            OutputDebugString((L"bowpad: error during registration: " + err + L"\n").c_str());
        else
            OutputDebugString(L"bowpad: registration/unregistration completed successfully\n");
    }
    catch (const std::exception& ex)
    {
        OutputDebugStringA((std::string("bowpad: exception during registration: ") + ex.what() + "\n").c_str());
    }
    // Finally we notify the shell that we have made changes, so it reloads the right click menu items.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, 0, 0);
}

void EnsureRegistrationOnCurrentUser()
{
    // check if the process name this dll is loaded into is explorer.exe
    wchar_t pathBuffer[FILENAME_MAX] = {0};
    GetModuleFileNameW(NULL, pathBuffer, FILENAME_MAX);
    PathStripPathW(pathBuffer);

    std::wstring moduleName(pathBuffer);
    std::transform(moduleName.begin(), moduleName.end(), moduleName.begin(), std::towlower);
    if (moduleName == L"explorer.exe")
    {
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
            // We are being loaded into explorer.exe on windows 11
            // start a thread to register (or unregister) the win 11 context menu entry for the current user
            auto registrationThread = std::thread(RegisterForCurrentUserWorker);
            registrationThread.detach();
        }
    }
}

class ExplorerCommandBase : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IExplorerCommand, IObjectWithSite>
{
public:
    virtual const wchar_t* Title(IShellItemArray* items) = 0;
    virtual EXPCMDFLAGS    Flags() { return ECF_DEFAULT; }
    virtual EXPCMDSTATE    State(_In_opt_ IShellItemArray* selection) { return ECS_ENABLED; }

    // IExplorerCommand
    IFACEMETHODIMP         GetTitle(_In_opt_ IShellItemArray* items, _Outptr_result_nullonfailure_ PWSTR* name) override
    {
        *name      = nullptr;
        auto title = wil::make_cotaskmem_string_nothrow(Title(items));
        RETURN_IF_NULL_ALLOC(title);
        *name = title.release();
        return S_OK;
    }
    IFACEMETHODIMP GetIcon(_In_opt_ IShellItemArray*, _Outptr_result_nullonfailure_ PWSTR* icon) override
    {
        *icon = nullptr;
        return E_NOTIMPL;
    }
    IFACEMETHODIMP GetToolTip(_In_opt_ IShellItemArray*, _Outptr_result_nullonfailure_ PWSTR* infoTip) override
    {
        *infoTip = nullptr;
        return E_NOTIMPL;
    }
    IFACEMETHODIMP GetCanonicalName(_Out_ GUID* guidCommandName) override
    {
        *guidCommandName = GUID_NULL;
        return S_OK;
    }
    IFACEMETHODIMP GetState(_In_opt_ IShellItemArray* selection, _In_ BOOL okToBeSlow, _Out_ EXPCMDSTATE* cmdState) override
    {
        *cmdState = State(selection);
        return S_OK;
    }
    IFACEMETHODIMP Invoke(_In_opt_ IShellItemArray* selection, _In_opt_ IBindCtx*) noexcept override
    try
    {
        HWND parent = nullptr;
        if (m_site)
        {
            ComPtr<IOleWindow> oleWindow;
            RETURN_IF_FAILED(m_site.As(&oleWindow));
            RETURN_IF_FAILED(oleWindow->GetWindow(&parent));
        }

        std::wostringstream title;
        title << Title(selection);

        if (selection)
        {
            DWORD count;
            RETURN_IF_FAILED(selection->GetCount(&count));
            title << L" (" << count << L" selected items)";
        }
        else
        {
            title << L"(no selected items)";
        }

        MessageBox(parent, title.str().c_str(), L"TestCommand", MB_OK);
        return S_OK;
    }
    CATCH_RETURN();

    IFACEMETHODIMP GetFlags(_Out_ EXPCMDFLAGS* flags) override
    {
        *flags = Flags();
        return S_OK;
    }
    IFACEMETHODIMP EnumSubCommands(_COM_Outptr_ IEnumExplorerCommand** enumCommands) override
    {
        *enumCommands = nullptr;
        return E_NOTIMPL;
    }

    // IObjectWithSite
    IFACEMETHODIMP SetSite(_In_ IUnknown* site) noexcept override
    {
        m_site = site;
        return S_OK;
    }
    IFACEMETHODIMP GetSite(_In_ REFIID riid, _COM_Outptr_ void** site) noexcept override { return m_site.CopyTo(riid, site); }

protected:
    ComPtr<IUnknown> m_site;
};

class __declspec(uuid("A2CCD0C8-0CA7-4E4C-A5E5-7B004022B19F")) BowPadExplorerCommandHandler final : public ExplorerCommandBase
{
public:
    const wchar_t* Title(IShellItemArray* items) override
    {
        bool isFile = false;
        if (items)
        {
            DWORD fileCount = 0;
            if (SUCCEEDED(items->GetCount(&fileCount)))
            {
                for (DWORD i = 0; i < fileCount; i++)
                {
                    IShellItem* shellItem = nullptr;
                    items->GetItemAt(i, &shellItem);
                    DWORD attribs = 0;
                    shellItem->GetAttributes(SFGAO_FOLDER, &attribs);
                    if ((attribs & SFGAO_FOLDER) == 0)
                    {
                        isFile = true;
                        break;
                    }
                }
            }
        }
        if (isFile)
            return L"Edit with BowPad";
        return L"Open Folder with BowPad";
    }

    EXPCMDSTATE State(_In_opt_ IShellItemArray* selection) override
    {
        if (m_site)
        {
            ComPtr<IOleWindow> oleWindow;
            m_site.As(&oleWindow);
            if (oleWindow)
            {
                // We don't want to show the menu on the classic context menu.
                // The classic menu provides an IOleWindow, but the main context
                // menu of the left treeview in explorer does too.
                // So we check the window class name: if it's "NamespaceTreeControl",
                // then we're dealing with the main context menu of the tree view.
                // If it's not, then we're dealing with the classic context menu
                // and there we hide this menu entry.
                HWND hWnd = nullptr;
                oleWindow->GetWindow(&hWnd);
                wchar_t szWndClassName[MAX_PATH] = {0};
                GetClassName(hWnd, szWndClassName, _countof(szWndClassName));
                if (wcscmp(szWndClassName, L"NamespaceTreeControl"))
                    return ECS_HIDDEN;
            }
        }
        if (selection)
        {
            DWORD fileCount = 0;
            RETURN_IF_FAILED(selection->GetCount(&fileCount));
            if (fileCount > 0)
                return ECS_ENABLED;
        }
        return ECS_ENABLED;
    }

    IFACEMETHODIMP GetIcon(_In_opt_ IShellItemArray*, _Outptr_result_nullonfailure_ PWSTR* icon) override
    {
        auto bpPath = CPathUtils::GetModuleDir(hDll);
        bpPath += L"\\BowPad.exe,-107";
        auto iconPath = wil::make_cotaskmem_string_nothrow(bpPath.c_str());
        RETURN_IF_NULL_ALLOC(iconPath);
        *icon = iconPath.release();
        return S_OK;
    }

    IFACEMETHODIMP Invoke(_In_opt_ IShellItemArray* selection, _In_opt_ IBindCtx* pCtx) noexcept override
    {
        try
        {
            auto bpPath = CPathUtils::GetModuleDir(hDll);
            bpPath += L"\\BowPad.exe";

            if (selection)
            {
                DWORD fileCount = 0;
                RETURN_IF_FAILED(selection->GetCount(&fileCount));
                for (DWORD i = 0; i < fileCount; i++)
                {
                    IShellItem* shellItem = nullptr;
                    selection->GetItemAt(i, &shellItem);
                    LPWSTR itemName = nullptr;
                    shellItem->GetDisplayName(SIGDN_FILESYSPATH, &itemName);
                    if (itemName)
                    {
                        std::wstring path = L"/path:\"";
                        path += itemName;
                        path += L"\"";
                        CoTaskMemFree(itemName);

                        // try to launch the exe with the explorer instance:
                        // this avoids that the exe is started with the identity of this dll,
                        // starting it as if it was started the normal way.
                        bool                     execSucceeded = false;
                        ComPtr<IServiceProvider> serviceProvider;
                        if (SUCCEEDED(m_site.As(&serviceProvider)))
                        {
                            ComPtr<IShellBrowser> shellBrowser;
                            if (SUCCEEDED(serviceProvider->QueryService(SID_SShellBrowser, IID_IShellBrowser, &shellBrowser)))
                            {
                                ComPtr<IShellView> shellView;
                                if (SUCCEEDED(shellBrowser->QueryActiveShellView(&shellView)))
                                {
                                    ComPtr<IDispatch> spdispView;
                                    if (SUCCEEDED(shellView->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&spdispView))))
                                    {
                                        ComPtr<IShellFolderViewDual> spFolderView;
                                        if (SUCCEEDED(spdispView.As(&spFolderView)))
                                        {
                                            ComPtr<IDispatch> spdispShell;
                                            if (SUCCEEDED(spFolderView->get_Application(&spdispShell)))
                                            {
                                                ComPtr<IShellDispatch2> spdispShell2;
                                                if (SUCCEEDED(spdispShell.As(&spdispShell2)))
                                                {
                                                    // without this, the launched app is not moved to the foreground
                                                    AllowSetForegroundWindow(ASFW_ANY);

                                                    if (SUCCEEDED(spdispShell2->ShellExecute(_bstr_t{bpPath.c_str()},
                                                                                             _variant_t{path.c_str()},
                                                                                             _variant_t{L""},
                                                                                             _variant_t{L"open"},
                                                                                             _variant_t{SW_NORMAL})))
                                                    {
                                                        execSucceeded = true;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (!execSucceeded)
                        {
                            // just in case the shell execute with explorer failed
                            SHELLEXECUTEINFO shExecInfo = {sizeof(SHELLEXECUTEINFO)};

                            shExecInfo.hwnd             = nullptr;
                            shExecInfo.lpVerb           = L"open";
                            shExecInfo.lpFile           = bpPath.c_str();
                            shExecInfo.lpParameters     = path.c_str();
                            shExecInfo.nShow            = SW_NORMAL;
                            ShellExecuteEx(&shExecInfo);
                        }
                    }
                }
            }

            return S_OK;
        }
        CATCH_RETURN();
    }
};

CoCreatableClass(BowPadExplorerCommandHandler);

CoCreatableClassWrlCreatorMapInclude(BowPadExplorerCommandHandler);

STDAPI DllGetActivationFactory(_In_ HSTRING activatableClassId, _COM_Outptr_ IActivationFactory** factory)
{
    return Module<ModuleType::InProc>::GetModule().GetActivationFactory(activatableClassId, factory);
}

STDAPI DllCanUnloadNow()
{
    return Module<InProc>::GetModule().GetObjectCount() == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(_In_ REFCLSID rclsid, _In_ REFIID riid, _COM_Outptr_ void** instance)
{
    return Module<InProc>::GetModule().GetClassObject(rclsid, riid, instance);
}
