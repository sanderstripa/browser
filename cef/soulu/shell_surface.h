#pragma once
#include <windows.h>
#include <vector>
#include "include/cef_render_handler.h"
#include "include/cef_browser.h"
namespace soulu {
// Only browser chrome uses OSR. Web content keeps the accelerated native view.
class ShellSurface final : public CefRenderHandler {
 public:
  explicit ShellSurface(HWND parent);
  void Attach(CefRefPtr<CefBrowser> browser);
  void Detach();
  // Layout owns HWND geometry. Prepare the OSR viewport before positioning
  // the children, then notify CEF only after the whole layout is committed.
  void PrepareResize(int width, int height, float scale);
  void CommitResize();
  static constexpr UINT kFirstFrame = WM_APP + 73;
  void Focus();
  void Cursor(HCURSOR cursor);
  HWND hwnd() const { return hwnd_; }
  int paint_error() const { return paint_error_; }
  int paint_count() const { return paint_count_; }
  int toolbar_alpha() const { return toolbar_alpha_; }
  bool popup_visible() const { return popup_visible_; }
  int popup_paint_count() const { return popup_paint_count_; }
  CefRect popup_bounds() const { return AdjustedPopupRect(); }
  void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override;
  bool GetScreenPoint(CefRefPtr<CefBrowser>, int x, int y, int& sx, int& sy) override;
  bool GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo& info) override;
  void OnPopupShow(CefRefPtr<CefBrowser>, bool show) override;
  void OnPopupSize(CefRefPtr<CefBrowser>, const CefRect& rect) override;
  void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type,
               const RectList&, const void* buffer, int width, int height) override;
 private:
  ~ShellSurface() override;
  static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
  void ReleaseBitmap();
  CefRect AdjustedPopupRect() const;
  void CompositePopup();
  void Present();
  CefMouseEvent Mouse(LPARAM pos, bool screen = false) const;
  static uint32_t Modifiers();
  HWND parent_ = nullptr;
  HWND hwnd_ = nullptr;
  HCURSOR cursor_ = nullptr;
  CefRefPtr<CefBrowser> browser_;
  HDC memory_ = nullptr;
  HBITMAP bitmap_ = nullptr;
  HGDIOBJ original_ = nullptr;
  void* pixels_ = nullptr;
  int bitmap_width_ = 0, bitmap_height_ = 0;
  int width_ = 1, height_ = 48;
  float scale_ = 1;
  bool tracking_ = false;
  bool resize_pending_ = false, screen_pending_ = false;
  bool first_frame_ = false;
  int paint_error_ = 0;
  int paint_count_ = 0, toolbar_alpha_ = 255;
  bool popup_visible_ = false;
  int popup_width_ = 0, popup_height_ = 0, popup_paint_count_ = 0;
  CefRect popup_rect_;
  std::vector<unsigned char> popup_pixels_, popup_background_;
  IMPLEMENT_REFCOUNTING(ShellSurface);
};
}
