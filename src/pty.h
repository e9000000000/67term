#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Messages posted by the reader thread to the notify window.
constexpr UINT kPtyDataMessage = WM_APP + 0x211;
constexpr UINT kPtyExitMessage = WM_APP + 0x212;

class PtySession {
public:
    PtySession() = default;
    ~PtySession();

    PtySession(const PtySession&) = delete;
    PtySession& operator=(const PtySession&) = delete;

    static bool available();
    static std::wstring defaultShell();

    bool start(const std::wstring& commandLine, const std::wstring& workingDirectory, int columns,
               int rows, HWND notify, std::wstring& error);

    void write(std::string_view bytes);
    void resize(int columns, int rows);

    // UI-thread drain. Returns false when there was nothing buffered.
    bool drain(std::string& out);
    bool exited() const { return exited_.load(std::memory_order_acquire); }
    bool running() const { return hpc_ != nullptr && !exited_.load(std::memory_order_acquire); }

    void close();

    DWORD processId() const { return pid_; }
    int columns() const { return columns_; }
    int rows() const { return rows_; }

private:
    void readerLoop();
    void waitLoop();
    void notifyExit();

    HPCON hpc_ = nullptr;
    HANDLE input_ = nullptr;
    HANDLE output_ = nullptr;
    HANDLE process_ = nullptr;
    DWORD pid_ = 0;
    HWND notify_ = nullptr;
    std::thread reader_;
    std::thread waiter_;
    std::atomic<bool> exited_{false};
    std::atomic<bool> exitPosted_{false};
    std::atomic<bool> closing_{false};
    int columns_ = 80;
    int rows_ = 24;
    size_t totalPending_ = 0;
    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<std::string> queue_;
    bool readerDone_ = false;

    std::mutex writeMutex_;
};
