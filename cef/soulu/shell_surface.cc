#include "examples/soulu/shell_surface.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <cstring>
namespace soulu {
ShellSurface::ShellSurface(HWND parent) : parent_(parent) {
  scale_ = GetDpiForWindow(parent) / 96.0f;
  if (scale_ <= 0) scale_ = 1;
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"SouluAlphaToolbar"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.style = CS_DBLCLKS;
  RegisterClassExW(&wc);
  hwnd_ = CreateWindowExW(WS_EX_LAYERED, wc.lpszClassName, L"Soulu toolbar",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 1, 48, parent, nullptr, wc.hInstance, this);
}
ShellSurface::~ShellSurface() {
  if (IsWindow(hwnd_)) DestroyWindow(hwnd_);
  ReleaseBitmap();
}
void ShellSurface::ReleaseBitmap() {
  if (memory_ && original_) SelectObject(memory_, original_);
  if (bitmap_) DeleteObject(bitmap_);
  if (memory_) DeleteDC(memory_);
  memory_ = nullptr; bitmap_ = nullptr; original_ = nullptr; pixels_ = nullptr;
}
void ShellSurface::Attach(CefRefPtr<CefBrowser> browser) { browser_ = browser; browser_->GetHost()->WasResized(); }
void ShellSurface::Detach() { browser_ = nullptr; }
void ShellSurface::Resize(int x, int y, int width, int height) {
  const float scale = std::max(1.0f, GetDpiForWindow(parent_) / 96.0f);
  const bool changed = width != width_ || height != height_ || scale != scale_;
  width_ = std::max(1, width); height_ = std::max(1, height); scale_ = scale;
  SetWindowPos(hwnd_, HWND_TOP, x, y, width_, height_, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  if (changed && browser_) {
    browser_->GetHost()->NotifyScreenInfoChanged();
    browser_->GetHost()->WasResized();
  }
}
void ShellSurface::Focus() { SetFocus(hwnd_); if (browser_) browser_->GetHost()->SetFocus(true); }
void ShellSurface::Cursor(HCURSOR cursor) { cursor_ = cursor; SetCursor(cursor ? cursor : LoadCursor(nullptr, IDC_ARROW)); }
void ShellSurface::GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) {
  rect = CefRect(0, 0, std::max(1, static_cast<int>(std::ceil(width_ / scale_))),
                       std::max(1, static_cast<int>(std::ceil(height_ / scale_))));
}
bool ShellSurface::GetScreenPoint(CefRefPtr<CefBrowser>, int x, int y, int& sx, int& sy) {
  POINT point = {static_cast<LONG>(x * scale_), static_cast<LONG>(y * scale_)};
  ClientToScreen(hwnd_, &point); sx = point.x; sy = point.y; return true;
}
bool ShellSurface::GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo& info) {
  MONITORINFO monitor = {sizeof(monitor)};
  GetMonitorInfoW(MonitorFromWindow(parent_, MONITOR_DEFAULTTONEAREST), &monitor);
  info.device_scale_factor = scale_; info.depth = 32; info.depth_per_component = 8;
  info.rect = CefRect(static_cast<int>(monitor.rcMonitor.left / scale_), static_cast<int>(monitor.rcMonitor.top / scale_),
      static_cast<int>((monitor.rcMonitor.right - monitor.rcMonitor.left) / scale_), static_cast<int>((monitor.rcMonitor.bottom - monitor.rcMonitor.top) / scale_));
  info.available_rect = CefRect(static_cast<int>(monitor.rcWork.left / scale_), static_cast<int>(monitor.rcWork.top / scale_),
      static_cast<int>((monitor.rcWork.right - monitor.rcWork.left) / scale_), static_cast<int>((monitor.rcWork.bottom - monitor.rcWork.top) / scale_));
  return true;
}
void ShellSurface::OnPaint(CefRefPtr<CefBrowser>, PaintElementType type, const RectList&,
                            const void* buffer, int width, int height) {
  if (type != PET_VIEW || !IsWindow(hwnd_) || width <= 0 || height <= 0) return;
  if (!bitmap_ || width != bitmap_width_ || height != bitmap_height_) {
    ReleaseBitmap();
    BITMAPINFO info = {}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    memory_ = CreateCompatibleDC(nullptr);
    bitmap_ = CreateDIBSection(memory_, &info, DIB_RGB_COLORS, &pixels_, nullptr, 0);
    if (!bitmap_) { ReleaseBitmap(); return; }
    original_ = SelectObject(memory_, bitmap_); bitmap_width_ = width; bitmap_height_ = height;
  }
  // CEF delivers premultiplied BGRA: preserve its alpha through Win32 composition.
  memcpy(pixels_, buffer, static_cast<size_t>(width) * height * 4);
  ++paint_count_;
  if (width > 120 && height > 5) toolbar_alpha_ = static_cast<const unsigned char*>(buffer)[(4 * width + 110) * 4 + 3];
  SIZE size = {width, height}; POINT source = {0, 0};
  BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  paint_error_ = UpdateLayeredWindow(hwnd_, nullptr, nullptr, &size, memory_, &source, 0, &blend, ULW_ALPHA) ? 0 : static_cast<int>(GetLastError());
}
uint32_t ShellSurface::Modifiers() {
  uint32_t flags = 0;
  if (GetKeyState(VK_SHIFT) & 0x8000) flags |= EVENTFLAG_SHIFT_DOWN;
  if (GetKeyState(VK_CONTROL) & 0x8000) flags |= EVENTFLAG_CONTROL_DOWN;
  if (GetKeyState(VK_MENU) & 0x8000) flags |= EVENTFLAG_ALT_DOWN;
  if (GetKeyState(VK_LBUTTON) & 0x8000) flags |= EVENTFLAG_LEFT_MOUSE_BUTTON;
  if (GetKeyState(VK_MBUTTON) & 0x8000) flags |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
  if (GetKeyState(VK_RBUTTON) & 0x8000) flags |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
  if (GetKeyState(VK_CAPITAL) & 1) flags |= EVENTFLAG_CAPS_LOCK_ON;
  return flags;
}
CefMouseEvent ShellSurface::Mouse(LPARAM pos, bool screen) const {
  POINT p = {GET_X_LPARAM(pos), GET_Y_LPARAM(pos)};
  if (screen) ScreenToClient(hwnd_, &p);
  CefMouseEvent event; event.x = static_cast<int>(p.x / scale_); event.y = static_cast<int>(p.y / scale_);
  event.modifiers = Modifiers(); return event;
}
LRESULT CALLBACK ShellSurface::Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  auto* self = reinterpret_cast<ShellSurface*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    self = static_cast<ShellSurface*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self || !self->browser_) return DefWindowProcW(hwnd, message, wp, lp);
  auto host = self->browser_->GetHost();
  switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR: SetCursor(self->cursor_ ? self->cursor_ : LoadCursor(nullptr, IDC_ARROW)); return TRUE;
    case WM_SETFOCUS: host->SetFocus(true); return 0;
    case WM_KILLFOCUS: host->SetFocus(false); return 0;
    case WM_MOUSEMOVE: {
      if (!self->tracking_) { TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&t); self->tracking_ = true; }
      host->SendMouseMoveEvent(self->Mouse(lp), false); return 0;
    }
    case WM_MOUSELEAVE: { self->tracking_ = false; CefMouseEvent e; e.modifiers = Modifiers(); host->SendMouseMoveEvent(e, true); return 0; }
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
    case WM_LBUTTONDBLCLK: case WM_RBUTTONDBLCLK: {
      self->Focus(); SetCapture(hwnd);
      const auto button = (message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK) ? MBT_RIGHT : message == WM_MBUTTONDOWN ? MBT_MIDDLE : MBT_LEFT;
      host->SendMouseClickEvent(self->Mouse(lp), button, false, (message == WM_LBUTTONDBLCLK || message == WM_RBUTTONDBLCLK) ? 2 : 1); return 0;
    }
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP:
      ReleaseCapture(); host->SendMouseClickEvent(self->Mouse(lp), message == WM_RBUTTONUP ? MBT_RIGHT : message == WM_MBUTTONUP ? MBT_MIDDLE : MBT_LEFT, true, 1); return 0;
    case WM_MOUSEWHEEL: host->SendMouseWheelEvent(self->Mouse(lp, true), 0, GET_WHEEL_DELTA_WPARAM(wp)); return 0;
    case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: case WM_CHAR: case WM_SYSCHAR: {
      CefKeyEvent e;
      e.type = (message == WM_CHAR || message == WM_SYSCHAR) ? KEYEVENT_CHAR :
          (message == WM_KEYUP || message == WM_SYSKEYUP) ? KEYEVENT_KEYUP : KEYEVENT_RAWKEYDOWN;
      e.windows_key_code = static_cast<int>(wp); e.native_key_code = static_cast<int>(lp);
      e.is_system_key = message == WM_SYSCHAR || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
      e.modifiers = Modifiers(); host->SendKeyEvent(e); return 0;
    }
  }
  return DefWindowProcW(hwnd, message, wp, lp);
}
}
