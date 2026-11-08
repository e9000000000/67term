#include "accel.h"

#include "commands.h"

HACCEL CreateAppAccelerators() {
    ACCEL accels[] = {
        {FVIRTKEY | FCONTROL | FSHIFT, 'T', kCmdNewTab},
        {FVIRTKEY | FCONTROL | FSHIFT, 'N', kCmdNewWindow},
        {FVIRTKEY | FCONTROL | FSHIFT, 'W', kCmdCloseTab},
        {FVIRTKEY | FCONTROL | FSHIFT, 'R', kCmdRestartShell},
        {FVIRTKEY | FCONTROL | FSHIFT, 'C', kCmdCopy},
        {FVIRTKEY | FCONTROL | FSHIFT, 'V', kCmdPaste},
        {FVIRTKEY | FCONTROL | FSHIFT, 'A', kCmdSelectAll},
        {FVIRTKEY | FCONTROL | FSHIFT, 'K', kCmdClearScrollback},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_HOME, kCmdScrollTop},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_END, kCmdScrollBottom},
        {FVIRTKEY | FCONTROL, VK_OEM_PLUS, kCmdZoomIn},
        {FVIRTKEY | FCONTROL, VK_ADD, kCmdZoomIn},
        {FVIRTKEY | FCONTROL, VK_OEM_MINUS, kCmdZoomOut},
        {FVIRTKEY | FCONTROL, VK_SUBTRACT, kCmdZoomOut},
        {FVIRTKEY | FCONTROL, '0', kCmdZoomReset},
        {FVIRTKEY | FCONTROL, VK_TAB, kCmdNextTab},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_TAB, kCmdPrevTab},
        {FVIRTKEY, VK_F11, kCmdToggleFullscreen},
        {FVIRTKEY | FALT, VK_RETURN, kCmdToggleFullscreen},
        {FVIRTKEY | FALT, 'F', kCmdMenuFile},
        {FVIRTKEY | FALT, 'E', kCmdMenuEdit},
        {FVIRTKEY | FALT, 'V', kCmdMenuView},
        {FVIRTKEY | FALT, 'H', kCmdMenuHelp},
        {FVIRTKEY, VK_F10, kCmdMenuFile},
        {FVIRTKEY | FCONTROL, '1', kCmdTabBase + 0},
        {FVIRTKEY | FCONTROL, '2', kCmdTabBase + 1},
        {FVIRTKEY | FCONTROL, '3', kCmdTabBase + 2},
        {FVIRTKEY | FCONTROL, '4', kCmdTabBase + 3},
        {FVIRTKEY | FCONTROL, '5', kCmdTabBase + 4},
        {FVIRTKEY | FCONTROL, '6', kCmdTabBase + 5},
        {FVIRTKEY | FCONTROL, '7', kCmdTabBase + 6},
        {FVIRTKEY | FCONTROL, '8', kCmdTabBase + 7},
        {FVIRTKEY | FCONTROL, '9', kCmdTabBase + 8},
    };
    return CreateAcceleratorTableW(accels, static_cast<int>(sizeof(accels) / sizeof(accels[0])));
}
