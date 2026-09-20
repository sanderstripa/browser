#include "examples/soulu/browser_window.h"

#include <algorithm>
#include <dwmapi.h>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "examples/soulu/browser_client.h"
#include "examples/soulu/resource.h"
#include "include/cef_app.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

namespace soulu {
namespace {
constexpr wchar_t kWindowClass[] = L"SouluBrowserWindow";

std::string ExecutableDirectory() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return std::filesystem::path(path).parent_path().u8string();
}

std::filesystem::path UserDataDirectory() {
  wchar_t local_app_data[MAX_PATH] = {};
  const DWORD length = GetEnvironmentVariableW(
      L"LOCALAPPDATA", local_app_data, MAX_PATH);
  std::filesystem::path root =
      length ? std::filesystem::path(local_app_data)
             : std::filesystem::path(ExecutableDirectory());
  const auto directory = root / L"Soulu" / L"User Data";
  std::filesystem::create_directories(directory);
  return directory;
}

std::string FileUrl(std::filesystem::path path) {
  std::string value = std::filesystem::absolute(path).u8string();
  std::replace(value.begin(), value.end(), '\\', '/');
  std::string encoded;
  for (unsigned char c : value) encoded += c == ' ' ? "%20" : std::string(1, c);
  return "file:///" + encoded;
}

CefRefPtr<CefValue> Wrap(CefRefPtr<CefDictionaryValue> dictionary) {
  auto value = CefValue::Create();
  value->SetDictionary(dictionary);
  return value;
}

CefRefPtr<CefValue> Wrap(CefRefPtr<CefListValue> list) {
  auto value = CefValue::Create();
  value->SetList(list);
  return value;
}

CefRefPtr<CefValue> EmptyValue() {
  auto value = CefValue::Create();
  value->SetNull();
  return value;
}
}

BrowserWindow::BrowserWindow()
    : settings_(CefDictionaryValue::Create()),
      bookmarks_(CefListValue::Create()), downloads_(CefListValue::Create()) {
  settings_->SetString("layout", "compact");
  settings_->SetString("theme", "system");
  settings_->SetString("language", "ru");
  settings_->SetBool("mattePanel", true);
  settings_->SetString("searchEngine", "google");
  settings_->SetString("addressOpenMode", "current");
  settings_->SetString("addressPosition", "center");
  settings_->SetString("extensionsPosition", "left");
  settings_->SetBool("vpnToolbarVisible", true);
  settings_->SetBool("showSidebar", true);
  settings_->SetBool("showBack", true);
  settings_->SetBool("showFavorites", true);
  settings_->SetBool("showNewTab", true);
  settings_->SetBool("showDownloads", true);
  settings_->SetString("downloadsMode", "dynamic");
  settings_->SetString("startPageMode", "blank");
  settings_->SetString("startPageUrl", "");
  settings_->SetBool("askDownloadLocation", true);
  settings_->SetString("downloadPath", "");
  settings_->SetString("updateChannel", "stable");
  settings_->SetBool("automaticUpdates", true);
}

void BrowserWindow::Create() {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<BrowserWindow> window = new BrowserWindow();
  if (window->CreateNativeWindow()) window->CreateShellBrowser();
}

bool BrowserWindow::CreateNativeWindow() {
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_SOULU));
  wc.hIconSm = wc.hIcon;
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wc.lpszClassName = kWindowClass;
  RegisterClassExW(&wc);

  RECT work = {};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  const int width = std::min(1280L, work.right - work.left - 80L);
  const int height = std::min(820L, work.bottom - work.top - 60L);
  const int x = work.left + (work.right - work.left - width) / 2;
  const int y = work.top + (work.bottom - work.top - height) / 2;
  hwnd_ = CreateWindowExW(0, kWindowClass, L"Soulu",
                          WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX |
                              WS_MAXIMIZEBOX | WS_SYSMENU,
                          x, y, width, height, nullptr, nullptr, wc.hInstance, this);
  if (!hwnd_) return false;

  // Windows 11 system backdrop gives the frameless shell a native matte surface.
  const DWORD backdrop = 2;  // DWMSBT_MAINWINDOW
  DwmSetWindowAttribute(hwnd_, 38, &backdrop, sizeof(backdrop));
  const MARGINS glass = {-1};
  DwmExtendFrameIntoClientArea(hwnd_, &glass);

  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);
  return true;
}

void BrowserWindow::CreateShellBrowser() {
  RECT rect = {};
  GetClientRect(hwnd_, &rect);
  CefWindowInfo info;
  info.SetAsChild(hwnd_, CefRect(0, 0, rect.right, rect.bottom));
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  const auto url = FileUrl(std::filesystem::u8path(ExecutableDirectory()) / "ui" / "index.html");
  CefBrowserHost::CreateBrowser(info, new BrowserClient(this, BrowserRole::kShell),
                                url, settings, nullptr, nullptr);
}

void BrowserWindow::InitializeProfiles() {
  if (!profiles_.empty()) return;
  const auto file_path = UserDataDirectory() / L"profiles.json";
  std::ifstream file(file_path, std::ios::binary);
  if (file) {
    std::stringstream buffer;
    buffer << file.rdbuf();
    auto parsed = CefParseJSON(buffer.str(), JSON_PARSER_RFC);
    if (parsed && parsed->GetType() == VTYPE_LIST) {
      auto list = parsed->GetList();
      for (size_t i = 0; i < list->GetSize(); ++i) {
        auto profile = list->GetDictionary(i);
        if (profile)
          CreateProfile(profile->GetString("name"),
                        profile->GetString("id"));
      }
    }
  }
  if (profiles_.empty()) CreateProfile("Личный", "personal");
  active_profile_id_ = profiles_.front().id;
}

void BrowserWindow::CreateProfile(const std::string& name,
                                  const std::string& requested_id) {
  std::string id = requested_id.empty()
      ? (profiles_.empty() ? "personal"
                           : "profile-" + std::to_string(profiles_.size() + 1))
      : requested_id;
  CefRequestContextSettings context_settings;
  const auto profile_path = UserDataDirectory() /
      std::filesystem::u8path("Profiles/" + id);
  std::filesystem::create_directories(profile_path);
  CefString(&context_settings.cache_path) = profile_path.wstring();
  context_settings.persist_session_cookies = 1;
  context_settings.persist_user_preferences = 1;

  Profile profile;
  profile.id = id;
  profile.name = name.empty() ? "Профиль" : name;
  profile.context = CefRequestContext::CreateContext(context_settings, nullptr);
  ApplyProxy(profile.context);
  profiles_.push_back(profile);
  SaveProfiles();
}

void BrowserWindow::SaveProfiles() const {
  auto list = CefListValue::Create();
  for (size_t i = 0; i < profiles_.size(); ++i) {
    auto row = CefDictionaryValue::Create();
    row->SetString("id", profiles_[i].id);
    row->SetString("name", profiles_[i].name);
    list->SetDictionary(i, row);
  }
  auto value = CefValue::Create();
  value->SetList(list);
  std::ofstream file(UserDataDirectory() / L"profiles.json",
                     std::ios::binary | std::ios::trunc);
  file << CefWriteJSON(value, JSON_WRITER_DEFAULT);
}

BrowserWindow::Profile* BrowserWindow::ActiveProfile() {
  auto it = std::find_if(profiles_.begin(), profiles_.end(),
      [this](const Profile& profile) { return profile.id == active_profile_id_; });
  return it == profiles_.end() ? nullptr : &*it;
}

CefRefPtr<CefRequestContext> BrowserWindow::ContextForNewTab(bool incognito) {
  if (incognito) {
    if (!incognito_context_) {
      CefRequestContextSettings context_settings;
      incognito_context_ = CefRequestContext::CreateContext(context_settings, nullptr);
      ApplyProxy(incognito_context_);
    }
    return incognito_context_;
  }
  if (auto* profile = ActiveProfile()) return profile->context;
  return CefRequestContext::GetGlobalContext();
}

void BrowserWindow::ApplyProxy(CefRefPtr<CefRequestContext> context) {
  if (!context) return;
  auto proxy = CefDictionaryValue::Create();
  proxy->SetString("mode", vpn_enabled_ ? "fixed_servers" : "direct");
  if (vpn_enabled_) {
    proxy->SetString("server", "socks5://127.0.0.1:17890");
    proxy->SetString("bypass_list", "<-loopback>");
  }
  auto value = CefValue::Create();
  value->SetDictionary(proxy);
  CefString error;
  context->SetPreference("proxy", value, error);
}

void BrowserWindow::SwitchProfile(const std::string& id) {
  const auto it = std::find_if(profiles_.begin(), profiles_.end(),
      [&id](const Profile& profile) { return profile.id == id; });
  if (it == profiles_.end()) return;
  active_profile_id_ = id;
  auto tab = std::find_if(tabs_.begin(), tabs_.end(),
      [&id](const Tab& item) { return !item.incognito && item.profile_id == id; });
  if (tab == tabs_.end()) NewTab();
  else {
    active_tab_id_ = tab->id;
    Layout();
    EmitState();
  }
}

void BrowserWindow::OpenSettingsTab() {
  const auto url = FileUrl(std::filesystem::u8path(ExecutableDirectory()) /
                           "ui" / "settings.html");
  auto existing = std::find_if(tabs_.begin(), tabs_.end(),
      [&url, this](const Tab& tab) {
        return !tab.incognito && tab.profile_id == active_profile_id_ &&
               tab.url == url;
      });
  if (existing != tabs_.end()) SwitchTab(existing->id);
  else NewTab(url);
}

void BrowserWindow::NewTab(const std::string& url, bool incognito) {
  InitializeProfiles();
  const int id = next_tab_id_++;
  Tab tab;
  tab.id = id;
  tab.url = url;
  tab.incognito = incognito;
  tab.profile_id = incognito ? "__incognito__" : active_profile_id_;
  if (url.find("/ui/settings.html") != std::string::npos)
    tab.title = settings_->GetString("language") == "en" ? "Settings" : "Настройки";
  tabs_.push_back(tab);
  active_tab_id_ = id;

  RECT rect = {};
  GetClientRect(hwnd_, &rect);
  CefWindowInfo info;
  info.SetAsChild(hwnd_, CefRect(0, 48, rect.right,
                                std::max(1L, rect.bottom - 48L)));
  CefBrowserSettings browser_settings;
  const BrowserRole role =
      url.find("/ui/settings.html") != std::string::npos
          ? BrowserRole::kSettings : BrowserRole::kContent;
  CefBrowserHost::CreateBrowser(
      info, new BrowserClient(this, role, id), url, browser_settings,
      nullptr, ContextForNewTab(incognito));
  EmitState();
  if (url == "about:blank") FocusAddress();
}

void BrowserWindow::AttachShell(CefRefPtr<CefBrowser> browser) {
  shell_ = browser;
  InitializeProfiles();
  NewTab();
  Layout();
  FocusAddress();
}

void BrowserWindow::AttachContent(int tab_id, CefRefPtr<CefBrowser> browser) {
  if (auto* tab = FindTab(tab_id)) tab->browser = browser;
  Layout();
  EmitState();
}

BrowserWindow::Tab* BrowserWindow::FindTab(int id) {
  auto it = std::find_if(tabs_.begin(), tabs_.end(), [id](const Tab& tab) { return tab.id == id; });
  return it == tabs_.end() ? nullptr : &*it;
}

BrowserWindow::Tab* BrowserWindow::ActiveTab() { return FindTab(active_tab_id_); }

std::string BrowserWindow::VisibleProfileId() const {
  auto it = std::find_if(tabs_.begin(), tabs_.end(),
      [this](const Tab& tab) { return tab.id == active_tab_id_; });
  return it != tabs_.end() && it->incognito ? "__incognito__" : active_profile_id_;
}

void BrowserWindow::SwitchTab(int id) {
  if (!FindTab(id)) return;
  active_tab_id_ = id;
  Layout();
  EmitState();
}

void BrowserWindow::CloseTab(int id) {
  auto it = std::find_if(tabs_.begin(), tabs_.end(),
      [id](const Tab& tab) { return tab.id == id; });
  if (it == tabs_.end()) return;
  if (it->browser) {
    it->browser->GetHost()->CloseBrowser(true);
    return;
  }
  const bool active = id == active_tab_id_;
  tabs_.erase(it);
  if (active) active_tab_id_ = 0;
  if (!closing_ && active_tab_id_ == 0) NewTab();
  Layout();
  EmitState();
}

void BrowserWindow::BrowserClosed(CefRefPtr<CefBrowser> browser, int tab_id,
                                  bool shell) {
  if (shell) shell_ = nullptr;
  else {
    tabs_.erase(std::remove_if(tabs_.begin(), tabs_.end(),
                               [tab_id](const Tab& tab) { return tab.id == tab_id; }),
                tabs_.end());
    if (active_tab_id_ == tab_id) {
      const std::string visible = active_profile_id_;
      auto next = std::find_if(tabs_.begin(), tabs_.end(),
          [&visible](const Tab& item) {
            return !item.incognito && item.profile_id == visible;
          });
      active_tab_id_ = next == tabs_.end() ? 0 : next->id;
    }
    if (!closing_ && active_tab_id_ == 0) NewTab();
  }
  if (closing_ && !shell_ && tabs_.empty()) DestroyWindow(hwnd_);
  else { Layout(); EmitState(); }
}

void BrowserWindow::FocusAddress() {
  if (!shell_ || !shell_->GetMainFrame()) return;
  const std::string script =
      "setTimeout(()=>window.__souluEmit&&window.__souluEmit('focusAddress',null),0)";
  shell_->GetMainFrame()->ExecuteJavaScript(
      script, shell_->GetMainFrame()->GetURL(), 0);
}

void BrowserWindow::Navigate(const std::string& value) {
  std::string url = NormalizeAddress(value);
  auto* tab = ActiveTab();
  if (!tab) return;
  if (settings_->GetString("addressOpenMode") == "newIfOccupied" &&
      tab->url != "about:blank" && tab->url != url) {
    NewTab(url);
    return;
  }
  if (tab->browser) tab->browser->GetMainFrame()->LoadURL(url);
}

std::string BrowserWindow::NormalizeAddress(const std::string& input) const {
  std::string value = input;
  value.erase(0, value.find_first_not_of(" \t\r\n"));
  value.erase(value.find_last_not_of(" \t\r\n") + 1);
  if (value.find("://") != std::string::npos || value.rfind("about:", 0) == 0) return value;
  if (value.find(' ') == std::string::npos && value.find('.') != std::string::npos)
    return "https://" + value;
  return "https://www.google.com/search?q=" + CefURIEncode(value, true).ToString();
}

void BrowserWindow::UpdateTitle(int id, const std::string& title) {
  if (auto* tab = FindTab(id)) tab->title = title.empty() ? "New Tab" : title;
  EmitState();
}
void BrowserWindow::UpdateAddress(int id, const std::string& url) {
  if (auto* tab = FindTab(id)) tab->url = url;
  EmitState();
}
void BrowserWindow::UpdateFavicon(int id, const std::string& url) {
  if (auto* tab = FindTab(id)) tab->favicon = url;
  EmitState();
}
void BrowserWindow::UpdateLoading(int id, bool loading, bool can_go_back) {
  if (auto* tab = FindTab(id)) { tab->loading = loading; tab->can_go_back = can_go_back; }
  EmitState();
}

void BrowserWindow::UpdateDownload(CefRefPtr<CefDownloadItem> item) {
  auto row = CefDictionaryValue::Create();
  row->SetInt("id", static_cast<int>(item->GetId()));
  row->SetString("filename", item->GetSuggestedFileName());
  row->SetDouble("receivedBytes", static_cast<double>(item->GetReceivedBytes()));
  row->SetDouble("totalBytes", static_cast<double>(item->GetTotalBytes()));
  row->SetString("state", item->IsComplete() ? "completed" : item->IsCanceled() ? "cancelled" : "progressing");
  row->SetString("profileId", VisibleProfileId());
  bool replaced = false;
  for (size_t i = 0; i < downloads_->GetSize(); ++i) {
    auto current = downloads_->GetDictionary(i);
    if (current && current->GetInt("id") == static_cast<int>(item->GetId())) {
      downloads_->SetDictionary(i, row); replaced = true; break;
    }
  }
  if (!replaced) downloads_->SetDictionary(downloads_->GetSize(), row);
  Emit("downloads", Wrap(ProfileDownloads()));
}

CefRefPtr<CefListValue> BrowserWindow::ProfileBookmarks() const {
  auto result = CefListValue::Create();
  size_t output = 0;
  const std::string profile = VisibleProfileId();
  for (size_t i = 0; i < bookmarks_->GetSize(); ++i) {
    auto item = bookmarks_->GetDictionary(i);
    if (item && item->GetString("profileId") == profile)
      result->SetDictionary(output++, item->Copy(false));
  }
  return result;
}

CefRefPtr<CefListValue> BrowserWindow::ProfileDownloads() const {
  auto result = CefListValue::Create();
  size_t output = 0;
  const std::string profile = VisibleProfileId();
  for (size_t i = 0; i < downloads_->GetSize(); ++i) {
    auto item = downloads_->GetDictionary(i);
    if (item && item->GetString("profileId") == profile)
      result->SetDictionary(output++, item->Copy(false));
  }
  return result;
}

void BrowserWindow::Layout() {
  if (!hwnd_) return;
  RECT client = {};
  GetClientRect(hwnd_, &client);
  const int width = client.right;
  const int height = client.bottom;
  const int toolbar = settings_->GetString("layout") == "classic" ? 76 : 48;
  if (shell_) {
    HWND shell_hwnd = shell_->GetHost()->GetWindowHandle();
    const bool expanded_shell =
        sidebar_visible_ || right_panel_width_ > 0 ||
        suggestions_height_ > toolbar;
    const int shell_height = expanded_shell ? height : toolbar;
    SetWindowPos(shell_hwnd, HWND_TOP, 0, 0, width, shell_height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
  const int x = sidebar_visible_ ? 276 : 0;
  const int y = std::max(toolbar, suggestions_height_);
  const std::string visible_profile = VisibleProfileId();
  for (auto& tab : tabs_) {
    if (!tab.browser) continue;
    HWND child = tab.browser->GetHost()->GetWindowHandle();
    const bool belongs =
        tab.incognito ? visible_profile == "__incognito__"
                      : tab.profile_id == visible_profile;
    if (belongs && tab.id == active_tab_id_) {
      SetWindowPos(child, HWND_TOP, x, y, std::max(1, width - x - right_panel_width_),
                   std::max(1, height - y), SWP_SHOWWINDOW | SWP_NOACTIVATE);
    } else ShowWindow(child, SW_HIDE);
  }
}

CefRefPtr<CefDictionaryValue> BrowserWindow::State() const {
  auto state = CefDictionaryValue::Create();
  auto list = CefListValue::Create();
  size_t output_index = 0;
  const std::string visible_profile = VisibleProfileId();
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const auto& tab = tabs_[i];
    if (tab.incognito ? visible_profile != "__incognito__"
                      : tab.profile_id != visible_profile) continue;
    auto row = CefDictionaryValue::Create();
    row->SetInt("id", tab.id);
    const bool is_settings = tab.url.find("/ui/settings.html") != std::string::npos;
    row->SetString("title", is_settings
        ? (settings_->GetString("language") == "en" ? "Settings" : "Настройки")
        : tab.title);
    row->SetString("url", tab.url == "about:blank" ? "" :
        (is_settings ? "soulu://settings" : tab.url));
    row->SetString("label", tab.url == "about:blank" ? "" :
        (is_settings ? "Настройки Soulu" : tab.url));
    row->SetString("favicon", tab.favicon);
    row->SetBool("loading", tab.loading);
    row->SetBool("active", tab.id == active_tab_id_);
    row->SetBool("incognito", tab.incognito);
    list->SetDictionary(output_index++, row);
  }
  state->SetList("tabs", list);
  state->SetInt("activeTabId", active_tab_id_);
  state->SetBool("sidebarVisible", sidebar_visible_);
  state->SetBool("maximized", IsZoomed(hwnd_) != FALSE);
  state->SetString("activeProfileId", active_profile_id_);
  state->SetBool("incognito", visible_profile == "__incognito__");
  auto profiles = CefListValue::Create();
  for (size_t i = 0; i < profiles_.size(); ++i) {
    auto profile = CefDictionaryValue::Create();
    profile->SetString("id", profiles_[i].id);
    profile->SetString("name", profiles_[i].name);
    profile->SetBool("active", profiles_[i].id == active_profile_id_);
    profiles->SetDictionary(i, profile);
  }
  state->SetList("profiles", profiles);
  auto update = CefDictionaryValue::Create();
  update->SetString("soulu", "0.9.0-cef-preview.3");
  update->SetString("recommended", "0.9.0-cef-preview.3");
  update->SetString("cef", "144.0.6");
  update->SetString("chromium", "144");
  update->SetBool("available", false);
  update->SetBool("security", false);
  state->SetDictionary("update", update);
  state->SetDictionary("settings", settings_->Copy(false));
  if (auto* tab = const_cast<BrowserWindow*>(this)->ActiveTab()) {
    auto page = CefDictionaryValue::Create();
    const bool is_settings =
        tab->url.find("/ui/settings.html") != std::string::npos;
    page->SetString("title", is_settings
        ? (settings_->GetString("language") == "en" ? "Settings" : "Настройки")
        : tab->title);
    page->SetString("url", tab->url == "about:blank" ? "" :
        (is_settings ? "soulu://settings" : tab->url));
    page->SetString("label", tab->url == "about:blank" ? "" :
        (is_settings ? "Настройки Soulu" : tab->url));
    page->SetString("favicon", tab->favicon);
    page->SetBool("loading", tab->loading);
    page->SetBool("canGoBack", tab->can_go_back);
    state->SetDictionary("page", page);
  }
  return state;
}

std::string BrowserWindow::Json(CefRefPtr<CefValue> value) const {
  return CefWriteJSON(value, JSON_WRITER_DEFAULT);
}
std::string BrowserWindow::Json(CefRefPtr<CefDictionaryValue> dictionary) const {
  return Json(Wrap(dictionary));
}
void BrowserWindow::Reply(CefRefPtr<CefMessageRouterBrowserSide::Callback> callback,
                          CefRefPtr<CefValue> value) { callback->Success(Json(value)); }
void BrowserWindow::Reply(CefRefPtr<CefMessageRouterBrowserSide::Callback> callback,
                          CefRefPtr<CefDictionaryValue> value) { callback->Success(Json(value)); }
void BrowserWindow::ReplyEmpty(CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  Reply(callback, EmptyValue());
}
void BrowserWindow::Emit(const std::string& event, CefRefPtr<CefValue> value) {
  const std::string script =
      "window.__souluEmit&&window.__souluEmit(\"" + event + "\"," +
      Json(value) + ");";
  if (shell_ && shell_->GetMainFrame())
    shell_->GetMainFrame()->ExecuteJavaScript(
        script, shell_->GetMainFrame()->GetURL(), 0);
  if (auto* tab = ActiveTab(); tab && tab->browser &&
      tab->url.find("/ui/settings.html") != std::string::npos) {
    tab->browser->GetMainFrame()->ExecuteJavaScript(
        script, tab->browser->GetMainFrame()->GetURL(), 0);
  }
}
void BrowserWindow::EmitState() { Emit("state", Wrap(State())); }

void BrowserWindow::SetSetting(const std::string& key, CefRefPtr<CefValue> value) {
  settings_->SetValue(key, value->Copy());
  if (key == "layout") Layout();
}

void BrowserWindow::HandleBridge(const std::string& request,
                                 CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  auto parsed = CefParseJSON(request, JSON_PARSER_RFC);
  if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) {
    callback->Failure(400, "Invalid bridge request"); return;
  }
  auto root = parsed->GetDictionary();
  const std::string action = root->GetString("action");
  auto payload = root->GetValue("payload");

  if (action == "browser.state.get") return Reply(callback, State());
  if (action == "browser.navigate") Navigate(payload->GetString());
  else if (action == "browser.back") { if (auto* t = ActiveTab(); t && t->browser) t->browser->GoBack(); }
  else if (action == "browser.reload") {
    if (auto* t = ActiveTab(); t && t->browser) t->loading ? t->browser->StopLoad() : t->browser->Reload();
  }
  else if (action == "browser.newTab") NewTab();
  else if (action == "browser.newIncognito") NewTab("about:blank", true);
  else if (action == "browser.profile.create") {
    std::string name = payload && payload->GetType() == VTYPE_STRING
        ? payload->GetString() : "Профиль";
    CreateProfile(name);
    active_profile_id_ = profiles_.back().id;
    NewTab();
    return Reply(callback, State());
  }
  else if (action == "browser.profile.switch") {
    SwitchProfile(payload->GetString());
    return Reply(callback, State());
  }
  else if (action == "browser.update.check") {
    auto update = CefDictionaryValue::Create();
    update->SetString("soulu", "0.9.0-cef-preview.3");
    update->SetString("recommended", "0.9.0-cef-preview.3");
    update->SetString("cef", "144.0.6");
    update->SetString("chromium", "144");
    update->SetBool("available", false);
    update->SetBool("security", false);
    return Reply(callback, update);
  }
  else if (action == "browser.switchTab") SwitchTab(payload->GetInt());
  else if (action == "browser.closeTab") CloseTab(payload->GetInt());
  else if (action == "browser.toggleSidebar") { sidebar_visible_ = !sidebar_visible_; Layout(); EmitState(); }
  else if (action == "browser.setRightPanel") { right_panel_width_ = payload->GetInt(); Layout(); }
  else if (action == "browser.setSuggestionsHeight") { suggestions_height_ = payload->GetInt(); Layout(); }
  else if (action == "browser.find") {
    if (auto* t = ActiveTab(); t && t->browser) t->browser->GetHost()->Find(payload->GetString(), true, false, false);
  }
  else if (action == "browser.downloads.get") return Reply(callback, Wrap(ProfileDownloads()));
  else if (action == "browser.bookmarks.get") return Reply(callback, Wrap(ProfileBookmarks()));
  else if (action == "browser.bookmarks.add") {
    if (auto* t = ActiveTab(); t && t->url != "about:blank") {
      auto mark = CefDictionaryValue::Create();
      mark->SetInt("id", static_cast<int>(bookmarks_->GetSize() + 1));
      mark->SetString("title", t->title); mark->SetString("url", t->url); mark->SetString("favicon", t->favicon);
      mark->SetString("profileId", VisibleProfileId());
      bookmarks_->SetDictionary(bookmarks_->GetSize(), mark);
    }
    return Reply(callback, Wrap(ProfileBookmarks()));
  }
  else if (action == "browser.bookmarks.remove") {
    const int id = payload->GetInt();
    for (size_t i = 0; i < bookmarks_->GetSize(); ++i)
      if (bookmarks_->GetDictionary(i)->GetInt("id") == id) { bookmarks_->Remove(i); break; }
    return Reply(callback, Wrap(ProfileBookmarks()));
  }
  else if (action == "browser.bookmarks.open") Navigate(payload->GetString());
  else if (action == "browser.settings.openWindow") OpenSettingsTab();
  else if (action == "browser.settings.get") return Reply(callback, settings_->Copy(false));
  else if (action == "browser.settings.set") {
    if (payload && payload->GetType() == VTYPE_DICTIONARY) {
      auto patch = payload->GetDictionary(); CefDictionaryValue::KeyList keys; patch->GetKeys(keys);
      for (const auto& key : keys) SetSetting(key, patch->GetValue(key));
      EmitState();
    }
    return Reply(callback, settings_->Copy(false));
  }
  else if (action == "browser.suggestions") {
    auto result = CefListValue::Create();
    std::string query = payload->GetString();
    std::string lower = query; std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    size_t out = 0;
    const std::string visible_profile = VisibleProfileId();
    for (const auto& tab : tabs_) {
      if (tab.incognito ? visible_profile != "__incognito__"
                        : tab.profile_id != visible_profile) continue;
      std::string hay = tab.title + " " + tab.url;
      std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
      if (!query.empty() && hay.find(lower) != std::string::npos && tab.url != "about:blank") {
        auto row = CefDictionaryValue::Create(); row->SetString("source", "tab");
        row->SetString("title", tab.title); row->SetString("url", tab.url); row->SetString("favicon", tab.favicon);
        result->SetDictionary(out++, row);
      }
    }
    for (size_t i = 0; i < bookmarks_->GetSize() && out < 8; ++i) {
      auto mark = bookmarks_->GetDictionary(i);
      if (!mark || mark->GetString("profileId") != visible_profile) continue;
      std::string hay = mark->GetString("title").ToString() + " " + mark->GetString("url").ToString();
      std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
      if (hay.find(lower) != std::string::npos) { auto row = mark->Copy(false); row->SetString("source", "bookmark"); result->SetDictionary(out++, row); }
    }
    return Reply(callback, Wrap(result));
  }
  else if (action == "browser.passwords.get") return Reply(callback, Wrap(CefListValue::Create()));
  else if (action == "vpn.settings.get") {
    auto result = CefDictionaryValue::Create(); result->SetString("lastProfileId", "");
    return Reply(callback, result);
  }
  else if (action == "vpn.settings.set") return Reply(callback, payload);
  else if (action == "vpn.send") {
    std::string command;
    if (payload && payload->GetType() == VTYPE_DICTIONARY)
      command = payload->GetDictionary()->GetString("action");
    if (command == "connect") vpn_enabled_ = true;
    else if (command == "disconnect") vpn_enabled_ = false;
    for (auto& profile : profiles_) ApplyProxy(profile.context);
    ApplyProxy(incognito_context_);
    auto result = CefDictionaryValue::Create();
    result->SetBool("ok", true);
    result->SetString("state", vpn_enabled_ ? "connected" : "disconnected");
    result->SetString("scope", "soulu-only");
    Emit("vpnState", Wrap(result->Copy(false)));
    return Reply(callback, result);
  }
  else if (action == "window.minimize") ShowWindow(hwnd_, SW_MINIMIZE);
  else if (action == "window.maximize") ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
  else if (action == "window.close") PostMessage(hwnd_, WM_CLOSE, 0, 0);
  else if (action == "window.beginDrag") { ReleaseCapture(); SendMessage(hwnd_, WM_NCLBUTTONDOWN, HTCAPTION, 0); }
  else if (action == "window.toolbarMenu") {
    POINT point = {}; GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Настройки");
    AppendMenuW(menu, MF_STRING, 2, L"Загрузки");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"Новая вкладка");
    AppendMenuW(menu, MF_STRING, 4, L"Новая вкладка инкогнито");
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                       point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (command == 1) OpenSettingsTab();
    else if (command == 2) Emit("openDownloads", EmptyValue());
    else if (command == 3) NewTab();
    else if (command == 4) NewTab("about:blank", true);
  }
  else if (action == "browser.pageMenu") {
    POINT point = {}; GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Копировать адрес");
    AppendMenuW(menu, MF_STRING, 2, L"Добавить в избранное");
    AppendMenuW(menu, MF_STRING, 3, L"Найти на странице");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 4, L"Настройки Soulu");
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                       point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (command == 1) {
      if (auto* t = ActiveTab(); t && OpenClipboard(hwnd_)) {
        EmptyClipboard(); const std::wstring wide = CefString(t->url).ToWString();
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
        if (memory) { memcpy(GlobalLock(memory), wide.c_str(), (wide.size() + 1) * sizeof(wchar_t)); GlobalUnlock(memory); SetClipboardData(CF_UNICODETEXT, memory); }
        CloseClipboard();
      }
    } else if (command == 2) Emit("openFavorites", EmptyValue());
    else if (command == 3) Emit("requestFind", EmptyValue());
    else if (command == 4) OpenSettingsTab();
  }
  else if (action == "browser.shareMenu") {
    if (auto* t = ActiveTab()) {
      if (OpenClipboard(hwnd_)) { EmptyClipboard();
        const std::wstring wide = CefString(t->url).ToWString();
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
        if (memory) { memcpy(GlobalLock(memory), wide.c_str(), (wide.size() + 1) * sizeof(wchar_t)); GlobalUnlock(memory); SetClipboardData(CF_UNICODETEXT, memory); }
        CloseClipboard();
      }
    }
  }
  ReplyEmpty(callback);
}

void BrowserWindow::CloseAll() {
  if (closing_) return;
  closing_ = true;
  for (auto& tab : tabs_) if (tab.browser) tab.browser->GetHost()->CloseBrowser(true);
  if (shell_) shell_->GetHost()->CloseBrowser(true);
  if (!shell_ && tabs_.empty()) DestroyWindow(hwnd_);
}

LRESULT CALLBACK BrowserWindow::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  BrowserWindow* self = reinterpret_cast<BrowserWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    auto create = reinterpret_cast<CREATESTRUCT*>(lparam);
    self = static_cast<BrowserWindow*>(create->lpCreateParams);
    self->AddRef(); self->hwnd_ = hwnd;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (!self) return DefWindowProc(hwnd, message, wparam, lparam);
  switch (message) {
    case WM_SIZE: self->Layout(); return 0;
    case WM_CLOSE: self->CloseAll(); return 0;
    case WM_DESTROY: CefQuitMessageLoop(); return 0;
    case WM_NCDESTROY:
      SetWindowLongPtr(hwnd, GWLP_USERDATA, 0); self->hwnd_ = nullptr; self->Release(); break;
  }
  return DefWindowProc(hwnd, message, wparam, lparam);
}
}
