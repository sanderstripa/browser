#include "examples/soulu/settings_overlay.h"
#include "examples/soulu/motion.h"
#include "examples/soulu/frosted_backdrop.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>

namespace soulu {
SettingsOverlay::SettingsOverlay(HWND owner, std::function<void()> closed,
    std::function<void()> request_close)
    : owner_(owner), closed_(std::move(closed)), request_close_(std::move(request_close)) {}
SettingsOverlay::~SettingsOverlay() {
  if (IsWindow(hwnd_)) DestroyWindow(hwnd_);
}
bool SettingsOverlay::Create() {
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"SouluSettingsOverlay";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassExW(&wc);
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP,
      wc.lpszClassName, L"Soulu Settings", WS_POPUP | WS_CLIPCHILDREN,
      0, 0, 1, 1, owner_, nullptr, wc.hInstance, this);
  if (!hwnd_) return false;
  // No independent Windows corners: the owner supplies the external silhouette.
  const DWORD corners = 1; // DWMWCP_DONOTROUND
  DwmSetWindowAttribute(hwnd_, 33, &corners, sizeof(corners));
  if (!ConfigureFrostedBackdrop(hwnd_, true)) { DestroyWindow(hwnd_); hwnd_ = nullptr; return false; }
  SetFrostedBackdropOpacity(hwnd_, 0);
  BOOL animate = TRUE;
  if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0)) reduced_ = !animate;
  duration_ = reduced_ ? motion::kReducedOverlayMs : motion::kStructuralMs;
  Layout();
  // The invisible panel starts above the clipping bounds. Showing the host now
  // blocks input immediately and allows Chromium to prepare its first frame.
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  return true;
}
void SettingsOverlay::Attach(HWND browser) { browser_ = browser; Layout(); }
void SettingsOverlay::Open() {
  if (state_ != State::Loading) { Focus(); return; }
  state_ = State::Opening; start_ = GetTickCount64();
  SetTimer(hwnd_, 1, 16, nullptr); Tick(); Focus();
}
void SettingsOverlay::Close() {
  if (state_ == State::Closing || state_ == State::Closed) return;
  state_ = State::Closing; close_start_ = progress_; start_ = GetTickCount64();
  SetTimer(hwnd_, 1, 16, nullptr); Tick();
}
void SettingsOverlay::Focus() {
  if (browser_ && IsWindowVisible(hwnd_) && state_ != State::Closing) SetFocus(browser_);
}
void SettingsOverlay::Layout() {
  if (!hwnd_) return;
  RECT r = {}; GetClientRect(owner_, &r);
  POINT p = {0, 0}; ClientToScreen(owner_, &p);
  width_ = std::max(1L, r.right); height_ = std::max(1L, r.bottom);
  const float scale = GetDpiForWindow(owner_) / 96.f;
  panel_width_ = std::min(width_, static_cast<int>(std::round(1020 * scale)));
  panel_height_ = std::min(height_, static_cast<int>(std::round(800 * scale)));
  SetWindowPos(hwnd_, HWND_TOP, p.x, p.y, width_, height_, SWP_NOACTIVATE | SWP_NOCOPYBITS);
  ResizeFrostedBackdrop(hwnd_, width_, height_);
  // Crop to the owner's client region, including its rounded normal-window
  // silhouette. No independent desktop-relative geometry is persisted.
  const int radius = IsZoomed(owner_) ? 0 : static_cast<int>(std::round(16 * scale));
  HRGN region = radius ? CreateRoundRectRgn(0, 0, width_+1, height_+1, radius, radius)
                      : CreateRectRgn(0, 0, width_, height_);
  SetWindowRgn(hwnd_, region, FALSE);
  if (IsIconic(owner_)) ShowWindow(hwnd_, SW_HIDE);
  else if (state_ != State::Closed) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  if (browser_) {
    const int corner = static_cast<int>(std::round(24 * scale));
    HRGN panel = CreateRoundRectRgn(0, 0, panel_width_+1, panel_height_+1, corner, corner);
    HRGN top = CreateRectRgn(0, 0, panel_width_, std::min(corner, panel_height_));
    CombineRgn(panel, panel, top, RGN_OR); DeleteObject(top);
    SetWindowRgn(browser_, panel, FALSE);
  }
  PlacePanel();
}
void SettingsOverlay::PlacePanel() {
  if (!browser_) return;
  const float distance = reduced_ ? std::min(12, panel_height_) : panel_height_;
  int y = static_cast<int>(std::round(-(1-progress_) * distance));
  if (state_ == State::Loading) y = -panel_height_;
  SetWindowPos(browser_, HWND_TOP, (width_-panel_width_)/2, y,
      panel_width_, panel_height_, SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_SHOWWINDOW);
}
void SettingsOverlay::Tick() {
  const float fraction = std::min(1.f, static_cast<float>(GetTickCount64()-start_) / duration_);
  const float eased = fraction >= 1 ? 1 : motion::EaseOut(fraction);
  progress_ = state_ == State::Closing ? close_start_ * (1-eased) : eased;
  ++ticks_;
  SetFrostedBackdropOpacity(hwnd_, progress_);
  PlacePanel();
  if (fraction < 1) return;
  KillTimer(hwnd_, 1);
  if (state_ == State::Closing) {
    state_ = State::Closed; ShowWindow(hwnd_, SW_HIDE);
    // The callback initiates asynchronous CEF close. The host is retained until
    // OnBeforeClose, so its browser cannot outlive the native parent.
    const auto closed = closed_; closed();
  } else { state_ = State::Open; Focus(); }
}
LRESULT CALLBACK SettingsOverlay::Proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
  auto* self = reinterpret_cast<SettingsOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    self = static_cast<SettingsOverlay*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProcW(hwnd, message, wp, lp);
  switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: if (wp == 1) self->Tick(); return 0;
    case WM_CLOSE: self->request_close_(); return 0;
    case WM_SETFOCUS: self->Focus(); return 0;
    case WM_MOUSEACTIVATE: self->Focus(); return MA_NOACTIVATE;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
    case WM_MOUSEWHEEL: self->Focus(); return 0; // Backdrop never dismisses.
    case WM_NCHITTEST: {
      // Keep the owner's native resize affordance, never expose background UI.
      if (!IsZoomed(self->owner_)) {
        RECT r = {}; GetWindowRect(hwnd, &r);
        const int e = MulDiv(6, GetDpiForWindow(self->owner_), 96);
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (x < r.left+e || x >= r.right-e || y < r.top+e || y >= r.bottom-e) return HTTRANSPARENT;
      }
      return HTCLIENT;
    }
    case WM_DESTROY: KillTimer(hwnd, 1); ReleaseFrostedBackdrop(hwnd); return 0;
  }
  return DefWindowProcW(hwnd, message, wp, lp);
}
}
