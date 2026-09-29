#include "examples/soulu/browser_client.h"

#include <string>

#include "examples/soulu/browser_window.h"
#include "include/cef_task.h"
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

class BrowserClosedTask final : public CefTask {
 public:
  BrowserClosedTask(CefRefPtr<BrowserWindow> owner, int tab_id, bool shell)
      : owner_(owner), tab_id_(tab_id), shell_(shell) {}

  void Execute() override {
    owner_->BrowserClosed(nullptr, tab_id_, shell_);
  }

 private:
  CefRefPtr<BrowserWindow> owner_;
  const int tab_id_;
  const bool shell_;
  IMPLEMENT_REFCOUNTING(BrowserClosedTask);
};
}

BrowserClient::BrowserClient(CefRefPtr<BrowserWindow> owner, BrowserRole role, int tab_id)
    : owner_(owner), role_(role), tab_id_(tab_id) {}

CefRefPtr<CefRenderHandler> BrowserClient::GetRenderHandler() {
  return role_ == BrowserRole::kShell ? owner_->surface() : nullptr;
}
bool BrowserClient::OnCursorChange(CefRefPtr<CefBrowser>, CefCursorHandle cursor, cef_cursor_type_t, const CefCursorInfo&) {
  if (role_ != BrowserRole::kShell || !owner_->surface()) return false;
  owner_->surface()->Cursor(cursor); return true;
}

bool BrowserClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                             CefRefPtr<CefFrame> frame,
                                             CefProcessId source_process,
                                             CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  return router_ && router_->OnProcessMessageReceived(browser, frame, source_process, message);
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  // Every browser gets a router. BridgeHandler itself strictly limits access
  // to Soulu's trusted local UI pages. Settings is intentionally opened as a
  // content tab, so excluding kContent made every settings control a no-op.
  CefMessageRouterConfig config;
  router_ = CefMessageRouterBrowserSide::Create(config);
  bridge_ = std::make_unique<BridgeHandler>(owner_);
  router_->AddHandler(bridge_.get(), false);
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
  // BrowserClosed may destroy the native top-level window when the final CEF
  // browser closes. Doing that synchronously from OnBeforeClose tears down the
  // Win32/CEF message loop while CEF is still unwinding this callback, which
  // can produce an access violation during graceful WM_CLOSE shutdown.
  // Defer only Soulu's bookkeeping until the callback has returned.
  CefPostTask(TID_UI, new BrowserClosedTask(
      owner_, tab_id_, role_ == BrowserRole::kShell));
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
  if (!loading && role_ != BrowserRole::kShell) owner_->ApplyContentTheme();
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
