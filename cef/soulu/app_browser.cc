#include "examples/soulu/app_factory.h"
#include "examples/soulu/browser_window.h"

namespace soulu {
class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
 public:
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }

  void OnBeforeCommandLineProcessing(const CefString& process_type,
                                     CefRefPtr<CefCommandLine> command_line) override {
    if (process_type.empty()) {
      command_line->AppendSwitch("enable-gpu-rasterization");
      command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
    }
  }

  void OnContextInitialized() override { BrowserWindow::Create(); }

 private:
  IMPLEMENT_REFCOUNTING(BrowserApp);
};

CefRefPtr<CefApp> CreateBrowserApp() { return new BrowserApp(); }
}
