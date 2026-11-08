#pragma once

#include <windows.h>

#include <memory>

#include "pty.h"

namespace handoff {

// Stable CLSID of the 67t terminal delegation class.
extern const wchar_t kTerminalClsid[];
extern const CLSID kTerminalClsidValue;

// True when this process was started by COM for a default-terminal handoff.
bool IsEmbeddingLaunch();

// Runs a single-use COM class factory for ITerminalHandoff3, pumps messages
// until one handoff arrives (or the timeout expires), and returns the attached
// session. COM is left initialized on success so the caller can run the app.
std::unique_ptr<PtyHandoff> RunServer(HINSTANCE instance);

}  // namespace handoff
