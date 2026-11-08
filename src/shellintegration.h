#pragma once

#include <string>

namespace shellint {

// File name of the PowerShell integration script, written next to settings.
extern const wchar_t kScriptName[];

// Returns the PowerShell script that emits OSC 7 when the location changes.
const wchar_t* PowerShellScript();

// True when the command line starts powershell.exe or pwsh.exe.
bool IsPowerShell(const std::wstring& commandLine);

// True when the command line already supplies -Command/-File/-EncodedCommand.
bool HasScriptArgument(const std::wstring& commandLine);

// Installs the integration script if the shell is PowerShell (and the user did
// not already pass a script), then returns the command line to launch. Other
// shells are returned unchanged.
std::wstring Prepare(const std::wstring& commandLine);

}  // namespace shellint
