#pragma once

#include "include/cef_client.h"
#include "include/wrapper/cef_message_router.h"

namespace soulu {
class BrowserWindow;

enum class BrowserRole { kShell, kContent };

class BrowserClient final : public CefClient,
                            public CefDisplayHandler,
                            public CefDownloadHandler,
                            public CefLifeSpanHandler,
                            public CefLoadHandler {
 public:
  BrowserClient(CefRefPtr<BrowserWindow> owner, BrowserRole role, int tab_id = 0);

  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;
  void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                       const CefString& url) override;
  void OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                          const std::vector<CefString>& icon_urls) override;
  void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading,
                            bool can_go_back, bool can_go_forward) override;
  bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefDownloadItem> download_item,
                        const CefString& suggested_name,
                        CefRefPtr<CefBeforeDownloadCallback> callback) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefDownloadItem> download_item,
                         CefRefPtr<CefDownloadItemCallback> callback) override;

 private:
  CefRefPtr<BrowserWindow> owner_;
  const BrowserRole role_;
  const int tab_id_;
  CefRefPtr<CefMessageRouterBrowserSide> router_;
  std::unique_ptr<CefMessageRouterBrowserSide::Handler> bridge_;
  IMPLEMENT_REFCOUNTING(BrowserClient);
};
}
