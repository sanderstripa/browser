#pragma once

#include <windows.h>

#include <string>
#include <vector>
#include <memory>
#include <map>
#include "examples/soulu/profile_data.h"

#include "include/cef_browser.h"
#include "examples/soulu/shell_surface.h"
#include "include/cef_download_item.h"
#include "include/cef_registration.h"
#include "include/cef_request_context.h"
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
  void StoreThumbnail(int id, const std::string& url, const std::string& data);
  void UpdateDownload(int tab_id, CefRefPtr<CefDownloadItem> item);
  void HandleBridge(const std::string& request,
                    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);

  HWND hwnd() const { return hwnd_; }
  CefRefPtr<ShellSurface> surface() const { return surface_; }
  void ApplyContentTheme();
  void OpenIncognitoLink(int source_id, CefRefPtr<CefBrowser> source,
                         const std::string& url);
  int PreparePopup(int source_id, const std::string& url, bool background,
                   CefWindowInfo& info);
  void AbortPopup(int tab_id);
  void OpenTabFrom(int source_id, CefRefPtr<CefBrowser> source,
                   const std::string& url, bool background);
  void CookieStoreFlushed();
  void RequestContextInitialized(CefRefPtr<CefRequestContext> context);
  bool IsTrustedUi(const std::string& url) const;
  bool IsIncognitoTab(int id);
  std::shared_ptr<SitePolicy> PolicyForTab(int id);
  bool AllowSite(int id, const std::string& origin, const std::string& permission);
  void ApplySiteSound();
  void OfferCredential(int id, CefRefPtr<CefFrame> frame,
                       const std::string& username, std::string password);

 private:
  struct Tab {
    int id = 0;
    CefRefPtr<CefBrowser> browser;
    std::string title = "New Tab";
    std::string url = "about:blank";
    std::string favicon;
    std::string thumbnail;
    CefRefPtr<CefRegistration> thumbnail_registration;
    std::string profile_id = "personal";
    bool incognito = false;
    bool activate_on_attach = false;
    bool loading = false;
    bool can_go_back = false;
  };

  struct Profile {
    std::string id;
    std::string name;
    CefRefPtr<CefRequestContext> context;
  };

  BrowserWindow();
  ~BrowserWindow() override = default;
  static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  bool CreateNativeWindow();
  void CreateShellBrowser();
  void OpenSettingsTab();
  void InitializeProfiles();
  void CreateProfile(const std::string& name, const std::string& requested_id = "");
  void SaveProfiles() const;
  void LoadSettings();
  bool SaveBookmarks(CefRefPtr<CefListValue> rows) const;
  void CaptureThumbnail();
  bool BookmarksBarVisible() const;
  void SaveSettings() const;
  void SwitchProfile(const std::string& id);
  void LoadProfileSettings();
  void ReleaseIncognito();
  Profile* ActiveProfile();
  CefRefPtr<CefRequestContext> ContextForNewTab(bool incognito);
  void ApplyProxy(CefRefPtr<CefRequestContext> context);
  void ApplyWindowAppearance();
  CefRefPtr<CefDictionaryValue> SendVpnHelper(
      CefRefPtr<CefDictionaryValue> request) const;
  void NewTab(const std::string& url = "about:blank", bool incognito = false,
              bool foreground = true, CefRefPtr<CefRequestContext> context = nullptr,
              const std::string& profile_id = "");
  void CloseTab(int id);
  void SwitchTab(int id);
  void Navigate(const std::string& value);
  void FocusAddress();
  void Layout();
  struct Geometry {
    int width, height, toolbar, shell_height, sidebar, panel;
    float scale;
    CefRect content;
  };
  Geometry CurrentGeometry() const;
  void ShowWhenReady();
  void CloseAll();
  void FinishClose();
  Tab* ActiveTab();
  Tab* FindTab(int id);
  std::string VisibleProfileId() const;
  std::string NormalizeAddress(const std::string& value) const;
  CefRefPtr<CefDictionaryValue> State() const;
  CefRefPtr<CefListValue> ProfileBookmarks() const;
  CefRefPtr<CefListValue> ProfileDownloads() const;
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
  HWND resize_border_ = nullptr;
  bool native_blur_ = false;
  bool shell_frame_ready_ = false;
  CefRefPtr<CefBrowser> shell_;
  CefRefPtr<ShellSurface> surface_;
  std::vector<Tab> tabs_;
  std::vector<Profile> profiles_;
  CefRefPtr<CefRequestContext> incognito_context_;
  CefRefPtr<CefDictionaryValue> settings_;
  CefRefPtr<CefDictionaryValue> initial_settings_;
  std::map<std::string, std::shared_ptr<SitePolicy>> policies_;
  CefRefPtr<CefDictionaryValue> vpn_settings_;
  CefRefPtr<CefListValue> bookmarks_;
  CefRefPtr<CefListValue> downloads_;
  std::string active_profile_id_ = "personal";
  int next_tab_id_ = 1;
  int active_tab_id_ = 0;
  int right_panel_width_ = 0;
  int suggestions_height_ = 0;
  bool sidebar_visible_ = false;
  bool overview_visible_ = false;
  bool popover_visible_ = false;
  bool bookmarks_auto_visible_ = false;
  bool vpn_enabled_ = false;
  bool closing_ = false;
  bool importing_ = false;
  bool close_after_import_ = false;
  bool flushing_cookies_ = false;
  int pending_cookie_flushes_ = 0;
  IMPLEMENT_REFCOUNTING(BrowserWindow);
};
}

