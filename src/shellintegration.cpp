#include "shellintegration.h"

#include <windows.h>

#include <cwctype>
#include <string>
#include <vector>

#include "util.h"

namespace shellint {

const wchar_t kScriptName[] = L"shell-integration.ps1";

namespace {

const wchar_t* kScript = LR"PS(
$67tEsc = [char]27
$global:__67tOriginalPrompt = $function:prompt
$global:__67tLastPath = ''
function global:prompt {
    $67tPath = $ExecutionContext.SessionState.Path.CurrentLocation.Path
    if ($global:__67tLastPath -ne $67tPath) {
        $global:__67tLastPath = $67tPath
        [Console]::Write("$67tEsc]7;file:///$($67tPath.Replace('\','/'))$67tEsc\")
    }
    if ($global:__67tOriginalPrompt) { & $global:__67tOriginalPrompt } else { "PS $($ExecutionContext.SessionState.Path.CurrentLocation)> " }
}
$67tPath = $ExecutionContext.SessionState.Path.CurrentLocation.Path
[Console]::Write("$67tEsc]7;file:///$($67tPath.Replace('\','/'))$67tEsc\")
$global:__67tLastPath = $67tPath
)PS";

std::wstring Lower(std::wstring value) {
    for (wchar_t& ch : value) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return value;
}

std::vector<std::wstring> Tokens(const std::wstring& commandLine) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    bool quoted = false;
    for (const wchar_t ch : commandLine) {
        if (ch == L'"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && (ch == L' ' || ch == L'\t')) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

bool IsPowerShellToken(std::wstring token) {
    const size_t slash = token.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        token.erase(0, slash + 1);
    }
    token = Lower(std::move(token));
    if (token.size() > 4 && token.compare(token.size() - 4, 4, L".exe") == 0) {
        token.resize(token.size() - 4);
    }
    return token == L"powershell" || token == L"pwsh";
}

}  // namespace

const wchar_t* PowerShellScript() {
    return kScript;
}

bool IsPowerShell(const std::wstring& commandLine) {
    const std::vector<std::wstring> tokens = Tokens(commandLine);
    return !tokens.empty() && IsPowerShellToken(tokens.front());
}

bool HasScriptArgument(const std::wstring& commandLine) {
    const std::vector<std::wstring> tokens = Tokens(commandLine);
    for (size_t i = 1; i < tokens.size(); ++i) {
        const std::wstring token = Lower(tokens[i]);
        if (token == L"-command" || token == L"-c" || token == L"-encodedcommand" ||
            token == L"-e" || token == L"-ec" || token == L"-file" || token == L"-f") {
            return true;
        }
    }
    return false;
}

std::wstring Prepare(const std::wstring& commandLine) {
    if (!IsPowerShell(commandLine) || HasScriptArgument(commandLine)) {
        return commandLine;
    }
    const std::wstring scriptPath = DataDirectory() + L"\\" + kScriptName;
    std::wstring error;
    if (!WriteTextFileAtomic(scriptPath, WideToUtf8(kScript), error)) {
        return commandLine;
    }
    std::wstring quoted;
    quoted.reserve(scriptPath.size() + 4);
    for (const wchar_t ch : scriptPath) {
        quoted.push_back(ch);
        if (ch == L'\'') {
            quoted.push_back(ch);
        }
    }
    return commandLine + L" -NoExit -Command \". '" + quoted + L"'\"";
}

}  // namespace shellint
