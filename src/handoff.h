#pragma once

#include <windows.h>

#include <functional>
#include <memory>

#include "pty.h"

namespace handoff {

// Stable CLSID of the 67t terminal delegation class.
extern const wchar_t kTerminalClsid[];
extern const CLSID kTerminalClsidValue;

// True when this process was started by COM for a default-terminal handoff.
bool IsEmbeddingLaunch();

// Registers the terminal COM class for the lifetime of this process. While the
// registration exists COM hands new delegated consoles to this process instead
// of launching another 67t.exe, so they can open as tabs.
bool StartServer(HINSTANCE instance);

// Bootstrap for `-Embedding`: registers the class, pumps until the first
// handoff arrives, and returns it. The registration stays active afterwards.
std::unique_ptr<PtyHandoff> RunServer(HINSTANCE instance);

// Removes one queued handoff, or null when none is waiting.
std::unique_ptr<PtyHandoff> TakePending();

// Called on the UI thread whenever a handoff arrives (immediately if one is
// already queued).
void SetHandler(std::function<void()> handler);

// Revokes the class registration and destroys the hidden host window.
void StopServer();

}  // namespace handoff
