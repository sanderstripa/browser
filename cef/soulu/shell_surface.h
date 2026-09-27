#pragma once
#include <windows.h>
#include "include/cef_render_handler.h"
#include "include/cef_browser.h"
namespace soulu {
// Only browser chrome uses OSR. Web content keeps the accelerated native view.
class ShellSurface final : public CefRenderHandler {
 public:
  explicit ShellSurface(HWND parent);
  void Attach(CefRefPtr<CefBrowser> browser);
  void Detach();
  void Resize(int x, int y, int width, int height);
  void Focus();
  void Cursor(HCURSOR cursor);
  HWND hwnd() const { return hwnd_; }
  int paint_count() const { return paint_count_; }
  int toolbar_alpha() const { return toolbar_alpha_; }
  void GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) override;
  bool GetScreenPoint(CefRefPtr<CefBrowser>, int x, int y, int& sx, int& sy) override;
  bool GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo& info) override;
  void OnPaint(CefRefPtr<CefBrowser>, PaintElementType type,
               const RectList&, const void* buffer, int width, int height) override;
 private:
  ~ShellSurface() override;
  static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
  void ReleaseBitmap();
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
  int paint_count_ = 0, toolbar_alpha_ = 255;
  IMPLEMENT_REFCOUNTING(ShellSurface);
};
}
