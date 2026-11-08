#include "pty.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace {

using CreatePseudoConsoleFn = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, HPCON*);
using ResizePseudoConsoleFn = HRESULT(WINAPI*)(HPCON, COORD);
using ClosePseudoConsoleFn = void(WINAPI*)(HPCON);

struct ConPtyApi {
    CreatePseudoConsoleFn create = nullptr;
    ResizePseudoConsoleFn resize = nullptr;
    ClosePseudoConsoleFn close = nullptr;
    bool ok = false;
};

const ConPtyApi& Api() {
    static const ConPtyApi api = [] {
        ConPtyApi value;
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        if (kernel) {
            value.create = reinterpret_cast<CreatePseudoConsoleFn>(
                GetProcAddress(kernel, "CreatePseudoConsole"));
            value.resize = reinterpret_cast<ResizePseudoConsoleFn>(
                GetProcAddress(kernel, "ResizePseudoConsole"));
            value.close = reinterpret_cast<ClosePseudoConsoleFn>(
                GetProcAddress(kernel, "ClosePseudoConsole"));
        }
        value.ok = value.create && value.resize && value.close;
        return value;
    }();
    return api;
}

std::wstring ErrorText(DWORD error) {
    wchar_t* buffer = nullptr;
    const DWORD count = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer),
        0, nullptr);
    std::wstring text = count && buffer ? std::wstring(buffer, count) : L"Unknown error";
    if (buffer) {
        LocalFree(buffer);
    }
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
        text.pop_back();
    }
    return text + L" (error " + std::to_wstring(error) + L")";
}

std::wstring QuoteIfNeeded(const std::wstring& path) {
    if (path.empty()) {
        return path;
    }
    if (path.front() == L'"') {
        return path;
    }
    if (path.find(L' ') == std::wstring::npos && path.find(L'\t') == std::wstring::npos) {
        return path;
    }
    return L"\"" + path + L"\"";
}

bool FindExecutable(const wchar_t* name, std::wstring& path) {
    wchar_t buffer[MAX_PATH * 2]{};
    const DWORD length = SearchPathW(nullptr, name, nullptr, static_cast<DWORD>(std::size(buffer)),
                                     buffer, nullptr);
    if (length == 0 || length >= std::size(buffer)) {
        return false;
    }
    path.assign(buffer, length);
    return true;
}

bool StartsWithInsensitive(const std::wstring& text, const wchar_t* prefix) {
    const size_t length = wcslen(prefix);
    if (text.size() < length) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if (std::towupper(text[i]) != std::towupper(prefix[i])) {
            return false;
        }
    }
    return true;
}

std::vector<wchar_t> BuildEnvironmentBlock() {
    std::vector<std::wstring> entries;
    if (LPWCH strings = GetEnvironmentStringsW()) {
        for (const wchar_t* cursor = strings; *cursor;) {
            const std::wstring entry(cursor);
            cursor += entry.size() + 1;
            if (StartsWithInsensitive(entry, L"TERM=") ||
                StartsWithInsensitive(entry, L"COLORTERM=") ||
                StartsWithInsensitive(entry, L"WT_SESSION=")) {
                continue;
            }
            entries.push_back(entry);
        }
        FreeEnvironmentStringsW(strings);
    }
    entries.emplace_back(L"TERM=xterm-256color");
    entries.emplace_back(L"COLORTERM=truecolor");

    std::vector<wchar_t> block;
    for (const auto& entry : entries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(0);
    }
    block.push_back(0);
    if (block.size() == 1) {
        block.push_back(0);
    }
    return block;
}

}  // namespace

PtySession::~PtySession() {
    close();
}

bool PtySession::available() {
    return Api().ok;
}

std::wstring PtySession::defaultShell() {
    std::wstring path;
    if (FindExecutable(L"pwsh.exe", path)) {
        return QuoteIfNeeded(path) + L" -NoLogo";
    }
    if (FindExecutable(L"powershell.exe", path)) {
        return QuoteIfNeeded(path) + L" -NoLogo";
    }
    wchar_t comspec[MAX_PATH * 2]{};
    const DWORD length = GetEnvironmentVariableW(L"COMSPEC", comspec,
                                                 static_cast<DWORD>(std::size(comspec)));
    if (length > 0 && length < std::size(comspec)) {
        return QuoteIfNeeded(std::wstring(comspec, length));
    }
    return L"cmd.exe";
}

bool PtySession::start(const std::wstring& commandLine, const std::wstring& workingDirectory,
                       int columns, int rows, HWND notify, std::wstring& error) {
    close();
    if (!Api().ok) {
        error = L"ConPTY is not available on this version of Windows.";
        return false;
    }

    columns = std::max(2, std::min(500, columns));
    rows = std::max(1, std::min(300, rows));
    notify_ = notify;
    columns_ = columns;
    rows_ = rows;
    exited_.store(false);
    closing_.store(false);
    exitPosted_.store(false);
    readerDone_ = false;
    totalPending_ = 0;
    {
        std::lock_guard lock(queueMutex_);
        queue_.clear();
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE ptyInput = nullptr;
    HANDLE inputWrite = nullptr;
    HANDLE outputRead = nullptr;
    HANDLE ptyOutput = nullptr;
    if (!CreatePipe(&ptyInput, &inputWrite, &security, 0) ||
        !CreatePipe(&outputRead, &ptyOutput, &security, 0)) {
        const DWORD code = GetLastError();
        if (ptyInput) CloseHandle(ptyInput);
        if (inputWrite) CloseHandle(inputWrite);
        if (outputRead) CloseHandle(outputRead);
        if (ptyOutput) CloseHandle(ptyOutput);
        error = L"Cannot create the ConPTY pipes: " + ErrorText(code);
        return false;
    }
    SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

    HPCON hpc = nullptr;
    const COORD size{static_cast<SHORT>(columns), static_cast<SHORT>(rows)};
    const HRESULT result = Api().create(size, ptyInput, ptyOutput, 0, &hpc);
    if (FAILED(result) || !hpc) {
        CloseHandle(ptyInput);
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        CloseHandle(ptyOutput);
        error = L"Cannot create the pseudo console: " + ErrorText(static_cast<DWORD>(result));
        return false;
    }
    CloseHandle(ptyInput);
    CloseHandle(ptyOutput);

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    SIZE_T attributeSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeSize);
    std::vector<unsigned char> attributeBuffer(attributeSize);
    startup.lpAttributeList =
        reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeBuffer.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attributeSize)) {
        const DWORD code = GetLastError();
        Api().close(hpc);
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        error = L"Cannot initialize the ConPTY process attributes: " + ErrorText(code);
        return false;
    }
    const BOOL attributeOk = UpdateProcThreadAttribute(
        startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hpc, sizeof(hpc),
        nullptr, nullptr);
    if (!attributeOk) {
        const DWORD code = GetLastError();
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        Api().close(hpc);
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        error = L"Cannot attach the pseudo console to the child: " + ErrorText(code);
        return false;
    }

    std::vector<wchar_t> command(commandLine.begin(), commandLine.end());
    command.push_back(0);
    std::vector<wchar_t> environment = BuildEnvironmentBlock();

    PROCESS_INFORMATION process{};
    const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;
    const BOOL created = CreateProcessW(
        nullptr, command.data(), nullptr, nullptr, FALSE, flags, environment.data(),
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(), &startup.StartupInfo,
        &process);

    DeleteProcThreadAttributeList(startup.lpAttributeList);

    if (!created) {
        const DWORD code = GetLastError();
        Api().close(hpc);
        CloseHandle(inputWrite);
        CloseHandle(outputRead);
        error = L"Cannot start the shell \"" + commandLine + L"\": " + ErrorText(code);
        return false;
    }
    CloseHandle(process.hThread);

    hpc_ = hpc;
    input_ = inputWrite;
    output_ = outputRead;
    process_ = process.hProcess;
    pid_ = process.dwProcessId;
    try {
        reader_ = std::thread(&PtySession::readerLoop, this);
        waiter_ = std::thread(&PtySession::waitLoop, this);
    } catch (...) {
        TerminateProcess(process_, 1);
        Api().close(hpc_);
        hpc_ = nullptr;
        if (reader_.joinable()) {
            reader_.join();
        }
        CloseHandle(process_);
        process_ = nullptr;
        CloseHandle(inputWrite);
        input_ = nullptr;
        CloseHandle(outputRead);
        output_ = nullptr;
        error = L"Cannot start the ConPTY reader thread.";
        return false;
    }
    error.clear();
    return true;
}

void PtySession::write(std::string_view bytes) {
    if (bytes.empty()) {
        return;
    }
    std::lock_guard lock(writeMutex_);
    if (!input_) {
        return;
    }
    const char* data = bytes.data();
    size_t remaining = bytes.size();
    while (remaining > 0) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(remaining, 32 * 1024));
        if (!WriteFile(input_, data, chunk, &written, nullptr) || written == 0) {
            return;
        }
        data += written;
        remaining -= written;
    }
}

void PtySession::resize(int columns, int rows) {
    columns = std::max(2, std::min(500, columns));
    rows = std::max(1, std::min(300, rows));
    if (!hpc_ || (columns == columns_ && rows == rows_)) {
        columns_ = columns;
        rows_ = rows;
        return;
    }
    const COORD size{static_cast<SHORT>(columns), static_cast<SHORT>(rows)};
    Api().resize(hpc_, size);
    columns_ = columns;
    rows_ = rows;
}

bool PtySession::drain(std::string& out) {
    std::deque<std::string> chunks;
    {
        std::lock_guard lock(queueMutex_);
        if (queue_.empty()) {
            return false;
        }
        chunks.swap(queue_);
        totalPending_ = 0;
    }
    queueCv_.notify_all();
    size_t total = 0;
    for (const auto& chunk : chunks) {
        total += chunk.size();
    }
    out.clear();
    out.reserve(total);
    for (auto& chunk : chunks) {
        out.append(chunk);
    }
    return true;
}

void PtySession::readerLoop() {
    constexpr size_t kMaxPending = 8ull * 1024ull * 1024ull;
    std::vector<char> buffer(16384);
    for (;;) {
        DWORD read = 0;
        const BOOL ok = ReadFile(output_, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                                 nullptr);
        if (!ok || read == 0) {
            break;
        }
        std::unique_lock lock(queueMutex_);
        if (closing_.load(std::memory_order_acquire)) {
            break;
        }
        while (totalPending_ >= kMaxPending && !closing_.load(std::memory_order_acquire)) {
            queueCv_.wait_for(lock, std::chrono::milliseconds(100));
        }
        if (closing_.load(std::memory_order_acquire)) {
            break;
        }
        const bool wasEmpty = queue_.empty();
        queue_.emplace_back(buffer.data(), static_cast<size_t>(read));
        totalPending_ += static_cast<size_t>(read);
        lock.unlock();
        if (wasEmpty && notify_) {
            PostMessageW(notify_, kPtyDataMessage, 0, 0);
        }
    }
    {
        std::lock_guard lock(queueMutex_);
        readerDone_ = true;
    }
    queueCv_.notify_all();
    exited_.store(true, std::memory_order_release);
    notifyExit();
}

void PtySession::waitLoop() {
    if (!process_) {
        return;
    }
    WaitForSingleObject(process_, INFINITE);
    // Give ConPTY a moment to flush the last output through the pipe.
    Sleep(120);
    exited_.store(true, std::memory_order_release);
    notifyExit();
}

void PtySession::notifyExit() {
    if (closing_.load(std::memory_order_acquire)) {
        return;
    }
    if (exitPosted_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    if (notify_) {
        PostMessageW(notify_, kPtyExitMessage, 0, 0);
    }
}

void PtySession::close() {
    if (closing_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    queueCv_.notify_all();

    if (process_) {
        DWORD code = 0;
        if (GetExitCodeProcess(process_, &code) && code == STILL_ACTIVE) {
            TerminateProcess(process_, 1);
        }
    }
    if (hpc_) {
        Api().close(hpc_);
        hpc_ = nullptr;
    }
    if (input_) {
        CloseHandle(input_);
        input_ = nullptr;
    }
    if (reader_.joinable()) {
        reader_.join();
    }
    if (waiter_.joinable()) {
        waiter_.join();
    }
    if (output_) {
        CloseHandle(output_);
        output_ = nullptr;
    }
    if (process_) {
        WaitForSingleObject(process_, 2000);
        CloseHandle(process_);
        process_ = nullptr;
    }
    exited_.store(true, std::memory_order_release);
}
