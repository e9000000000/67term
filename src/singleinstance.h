#pragma once

#include <windows.h>

#include <functional>
#include <string>

struct LaunchOptions;

namespace singleinstance {

// Called once at startup for normal (non-embedding) launches. If another 67t
// window is already running, forwards this launch to it as a new tab and
// returns true; the caller should exit. Otherwise this process becomes the
// primary instance, its receiver window has been created, and the caller should
// create the window as usual.
bool ForwardOrBecomePrimary(const LaunchOptions& options, HINSTANCE instance);

// Installed by the primary instance before the message loop starts. Requests
// received earlier are queued and replayed here.
void SetHandler(std::function<void(std::wstring cwd, std::wstring shell)> handler);

// Destroys the receiver window when the primary instance shuts down.
void Shutdown();

}  // namespace singleinstance
