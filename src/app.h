#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "cli.h"
#include "menubar.h"
#include "settings.h"
#include "statusbar.h"
#include "tabbar.h"
#include "view.h"

struct TermTab {
    std::unique_ptr<TerminalView> view;
    bool activity = false;
    bool exited = false;
    std::wstring lastTitle;
};

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();

    bool create(const LaunchOptions& options);
    int run();
    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void createAccelerators();
    void refreshMenuState();
    void onCommand(UINT id);

    void layout();
    void updateTabBar();
    void updateStatus();
    void updateTitle();

    TermTab* activeTab();
    int activeIndex() const { return active_; }
    int indexOfView(HWND view) const;
    TermTab& addTab(const std::wstring& workingDirectory);
    void closeTab(int index);
    void closeOtherTabs(int keep);
    void closeAllTabs();
    void selectTab(int index);
    void cycleTab(int direction);
    void newWindow();

    std::wstring defaultWorkingDirectory();
    std::wstring chooseShell() const;
    std::wstring shellDisplayName() const;

    void toggleFullscreen();
    void applyFontSize(int size);
    void applyScrollback(int lines);
    void applyCursorBlink(bool enabled);
    void applySettingsToAllViews();

    void saveWindowPlacement();
    void showAbout();
    void openSettingsFolder();
    void showViewContextMenu(HWND view, POINT screenPoint);
    void showTabContextMenu(int index, POINT screenPoint);
    void handleDropFiles(WPARAM wParam);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HACCEL accel_ = nullptr;
    HBRUSH backgroundBrush_ = nullptr;
    UINT dpi_ = 96;

    MenuBar menuBar_;
    TabBar tabBar_;
    StatusBar statusBar_;

    std::vector<std::unique_ptr<TermTab>> tabs_;
    int active_ = -1;
    Settings settings_;
    LaunchOptions options_;
    std::wstring shellCommand_;

    bool closing_ = false;
    bool fullscreen_ = false;
    bool sessionReady_ = false;
    LONG_PTR savedStyle_ = 0;
    RECT savedRect_{};
    bool savedMaximized_ = false;
};
