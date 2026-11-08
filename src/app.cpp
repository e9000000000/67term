#include "app.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "accel.h"
#include "commands.h"
#include "theme.h"
#include "util.h"
#include "windowrect.h"

namespace {

constexpr wchar_t kMainClass[] = L"67tMainWindow";
constexpr wchar_t kAppName[] = L"67t";
constexpr UINT_PTR kTimerStatus = 1;
constexpr UINT kStatusIntervalMs = 250;
constexpr int kDefaultFontSize = 16;

void EnableDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }
}

std::wstring ShellNameFromCommand(const std::wstring& commandLine) {
    std::wstring path = commandLine;
    if (!path.empty() && path.front() == L'"') {
        const size_t end = path.find(L'"', 1);
        if (end != std::wstring::npos) {
            path = path.substr(1, end - 1);
        }
    } else {
        const size_t space = path.find(L' ');
        if (space != std::wstring::npos) {
            path = path.substr(0, space);
        }
    }
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".exe") == 0) {
        name.resize(name.size() - 4);
    }
    return name.empty() ? L"shell" : name;
}

std::wstring FriendlyTabTitle(const std::wstring& title) {
    if (title.size() >= 3 && title[1] == L':' &&
        (title[2] == L'\\' || title[2] == L'/')) {
        std::wstring name = FileNameOf(title);
        if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".exe") == 0) {
            name.resize(name.size() - 4);
        }
        if (!name.empty()) {
            return name;
        }
    }
    if (title.rfind(L"\\\\", 0) == 0) {
        std::wstring name = FileNameOf(title);
        if (!name.empty()) {
            return name;
        }
    }
    return title;
}

bool IsDirectory(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

void CenterWindow(HWND hwnd, int width, int height) {
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int workWidth = work.right - work.left;
    const int workHeight = work.bottom - work.top;
    width = std::min(width, std::max(640, workWidth - 60));
    height = std::min(height, std::max(400, workHeight - 60));
    const int x = work.left + (workWidth - width) / 2;
    const int y = work.top + (workHeight - height) / 2;
    SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

MenuItem MakeMenuItem(UINT id, const wchar_t* label, const wchar_t* shortcut, bool enabled,
                      bool checked = false) {
    MenuItem item;
    item.id = id;
    item.label = label;
    item.shortcut = shortcut;
    item.enabled = enabled;
    item.checked = checked;
    return item;
}

MenuItem MakeSeparator() {
    MenuItem item;
    item.separator = true;
    return item;
}

}  // namespace

App::App(HINSTANCE instance) : instance_(instance) {}

App::~App() {
    if (backgroundBrush_) {
        DeleteObject(backgroundBrush_);
    }
}

bool App::create(const LaunchOptions& options) {
    options_ = options;
    EnsureDirectory(DataDirectory());
    LoadSettings(settings_);
    shellCommand_ = chooseShell();

    backgroundBrush_ = CreateSolidBrush(theme::kBg);
    if (!backgroundBrush_) {
        return false;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance_;
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(instance_, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = backgroundBrush_;
    wc.lpszClassName = kMainClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }

    hwnd_ = CreateWindowExW(WS_EX_ACCEPTFILES, kMainClass, kAppName,
                            (WS_OVERLAPPEDWINDOW & ~WS_CAPTION) | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, 1200, 780, nullptr, nullptr, instance_,
                            this);
    if (!hwnd_) {
        return false;
    }

    dpi_ = DpiOfWindow(hwnd_);
    EnableDarkTitleBar(hwnd_);
    AllowDarkModeForWindow(hwnd_);
    createAccelerators();

    if (!menuBar_.create(hwnd_, instance_)) {
        return false;
    }
    if (!tabBar_.create(hwnd_, instance_)) {
        return false;
    }
    if (!statusBar_.create(hwnd_, instance_)) {
        return false;
    }

    bool placed = false;
    if (settings_.hasWindow) {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const HMONITOR monitor = MonitorFromRect(&settings_.window, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (monitor && GetMonitorInfoW(monitor, &info)) {
            work = info.rcWork;
        }
        const RECT clamped = windowrect::ClampToWork(settings_.window, work);
        const int width = clamped.right - clamped.left;
        const int height = clamped.bottom - clamped.top;
        if (width >= 400 && height >= 260) {
            SetWindowPos(hwnd_, nullptr, clamped.left, clamped.top, width, height,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            placed = true;
        }
    }
    if (!placed) {
        CenterWindow(hwnd_, theme::Scale(1200, dpi_), theme::Scale(780, dpi_));
    }

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    layout();
    addTab(options_.cwd);
    if (settings_.maximized) {
        ShowWindow(hwnd_, SW_MAXIMIZE);
    }
    SetTimer(hwnd_, kTimerStatus, kStatusIntervalMs, nullptr);
    if (TermTab* tab = activeTab()) {
        tab->view->focus();
    }
    sessionReady_ = true;
    return true;
}

int App::run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (accel_ && TranslateAcceleratorW(hwnd_, accel_, &message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

std::wstring App::chooseShell() const {
    if (!options_.shell.empty()) {
        return options_.shell;
    }
    if (!settings_.shell.empty()) {
        return settings_.shell;
    }
    return PtySession::defaultShell();
}

std::wstring App::shellDisplayName() const {
    return ShellNameFromCommand(shellCommand_);
}

std::wstring App::defaultWorkingDirectory() {
    if (TermTab* tab = activeTab()) {
        const std::wstring& cwd = tab->view->workingDirectory();
        if (!cwd.empty() && IsDirectory(cwd)) {
            return cwd;
        }
    }
    if (!options_.cwd.empty() && IsDirectory(options_.cwd)) {
        return options_.cwd;
    }
    wchar_t buffer[MAX_PATH * 2]{};
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", buffer,
                                                 static_cast<DWORD>(std::size(buffer)));
    if (length > 0 && length < std::size(buffer) && IsDirectory(buffer)) {
        return buffer;
    }
    const DWORD cwdLength = GetCurrentDirectoryW(static_cast<DWORD>(std::size(buffer)), buffer);
    if (cwdLength > 0 && cwdLength < std::size(buffer)) {
        return buffer;
    }
    return L".";
}

void App::createAccelerators() {
    accel_ = CreateAppAccelerators();
}

TermTab* App::activeTab() {
    if (active_ < 0 || active_ >= static_cast<int>(tabs_.size())) {
        return nullptr;
    }
    return tabs_[static_cast<size_t>(active_)].get();
}

int App::indexOfView(HWND view) const {
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (tabs_[i]->view->hwnd() == view) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

TermTab& App::addTab(const std::wstring& workingDirectory) {
    auto tab = std::make_unique<TermTab>();
    tab->view = std::make_unique<TerminalView>();
    const bool created = tab->view->create(hwnd_, instance_);
    if (!created) {
        tab->view.reset();
        tab->activity = true;
        tab->exited = true;
        tab->lastTitle = L"Failed to create terminal";
        tabs_.push_back(std::move(tab));
        active_ = static_cast<int>(tabs_.size()) - 1;
        updateTabBar();
        updateStatus();
        return *tabs_.back();
    }
    tab->view->setAppearance(settings_.fontFamily, settings_.fontSize, settings_.cursorBlink,
                             settings_.scrollback);
    tab->lastTitle = shellDisplayName();
    tabs_.push_back(std::move(tab));
    active_ = static_cast<int>(tabs_.size()) - 1;
    layout();
    updateTabBar();
    TermTab& added = *tabs_.back();
    const std::wstring cwd =
        workingDirectory.empty() ? defaultWorkingDirectory() : workingDirectory;
    if (!added.view->start(shellCommand_, cwd, shellDisplayName())) {
        added.exited = true;
    }
    updateTabBar();
    updateStatus();
    updateTitle();
    added.view->focus();
    return added;
}

void App::selectTab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) {
        return;
    }
    if (active_ == index) {
        if (TermTab* tab = activeTab()) {
            tab->view->focus();
        }
        return;
    }
    active_ = index;
    if (TermTab* tab = activeTab()) {
        tab->activity = false;
    }
    layout();
    updateTabBar();
    updateStatus();
    updateTitle();
    if (TermTab* tab = activeTab()) {
        tab->view->focus();
    }
}

void App::cycleTab(int direction) {
    if (tabs_.empty()) {
        return;
    }
    const int count = static_cast<int>(tabs_.size());
    selectTab((active_ + direction + count) % count);
}

void App::closeTab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) {
        return;
    }
    if (tabs_.size() == 1) {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return;
    }
    TermTab& tab = *tabs_[static_cast<size_t>(index)];
    if (tab.view && tab.view->hwnd()) {
        DestroyWindow(tab.view->hwnd());
    }
    tabs_.erase(tabs_.begin() + index);
    if (active_ >= static_cast<int>(tabs_.size())) {
        active_ = static_cast<int>(tabs_.size()) - 1;
    } else if (active_ > index) {
        --active_;
    }
    if (active_ < 0 && !tabs_.empty()) {
        active_ = 0;
    }
    layout();
    updateTabBar();
    updateStatus();
    updateTitle();
    if (TermTab* current = activeTab()) {
        current->view->focus();
    }
}

void App::closeOtherTabs(int keep) {
    if (keep < 0 || keep >= static_cast<int>(tabs_.size())) {
        return;
    }
    // Destroy every other view before erasing so no window outlives its owner.
    for (size_t i = 0; i < tabs_.size(); ++i) {
        if (static_cast<int>(i) != keep && tabs_[i]->view && tabs_[i]->view->hwnd()) {
            DestroyWindow(tabs_[i]->view->hwnd());
        }
    }
    std::unique_ptr<TermTab> kept = std::move(tabs_[static_cast<size_t>(keep)]);
    tabs_.clear();
    tabs_.push_back(std::move(kept));
    active_ = 0;
    layout();
    updateTabBar();
    updateStatus();
    updateTitle();
    if (TermTab* tab = activeTab()) {
        tab->view->focus();
    }
}

void App::closeAllTabs() {
    for (auto& tab : tabs_) {
        if (tab->view && tab->view->hwnd()) {
            DestroyWindow(tab->view->hwnd());
        }
    }
    tabs_.clear();
    active_ = -1;
}

void App::newWindow() {
    wchar_t exe[MAX_PATH * 2]{};
    const DWORD length = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    if (length == 0 || length >= std::size(exe)) {
        return;
    }
    std::wstring command = L"\"" + std::wstring(exe, length) + L"\"";
    std::wstring cwd;
    if (TermTab* tab = activeTab()) {
        cwd = tab->view->workingDirectory();
        if (!cwd.empty()) {
            command += L" --cwd \"" + cwd + L"\"";
        }
    }
    std::vector<wchar_t> buffer(command.begin(), command.end());
    buffer.push_back(0);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, buffer.data(), nullptr, nullptr, FALSE, 0, nullptr,
                       cwd.empty() ? nullptr : cwd.c_str(), &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}

void App::refreshMenuState() {
    MenuState state;
    TermTab* tab = activeTab();
    state.hasTab = tab != nullptr;
    if (tab) {
        state.running = tab->view->sessionRunning();
        state.exited = tab->view->exited();
        state.hasSelection = tab->view->hasSelection();
    }
    state.cursorBlink = settings_.cursorBlink;
    state.fullscreen = fullscreen_;
    state.fontSize = settings_.fontSize;
    state.scrollback = settings_.scrollback;
    state.shell = shellDisplayName();
    menuBar_.setState(state);
}

void App::onCommand(UINT id) {
    TermTab* tab = activeTab();
    if (id >= kCmdTabBase && id < kCmdTabBase + 9) {
        selectTab(static_cast<int>(id - kCmdTabBase));
        return;
    }
    switch (id) {
    case kCmdNewTab:
        addTab(defaultWorkingDirectory());
        break;
    case kCmdNewWindow:
        newWindow();
        break;
    case kCmdDuplicateTab:
        if (tab) {
            addTab(tab->view->workingDirectory());
        }
        break;
    case kCmdCloseTab:
        closeTab(active_);
        break;
    case kCmdCloseOthers:
        closeOtherTabs(active_);
        break;
    case kCmdCloseAll:
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        break;
    case kCmdRestartShell:
        if (tab) {
            tab->activity = false;
            tab->exited = false;
            tab->view->restart();
            updateTabBar();
            updateStatus();
        }
        break;
    case kCmdExit:
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        break;
    case kCmdCopy:
        if (tab) {
            tab->view->copySelection();
        }
        break;
    case kCmdPaste:
        if (tab) {
            tab->view->pasteClipboard();
        }
        break;
    case kCmdSelectAll:
        if (tab) {
            tab->view->selectAll();
        }
        break;
    case kCmdClearSelection:
        if (tab) {
            tab->view->clearSelection();
        }
        break;
    case kCmdClearScrollback:
        if (tab) {
            tab->view->clearScrollback();
        }
        break;
    case kCmdScrollTop:
        if (tab) {
            tab->view->scrollLines(1 << 24);
        }
        break;
    case kCmdScrollBottom:
        if (tab) {
            tab->view->scrollToBottom();
        }
        break;
    case kCmdPageUp:
        if (tab) {
            tab->view->scrollLines(std::max(1, tab->view->rows() / 2));
        }
        break;
    case kCmdPageDown:
        if (tab) {
            tab->view->scrollLines(-std::max(1, tab->view->rows() / 2));
        }
        break;
    case kCmdZoomIn:
        applyFontSize(settings_.fontSize + 1);
        break;
    case kCmdZoomOut:
        applyFontSize(settings_.fontSize - 1);
        break;
    case kCmdZoomReset:
        applyFontSize(kDefaultFontSize);
        break;
    case kCmdToggleFullscreen:
        toggleFullscreen();
        break;
    case kCmdToggleCursorBlink:
        applyCursorBlink(!settings_.cursorBlink);
        break;
    case kCmdScrollback1k:
        applyScrollback(1000);
        break;
    case kCmdScrollback10k:
        applyScrollback(10000);
        break;
    case kCmdScrollback50k:
        applyScrollback(50000);
        break;
    case kCmdNextTab:
        cycleTab(1);
        break;
    case kCmdPrevTab:
        cycleTab(-1);
        break;
    case kCmdAbout:
        showAbout();
        break;
    case kCmdOpenSettings:
        openSettingsFolder();
        break;
    case kCmdMenuFile:
        menuBar_.openMenu(0);
        break;
    case kCmdMenuEdit:
        menuBar_.openMenu(1);
        break;
    case kCmdMenuView:
        menuBar_.openMenu(2);
        break;
    case kCmdMenuHelp:
        menuBar_.openMenu(3);
        break;
    default:
        break;
    }
}

void App::applySettingsToAllViews() {
    for (auto& tab : tabs_) {
        tab->view->setAppearance(settings_.fontFamily, settings_.fontSize, settings_.cursorBlink,
                                 settings_.scrollback);
    }
}

void App::applyFontSize(int size) {
    const int clamped = std::clamp(size, 6, 72);
    if (clamped == settings_.fontSize) {
        return;
    }
    settings_.fontSize = clamped;
    applySettingsToAllViews();
    SaveSettings(settings_);
    layout();
    updateStatus();
}

void App::applyScrollback(int lines) {
    settings_.scrollback = std::clamp(lines, 0, 100000);
    for (auto& tab : tabs_) {
        tab->view->setScrollbackLimit(settings_.scrollback);
    }
    SaveSettings(settings_);
    updateStatus();
}

void App::applyCursorBlink(bool enabled) {
    settings_.cursorBlink = enabled;
    applySettingsToAllViews();
    SaveSettings(settings_);
    refreshMenuState();
}

void App::toggleFullscreen() {
    if (!fullscreen_) {
        savedStyle_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);
        savedMaximized_ = IsZoomed(hwnd_) != FALSE;
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        savedRect_ = rect;
        const HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (monitor && GetMonitorInfoW(monitor, &info)) {
            SetWindowLongPtrW(hwnd_, GWL_STYLE,
                              (savedStyle_ & ~WS_OVERLAPPEDWINDOW) | WS_POPUP | WS_VISIBLE);
            SetWindowPos(hwnd_, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
                         info.rcMonitor.right - info.rcMonitor.left,
                         info.rcMonitor.bottom - info.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
            fullscreen_ = true;
        }
    } else {
        SetWindowLongPtrW(hwnd_, GWL_STYLE, savedStyle_);
        SetWindowPos(hwnd_, nullptr, savedRect_.left, savedRect_.top, savedRect_.right - savedRect_.left,
                     savedRect_.bottom - savedRect_.top,
                     SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
        if (savedMaximized_) {
            ShowWindow(hwnd_, SW_MAXIMIZE);
        }
        fullscreen_ = false;
    }
    layout();
    refreshMenuState();
    updateStatus();
}

void App::saveWindowPlacement() {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(hwnd_, &placement)) {
        settings_.window = placement.rcNormalPosition;
        settings_.hasWindow = true;
        settings_.maximized = placement.showCmd == SW_SHOWMAXIMIZED;
    }
    SaveSettings(settings_);
}

void App::showAbout() {
    const std::wstring message =
        L"67t 0.1\n"
        L"A native Windows terminal\n\n"
        L"Win32 + ConPTY + GDI, one small executable.\n"
        L"Shell: " +
        shellCommand_ +
        L"\n\n"
        L"Ctrl+Shift+T  new tab\n"
        L"Ctrl+Shift+W  close tab\n"
        L"Ctrl+Shift+C/V  copy / paste\n"
        L"Ctrl+Shift+R  restart shell\n"
        L"F11  full screen";
    MessageBoxW(hwnd_, message.c_str(), L"About 67t", MB_OK | MB_ICONINFORMATION);
}

void App::openSettingsFolder() {
    EnsureDirectory(DataDirectory());
    ShellExecuteW(hwnd_, L"open", DataDirectory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::showViewContextMenu(HWND view, POINT screenPoint) {
    TermTab* target = nullptr;
    for (auto& tab : tabs_) {
        if (tab->view->hwnd() == view) {
            target = tab.get();
            break;
        }
    }
    if (!target) {
        return;
    }
    std::vector<MenuItem> items;
    items.push_back(MakeMenuItem(kCmdCopy, L"Copy", L"Ctrl+Shift+C",
                                 target->view->hasSelection()));
    items.push_back(MakeMenuItem(kCmdPaste, L"Paste", L"Ctrl+Shift+V",
                                 target->view->sessionRunning()));
    items.push_back(MakeMenuItem(kCmdSelectAll, L"Select All", L"Ctrl+Shift+A", true));
    items.push_back(MakeMenuItem(kCmdClearSelection, L"Clear Selection", L"",
                                 target->view->hasSelection()));
    items.push_back(MakeSeparator());
    items.push_back(MakeMenuItem(kCmdClearScrollback, L"Clear Scrollback", L"Ctrl+Shift+K",
                                 true));
    items.push_back(MakeSeparator());
    items.push_back(MakeMenuItem(kCmdNewTab, L"New Tab", L"Ctrl+Shift+T", true));
    items.push_back(MakeMenuItem(kCmdCloseTab, L"Close Tab", L"Ctrl+Shift+W", true));
    menuBar_.showContextMenu(screenPoint, std::move(items));
}

void App::showTabContextMenu(int index, POINT screenPoint) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) {
        return;
    }
    std::vector<MenuItem> items;
    items.push_back(MakeMenuItem(kCmdNewTab, L"New Tab", L"Ctrl+Shift+T", true));
    items.push_back(MakeMenuItem(kCmdDuplicateTab, L"Duplicate Tab", L"", true));
    items.push_back(MakeMenuItem(kCmdRestartShell, L"Restart Shell", L"Ctrl+Shift+R", true));
    items.push_back(MakeSeparator());
    items.push_back(MakeMenuItem(kCmdCloseTab, L"Close", L"Ctrl+Shift+W", true));
    items.push_back(MakeMenuItem(kCmdCloseOthers, L"Close Others", L"", tabs_.size() > 1));
    items.push_back(MakeMenuItem(kCmdCloseAll, L"Close All", L"", true));
    menuBar_.showContextMenu(screenPoint, std::move(items));
}

void App::handleDropFiles(WPARAM wParam) {
    HDROP drop = reinterpret_cast<HDROP>(wParam);
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i) {
        const UINT length = DragQueryFileW(drop, i, nullptr, 0);
        if (length == 0) {
            continue;
        }
        std::wstring path(length + 1, L'\0');
        DragQueryFileW(drop, i, path.data(), length + 1);
        path.resize(length);
        if (IsDirectory(path)) {
            addTab(path);
        } else {
            std::wstring quoted = L"\"" + path + L"\"";
            if (TermTab* tab = activeTab(); tab && tab->view->sessionRunning()) {
                tab->view->pasteText(quoted);
            } else {
                addTab(DirectoryOf(path));
            }
        }
    }
    DragFinish(drop);
}

void App::layout() {
    if (!hwnd_) {
        return;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) {
        return;
    }
    dpi_ = DpiOfWindow(hwnd_);
    const int tabHeight = tabBar_.preferredHeight(hwnd_);
    const int menuHeight = menuBar_.preferredHeight(hwnd_);
    const int statusHeight = statusBar_.preferredHeight(hwnd_);
    const int contentTop = tabHeight + menuHeight;
    const int contentHeight = std::max(0, height - contentTop - statusHeight);

    tabBar_.layout(0, 0, width, tabHeight);
    menuBar_.layout(0, tabHeight, width, menuHeight);
    statusBar_.layout(0, height - statusHeight, width, statusHeight);

    for (size_t i = 0; i < tabs_.size(); ++i) {
        TermTab& tab = *tabs_[i];
        if (!tab.view->hwnd()) {
            continue;
        }
        tab.view->layout(0, contentTop, width, contentHeight);
        tab.view->show(static_cast<int>(i) == active_);
    }
}

void App::updateTabBar() {
    if (!tabBar_.hwnd()) {
        return;
    }
    std::vector<TabVisual> visuals;
    visuals.reserve(tabs_.size());
    for (const auto& tab : tabs_) {
        std::wstring title = tab->view ? tab->view->title() : L"";
        if (title.empty()) {
            title = L"Terminal";
        }
        title = FriendlyTabTitle(title);
        if (tab->exited) {
            title += L" (exited)";
        }
        visuals.push_back(TabVisual{title, tab->activity});
    }
    tabBar_.setTabs(std::move(visuals), active_);
}

void App::updateStatus() {
    if (!statusBar_.hwnd()) {
        return;
    }
    StatusInfo info;
    TermTab* tab = activeTab();
    if (tab && tab->view) {
        info.position = L"Ln " + std::to_wstring(tab->view->cursorRow() + 1) + L", Col " +
                        std::to_wstring(tab->view->cursorColumn() + 1);
        info.size = std::to_wstring(tab->view->columns()) + L"x" +
                    std::to_wstring(tab->view->rows());
        const int scrollback = tab->view->scrollbackLines();
        const int offset = tab->view->scrollOffset();
        if (scrollback > 0) {
            info.scrollback = L"Scrollback " + std::to_wstring(scrollback);
            if (offset > 0) {
                info.scrollback += L" (+" + std::to_wstring(offset) + L")";
            }
        }
        info.shell = shellDisplayName();
        if (tab->view->startFailed()) {
            info.message = L"Shell failed to start";
        } else if (tab->view->exited()) {
            info.message = L"Process exited - Ctrl+Shift+R to restart";
        } else if (offset > 0) {
            info.mode = L"SCROLLBACK";
        } else if (tab->view->hasSelection()) {
            info.mode = L"SELECTION";
        } else {
            info.mode = L"ConPTY";
        }
        if (tab->view->processId() != 0 && !tab->view->exited()) {
            info.right = L"PID " + std::to_wstring(tab->view->processId());
        }
    } else {
        info.message = L"No terminal";
    }
    statusBar_.setInfo(info);
    refreshMenuState();
}

void App::updateTitle() {
    if (!hwnd_) {
        return;
    }
    TermTab* tab = activeTab();
    std::wstring title = (tab && tab->view) ? tab->view->title() : L"";
    if (title.empty()) {
        title = kAppName;
    } else {
        if (tab && tab->exited) {
            title += L" [exited]";
        }
        title += L" - ";
        title += kAppName;
    }
    SetWindowTextW(hwnd_, title.c_str());
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    App* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<App*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) {
            self->hwnd_ = hwnd;
        }
    }
    if (self) {
        return self->handleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT App::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message >= kViewTitleChanged && message <= kViewContextMenu) {
        switch (message) {
        case kViewTitleChanged:
            if (const int index = indexOfView(reinterpret_cast<HWND>(wParam)); index >= 0) {
                tabs_[static_cast<size_t>(index)]->lastTitle =
                    tabs_[static_cast<size_t>(index)]->view->title();
                updateTabBar();
                if (index == active_) {
                    updateTitle();
                    updateStatus();
                }
            }
            return 0;
        case kViewStateChanged:
            if (indexOfView(reinterpret_cast<HWND>(wParam)) == active_) {
                updateStatus();
            }
            return 0;
        case kViewExited:
            if (const int index = indexOfView(reinterpret_cast<HWND>(wParam)); index >= 0) {
                TermTab& tab = *tabs_[static_cast<size_t>(index)];
                tab.exited = true;
                tab.activity = true;
                updateTabBar();
                if (index == active_) {
                    updateStatus();
                    updateTitle();
                }
                FLASHWINFO flash{sizeof(flash), hwnd_, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
                FlashWindowEx(&flash);
            }
            return 0;
        case kViewBell:
            if (const int index = indexOfView(reinterpret_cast<HWND>(wParam)); index >= 0) {
                tabs_[static_cast<size_t>(index)]->activity = true;
                updateTabBar();
                FLASHWINFO flash{sizeof(flash), hwnd_, FLASHW_TRAY | FLASHW_TIMERNOFG, 2, 0};
                FlashWindowEx(&flash);
            }
            return 0;
        case kViewContextMenu:
            if (const int index = indexOfView(reinterpret_cast<HWND>(wParam)); index >= 0) {
                if (index != active_) {
                    selectTab(index);
                }
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                showViewContextMenu(reinterpret_cast<HWND>(wParam), point);
            }
            return 0;
        default:
            break;
        }
    }

    switch (message) {
    case WM_COMMAND:
        onCommand(LOWORD(wParam));
        return 0;
    case WM_NCCALCSIZE:
        if (wParam) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            const UINT dpi = DpiOfWindow(hwnd_);
            const int frameX = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) +
                               GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            const int frameY = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) +
                               GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            params->rgrc[0].left += frameX;
            params->rgrc[0].right -= frameX;
            params->rgrc[0].top += frameY;
            params->rgrc[0].bottom -= frameY;
            return 0;
        }
        break;
    case WM_NCHITTEST: {
        POINT client{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &client);
        const int titleBottom = tabBar_.preferredHeight(hwnd_) + menuBar_.preferredHeight(hwnd_);
        if (client.x >= 0 && client.y >= 0 && client.y < titleBottom) {
            return HTCAPTION;
        }
        break;
    }
    case WM_SIZE:
        layout();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd_, &ps);
        RECT client{};
        GetClientRect(hwnd_, &client);
        FillRect(dc, &client, backgroundBrush_);
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFOCUS:
        if (TermTab* tab = activeTab()) {
            tab->view->focus();
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wParam) != WA_INACTIVE) {
            if (TermTab* tab = activeTab()) {
                if (tab->activity) {
                    tab->activity = false;
                    updateTabBar();
                }
            }
        }
        return 0;
    case WM_EXITSIZEMOVE:
        if (!fullscreen_) {
            saveWindowPlacement();
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = theme::Scale(560, dpi_);
        info->ptMinTrackSize.y = theme::Scale(340, dpi_);
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        for (auto& tab : tabs_) {
            tab->view->applyDpi(dpi_);
        }
        layout();
        updateStatus();
        return 0;
    }
    case WM_TIMER:
        if (wParam == kTimerStatus) {
            updateStatus();
        }
        return 0;
    case WM_DROPFILES:
        handleDropFiles(wParam);
        return 0;
    case tabbar_events::kSelect:
        selectTab(static_cast<int>(wParam));
        return 0;
    case tabbar_events::kClose:
        closeTab(static_cast<int>(wParam));
        return 0;
    case tabbar_events::kNew:
        addTab(defaultWorkingDirectory());
        return 0;
    case tabbar_events::kMinimize:
        ShowWindow(hwnd_, SW_MINIMIZE);
        return 0;
    case tabbar_events::kMaximize:
        ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
        return 0;
    case tabbar_events::kCloseWindow:
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        return 0;
    case tabbar_events::kReorder: {
        const int from = static_cast<int>(wParam);
        const int to = static_cast<int>(lParam);
        if (from >= 0 && to >= 0 && from < static_cast<int>(tabs_.size()) &&
            to < static_cast<int>(tabs_.size()) && from != to) {
            TermTab* current = activeTab();
            auto moved = std::move(tabs_[static_cast<size_t>(from)]);
            tabs_.erase(tabs_.begin() + from);
            tabs_.insert(tabs_.begin() + to, std::move(moved));
            active_ = current ? indexOfView(current->view->hwnd()) : -1;
            if (active_ < 0 && !tabs_.empty()) {
                active_ = 0;
            }
            updateTabBar();
            layout();
            updateStatus();
        }
        return 0;
    }
    case tabbar_events::kContext: {
        const int index = static_cast<int>(wParam);
        if (index < 0 || index >= static_cast<int>(tabs_.size())) {
            return 0;
        }
        if (active_ != index) {
            selectTab(index);
        }
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ClientToScreen(tabBar_.hwnd(), &point);
        showTabContextMenu(index, point);
        return 0;
    }
    case WM_CLOSE:
        if (closing_) {
            return 0;
        }
        if (!fullscreen_) {
            saveWindowPlacement();
        } else {
            SaveSettings(settings_);
        }
        closing_ = true;
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd_, kTimerStatus);
        closeAllTabs();
        if (accel_) {
            DestroyAcceleratorTable(accel_);
            accel_ = nullptr;
        }
        hwnd_ = nullptr;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
