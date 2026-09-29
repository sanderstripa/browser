#include "examples/soulu/browser_window.h"

#include <windowsx.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include "include/cef_urlrequest.h"
#include "include/cef_task.h"
#include <dwmapi.h>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "examples/soulu/browser_client.h"
#include "examples/soulu/frosted_backdrop.h"
#include "examples/soulu/resource.h"
#include "include/cef_app.h"
#include "include/cef_cookie.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

namespace soulu {

class CookieFlushCallback final : public CefCompletionCallback {
 public:
  explicit CookieFlushCallback(CefRefPtr<BrowserWindow> owner)
      : owner_(owner) {}

  void OnComplete() override { owner_->OnCookieFlushComplete(); }

 private:
  CefRefPtr<BrowserWindow> owner_;
  IMPLEMENT_REFCOUNTING(CookieFlushCallback);
};

namespace {
class SuggestClient final : public CefURLRequestClient {
 public:
  SuggestClient(CefRefPtr<CefListValue> local, CefRefPtr<CefMessageRouterBrowserSide::Callback> reply)
      : rows_(local), reply_(reply) {}
  void ReplyNow() {
    if (!reply_) return;
    auto value = CefValue::Create(); value->SetList(rows_);
    reply_->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT));
    reply_ = nullptr;
  }
  void OnRequestComplete(CefRefPtr<CefURLRequest>) override {
    if (!reply_) return;
    auto data = CefParseJSON(body_, JSON_PARSER_RFC);
    if (data && data->GetType() == VTYPE_LIST) {
      auto list = data->GetList();
      if (list->GetSize() > 1 && list->GetType(1) == VTYPE_LIST) {
        auto suggestions = list->GetList(1);
        for (size_t i = 0; i < suggestions->GetSize() && rows_->GetSize() < 9; ++i) {
          if (suggestions->GetType(i) != VTYPE_STRING) continue;
          const std::string text = suggestions->GetString(i);
          auto row = CefDictionaryValue::Create(); row->SetString("source", "search");
          row->SetString("title", text); row->SetString("query", text);
          rows_->SetDictionary(rows_->GetSize(), row);
        }
      }
    }
    ReplyNow();
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest>, int64_t, int64_t) override {}
  void OnDownloadData(CefRefPtr<CefURLRequest>, const void* data, size_t size) override {
    if (body_.size() + size <= 131072) body_.append(static_cast<const char*>(data), size);
  }
  bool GetAuthCredentials(bool, const CefString&, int, const CefString&, const CefString&, CefRefPtr<CefAuthCallback>) override { return false; }
 private:
  std::string body_;
  CefRefPtr<CefListValue> rows_;
  CefRefPtr<CefMessageRouterBrowserSide::Callback> reply_;
  IMPLEMENT_REFCOUNTING(SuggestClient);
};
class SuggestTimeout final : public CefTask {
 public:
  explicit SuggestTimeout(CefRefPtr<SuggestClient> client) : client_(client) {}
  void Execute() override { client_->ReplyNow(); }
 private:
  CefRefPtr<SuggestClient> client_;
  IMPLEMENT_REFCOUNTING(SuggestTimeout);
};
// Native blur is explicit instead of the system acrylic's opaque fallback.
struct AccentPolicy { int state; int flags; DWORD tint; int animation; };
struct CompositionData { int attribute; void* data; SIZE_T size; };
using SetComposition = BOOL(WINAPI*)(HWND, CompositionData*);
int ResizeHit(HWND parent, POINT p) {
  RECT r={};GetWindowRect(parent,&r);
  const int edge=std::max(4,static_cast<int>(GetDpiForWindow(parent)*6/96));
  const bool l=p.x<r.left+edge, rr=p.x>=r.right-edge;
  const bool t=p.y<r.top+edge,b=p.y>=r.bottom-edge;
  if(t&&l)return HTTOPLEFT;if(t&&rr)return HTTOPRIGHT;
  if(b&&l)return HTBOTTOMLEFT;if(b&&rr)return HTBOTTOMRIGHT;
  return l?HTLEFT:rr?HTRIGHT:t?HTTOP:HTBOTTOM;
}
LRESULT CALLBACK ResizeProc(HWND window,UINT message,WPARAM wp,LPARAM lp){
  HWND parent=GetParent(window);POINT p={};GetCursorPos(&p);
  const int hit=ResizeHit(parent,p);
  if(message==WM_SETCURSOR){
    const auto cursor=(hit==HTLEFT||hit==HTRIGHT)?IDC_SIZEWE:(hit==HTTOP||hit==HTBOTTOM)?IDC_SIZENS:
      (hit==HTTOPLEFT||hit==HTBOTTOMRIGHT)?IDC_SIZENWSE:IDC_SIZENESW;
    SetCursor(LoadCursor(nullptr,cursor));return TRUE;
  }
  if(message==WM_LBUTTONDOWN){ReleaseCapture();SendMessageW(parent,WM_NCLBUTTONDOWN,hit,MAKELPARAM(p.x,p.y));return 0;}
  return DefWindowProcW(window,message,wp,lp);
}
constexpr wchar_t kWindowClass[] = L"SouluBrowserWindow";

std::string ExecutableDirectory() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  return CefString(std::filesystem::path(path).parent_path().wstring()).ToString();
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
  std::string value = CefString(std::filesystem::absolute(path).wstring()).ToString();
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

bool IsWindowsDarkMode() {
  DWORD value = 1;
  DWORD size = sizeof(value);
  RegGetValueW(HKEY_CURRENT_USER,
               L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
               L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
  return value == 0;
}
}

BrowserWindow::BrowserWindow()
    : settings_(CefDictionaryValue::Create()),
      vpn_settings_(CefDictionaryValue::Create()),
      bookmarks_(CefListValue::Create()), downloads_(CefListValue::Create()) {
  WSADATA winsock = {};
  WSAStartup(MAKEWORD(2, 2), &winsock);
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
  settings_->SetBool("openStartPageAfterLastTab", false);
  settings_->SetBool("askDownloadLocation", true);
  settings_->SetString("downloadPath", "");
  settings_->SetString("updateChannel", "stable");
  settings_->SetBool("automaticUpdates", true);
  vpn_settings_->SetString("protocol", "vless");
  vpn_settings_->SetString("link", "");
  vpn_settings_->SetString("address", "");
  vpn_settings_->SetString("region", "");
  vpn_settings_->SetString("lastProfileId", "");
  LoadSettings();
}

void BrowserWindow::LoadSettings() {
  std::ifstream file(UserDataDirectory() / L"settings.json", std::ios::binary);
  if (!file) { settings_->SetBool("matteDefaultV15", true); return; }
  std::stringstream buffer; buffer << file.rdbuf();
  auto parsed = CefParseJSON(buffer.str(), JSON_PARSER_RFC);
  if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) return;
  auto root = parsed->GetDictionary();
  if (auto saved = root->GetDictionary("settings")) {
    CefDictionaryValue::KeyList keys; saved->GetKeys(keys);
    for (const auto& key : keys) settings_->SetValue(key, saved->GetValue(key)->Copy());
  }
  if (auto saved = root->GetDictionary("vpn")) vpn_settings_ = saved->Copy(false);
  // Earlier previews persisted the disabled default. Apply the new default
  // once to existing profiles; subsequent user choices remain untouched.
  if (!settings_->HasKey("matteDefaultV15")) {
    settings_->SetBool("mattePanel", true);
    settings_->SetBool("matteDefaultV15", true);
    SaveSettings();
  }
}

void BrowserWindow::SaveSettings() const {
  auto root = CefDictionaryValue::Create();
  root->SetDictionary("settings", settings_->Copy(false));
  root->SetDictionary("vpn", vpn_settings_->Copy(false));
  std::ofstream file(UserDataDirectory() / L"settings.json", std::ios::binary | std::ios::trunc);
  file << CefWriteJSON(Wrap(root), JSON_WRITER_PRETTY_PRINT);
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
  wc.hIcon = static_cast<HICON>(LoadImageW(wc.hInstance,
      MAKEINTRESOURCEW(IDI_SOULU), IMAGE_ICON,
      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
  wc.hIconSm = static_cast<HICON>(LoadImageW(wc.hInstance,
      MAKEINTRESOURCEW(IDI_SOULU), IMAGE_ICON,
      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
  // A permanent white class brush shows through translucent CEF pixels even
  // after switching to dark mode. Paint the exposed native surface per theme.
  wc.hbrBackground = nullptr;
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

  WNDCLASSEXW edgeClass={sizeof(edgeClass)};
  edgeClass.lpfnWndProc=ResizeProc;edgeClass.hInstance=wc.hInstance;
  edgeClass.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  edgeClass.lpszClassName=L"SouluResizeHitArea";RegisterClassExW(&edgeClass);
  resize_border_=CreateWindowExW(WS_EX_LAYERED,edgeClass.lpszClassName,L"",WS_CHILD,
    0,0,width,height,hwnd_,nullptr,wc.hInstance,nullptr);
  // Alpha 1 keeps mouse hit testing enabled with no visible reserved frame.
  SetLayeredWindowAttributes(resize_border_,0,1,LWA_ALPHA);
  ApplyWindowAppearance();

  ShowWindow(hwnd_, SW_SHOW);
  UpdateWindow(hwnd_);
  return true;
}

void BrowserWindow::CreateShellBrowser() {
  RECT rect = {};
  GetClientRect(hwnd_, &rect);
  CefWindowInfo info;
  surface_ = new ShellSurface(hwnd_);
  surface_->Resize(0, 0, rect.right, 48);
  info.SetAsWindowless(surface_->hwnd());
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  settings.windowless_frame_rate = 30;
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

void BrowserWindow::ApplyWindowAppearance() {
  if (!hwnd_) return;
  const std::string theme = settings_->GetString("theme");
  const BOOL dark = theme == "dark" || (theme == "system" && IsWindowsDarkMode());
  DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));

  const DWORD corner=2, noBorder=0xFFFFFFFE, noBackdrop=1;
  DwmSetWindowAttribute(hwnd_,33,&corner,sizeof(corner));
  DwmSetWindowAttribute(hwnd_,34,&noBorder,sizeof(noBorder));
  DwmSetWindowAttribute(hwnd_,38,&noBackdrop,sizeof(noBackdrop));
  const bool matte=settings_->GetBool("mattePanel");
  const auto compose=reinterpret_cast<SetComposition>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetWindowCompositionAttribute"));
  AccentPolicy policy={0,0,0,0};
  CompositionData data={19,&policy,sizeof(policy)};
  if(compose)compose(hwnd_,&data);
  native_blur_=ConfigureFrostedBackdrop(hwnd_,matte);
  RECT area={};GetClientRect(hwnd_,&area);
  ResizeFrostedBackdrop(hwnd_,area.right,static_cast<int>((settings_->GetString("layout")=="classic"?82:48)*GetDpiForWindow(hwnd_)/96));
  const MARGINS glass=matte?MARGINS{-1,-1,-1,-1}:MARGINS{0,0,0,0};
  DwmExtendFrameIntoClientArea(hwnd_,&glass);
  // Keep the native redirection surface for layered child chrome, but expose
  // its transparent pixels to the desktop composition backdrop.
  HRGN region=CreateRectRgn(0,0,-1,-1);
  DWM_BLURBEHIND blur={};
  blur.dwFlags=DWM_BB_ENABLE|DWM_BB_BLURREGION;
  blur.fEnable=matte;
  blur.hRgnBlur=region;
  DwmEnableBlurBehindWindow(hwnd_,&blur);
  DeleteObject(region);
  RedrawWindow(hwnd_,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN);

}

CefRefPtr<CefDictionaryValue> BrowserWindow::SendVpnHelper(
    CefRefPtr<CefDictionaryValue> request) const {
  auto result = CefDictionaryValue::Create();
  const auto helper = std::filesystem::u8path(ExecutableDirectory()) /
                      "vpn" / "native-host" / "VlessXhttpNativeHost.exe";
  if (!std::filesystem::exists(helper)) {
    result->SetBool("ok", false);
    result->SetString("error", "VPN helper is missing from the installation.");
    return result;
  }

  SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  HANDLE input_read = nullptr, input_write = nullptr;
  HANDLE output_read = nullptr, output_write = nullptr;
  if (!CreatePipe(&input_read, &input_write, &security, 0) ||
      !CreatePipe(&output_read, &output_write, &security, 0)) {
    result->SetBool("ok", false);
    result->SetString("error", "Cannot create VPN helper pipes.");
    return result;
  }
  SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW startup = {sizeof(STARTUPINFOW)};
  startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  startup.hStdInput = input_read;
  startup.hStdOutput = output_write;
  startup.hStdError = output_write;
  PROCESS_INFORMATION process = {};
  std::wstring command = L"\"" + helper.wstring() + L"\"";
  const BOOL started = CreateProcessW(
      nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
      nullptr, helper.parent_path().c_str(), &startup, &process);
  CloseHandle(input_read);
  CloseHandle(output_write);
  if (!started) {
    CloseHandle(input_write);
    CloseHandle(output_read);
    result->SetBool("ok", false);
    result->SetString("error", "VPN helper could not be started.");
    return result;
  }

  const std::string json = Json(request);
  const uint32_t length = static_cast<uint32_t>(json.size());
  DWORD written = 0;
  const bool sent = WriteFile(input_write, &length, sizeof(length), &written, nullptr) &&
                    written == sizeof(length) &&
                    WriteFile(input_write, json.data(), length, &written, nullptr) &&
                    written == length;
  CloseHandle(input_write);

  uint32_t response_length = 0;
  DWORD read = 0;
  bool received = sent && ReadFile(output_read, &response_length,
                                   sizeof(response_length), &read, nullptr) &&
                  read == sizeof(response_length) && response_length < 4 * 1024 * 1024;
  std::string response(received ? response_length : 0, '\0');
  if (received && response_length) {
    received = ReadFile(output_read, response.data(), response_length,
                        &read, nullptr) && read == response_length;
  }
  CloseHandle(output_read);
  WaitForSingleObject(process.hProcess, 15000);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);

  if (received) {
    auto parsed = CefParseJSON(response, JSON_PARSER_RFC);
    if (parsed && parsed->GetType() == VTYPE_DICTIONARY)
      return parsed->GetDictionary()->Copy(false);
  }
  result->SetBool("ok", false);
  result->SetString("error", "VPN helper returned an invalid response.");
  return result;
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
  else if (auto* tab = ActiveTab(); tab && tab->url == "about:blank" && tab->browser) {
    tab->url = url;
    tab->title = settings_->GetString("language") == "en" ? "Settings" : "Настройки";
    tab->browser->GetMainFrame()->LoadURL(url);
    EmitState();
  } else NewTab(url);
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
  const bool dark = settings_->GetString("theme") == "dark" || (settings_->GetString("theme") == "system" && IsWindowsDarkMode());
  browser_settings.background_color = dark ? CefColorSetARGB(255,8,9,11) : CefColorSetARGB(255,250,250,250);
  const BrowserRole role =
      url.find("/ui/settings.html") != std::string::npos
          ? BrowserRole::kSettings : BrowserRole::kContent;
  CefBrowserHost::CreateBrowser(
      info, new BrowserClient(this, role, id),
      url == "about:blank" ? FileUrl(std::filesystem::u8path(ExecutableDirectory()) / "ui" / "start.html") : url, browser_settings,
      nullptr, ContextForNewTab(incognito));
  EmitState();
  if (url == "about:blank") FocusAddress();
}

void BrowserWindow::AttachShell(CefRefPtr<CefBrowser> browser) {
  shell_ = browser;
  surface_->Attach(browser);
  InitializeProfiles();
  std::string start_url = "about:blank";
  const std::string custom_url = settings_->GetString("startPageUrl");
  if (settings_->GetString("startPageMode") == "custom" && !custom_url.empty())
    start_url = NormalizeAddress(custom_url);
  NewTab(start_url);
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
  if (!closing_ && active_tab_id_ == 0) {
    if (settings_->GetBool("openStartPageAfterLastTab")) {
      std::string start_url = "about:blank";
      const std::string custom_url = settings_->GetString("startPageUrl");
      if (settings_->GetString("startPageMode") == "custom" && !custom_url.empty())
        start_url = NormalizeAddress(custom_url);
      NewTab(start_url);
    } else {
      NewTab();
    }
  }
  Layout();
  EmitState();
}

void BrowserWindow::BrowserClosed(CefRefPtr<CefBrowser> browser, int tab_id,
                                  bool shell) {
  if (shell) { if (surface_) surface_->Detach(); shell_ = nullptr; }
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
    if (!closing_ && active_tab_id_ == 0) {
      if (settings_->GetBool("openStartPageAfterLastTab")) {
        std::string start_url = "about:blank";
        const std::string custom_url = settings_->GetString("startPageUrl");
        if (settings_->GetString("startPageMode") == "custom" && !custom_url.empty())
          start_url = NormalizeAddress(custom_url);
        NewTab(start_url);
      } else {
        NewTab();
      }
    }
  }
  if (closing_ && !shell_ && tabs_.empty()) DestroyWindow(hwnd_);
  else { Layout(); EmitState(); }
}

void BrowserWindow::FocusAddress() {
  if (!shell_ || !shell_->GetMainFrame()) return;
  if (surface_) surface_->Focus();
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
  const std::string engine = settings_->GetString("searchEngine");
  const std::string base = engine == "yandex" ? "https://yandex.ru/search/?text=" :
      engine == "bing" ? "https://www.bing.com/search?q=" :
      engine == "duckduckgo" ? "https://duckduckgo.com/?q=" : "https://www.google.com/search?q=";
  return base + CefURIEncode(value, true).ToString();
}

void BrowserWindow::UpdateTitle(int id, const std::string& title) {
  if (auto* tab = FindTab(id)) tab->title = title.empty() ? "New Tab" : title;
  EmitState();
}
void BrowserWindow::UpdateAddress(int id, const std::string& url) {
  if (auto* tab = FindTab(id)) tab->url = url.find("/ui/start.html") != std::string::npos ? "about:blank" : url;
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
  RECT client = {}; GetClientRect(hwnd_, &client);
  const float scale = GetDpiForWindow(hwnd_) / 96.0f;
  const auto px = [scale](int value) { return static_cast<int>(std::round(value * scale)); };
  // Content reaches every edge; a transparent hit area handles resizing.
  const int border = 0;
  const int width = std::max(1L, client.right - border * 2);
  const int height = std::max(1L, client.bottom - border * 2);
  const int toolbar = px(settings_->GetString("layout") == "classic" ? 82 : 48);
  ResizeFrostedBackdrop(hwnd_,width,toolbar);
  const int x = sidebar_visible_ ? px(276) : 0;
  const std::string profile = VisibleProfileId();
  for (auto& tab : tabs_) {
    if (!tab.browser) continue;
    HWND child = tab.browser->GetHost()->GetWindowHandle();
    const bool belongs = tab.incognito ? profile == "__incognito__" : tab.profile_id == profile;
    if (belongs && tab.id == active_tab_id_)
      SetWindowPos(child, HWND_TOP, border + x, border + toolbar,
        std::max(1, width - x - px(right_panel_width_)), std::max(1, height - toolbar), SWP_SHOWWINDOW | SWP_NOACTIVATE);
    else ShowWindow(child, SW_HIDE);
  }
  if (surface_) {
    const int shell_height = sidebar_visible_ || right_panel_width_ > 0 ? height :
        std::min(height, std::max(toolbar, px(suggestions_height_)));
    surface_->Resize(border, border, width, shell_height);
  }
  if(resize_border_){
    if(IsZoomed(hwnd_))ShowWindow(resize_border_,SW_HIDE);
    else{
      const int edge=px(6);
      HRGN ring=CreateRectRgn(0,0,width,height),inside=CreateRectRgn(edge,edge,width-edge,height-edge);
      CombineRgn(ring,ring,inside,RGN_DIFF);DeleteObject(inside);
      SetWindowRgn(resize_border_,ring,FALSE);
      SetWindowPos(resize_border_,HWND_TOP,0,0,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
  }
}

void BrowserWindow::ApplyContentTheme() {
  const bool dark = settings_->GetString("theme") == "dark" ||
      (settings_->GetString("theme") == "system" && IsWindowsDarkMode());
  for (auto& tab : tabs_) {
    if (!tab.browser || tab.url != "about:blank") continue;
    auto frame = tab.browser->GetMainFrame();
    if (frame && frame->GetURL().ToString().find("/ui/start.html") != std::string::npos)
      frame->ExecuteJavaScript(std::string("document.body.dataset.theme='") + (dark ? "dark" : "light") + "';document.documentElement.style.background='" + (dark ? "#08090b" : "#fafafa") + "';", frame->GetURL(), 0);
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
  update->SetString("soulu", "0.9.0-cef-preview.18");
  update->SetString("recommended", "0.9.0-cef-preview.18");
  update->SetString("cef", "154.0.32");
  update->SetString("chromium", "154.0.8037.58");
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
  SaveSettings();
  if (key == "layout") Layout();
  if (key == "theme" || key == "mattePanel") { ApplyWindowAppearance(); ApplyContentTheme(); }
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
  if (action == "browser.surfaceDiagnostics") {
    wchar_t port[12] = {};
    if (!GetEnvironmentVariableW(L"SOULU_UI_TEST_PORT", port, 12)) { callback->Failure(403, "Test mode required"); return; }
    if(payload && payload->GetType()==VTYPE_INT && payload->GetInt()>0){
      const int mode=payload->GetInt();
      ConfigureFrostedBackdrop(hwnd_,mode>=4);
      DWORD backdrop=mode==3?3:1;
      DwmSetWindowAttribute(hwnd_,38,&backdrop,sizeof(backdrop));
      const auto compose=reinterpret_cast<SetComposition>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetWindowCompositionAttribute"));
      AccentPolicy policy={mode==1||mode==4?3:mode==2||mode==5?4:0,2,0x20000000,0};
      CompositionData data={19,&policy,sizeof(policy)};
      if(compose)compose(hwnd_,&data);
      RedrawWindow(hwnd_,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN);
    }
    auto result = CefDictionaryValue::Create();
    result->SetInt("backdropCapabilities",BackdropCapabilities());
    result->SetBool("nativeBlur",native_blur_);
    result->SetBool("windowless", shell_ && shell_->GetHost()->IsWindowRenderingDisabled());
    result->SetInt("paintError", surface_ ? surface_->paint_error() : -1);
    result->SetInt("paintCount", surface_ ? surface_->paint_count() : 0);
    result->SetInt("toolbarAlpha", surface_ ? surface_->toolbar_alpha() : 255);
    return Reply(callback, result);
  }
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
    update->SetString("soulu", "0.9.0-cef-preview.18");
    update->SetString("recommended", "0.9.0-cef-preview.18");
    update->SetString("cef", "154.0.32");
    update->SetString("chromium", "154.0.8037.58");
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
      Emit("settings", Wrap(settings_->Copy(false)));
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
    if (!query.empty() && query.find(' ') == std::string::npos && query.find('.') != std::string::npos) {
      auto row = CefDictionaryValue::Create(); row->SetString("source", "website");
      row->SetString("title", query); row->SetString("url", NormalizeAddress(query));
      result->SetDictionary(result->GetSize(), row);
    }
    if (query.size() >= 2 && query.size() <= 200) {
      auto suggestion_request = CefRequest::Create();
      suggestion_request->SetURL("https://suggestqueries.google.com/complete/search?client=firefox&hl=ru&q=" + CefURIEncode(query, true).ToString());
      suggestion_request->SetMethod("GET"); suggestion_request->SetFlags(UR_FLAG_SKIP_CACHE);
      CefRefPtr<CefRequestContext> context;
      if (auto* tab = ActiveTab(); tab && tab->browser) context = tab->browser->GetHost()->GetRequestContext();
      CefRefPtr<SuggestClient> client = new SuggestClient(result, callback);
      auto pending = CefURLRequest::Create(suggestion_request, client, context);
      if (pending) {
        // Offline/slow search must never suppress local site and tab suggestions.
        CefPostDelayedTask(TID_UI, new SuggestTimeout(client), 1200);
        return;
      }
    }
    return Reply(callback, Wrap(result));
  }
  else if (action == "browser.passwords.get") return Reply(callback, Wrap(CefListValue::Create()));
  else if (action == "vpn.resolve") {
    auto result = CefDictionaryValue::Create();
    const std::wstring host = CefString(payload->GetString()).ToWString();
    ADDRINFOW hints = {}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    ADDRINFOW* addresses = nullptr;
    if (GetAddrInfoW(host.c_str(), nullptr, &hints, &addresses) == 0 && addresses) {
      wchar_t text[INET6_ADDRSTRLEN] = {};
      void* source = addresses->ai_family == AF_INET
          ? static_cast<void*>(&reinterpret_cast<sockaddr_in*>(addresses->ai_addr)->sin_addr)
          : static_cast<void*>(&reinterpret_cast<sockaddr_in6*>(addresses->ai_addr)->sin6_addr);
      if (InetNtopW(addresses->ai_family, source, text, INET6_ADDRSTRLEN))
        result->SetString("ip", CefString(text));
      FreeAddrInfoW(addresses);
    }
    return Reply(callback, result);
  }
  else if (action == "vpn.settings.get") {
    return Reply(callback, vpn_settings_->Copy(false));
  }
  else if (action == "vpn.settings.set") {
    if (payload && payload->GetType() == VTYPE_DICTIONARY) {
      auto patch = payload->GetDictionary(); CefDictionaryValue::KeyList keys; patch->GetKeys(keys);
      for (const auto& key : keys) vpn_settings_->SetValue(key, patch->GetValue(key)->Copy());
      auto native = CefDictionaryValue::Create();
      native->SetString("action", "save_profile");
      native->SetString("id", vpn_settings_->GetString("lastProfileId"));
      native->SetString("name", vpn_settings_->GetString("region").empty()
          ? "Soulu VPN" : vpn_settings_->GetString("region"));
      native->SetString("country", vpn_settings_->GetString("region"));
      native->SetString("url", vpn_settings_->GetString("link"));
      auto saved = SendVpnHelper(native);
      if (!saved->GetBool("ok")) return Reply(callback, saved);
      std::string id = saved->GetString("id");
      if (id.empty()) id = saved->GetString("profileId");
      if (!id.empty()) vpn_settings_->SetString("lastProfileId", id);
      SaveSettings();
    }
    return Reply(callback, vpn_settings_->Copy(false));
  }
  else if (action == "vpn.send") {
    std::string command;
    auto arguments = CefDictionaryValue::Create();
    if (payload && payload->GetType() == VTYPE_DICTIONARY)
      command = payload->GetDictionary()->GetString("action");
    arguments->SetString("action", command == "status" ? "status" : command);
    if (command == "connect") {
      std::string id = vpn_settings_->GetString("lastProfileId");
      if (payload && payload->GetType() == VTYPE_DICTIONARY) {
        auto options = payload->GetDictionary()->GetDictionary("payload");
        if (options && !options->GetString("profileId").empty())
          id = options->GetString("profileId");
      }
      arguments->SetString("profileId", id);
    }
    auto helper_result = SendVpnHelper(arguments);
    if (!helper_result->GetBool("ok")) return Reply(callback, helper_result);
    vpn_enabled_ = command == "connect" ||
        (command == "status" && helper_result->GetBool("connected"));
    if (command == "disconnect") vpn_enabled_ = false;
    for (auto& profile : profiles_) ApplyProxy(profile.context);
    ApplyProxy(incognito_context_);
    helper_result->SetString("state", vpn_enabled_ ? "connected" : "disconnected");
    helper_result->SetString("scope", "soulu-only");
    Emit("vpnState", Wrap(helper_result->Copy(false)));
    return Reply(callback, helper_result);
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
    AppendMenuW(menu, MF_STRING, 3, L"Найти на странице");
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
    } else if (command == 3) Emit("requestFind", EmptyValue());
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

void BrowserWindow::OnCookieFlushComplete() {
  if (pending_cookie_flushes_ == 0) return;
  if (--pending_cookie_flushes_ == 0) CloseBrowsers();
}

void BrowserWindow::CloseBrowsers() {
  for (auto& tab : tabs_) if (tab.browser) tab.browser->GetHost()->CloseBrowser(true);
  if (shell_) shell_->GetHost()->CloseBrowser(true);
  if (!shell_ && tabs_.empty()) DestroyWindow(hwnd_);
}

void BrowserWindow::CloseAll() {
  if (closing_) return;
  closing_ = true;
  pending_cookie_flushes_ = profiles_.size();
  if (pending_cookie_flushes_ == 0) {
    CloseBrowsers();
    return;
  }
  for (auto& profile : profiles_) {
    auto manager = profile.context
        ? profile.context->GetCookieManager(nullptr) : nullptr;
    if (!manager || !manager->FlushStore(new CookieFlushCallback(this)))
      OnCookieFlushComplete();
  }
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
    case WM_NCCALCSIZE:
      if (wparam) return 0;
      break;
    case WM_GETMINMAXINFO: {
      auto* sizes = reinterpret_cast<MINMAXINFO*>(lparam);
      sizes->ptMinTrackSize = {620, 420}; return 0;
    }
    case WM_NCHITTEST: {
      if (IsZoomed(hwnd)) return HTCLIENT;
      const LRESULT hit = DefWindowProc(hwnd, message, wparam, lparam);
      if (hit != HTCLIENT) return hit;
      RECT r = {}; GetWindowRect(hwnd, &r);
      const int x = GET_X_LPARAM(lparam), y = GET_Y_LPARAM(lparam), edge = 7;
      const bool left = x < r.left + edge, right = x >= r.right - edge;
      const bool top = y < r.top + edge, bottom = y >= r.bottom - edge;
      if (top && left) return HTTOPLEFT; if (top && right) return HTTOPRIGHT;
      if (bottom && left) return HTBOTTOMLEFT; if (bottom && right) return HTBOTTOMRIGHT;
      if (left) return HTLEFT; if (right) return HTRIGHT;
      if (top) return HTTOP; if (bottom) return HTBOTTOM;
      return HTCLIENT;
    }
    case WM_ERASEBKGND: {
      const std::string theme = self->settings_->GetString("theme");
      const bool dark = theme == "dark" ||
          (theme == "system" && IsWindowsDarkMode());
      RECT client = {};
      GetClientRect(hwnd, &client);
      HBRUSH background = CreateSolidBrush(dark ? RGB(8, 9, 11) : RGB(245, 246, 248));
      FillRect(reinterpret_cast<HDC>(wparam), &client,
          self->settings_->GetBool("mattePanel") ? static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)) : background);
      DeleteObject(background);
      return 1;
    }
    case WM_SIZE: self->Layout(); return 0;
    case WM_MOVE: if (self->shell_) self->shell_->GetHost()->NotifyMoveOrResizeStarted(); break;
    case WM_DWMCOMPOSITIONCHANGED: self->ApplyWindowAppearance(); return 0;
    case WM_SETTINGCHANGE: self->ApplyWindowAppearance(); self->ApplyContentTheme(); break;
    case WM_CLOSE: self->CloseAll(); return 0;
    case WM_DESTROY: ReleaseFrostedBackdrop(hwnd); CefQuitMessageLoop(); return 0;
    case WM_NCDESTROY:
      SetWindowLongPtr(hwnd, GWLP_USERDATA, 0); self->hwnd_ = nullptr; self->Release(); break;
  }
  return DefWindowProc(hwnd, message, wparam, lparam);
}
}
