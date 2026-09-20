#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "include/cef_browser.h"
#include "include/cef_download_item.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_message_router.h"

namespace soulu {
class BrowserWindow final : public CefBaseRefCounted {
 public:
  static void Create();

  void AttachShell(CefRefPtr<CefBrowser> browser);
  void AttachContent(int tab_id, CefRefPtr<CefBrowser> browser);
  void BrowserClosed(CefRefPtr<CefBrowser> browser, int tab_id, bool shell);
  void UpdateTitle(int tab_id, const std::string& title);
  void UpdateAddress(int tab_id, const std::string& url);
  void UpdateFavicon(int tab_id, const std::string& url);
  void UpdateLoading(int tab_id, bool loading, bool can_go_back);
  void UpdateDownload(CefRefPtr<CefDownloadItem> item);
  void HandleBridge(const std::string& request,
                    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);

  HWND hwnd() const { return hwnd_; }

 private:
  struct Tab {
    int id = 0;
    CefRefPtr<CefBrowser> browser;
    std::string title = "New Tab";
    std::string url = "about:blank";
    std::string favicon;
    bool loading = false;
    bool can_go_back = false;
  };

  BrowserWindow();
  ~BrowserWindow() override = default;
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  bool CreateNativeWindow();
  void CreateShellBrowser();
  void NewTab(const std::string& url = "about:blank");
  void CloseTab(int id);
  void SwitchTab(int id);
  void Navigate(const std::string& value);
  void Layout();
  void CloseAll();
  Tab* ActiveTab();
  Tab* FindTab(int id);
  std::string NormalizeAddress(const std::string& value) const;
  CefRefPtr<CefDictionaryValue> State() const;
  std::string Json(CefRefPtr<CefValue> value) const;
  std::string Json(CefRefPtr<CefDictionaryValue> value) const;
  void Reply(CefRefPtr<CefMessageRouterBrowserSide::Callback> callback,
             CefRefPtr<CefValue> value);
  void Reply(CefRefPtr<CefMessageRouterBrowserSide::Callback> callback,
             CefRefPtr<CefDictionaryValue> value);
  void ReplyEmpty(CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);
  void Emit(const std::string& event, CefRefPtr<CefValue> value);
  void EmitState();
  void SetSetting(const std::string& key, CefRefPtr<CefValue> value);

  HWND hwnd_ = nullptr;
  CefRefPtr<CefBrowser> shell_;
  std::vector<Tab> tabs_;
  CefRefPtr<CefDictionaryValue> settings_;
  CefRefPtr<CefListValue> bookmarks_;
  CefRefPtr<CefListValue> downloads_;
  int next_tab_id_ = 1;
  int active_tab_id_ = 0;
  int right_panel_width_ = 0;
  int suggestions_height_ = 0;
  bool sidebar_visible_ = false;
  bool closing_ = false;
  IMPLEMENT_REFCOUNTING(BrowserWindow);
};
}
