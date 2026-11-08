#pragma once

#include <string>

namespace defterm {

constexpr wchar_t kTerminalClsid[] = L"{AE70E244-EDF9-4479-A99B-B17033538EDB}";

// True when 67t is the current per-user default terminal.
bool IsDefault();

// True when a previous default-terminal pair was saved and can be restored.
bool CanRestore();

// Registers the 67t COM terminal server + the OpenConsole handoff proxy and
// points HKCU\Console\%%Startup at them. No administrator rights required.
bool SetAsDefault(std::wstring& error);

// Restores the pair saved by SetAsDefault, or the Windows automatic default.
bool RestorePrevious(std::wstring& error);

}  // namespace defterm
