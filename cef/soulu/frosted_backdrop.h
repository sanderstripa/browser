#pragma once
#include <windows.h>
namespace soulu {
int BackdropCapabilities();
bool ConfigureFrostedBackdrop(HWND window, bool enabled);
void ResizeFrostedBackdrop(HWND window, int width, int height);
void SetFrostedBackdropOpacity(HWND window, float opacity);
void ReleaseFrostedBackdrop(HWND window);
}
