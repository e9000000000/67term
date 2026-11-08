#include "singleinstance.h"

#include <cstring>
#include <cwchar>
#include <utility>
#include <vector>

#include "cli.h"

namespace singleinstance {
namespace {

constexpr wchar_t kHostClass[] = L"67tInstanceHost";
constexpr wchar_t kMutexName[] = L"Local\\67t.SingleInstance.7A1B";
constexpr ULONG_PTR kRequestMagic = 0x36377431;  // "67t1"
constexpr size_t kWireText = 2048;
constexpr DWORD kForwardTimeoutMs = 5000;
constexpr DWORD kMutexTimeoutMs = 10000;

struct WireRequest {
    wchar_t cwd[kWireText];
    wchar_t shell[kWireText];
};

HWND g_host = nullptr;
std::function<void(std::wstring, std::wstring)> g_handler;
std::vector<std::pair<std::wstring, std::wstring>> g_pending;

void CopyText(wchar_t (&destination)[kWireText], const std::wstring& source) {
    const size_t count = source.size() < kWireText - 1 ? source.size() : kWireText - 1;
    std::wmemcpy(destination, source.c_str(), count);
    destination[count] = L'\0';
}

void Dispatch(const WireRequest& request) {
    std::wstring cwd(request.cwd);
    std::wstring shell(request.shell);
    if (g_handler) {
        g_handler(std::move(cwd), std::move(shell));
    } else {
        g_pending.emplace_back(std::move(cwd), std::move(shell));
    }
}

LRESULT CALLBACK HostProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_COPYDATA: {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (!data || data->dwData != kRequestMagic || data->cbData != sizeof(WireRequest) ||
            !data->lpData) {
            return 0;
        }
        WireRequest request{};
        std::memcpy(&request, data->lpData, sizeof(request));
        request.cwd[kWireText - 1] = L'\0';
        request.shell[kWireText - 1] = L'\0';
        Dispatch(request);
        return 1;
    }
    case WM_DESTROY:
        g_host = nullptr;
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

bool CreateHost(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HostProc;
    wc.hInstance = instance;
    wc.lpszClassName = kHostClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    // A hidden top-level window (rather than HWND_MESSAGE) so FindWindow can
    // locate it from another process without joining any message-only tree.
    g_host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kHostClass, L"", WS_POPUP, 0, 0,
                             0, 0, nullptr, nullptr, instance, nullptr);
    return g_host != nullptr;
}

// Must be called with the startup mutex held.
void CreateHostIfNone(HINSTANCE instance) {
    if (!FindWindowW(kHostClass, nullptr)) {
        CreateHost(instance);
    }
}

}  // namespace

bool ForwardOrBecomePrimary(const LaunchOptions& options, HINSTANCE instance) {
    // A named mutex serializes startup so two simultaneous launches cannot both
    // decide they are the primary instance.
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (mutex) {
        WaitForSingleObject(mutex, kMutexTimeoutMs);
    }

    HWND existing = FindWindowW(kHostClass, nullptr);
    if (existing) {
        WireRequest request{};
        CopyText(request.cwd, options.cwd);
        CopyText(request.shell, options.shell);

        COPYDATASTRUCT data{};
        data.dwData = kRequestMagic;
        data.cbData = sizeof(request);
        data.lpData = &request;

        DWORD_PTR result = 0;
        const LRESULT sent =
            SendMessageTimeoutW(existing, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                                SMTO_ABORTIFHUNG | SMTO_BLOCK, kForwardTimeoutMs, &result);
        if (sent != 0 && result == 1) {
            if (mutex) {
                ReleaseMutex(mutex);
                CloseHandle(mutex);
            }
            return true;
        }
        // The other window exists but did not accept the request (for example
        // it is shutting down); fall through and open our own window.
    }

    CreateHostIfNone(instance);
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return false;
}

void BecomePrimary(HINSTANCE instance) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (mutex) {
        WaitForSingleObject(mutex, kMutexTimeoutMs);
    }
    CreateHostIfNone(instance);
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
}

void SetHandler(std::function<void(std::wstring, std::wstring)> handler) {
    g_handler = std::move(handler);
    auto pending = std::move(g_pending);
    g_pending.clear();
    for (auto& request : pending) {
        if (g_handler) {
            g_handler(std::move(request.first), std::move(request.second));
        }
    }
}

void Shutdown() {
    g_handler = nullptr;
    g_pending.clear();
    if (g_host) {
        DestroyWindow(g_host);
        g_host = nullptr;
    }
}

}  // namespace singleinstance
