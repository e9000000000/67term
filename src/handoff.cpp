#include "handoff.h"

#include <objbase.h>
#include <oleauto.h>
#include <shellapi.h>

#include <atomic>
#include <deque>
#include <functional>
#include <iterator>
#include <mutex>
#include <new>
#include <string>

namespace handoff {

const wchar_t kTerminalClsid[] = L"{AE70E244-EDF9-4479-A99B-B17033538EDB}";
const CLSID kTerminalClsidValue = {0xAE70E244,
                                   0xEDF9,
                                   0x4479,
                                   {0xA9, 0x9B, 0xB1, 0x70, 0x33, 0x53, 0x8E, 0xDB}};

namespace {

constexpr UINT kHandoffMessage = WM_APP + 0x320;
constexpr UINT_PTR kHandoffTimeout = 1;
constexpr UINT kHandoffTimeoutMs = 15000;
constexpr wchar_t kHostClass[] = L"67tHandoffHost";

// Included here because the Windows SDK does not ship the delegation IDL.
struct TERMINAL_STARTUP_INFO {
    BSTR pszTitle;
    BSTR pszIconPath;
    LONG iconIndex;
    DWORD dwX;
    DWORD dwY;
    DWORD dwXSize;
    DWORD dwYSize;
    DWORD dwXCountChars;
    DWORD dwYCountChars;
    DWORD dwFillAttribute;
    DWORD dwFlags;
    WORD wShowWindow;
};

struct __declspec(uuid("6F23DA90-15C5-4203-9DB0-64E73F1B1B00")) ITerminalHandoff3Ex
    : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE EstablishPtyHandoff(
        HANDLE* in, HANDLE* out, HANDLE signal, HANDLE reference, HANDLE server, HANDLE client,
        const TERMINAL_STARTUP_INFO* startupInfo) = 0;
};

HWND g_host = nullptr;
DWORD g_cookie = 0;
bool g_registered = false;
bool g_bootstrapped = false;
std::mutex g_mutex;
std::deque<std::unique_ptr<PtyHandoff>> g_queue;
std::function<void()> g_handler;

bool DuplicateHandleLocal(HANDLE source, HANDLE& destination) {
    destination = nullptr;
    return DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &destination, 0,
                           FALSE, DUPLICATE_SAME_ACCESS) != FALSE;
}

std::wstring ClientName(HANDLE process) {
    wchar_t path[MAX_PATH * 2]{};
    DWORD size = static_cast<DWORD>(std::size(path));
    if (!QueryFullProcessImageNameW(process, 0, path, &size)) {
        return {};
    }
    std::wstring value(path, size);
    const size_t slash = value.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        value.erase(0, slash + 1);
    }
    if (value.size() > 4 && _wcsicmp(value.c_str() + value.size() - 4, L".exe") == 0) {
        value.resize(value.size() - 4);
    }
    return value;
}

bool CreateTerminalPipe(HANDLE& server, HANDLE& input, HANDLE& output) {
    wchar_t name[160]{};
    swprintf_s(name, L"\\\\.\\pipe\\67t-defterm-%lu-%llu", GetCurrentProcessId(),
               GetTickCount64());
    server = CreateNamedPipeW(name,
                              PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                  PIPE_REJECT_REMOTE_CLIENTS,
                              1, 128 * 1024, 128 * 1024, 0, nullptr);
    if (server == INVALID_HANDLE_VALUE || !server) {
        server = nullptr;
        return false;
    }
    input = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (input == INVALID_HANDLE_VALUE || !input) {
        input = nullptr;
        CloseHandle(server);
        server = nullptr;
        return false;
    }
    if (!ConnectNamedPipe(server, nullptr)) {
        const DWORD code = GetLastError();
        if (code != ERROR_PIPE_CONNECTED) {
            CloseHandle(input);
            input = nullptr;
            CloseHandle(server);
            server = nullptr;
            return false;
        }
    }
    if (!DuplicateHandle(GetCurrentProcess(), input, GetCurrentProcess(), &output, 0, FALSE,
                         DUPLICATE_SAME_ACCESS)) {
        output = nullptr;
        CloseHandle(input);
        input = nullptr;
        CloseHandle(server);
        server = nullptr;
        return false;
    }
    return true;
}

class TerminalHandoff final : public ITerminalHandoff3Ex {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) {
            return E_POINTER;
        }
        if (iid == IID_IUnknown || iid == __uuidof(ITerminalHandoff3Ex)) {
            *object = static_cast<ITerminalHandoff3Ex*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(++references_);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG value = static_cast<ULONG>(--references_);
        if (value == 0) {
            delete this;
        }
        return value;
    }

    HRESULT STDMETHODCALLTYPE EstablishPtyHandoff(
        HANDLE* in, HANDLE* out, HANDLE signal, HANDLE reference, HANDLE server, HANDLE client,
        const TERMINAL_STARTUP_INFO* startupInfo) override {
        if (!in || !out) {
            return E_UNEXPECTED;
        }
        *in = nullptr;
        *out = nullptr;

        auto handoff = std::make_unique<PtyHandoff>();
        if (!DuplicateHandleLocal(signal, handoff->signal) ||
            !DuplicateHandleLocal(reference, handoff->reference) ||
            !DuplicateHandleLocal(server, handoff->server) ||
            !DuplicateHandleLocal(client, handoff->client)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        handoff->clientName = ClientName(handoff->client);
        if (startupInfo && startupInfo->pszTitle) {
            handoff->title.assign(startupInfo->pszTitle, SysStringLen(startupInfo->pszTitle));
        }

        HANDLE pipeServer = nullptr;
        HANDLE pipeInput = nullptr;
        HANDLE pipeOutput = nullptr;
        if (!CreateTerminalPipe(pipeServer, pipeInput, pipeOutput)) {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        handoff->duplex = pipeServer;
        *in = pipeInput;
        *out = pipeOutput;

        if (!g_host) {
            CloseHandle(*in);
            CloseHandle(*out);
            *in = nullptr;
            *out = nullptr;
            return E_UNEXPECTED;
        }
        {
            std::lock_guard lock(g_mutex);
            g_queue.push_back(std::move(handoff));
        }
        if (!PostMessageW(g_host, kHandoffMessage, 0, 0)) {
            std::lock_guard lock(g_mutex);
            g_queue.pop_back();
            CloseHandle(*in);
            CloseHandle(*out);
            *in = nullptr;
            *out = nullptr;
            return HRESULT_FROM_WIN32(GetLastError());
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> references_{1};
};

class HandoffFactory final : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) {
            return E_POINTER;
        }
        if (iid == IID_IUnknown || iid == IID_IClassFactory) {
            *object = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(++references_);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG value = static_cast<ULONG>(--references_);
        if (value == 0) {
            delete this;
        }
        return value;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** object) override {
        if (outer) {
            return CLASS_E_NOAGGREGATION;
        }
        auto* handoff = new (std::nothrow) TerminalHandoff();
        if (!handoff) {
            return E_OUTOFMEMORY;
        }
        const HRESULT result = handoff->QueryInterface(iid, object);
        handoff->Release();
        return result;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) {
            ++references_;
        } else {
            Release();
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> references_{1};
};

LRESULT CALLBACK HostProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case kHandoffMessage:
        if (g_handler) {
            g_handler();
        } else if (!g_bootstrapped) {
            // Ends the RunServer pump; the handoff stays queued.
            PostQuitMessage(0);
        }
        return 0;
    case WM_TIMER:
        if (wParam == kHandoffTimeout) {
            PostQuitMessage(0);
        }
        return 0;
    case WM_DESTROY:
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool CreateHost(HINSTANCE instance) {
    if (g_host) {
        return true;
    }
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HostProc;
    wc.hInstance = instance;
    wc.lpszClassName = kHostClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    g_host = CreateWindowExW(0, kHostClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance,
                             nullptr);
    return g_host != nullptr;
}

}  // namespace

bool IsEmbeddingLaunch() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return false;
    }
    bool embedding = false;
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"-Embedding") == 0 || _wcsicmp(argv[i], L"/Embedding") == 0) {
            embedding = true;
            break;
        }
    }
    LocalFree(argv);
    return embedding;
}

bool StartServer(HINSTANCE instance) {
    if (g_registered) {
        return true;
    }
    if (!CreateHost(instance)) {
        return false;
    }

    auto* factory = new (std::nothrow) HandoffFactory();
    if (!factory) {
        return false;
    }
    // MULTIPLEUSE keeps this process as the COM server for every delegated
    // console, so each one arrives as a tab instead of a new window.
    const HRESULT registered = CoRegisterClassObject(kTerminalClsidValue, factory,
                                                     CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE,
                                                     &g_cookie);
    factory->Release();
    if (FAILED(registered)) {
        return false;
    }
    g_registered = true;
    return true;
}

std::unique_ptr<PtyHandoff> RunServer(HINSTANCE instance) {
    if (!StartServer(instance)) {
        return nullptr;
    }
    SetTimer(g_host, kHandoffTimeout, kHandoffTimeoutMs, nullptr);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    KillTimer(g_host, kHandoffTimeout);
    g_bootstrapped = true;
    return TakePending();
}

std::unique_ptr<PtyHandoff> TakePending() {
    std::lock_guard lock(g_mutex);
    if (g_queue.empty()) {
        return nullptr;
    }
    auto handoff = std::move(g_queue.front());
    g_queue.pop_front();
    return handoff;
}

void SetHandler(std::function<void()> handler) {
    g_handler = std::move(handler);
    if (!g_handler) {
        return;
    }
    // Replay a handoff that arrived while the main window was being created.
    bool pending = false;
    {
        std::lock_guard lock(g_mutex);
        pending = !g_queue.empty();
    }
    if (pending) {
        g_handler();
    }
}

void StopServer() {
    g_handler = nullptr;
    if (g_registered) {
        CoRevokeClassObject(g_cookie);
        g_registered = false;
    }
    if (g_host) {
        DestroyWindow(g_host);
        g_host = nullptr;
    }
    std::lock_guard lock(g_mutex);
    g_queue.clear();
}

}  // namespace handoff
