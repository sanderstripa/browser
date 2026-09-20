#include "examples/soulu/browser_client.h"

#include <string>

#include "examples/soulu/browser_window.h"
#include "include/wrapper/cef_helpers.h"

namespace soulu {
namespace {
class BridgeHandler final : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit BridgeHandler(CefRefPtr<BrowserWindow> owner) : owner_(owner) {}
  bool OnQuery(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int64_t,
               const CefString& request, bool,
               CefRefPtr<Callback> callback) override {
    if (!frame->IsMain()) return false;
    const std::string url = frame->GetURL();
    if (url.rfind("file://", 0) != 0 ||
        (url.find("/ui/index.html") == std::string::npos &&
         url.find("/ui/settings.html") == std::string::npos)) {
      return false;
    }
    owner_->HandleBridge(request, callback);
    return true;
  }
 private:
  CefRefPtr<BrowserWindow> owner_;
};
}

BrowserClient::BrowserClient(CefRefPtr<BrowserWindow> owner, BrowserRole role, int tab_id)
    : owner_(owner), role_(role), tab_id_(tab_id) {}

bool BrowserClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                             CefRefPtr<CefFrame> frame,
                                             CefProcessId source_process,
                                             CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  return router_ && router_->OnProcessMessageReceived(browser, frame, source_process, message);
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (role_ != BrowserRole::kContent) {
    CefMessageRouterConfig config;
    router_ = CefMessageRouterBrowserSide::Create(config);
    bridge_ = std::make_unique<BridgeHandler>(owner_);
    router_->AddHandler(bridge_.get(), false);
  }
  if (role_ == BrowserRole::kShell) owner_->AttachShell(browser);
  else owner_->AttachContent(tab_id_, browser);
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser>) { return false; }

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (router_) {
    router_->RemoveHandler(bridge_.get());
    bridge_.reset();
    router_ = nullptr;
  }
  owner_->BrowserClosed(browser, tab_id_, role_ == BrowserRole::kShell);
}

void BrowserClient::OnTitleChange(CefRefPtr<CefBrowser>, const CefString& title) {
  if (role_ != BrowserRole::kShell) owner_->UpdateTitle(tab_id_, title);
}

void BrowserClient::OnAddressChange(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                                    const CefString& url) {
  if (role_ != BrowserRole::kShell && frame->IsMain()) owner_->UpdateAddress(tab_id_, url);
}

void BrowserClient::OnFaviconURLChange(CefRefPtr<CefBrowser>,
                                       const std::vector<CefString>& icon_urls) {
  if (role_ != BrowserRole::kShell)
    owner_->UpdateFavicon(tab_id_, icon_urls.empty() ? "" : icon_urls.front());
}

void BrowserClient::OnLoadingStateChange(CefRefPtr<CefBrowser>, bool loading,
                                         bool can_go_back, bool) {
  if (role_ != BrowserRole::kShell)
    owner_->UpdateLoading(tab_id_, loading, can_go_back);
}

bool BrowserClient::OnBeforeDownload(CefRefPtr<CefBrowser>,
                                     CefRefPtr<CefDownloadItem>,
                                     const CefString& suggested_name,
                                     CefRefPtr<CefBeforeDownloadCallback> callback) {
  callback->Continue(suggested_name, true);
  return true;
}

void BrowserClient::OnDownloadUpdated(CefRefPtr<CefBrowser>,
                                      CefRefPtr<CefDownloadItem> item,
                                      CefRefPtr<CefDownloadItemCallback>) {
  owner_->UpdateDownload(item);
}
}
