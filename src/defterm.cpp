#include "defterm.h"

#include <windows.h>

#include <iterator>
#include <string>
#include <vector>

#include "util.h"

namespace defterm {
namespace {

constexpr wchar_t kClasses[] = L"Software\\Classes";
constexpr wchar_t kStartupKey[] = L"Console\\%%Startup";
constexpr wchar_t kBackupKey[] = L"Software\\67t\\DefaultTerminal";
constexpr wchar_t kProxyClsid[] = L"{3171DE52-6EFA-4AEF-8A9F-D02BD67E7A4F}";
constexpr wchar_t kInterface3[] = L"{6F23DA90-15C5-4203-9DB0-64E73F1B1B00}";
constexpr wchar_t kInterface2[] = L"{AA6B364F-4A50-4176-9002-0AE755E7B5EF}";
constexpr wchar_t kInterface1[] = L"{59D55CCE-FC8A-48B4-ACE8-0A9286C6557F}";
constexpr wchar_t kZeroGuid[] = L"{00000000-0000-0000-0000-000000000000}";
constexpr wchar_t kPackagesKey[] =
    L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\"
    L"AppModel\\Repository\\Packages";

bool ReadString(HKEY root, const wchar_t* subkey, const wchar_t* name, std::wstring& value) {
    wchar_t buffer[1024]{};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const LSTATUS status = RegGetValueW(root, subkey, name, RRF_RT_REG_SZ, &type, buffer, &size);
    if (status != ERROR_SUCCESS) {
        return false;
    }
    value.assign(buffer);
    return true;
}

bool WriteString(HKEY root, const wchar_t* subkey, const wchar_t* name,
                 const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS status =
        RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool WriteDword(HKEY root, const wchar_t* subkey, const wchar_t* name, DWORD value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS status = RegSetValueExW(key, name, 0, REG_DWORD,
                                          reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

bool ReadDword(HKEY root, const wchar_t* subkey, const wchar_t* name, DWORD& value) {
    DWORD size = sizeof(value);
    DWORD type = 0;
    return RegGetValueW(root, subkey, name, RRF_RT_REG_DWORD, &type, &value, &size) ==
           ERROR_SUCCESS;
}

std::wstring ExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size() - 1) {
            buffer.resize(length);
            return buffer;
        }
        buffer.resize(buffer.size() * 2);
    }
}

bool StartsWith(const std::wstring& text, const wchar_t* prefix) {
    return text.rfind(prefix, 0) == 0;
}

bool FindOpenConsole(std::wstring& proxyPath, std::wstring& consoleClsid,
                     std::wstring& error) {
    HKEY packages = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kPackagesKey, 0, KEY_READ, &packages) != ERROR_SUCCESS) {
        error = L"Windows Terminal is not installed for this user. Its OpenConsole host is "
                L"required for default-terminal delegation.";
        return false;
    }

    struct Candidate {
        std::wstring package;
        const wchar_t* clsid;
    };
    const Candidate candidates[] = {
        {L"Microsoft.WindowsTerminal_", L"{2EACA947-7F5F-4CFA-BA87-8F7FBEEFBE69}"},
        {L"Microsoft.WindowsTerminalPreview_", L"{06EC847C-C0A5-46B8-92CB-7C92F6E35CD5}"},
        {L"Microsoft.WindowsTerminalCanary_", L"{A854D02A-F2FE-44A5-BB24-D03F4CF830D4}"},
        {L"Microsoft.WindowsTerminalDev_", L"{1F9F2BF5-5BC3-4F17-B0E6-912413F1F451}"},
    };

    bool found = false;
    for (const auto& candidate : candidates) {
        for (DWORD index = 0;; ++index) {
            wchar_t name[256]{};
            DWORD length = static_cast<DWORD>(std::size(name));
            const LSTATUS status = RegEnumKeyExW(packages, index, name, &length, nullptr, nullptr,
                                                 nullptr, nullptr);
            if (status == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (status != ERROR_SUCCESS) {
                break;
            }
            const std::wstring packageName(name, length);
            if (!StartsWith(packageName, candidate.package.c_str())) {
                continue;
            }
            std::wstring root;
            if (!ReadString(HKEY_CURRENT_USER, (std::wstring(kPackagesKey) + L"\\" + packageName).c_str(),
                            L"PackageRootFolder", root)) {
                continue;
            }
            const std::wstring dll = root + L"\\OpenConsoleProxy.dll";
            if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
                continue;
            }
            proxyPath = dll;
            consoleClsid = candidate.clsid;
            found = true;
            break;
        }
        if (found) {
            break;
        }
    }
    RegCloseKey(packages);
    if (!found) {
        error = L"Windows Terminal's OpenConsoleProxy.dll was not found. Install or repair "
                L"Windows Terminal, then try again.";
        return false;
    }
    return true;
}

bool RegisterServer(const std::wstring& proxySource, std::wstring& error) {
    const std::wstring exe = ExecutablePath();
    if (exe.empty()) {
        error = L"Cannot resolve the 67t executable path.";
        return false;
    }

    // The proxy/stub DLL lives inside the Windows Terminal package, whose
    // directory cannot be loaded by an ordinary process. Copy it somewhere
    // loadable by both OpenConsole and 67t, then register that copy.
    const std::wstring proxyPath = DataDirectory() + L"\\OpenConsoleProxy.dll";
    if (!EnsureDirectory(DataDirectory())) {
        error = L"Cannot create the 67t data directory.";
        return false;
    }
    if (!CopyFileW(proxySource.c_str(), proxyPath.c_str(), FALSE) &&
        GetFileAttributesW(proxyPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const DWORD code = GetLastError();
        error = L"Cannot install the terminal handoff proxy: " + GetLastErrorText(code);
        return false;
    }

    const std::wstring clsids = std::wstring(kClasses) + L"\\CLSID\\" + kTerminalClsid;
    const std::wstring localServer = clsids + L"\\LocalServer32";
    if (!WriteString(HKEY_CURRENT_USER, clsids.c_str(), nullptr, L"67t Terminal") ||
        !WriteString(HKEY_CURRENT_USER, localServer.c_str(), nullptr,
                     L"\"" + exe + L"\" -Embedding")) {
        error = L"Cannot register the 67t COM terminal server.";
        return false;
    }

    const std::wstring proxyClsidPath = std::wstring(kClasses) + L"\\CLSID\\" + kProxyClsid;
    const std::wstring proxyInproc = proxyClsidPath + L"\\InprocServer32";
    if (!WriteString(HKEY_CURRENT_USER, proxyClsidPath.c_str(), nullptr,
                     L"OpenConsoleHandoffProxy") ||
        !WriteString(HKEY_CURRENT_USER, proxyInproc.c_str(), nullptr, proxyPath) ||
        !WriteString(HKEY_CURRENT_USER, proxyInproc.c_str(), L"ThreadingModel", L"Both")) {
        error = L"Cannot register the OpenConsole handoff proxy.";
        return false;
    }

    for (const wchar_t* iid : {kInterface3, kInterface2, kInterface1}) {
        const std::wstring interfaceKey = std::wstring(kClasses) + L"\\Interface\\" + iid;
        const std::wstring proxyKey = interfaceKey + L"\\ProxyStubClsid32";
        if (!WriteString(HKEY_CURRENT_USER, interfaceKey.c_str(), nullptr, L"ITerminalHandoff") ||
            !WriteDword(HKEY_CURRENT_USER, interfaceKey.c_str(), L"NumMethods", 4) ||
            !WriteString(HKEY_CURRENT_USER, proxyKey.c_str(), nullptr, kProxyClsid)) {
            error = L"Cannot register the terminal handoff interface proxy.";
            return false;
        }
    }
    return true;
}

}  // namespace

bool IsDefault() {
    std::wstring terminal;
    if (!ReadString(HKEY_CURRENT_USER, kStartupKey, L"DelegationTerminal", terminal)) {
        return false;
    }
    return _wcsicmp(terminal.c_str(), kTerminalClsid) == 0;
}

bool CanRestore() {
    DWORD present = 0;
    return IsDefault() || (ReadDword(HKEY_CURRENT_USER, kBackupKey, L"BackupPresent", present) &&
                           present != 0);
}

bool SetAsDefault(std::wstring& error) {
    std::wstring proxyPath;
    std::wstring consoleClsid;
    if (!FindOpenConsole(proxyPath, consoleClsid, error)) {
        return false;
    }
    if (!RegisterServer(proxyPath, error)) {
        return false;
    }

    if (!IsDefault()) {
        std::wstring oldConsole;
        std::wstring oldTerminal;
        ReadString(HKEY_CURRENT_USER, kStartupKey, L"DelegationConsole", oldConsole);
        ReadString(HKEY_CURRENT_USER, kStartupKey, L"DelegationTerminal", oldTerminal);
        if (oldConsole.empty()) {
            oldConsole = kZeroGuid;
        }
        if (oldTerminal.empty()) {
            oldTerminal = kZeroGuid;
        }
        if (!WriteString(HKEY_CURRENT_USER, kBackupKey, L"PreviousConsole", oldConsole) ||
            !WriteString(HKEY_CURRENT_USER, kBackupKey, L"PreviousTerminal", oldTerminal) ||
            !WriteDword(HKEY_CURRENT_USER, kBackupKey, L"BackupPresent", 1)) {
            error = L"Cannot save the current default-terminal setting.";
            return false;
        }
    }

    if (!WriteString(HKEY_CURRENT_USER, kStartupKey, L"DelegationConsole", consoleClsid) ||
        !WriteString(HKEY_CURRENT_USER, kStartupKey, L"DelegationTerminal", kTerminalClsid)) {
        error = L"Cannot write the default-terminal registry values.";
        return false;
    }

    std::wstring verify;
    if (!ReadString(HKEY_CURRENT_USER, kStartupKey, L"DelegationTerminal", verify) ||
        _wcsicmp(verify.c_str(), kTerminalClsid) != 0) {
        error = L"The default-terminal setting did not stick.";
        return false;
    }
    error.clear();
    return true;
}

bool RestorePrevious(std::wstring& error) {
    std::wstring previousConsole;
    std::wstring previousTerminal;
    DWORD present = 0;
    if (ReadDword(HKEY_CURRENT_USER, kBackupKey, L"BackupPresent", present) && present != 0) {
        ReadString(HKEY_CURRENT_USER, kBackupKey, L"PreviousConsole", previousConsole);
        ReadString(HKEY_CURRENT_USER, kBackupKey, L"PreviousTerminal", previousTerminal);
    }
    if (previousConsole.empty()) {
        previousConsole = kZeroGuid;
    }
    if (previousTerminal.empty()) {
        previousTerminal = kZeroGuid;
    }
    if (!WriteString(HKEY_CURRENT_USER, kStartupKey, L"DelegationConsole", previousConsole) ||
        !WriteString(HKEY_CURRENT_USER, kStartupKey, L"DelegationTerminal", previousTerminal)) {
        error = L"Cannot restore the previous default-terminal setting.";
        return false;
    }
    // One restore per saved setting; a later "make default" saves a new pair.
    WriteDword(HKEY_CURRENT_USER, kBackupKey, L"BackupPresent", 0);
    error.clear();
    return true;
}

}  // namespace defterm
