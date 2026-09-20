#include "examples/soulu/browser_window.h"

#include <algorithm>
#include <filesystem>
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
  settings_->SetBool("mattePanel", false);
  settings_->SetString("searchEngine", "google");
  settings_->SetString("addressOpenMode", "current");
  settings_->SetString("addressPosition", "center");
  settings_->SetString("extensionsPosition", "left");
  settings_->SetBool("vpnToolbarVisible", true);
  settings_->SetString("downloadsMode", "dynamic");
  settings_->SetString("startPageMode", "blank");
  settings_->SetString("startPageUrl", "");
  settings_->SetBool("askDownloadLocation", true);
  settings_->SetString("downloadPath", "");
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
  const auto url = FileUrl(std::filesystem::u8path(ExecutableDirectory()) / "ui" / "index.html");
  CefBrowserHost::CreateBrowser(info, new BrowserClient(this, BrowserRole::kShell),
                                url, settings, nullptr, nullptr);
}

void BrowserWindow::NewTab(const std::string& url) {
  const int id = next_tab_id_++;
  Tab tab;
  tab.id = id;
  tab.url = url;
  tabs_.push_back(tab);
  active_tab_id_ = id;
  RECT rect = {};
  GetClientRect(hwnd_, &rect);
  CefWindowInfo info;
  info.SetAsChild(hwnd_, CefRect(0, 54, rect.right, std::max(1L, rect.bottom - 54L)));
  CefBrowserSettings settings;
  CefBrowserHost::CreateBrowser(info, new BrowserClient(this, BrowserRole::kContent, id),
                                url, settings, nullptr, nullptr);
  EmitState();
}

void BrowserWindow::AttachShell(CefRefPtr<CefBrowser> browser) {
  shell_ = browser;
  NewTab();
  Layout();
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

void BrowserWindow::SwitchTab(int id) {
  if (!FindTab(id)) return;
  active_tab_id_ = id;
  Layout();
  EmitState();
}

void BrowserWindow::CloseTab(int id) {
  auto it = std::find_if(tabs_.begin(), tabs_.end(), [id](const Tab& tab) { return tab.id == id; });
  if (it == tabs_.end()) return;
  const bool active = id == active_tab_id_;
  auto browser = it->browser;
  if (!browser) {
    tabs_.erase(it);
  } else {
    browser->GetHost()->CloseBrowser(true);
  }
  if (active && tabs_.size() > 1) {
    auto next = std::find_if(tabs_.begin(), tabs_.end(), [id](const Tab& tab) { return tab.id != id; });
    if (next != tabs_.end()) active_tab_id_ = next->id;
  }
  Layout();
  EmitState();
}

void BrowserWindow::BrowserClosed(CefRefPtr<CefBrowser> browser, int tab_id, bool shell) {
  if (shell) shell_ = nullptr;
  else {
    tabs_.erase(std::remove_if(tabs_.begin(), tabs_.end(),
                               [tab_id](const Tab& tab) { return tab.id == tab_id; }),
                tabs_.end());
    if (active_tab_id_ == tab_id) active_tab_id_ = tabs_.empty() ? 0 : tabs_.front().id;
    if (!closing_ && tabs_.empty()) NewTab();
  }
  if (closing_ && !shell_ && tabs_.empty()) DestroyWindow(hwnd_);
  else { Layout(); EmitState(); }
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
  bool replaced = false;
  for (size_t i = 0; i < downloads_->GetSize(); ++i) {
    auto current = downloads_->GetDictionary(i);
    if (current && current->GetInt("id") == static_cast<int>(item->GetId())) {
      downloads_->SetDictionary(i, row); replaced = true; break;
    }
  }
  if (!replaced) downloads_->SetDictionary(downloads_->GetSize(), row);
  Emit("downloads", Wrap(downloads_->Copy()));
}

void BrowserWindow::Layout() {
  if (!hwnd_) return;
  RECT client = {};
  GetClientRect(hwnd_, &client);
  const int width = client.right;
  const int height = client.bottom;
  if (shell_) {
    HWND shell_hwnd = shell_->GetHost()->GetWindowHandle();
    SetWindowPos(shell_hwnd, HWND_BOTTOM, 0, 0, width, height, SWP_NOACTIVATE);
  }
  const int toolbar = settings_->GetString("layout") == "classic" ? 82 : 54;
  const int x = sidebar_visible_ ? 276 : 0;
  const int y = std::max(toolbar, suggestions_height_);
  for (auto& tab : tabs_) {
    if (!tab.browser) continue;
    HWND child = tab.browser->GetHost()->GetWindowHandle();
    if (tab.id == active_tab_id_) {
      SetWindowPos(child, HWND_TOP, x, y, std::max(1, width - x - right_panel_width_),
                   std::max(1, height - y), SWP_SHOWWINDOW | SWP_NOACTIVATE);
    } else ShowWindow(child, SW_HIDE);
  }
}

CefRefPtr<CefDictionaryValue> BrowserWindow::State() const {
  auto state = CefDictionaryValue::Create();
  auto list = CefListValue::Create();
  for (size_t i = 0; i < tabs_.size(); ++i) {
    const auto& tab = tabs_[i];
    auto row = CefDictionaryValue::Create();
    row->SetInt("id", tab.id);
    row->SetString("title", tab.title);
    row->SetString("url", tab.url == "about:blank" ? "" : tab.url);
    row->SetString("label", tab.url == "about:blank" ? "" : tab.url);
    row->SetString("favicon", tab.favicon);
    row->SetBool("loading", tab.loading);
    row->SetBool("active", tab.id == active_tab_id_);
    list->SetDictionary(i, row);
  }
  state->SetList("tabs", list);
  state->SetInt("activeTabId", active_tab_id_);
  state->SetBool("sidebarVisible", sidebar_visible_);
  state->SetBool("maximized", IsZoomed(hwnd_) != FALSE);
  state->SetDictionary("settings", settings_->Copy(false));
  if (auto* tab = const_cast<BrowserWindow*>(this)->ActiveTab()) {
    auto page = CefDictionaryValue::Create();
    page->SetString("title", tab->title);
    page->SetString("url", tab->url == "about:blank" ? "" : tab->url);
    page->SetString("label", tab->url == "about:blank" ? "" : tab->url);
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
  if (!shell_ || !shell_->GetMainFrame()) return;
  const std::string script = "window.__souluEmit&&window.__souluEmit(\"" + event + "\"," + Json(value) + ");";
  shell_->GetMainFrame()->ExecuteJavaScript(script, shell_->GetMainFrame()->GetURL(), 0);
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
  else if (action == "browser.switchTab") SwitchTab(payload->GetInt());
  else if (action == "browser.closeTab") CloseTab(payload->GetInt());
  else if (action == "browser.toggleSidebar") { sidebar_visible_ = !sidebar_visible_; Layout(); EmitState(); }
  else if (action == "browser.setRightPanel") { right_panel_width_ = payload->GetInt(); Layout(); }
  else if (action == "browser.setSuggestionsHeight") { suggestions_height_ = payload->GetInt(); Layout(); }
  else if (action == "browser.find") {
    if (auto* t = ActiveTab(); t && t->browser) t->browser->GetHost()->Find(payload->GetString(), true, false, false);
  }
  else if (action == "browser.downloads.get") return Reply(callback, Wrap(downloads_->Copy()));
  else if (action == "browser.bookmarks.get") return Reply(callback, Wrap(bookmarks_->Copy()));
  else if (action == "browser.bookmarks.add") {
    if (auto* t = ActiveTab(); t && t->url != "about:blank") {
      auto mark = CefDictionaryValue::Create();
      mark->SetInt("id", static_cast<int>(bookmarks_->GetSize() + 1));
      mark->SetString("title", t->title); mark->SetString("url", t->url); mark->SetString("favicon", t->favicon);
      bookmarks_->SetDictionary(bookmarks_->GetSize(), mark);
    }
    return Reply(callback, Wrap(bookmarks_->Copy()));
  }
  else if (action == "browser.bookmarks.remove") {
    const int id = payload->GetInt();
    for (size_t i = 0; i < bookmarks_->GetSize(); ++i)
      if (bookmarks_->GetDictionary(i)->GetInt("id") == id) { bookmarks_->Remove(i); break; }
    return Reply(callback, Wrap(bookmarks_->Copy()));
  }
  else if (action == "browser.bookmarks.open") Navigate(payload->GetString());
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
    for (const auto& tab : tabs_) {
      std::string hay = tab.title + " " + tab.url;
      std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
      if (!query.empty() && hay.find(lower) != std::string::npos && tab.url != "about:blank") {
        auto row = CefDictionaryValue::Create(); row->SetString("source", "tab");
        row->SetString("title", tab.title); row->SetString("url", tab.url); row->SetString("favicon", tab.favicon);
        result->SetDictionary(out++, row);
      }
    }
    for (size_t i = 0; i < bookmarks_->GetSize() && out < 8; ++i) {
      auto mark = bookmarks_->GetDictionary(i); std::string hay = mark->GetString("title").ToString() + " " + mark->GetString("url").ToString();
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
    auto result = CefDictionaryValue::Create(); result->SetBool("ok", true); result->SetString("state", "disconnected");
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
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                       point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (command == 1) Emit("openSettings", EmptyValue());
    else if (command == 2) Emit("openDownloads", EmptyValue());
    else if (command == 3) NewTab();
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
    else if (command == 4) Emit("openSettings", EmptyValue());
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
