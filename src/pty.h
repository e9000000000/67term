#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Messages posted by the reader thread to the notify window.
constexpr UINT kPtyDataMessage = WM_APP + 0x211;
constexpr UINT kPtyExitMessage = WM_APP + 0x212;

// Handles and metadata received from console delegation (Windows default
// terminal handoff). The terminal owns these after the COM call returns.
struct PtyHandoff {
    HANDLE duplex = nullptr;     // our side of the terminal I/O pipe (v3)
    HANDLE input = nullptr;      // terminal input pipe (v1/v2)
    HANDLE output = nullptr;     // terminal output pipe (v1/v2)
    HANDLE signal = nullptr;
    HANDLE reference = nullptr;
    HANDLE server = nullptr;     // OpenConsole process
    HANDLE client = nullptr;     // console client process
    std::wstring title;
    std::wstring clientName;

    PtyHandoff() = default;
    ~PtyHandoff();
    PtyHandoff(const PtyHandoff&) = delete;
    PtyHandoff& operator=(const PtyHandoff&) = delete;
    PtyHandoff(PtyHandoff&& other) noexcept;
    PtyHandoff& operator=(PtyHandoff&& other) noexcept;
    void close();
};

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

    // Attach to a console session handed off by Windows default-terminal
    // delegation. No child process is spawned by us.
    bool startAttached(std::unique_ptr<PtyHandoff> handoff, int columns, int rows, HWND notify,
                       std::wstring& error);

    void write(std::string_view bytes);
    void resize(int columns, int rows);

    // UI-thread drain. Returns false when there was nothing buffered.
    bool drain(std::string& out);
    bool exited() const { return exited_.load(std::memory_order_acquire); }
    bool running() const;
    bool attached() const { return attached_; }

    void close();

    DWORD processId() const { return pid_; }
    int columns() const { return columns_; }
    int rows() const { return rows_; }

private:
    void readerLoop();
    void writerLoop();
    void waitLoop();
    void notifyExit();
    void sendAttachedResize(int columns, int rows);
    void startWriter();
    void stopWriter();

    HPCON hpc_ = nullptr;
    HANDLE input_ = nullptr;
    HANDLE output_ = nullptr;
    HANDLE process_ = nullptr;
    DWORD pid_ = 0;
    HWND notify_ = nullptr;
    bool attached_ = false;
    HANDLE duplex_ = nullptr;
    HANDLE attachSignal_ = nullptr;
    HANDLE attachReference_ = nullptr;
    HANDLE attachServer_ = nullptr;
    HANDLE attachClient_ = nullptr;
    std::thread reader_;
    std::thread writer_;
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
    std::condition_variable writeCv_;
    std::deque<std::string> writeQueue_;
    size_t writePending_ = 0;

    // Signalled on close so the waiter thread never depends on the client
    // process exiting first.
    HANDLE stopEvent_ = nullptr;
};
