#include "examples/soulu/typography_native.h"
#include "examples/soulu/browser_window.h"
#include "examples/soulu/app_version.h"
#include "examples/soulu/motion.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/cef_devtools_message_observer.h"

#include <windowsx.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <cmath>
#include "include/cef_urlrequest.h"
#include "include/cef_task.h"
#include <dwmapi.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "examples/soulu/browser_client.h"
#include "examples/soulu/engine_version.h"
#include "examples/soulu/frosted_backdrop.h"
#include "examples/soulu/resource.h"
#include "include/cef_app.h"
#include "include/cef_cookie.h"
#include "include/cef_parser.h"
#include "include/cef_ssl_info.h"
#include "include/wrapper/cef_helpers.h"
#include <functional>

namespace soulu {
namespace {
class FunctionTask final : public CefTask {
 public:
  explicit FunctionTask(std::function<void()> function):function_(std::move(function)){}
  void Execute() override {auto function=std::move(function_);function();}
 private:
  std::function<void()> function_;
  IMPLEMENT_REFCOUNTING(FunctionTask);
};
// One operation owns its observer and deadline. Chromium's isolated world
// prevents page scripts from replacing Readability, DOM APIs or URL parsing.
class ReaderJob final : public CefDevToolsMessageObserver {
 public:
  ReaderJob(CefRefPtr<BrowserWindow> owner,int id,std::string url,int generation,bool enter,
      std::string script,CefRefPtr<CefMessageRouterBrowserSide::Callback> callback)
      :owner_(owner),id_(id),url_(std::move(url)),generation_(generation),enter_(enter),
       script_(std::move(script)),callback_(callback) {}
  void Start(CefRefPtr<CefBrowser> browser) {
    browser_=browser;registration_=browser->GetHost()->AddDevToolsMessageObserver(this);
    message_=browser->GetHost()->ExecuteDevToolsMethod(0,"Page.getFrameTree",nullptr);
    CefRefPtr<ReaderJob> self=this;
    CefPostDelayedTask(TID_UI,new FunctionTask([self]{self->Finish(nullptr);}),10000);
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>,int id,bool success,const void* result,size_t size) override {
    if(!callback_||id!=message_)return;
    auto parsed=success?CefParseJSON(std::string(static_cast<const char*>(result),size),JSON_PARSER_RFC):nullptr;
    if(!parsed||parsed->GetType()!=VTYPE_DICTIONARY){Finish(nullptr);return;}
    auto data=parsed->GetDictionary();auto params=CefDictionaryValue::Create();
    if(stage_==0){
      auto tree=data->GetDictionary("frameTree");auto frame=tree?tree->GetDictionary("frame"):nullptr;
      if(!frame||frame->GetString("url")!=url_){Finish(nullptr);return;}
      params->SetString("frameId",frame->GetString("id"));params->SetString("worldName","SouluReader");
      stage_=1;message_=browser_->GetHost()->ExecuteDevToolsMethod(0,"Page.createIsolatedWorld",params);
    }else if(stage_==1){
      if(!data->HasKey("executionContextId")){Finish(nullptr);return;}
      params->SetInt("contextId",data->GetInt("executionContextId"));params->SetString("expression",script_);
      params->SetBool("returnByValue",true);params->SetBool("awaitPromise",false);
      stage_=2;message_=browser_->GetHost()->ExecuteDevToolsMethod(0,"Runtime.evaluate",params);
    }else{
      auto remote=data->GetDictionary("result");auto article=remote?remote->GetDictionary("value"):nullptr;
      Finish(data->HasKey("exceptionDetails")?nullptr:article);
    }
  }
 private:
  void Finish(CefRefPtr<CefDictionaryValue> article) {
    if(!callback_)return;auto callback=callback_;callback_=nullptr;
    owner_->FinishReader(id_,url_,generation_,enter_,article,callback);
    registration_=nullptr;browser_=nullptr;owner_=nullptr;
  }
  CefRefPtr<BrowserWindow> owner_;CefRefPtr<CefBrowser> browser_;CefRefPtr<CefRegistration> registration_;
  int id_,generation_,message_=0,stage_=0;std::string url_,script_;bool enter_;
  CefRefPtr<CefMessageRouterBrowserSide::Callback> callback_;
  IMPLEMENT_REFCOUNTING(ReaderJob);
};
class SiteStorageJob final : public CefDevToolsMessageObserver {
 public:
  explicit SiteStorageJob(CefRefPtr<CefMessageRouterBrowserSide::Callback> callback):callback_(callback) {}
  void Start(CefRefPtr<CefBrowser> browser,const std::string& origin) {
    registration_=browser->GetHost()->AddDevToolsMessageObserver(this);
    auto params=CefDictionaryValue::Create();params->SetString("origin",origin);
    params->SetString("storageTypes","local_storage,indexeddb,cache_storage,service_workers");
    message_=browser->GetHost()->ExecuteDevToolsMethod(0,"Storage.clearDataForOrigin",params);
    CefRefPtr<SiteStorageJob> self=this;
    CefPostDelayedTask(TID_UI,new FunctionTask([self]{self->Finish(false);}),10000);
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>,int id,bool success,const void*,size_t) override {
    if(id==message_)Finish(success);
  }
 private:
  void Finish(bool success){if(!callback_)return;auto cb=callback_;callback_=nullptr;
    if(success)cb->Success("{\"cleared\":true,\"cookiesRetained\":true}");else cb->Failure(500,"Не удалось подтвердить очистку хранилищ сайта");
    registration_=nullptr;}
  int message_=0;CefRefPtr<CefRegistration> registration_;
  CefRefPtr<CefMessageRouterBrowserSide::Callback> callback_;
  IMPLEMENT_REFCOUNTING(SiteStorageJob);
};
// A single request at a navigation boundary; no additional CEF renderer or HWND.
class ThumbnailObserver final : public CefDevToolsMessageObserver {
 public:
  ThumbnailObserver(CefRefPtr<BrowserWindow> owner, int id, std::string url)
      : owner_(owner), id_(id), url_(std::move(url)) {}
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>, int message_id, bool success,
                             const void* result, size_t size) override {
    if(message_id!=900001 || !success) return;
    auto parsed=CefParseJSON(std::string(static_cast<const char*>(result),size),JSON_PARSER_RFC);
    if(parsed && parsed->GetType()==VTYPE_DICTIONARY) {
      auto data=parsed->GetDictionary()->GetString("data").ToString();
      if(!data.empty()) owner_->StoreThumbnail(id_,url_,"data:image/jpeg;base64,"+data);
    }
  }
 private:
  CefRefPtr<BrowserWindow> owner_; int id_; std::string url_;
  IMPLEMENT_REFCOUNTING(ThumbnailObserver);
};

class ProfileContextHandler final : public CefRequestContextHandler {
 public:
  explicit ProfileContextHandler(CefRefPtr<BrowserWindow> owner) : owner_(owner) {}
  void OnRequestContextInitialized(CefRefPtr<CefRequestContext> context) override {
    CEF_REQUIRE_UI_THREAD();
    // Release the one-shot owner reference to avoid a profile/context cycle.
    auto owner = owner_;
    owner_ = nullptr;
    if (owner) owner->RequestContextInitialized(context);
  }
 private:
  CefRefPtr<BrowserWindow> owner_;
  IMPLEMENT_REFCOUNTING(ProfileContextHandler);
};

class CookieFlushCompletion final : public CefCompletionCallback {
 public:
  explicit CookieFlushCompletion(CefRefPtr<BrowserWindow> owner) : owner_(owner) {}
  void OnComplete() override { owner_->CookieStoreFlushed(); }
 private:
  CefRefPtr<BrowserWindow> owner_;
  IMPLEMENT_REFCOUNTING(CookieFlushCompletion);
};

// Images are fetched with the source tab's request context, never the shell's
// persistent context (especially when reading an incognito article).
class ReaderImageClient final : public CefURLRequestClient {
 public:
  explicit ReaderImageClient(CefRefPtr<CefMessageRouterBrowserSide::Callback> callback):callback_(callback) {}
  void Timeout(CefRefPtr<CefURLRequest> request){if(callback_){request->Cancel();Fail();}}
  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override {
    if(!callback_)return;auto response=request->GetResponse();
    const std::string type=response?response->GetMimeType().ToString():"";
    if(request->GetRequestStatus()!=UR_SUCCESS||!response||response->GetStatus()!=200||body_.empty()||
       (type!="image/png"&&type!="image/jpeg"&&type!="image/webp"&&type!="image/gif"&&type!="image/avif")){Fail();return;}
    auto value=CefValue::Create();value->SetString("data:"+type+";base64,"+CefBase64Encode(body_.data(),body_.size()).ToString());
    auto callback=callback_;callback_=nullptr;body_.clear();callback->Success(CefWriteJSON(value,JSON_WRITER_DEFAULT));
  }
  void OnUploadProgress(CefRefPtr<CefURLRequest>,int64_t,int64_t) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest> request,int64_t current,int64_t total) override {
    if(current>4*1024*1024||total>4*1024*1024){request->Cancel();Fail();}
  }
  void OnDownloadData(CefRefPtr<CefURLRequest> request,const void* data,size_t size) override {
    if(body_.size()+size>4*1024*1024){request->Cancel();Fail();return;}
    body_.append(static_cast<const char*>(data),size);
  }
  bool GetAuthCredentials(bool,const CefString&,int,const CefString&,const CefString&,CefRefPtr<CefAuthCallback>) override {return false;}
 private:
  void Fail(){if(!callback_)return;auto callback=callback_;callback_=nullptr;body_.clear();callback->Failure(422,"Изображение недоступно или превышает 4 МБ");}
  CefRefPtr<CefMessageRouterBrowserSide::Callback> callback_;std::string body_;
  IMPLEMENT_REFCOUNTING(ReaderImageClient);
};

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
  const auto directory = DataRoot();
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
  settings_->SetBool("saveHistory", true);
  settings_->SetBool("historyGroupDays", true);
  settings_->SetString("historyDefaultFilter", "all");
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
  settings_->SetString("startupMode", "soulu");
  settings_->SetString("startupUrl", "");
  settings_->SetString("newTabMode", "soulu");
  settings_->SetString("newTabUrl", "");
  settings_->SetString("homeMode", "soulu");
  settings_->SetString("homeUrl", "");
  for (const auto* key : {"homeShowLogo", "homeShowSearch", "homeShowWeather", "homeShowShortcuts", "homeShowBackground"})
    settings_->SetBool(key, true);
  settings_->SetString("homeWeatherCity", "");
  settings_->SetList("homeShortcuts", CefListValue::Create());
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
  settings_->SetString("bookmarksBarMode", "newTab");
  settings_->SetString("bookmarksBarPosition", "above");
  settings_->SetBool("bookmarksIconsOnly", false);
  LoadSettings();
  initial_settings_ = settings_->Copy(false);
  initial_settings_->Remove("onboarding");
  std::ifstream marks(UserDataDirectory() / L"bookmarks.json", std::ios::binary);
  std::stringstream data; data << marks.rdbuf();
  auto saved = CefParseJSON(data.str(), JSON_PARSER_RFC);
  if (saved && saved->GetType() == VTYPE_LIST) bookmarks_ = saved->GetList()->Copy();
}

void BrowserWindow::LoadSettings() {
  std::ifstream file(UserDataDirectory() / L"settings.json", std::ios::binary);
  if (!file) { settings_->SetBool("matteDefaultV15", true); return; }
  std::stringstream buffer; buffer << file.rdbuf();
  auto parsed = CefParseJSON(buffer.str(), JSON_PARSER_RFC);
  if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) return;
  auto root = parsed->GetDictionary();
  if (auto saved = root->GetDictionary("settings")) {
    if (!saved->HasKey("startupMode") && saved->HasKey("startPageMode")) {
      settings_->SetString("startupMode", saved->GetString("startPageMode"));
      settings_->SetString("startupUrl", saved->GetString("startPageUrl"));
    }
    CefDictionaryValue::KeyList keys; saved->GetKeys(keys);
    for (const auto& key : keys) settings_->SetValue(key, saved->GetValue(key)->Copy());
  }
  if (auto saved = root->GetDictionary("vpn")) vpn_settings_ = saved->Copy(false);
  if (settings_->GetString("bookmarksBarPosition") == "below")
    settings_->SetString("bookmarksBarPosition", "above");
  if (settings_->GetString("bookmarksBarMode") == "never") {
    settings_->SetString("bookmarksBarPosition", "hidden");
    settings_->SetString("bookmarksBarMode", "always");
  } else if (settings_->GetString("bookmarksBarMode") == "auto") {
    settings_->SetString("bookmarksBarMode", "always");
  }
  // Earlier previews persisted the disabled default. Apply the new default
  // once to existing profiles; subsequent user choices remain untouched.
  if (!settings_->HasKey("matteDefaultV15")) {
    settings_->SetBool("mattePanel", true);
    settings_->SetBool("matteDefaultV15", true);
    SaveSettings();
  }
}

bool BrowserWindow::SaveSettings() const {
  auto root = CefDictionaryValue::Create();
  root->SetDictionary("settings", settings_->Copy(false));
  root->SetDictionary("vpn", vpn_settings_->Copy(false));
  const auto profile = std::find_if(profiles_.begin(), profiles_.end(),
      [this](const Profile& p){return p.id==active_profile_id_;});
  if(profile!=profiles_.end()) {
    if(!WriteJson(ProfileRoot(profile->id)/L"soulu-settings.json",Wrap(settings_)))return false;
    // VPN remains owned by the legacy global file. Preserve its settings
    // migration template rather than copying the active profile into it.
    auto legacy=ReadJson(UserDataDirectory()/L"settings.json");
    if(legacy&&legacy->GetType()==VTYPE_DICTIONARY)
      root=legacy->GetDictionary()->Copy(false);
    else if(initial_settings_)
      root->SetDictionary("settings",initial_settings_->Copy(false));
    root->SetDictionary("vpn",vpn_settings_->Copy(false));
  }
  return WriteJson(UserDataDirectory()/L"settings.json",Wrap(root));
}

void BrowserWindow::LoadProfileSettings() {
  settings_=initial_settings_->Copy(false);
  auto saved=ReadJson(ProfileRoot(active_profile_id_)/L"soulu-settings.json");
  if(saved&&saved->GetType()==VTYPE_DICTIONARY){
    if (!saved->GetDictionary()->HasKey("startupMode") && saved->GetDictionary()->HasKey("startPageMode")) {
      settings_->SetString("startupMode", saved->GetDictionary()->GetString("startPageMode"));
      settings_->SetString("startupUrl", saved->GetDictionary()->GetString("startPageUrl"));
    }
    CefDictionaryValue::KeyList keys;saved->GetDictionary()->GetKeys(keys);
    for(const auto& key:keys)settings_->SetValue(key,saved->GetDictionary()->GetValue(key)->Copy());
  }
  SaveSettings();ApplyWindowAppearance();ApplyContentTheme();
}

void BrowserWindow::Create() {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<BrowserWindow> window = new BrowserWindow();
  current_=window.get();
  if (window->CreateNativeWindow()) window->CreateShellBrowser();
}

bool BrowserWindow::CreateNativeWindow() {
  WNDCLASSEXW wc = {sizeof(wc)};
  wc.style = 0;
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
                          WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_CLIPCHILDREN |
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

  // Composition and both browser hosts are prepared while the window is
  // hidden. ShowWhenReady exposes the first composed shell/content layout.
  return true;
}

void BrowserWindow::CreateShellBrowser() {
  CefWindowInfo info;
  surface_ = new ShellSurface(hwnd_);
  Layout();
  info.SetAsWindowless(surface_->hwnd());
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings settings;
  settings.background_color = CefColorSetARGB(0, 0, 0, 0);
  settings.windowless_frame_rate = motion::kShellIdleFrameRate;
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
                        profile->GetString("id"), true);
      }
    }
  }
  if (profiles_.empty()) CreateProfile("Личный", "personal");
  active_profile_id_ = profiles_.front().id;
  LoadProfileSettings();
}

void BrowserWindow::CreateProfile(const std::string& name,
                                  const std::string& requested_id, bool existing) {
  std::string id = requested_id.empty()
      ? (profiles_.empty() ? "personal" : "profile-" + RandomId())
      : requested_id;
  if(!ValidProfileId(id) || std::any_of(profiles_.begin(),profiles_.end(),
       [&id](const Profile& p){return p.id==id;}))return;
  CefRequestContextSettings context_settings;
  auto profile_path = ProfileRoot(id);
  // Decide before CEF creates any cache files. Old profiles and legacy installs
  // never acquire a first-run prompt merely by upgrading.
  std::error_code probe;
  existing = existing || std::filesystem::exists(profile_path, probe) ||
      (id == "personal" && (std::filesystem::exists(UserDataDirectory()/L"settings.json", probe) ||
       std::filesystem::exists(UserDataDirectory()/L"bookmarks.json", probe)));
  // CEF's Windows context registry compares path strings. Match Chromium's
  // native initial-profile path so a restored last-used profile reuses its
  // context instead of opening the same databases a second time.
  profile_path.make_preferred();
  std::filesystem::create_directories(profile_path);
  CefString(&context_settings.cache_path) = profile_path.wstring();
  context_settings.persist_session_cookies = 1;

  Profile profile;
  profile.id = id;
  profile.name = name.empty() ? "Профиль" : name;
  profile.context = CefRequestContext::CreateContext(
      context_settings, new ProfileContextHandler(this));
  profiles_.push_back(profile);
  policies_[id]=std::make_shared<SitePolicy>(id);
  auto saved=ReadJson(profile_path/L"soulu-settings.json");
  auto config=saved&&saved->GetType()==VTYPE_DICTIONARY?saved->GetDictionary()->Copy(false):initial_settings_->Copy(false);
  if(!config->GetDictionary("onboarding")) {
    auto flow=CefDictionaryValue::Create();flow->SetString("status",existing?"skipped":"not_started");
    flow->SetInt("step",1);config->SetDictionary("onboarding",flow);
    WriteJson(profile_path/L"soulu-settings.json",Wrap(config));
  }
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
      incognito_context_ = CefRequestContext::CreateContext(
          context_settings, new ProfileContextHandler(this));
    }
    return incognito_context_;
  }
  if (auto* profile = ActiveProfile()) return profile->context;
  return CefRequestContext::GetGlobalContext();
}

void BrowserWindow::ApplyProxy(CefRefPtr<CefRequestContext> context) {
  if (!context) return;
  auto proxy = CefDictionaryValue::Create();
  // With Soulu's tunnel off, respect the user's existing system network route.
  // Forcing direct bypasses a configured Windows proxy and breaks reachable sites.
  proxy->SetString("mode", vpn_enabled_ ? "fixed_servers" : "system");
  if (vpn_enabled_) {
    proxy->SetString("server", "socks5://127.0.0.1:17890");
    proxy->SetString("bypass_list", "<-loopback>");
  }
  auto value = CefValue::Create();
  value->SetDictionary(proxy);
  CefString error;
  context->SetPreference("proxy", value, error);
}

void BrowserWindow::RequestContextInitialized(CefRefPtr<CefRequestContext> context) {
  CEF_REQUIRE_UI_THREAD();
  ApplyProxy(context);
}

bool BrowserWindow::IsTrustedUi(const std::string& url) const {
  const auto ui=std::filesystem::u8path(ExecutableDirectory())/"ui";
  return url==FileUrl(ui/"index.html") || IsSettingsUrl(url);
}
bool BrowserWindow::IsIncognitoTab(int id) {auto* tab=FindTab(id);return tab&&tab->incognito;}
std::shared_ptr<SitePolicy> BrowserWindow::PolicyForTab(int id) {
  auto* tab=FindTab(id);if(!tab)return nullptr;
  auto profile=tab->incognito?"__incognito__":tab->profile_id;
  auto& policy=policies_[profile];
  if(!policy)policy=std::make_shared<SitePolicy>(profile);
  return policy;
}
bool BrowserWindow::AllowSite(int id,const std::string& origin,const std::string& permission) {
  auto policy=PolicyForTab(id);if(!policy||WebOrigin(origin).empty())return false;
  int rule=policy->Rule(origin,permission);if(rule==0)return true;if(rule==2)return false;
  const std::map<std::string,std::wstring> labels={{"geolocation",L"геолокацию"},
    {"camera",L"камеру"},{"microphone",L"микрофон"},{"notifications",L"уведомления"},
    {"popups",L"всплывающее окно"},{"downloads",L"загрузку файла"}};
  auto label=labels.find(permission);if(label==labels.end())return false;
  const auto text=CefString(WebOrigin(origin)).ToWString()+L" запрашивает "+label->second+L". Разрешить один раз?";
  return TypographyMessageBox(hwnd_,text.c_str(),L"Разрешение сайта — Soulu",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES;
}
void BrowserWindow::ApplySiteSound() {
  for(auto& tab:tabs_)if(tab.browser){auto policy=PolicyForTab(tab.id);
    tab.browser->GetHost()->SetAudioMuted(policy&&policy->Rule(tab.url,"sound")!=0);
    SyncSitePolicy(tab.id,tab.url);}
}
void BrowserWindow::SyncSitePolicy(int id,const std::string& url) {
  auto* tab=FindTab(id);auto policy=PolicyForTab(id);
  if(!tab||!tab->browser||!policy||WebOrigin(url).empty())return;
  auto context=tab->browser->GetHost()->GetRequestContext();
  const std::pair<const char*,cef_content_setting_types_t> types[]={
    {"geolocation",CEF_CONTENT_SETTING_TYPE_GEOLOCATION},
    {"camera",CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_CAMERA},
    {"microphone",CEF_CONTENT_SETTING_TYPE_MEDIASTREAM_MIC},
    {"notifications",CEF_CONTENT_SETTING_TYPE_NOTIFICATIONS},
    {"downloads",CEF_CONTENT_SETTING_TYPE_AUTOMATIC_DOWNLOADS},
    {"sound",CEF_CONTENT_SETTING_TYPE_SOUND}};
  for(const auto& [name,type]:types){int rule=policy->Rule(url,name);
    auto value=rule==0?CEF_CONTENT_SETTING_VALUE_ALLOW:rule==2?CEF_CONTENT_SETTING_VALUE_BLOCK:CEF_CONTENT_SETTING_VALUE_ASK;
    if(std::string(name)=="sound"&&rule==1)value=CEF_CONTENT_SETTING_VALUE_ALLOW;
    context->SetContentSetting(WebOrigin(url),WebOrigin(url),type,value);
  }
  // Chromium 154 stores approximate/precise geolocation as a website-setting
  // dictionary. The legacy integer setting alone leaves permission at Ask.
  // Schema: chromium/components/content_settings/core/browser/
  // geolocation_setting_delegate.cc (approved Chromium 154.0.8037.58).
  const int geo_rule=policy->Rule(url,"geolocation");
  const int geo_value=geo_rule==0?1:geo_rule==2?2:3;
  auto geo=CefDictionaryValue::Create();
  geo->SetInt("approximate",geo_value);geo->SetInt("precise",geo_value);
  context->SetWebsiteSetting(WebOrigin(url),WebOrigin(url),
      CEF_CONTENT_SETTING_TYPE_GEOLOCATION_WITH_OPTIONS,Wrap(geo));
  // Native popup gating lives in OnBeforePopup: allow Chromium to deliver the
  // request, retaining ordinary user-initiated target=_blank navigation.
  context->SetContentSetting(WebOrigin(url),WebOrigin(url),CEF_CONTENT_SETTING_TYPE_POPUPS,CEF_CONTENT_SETTING_VALUE_ALLOW);
}
void BrowserWindow::ReleaseIncognito() {
  if(std::any_of(tabs_.begin(),tabs_.end(),[](const Tab& t){return t.incognito;}))return;
  private_page_settings_=nullptr;
  incognito_context_=nullptr;policies_.erase("__incognito__");reader_preferences_.erase("__incognito__");
  auto retained=CefListValue::Create();
  for(size_t i=0;i<bookmarks_->GetSize();++i){auto row=bookmarks_->GetDictionary(i);
    if(row&&row->GetString("profileId")!="__incognito__")retained->SetDictionary(retained->GetSize(),row->Copy(false));}
  bookmarks_=retained;
  auto history=CefListValue::Create();
  for(size_t i=0;i<downloads_->GetSize();++i){auto row=downloads_->GetDictionary(i);
    if(row&&row->GetString("profileId")!="__incognito__")history->SetDictionary(history->GetSize(),row->Copy(false));}
  downloads_=history;
}
void BrowserWindow::OfferCredential(int id,CefRefPtr<CefFrame> frame,
                                   const std::string& username,std::string password,
                                   const std::string& submitted_url) {
  auto* tab=FindTab(id);
  if(!tab||!tab->browser||tab->incognito||!frame||!frame->IsMain()||importing_ ||
     WebOrigin(submitted_url).empty()||WebOrigin(submitted_url)!=WebOrigin(frame->GetURL())||
     username.size()>4096||password.size()>16384||password.empty()){
    if(!password.empty())SecureZeroMemory(password.data(),password.size());return;}
  const auto profile=tab->profile_id,origin=WebOrigin(submitted_url);
  PasswordVault vault(profile);bool exists=vault.Contains(origin,username);
  if(exists){auto rows=vault.List();for(size_t i=0;i<rows->GetSize();++i){auto row=rows->GetDictionary(i);
    if(row&&row->GetString("origin")==origin&&row->GetString("username")==username){
      std::string previous;bool same=vault.Reveal(row->GetString("id"),previous)&&previous==password;
      if(!previous.empty())SecureZeroMemory(previous.data(),previous.size());
      if(same){SecureZeroMemory(password.data(),password.size());return;}break;}}}
  const auto prompt=CefString(origin).ToWString()+L"\n"+
    (exists?L"Обновить сохранённый пароль после отправки формы входа?":L"Сохранить пароль после отправки формы входа?")+L"\nВход ещё может потребовать подтверждения на сайте.";
  if(TypographyMessageBox(hwnd_,prompt.c_str(),L"Пароли Soulu",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES &&
     !vault.Put(origin,username,password,true))
    TypographyMessageBox(hwnd_,L"Не удалось сохранить пароль.",L"Soulu",MB_OK|MB_ICONERROR);
  if(auto* current=FindTab(id);current&&current==ActiveTab()&&current->browser)
    current->browser->GetHost()->SetFocus(true);
  SecureZeroMemory(password.data(),password.size());
}

void BrowserWindow::ApplyWindowAppearance() {
  if (!hwnd_) return;
  const std::string theme = EffectiveSettings()->GetString("theme");
  const BOOL dark = theme == "dark" || (theme == "system" && IsWindowsDarkMode());
  DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark));

  const DWORD corner=2, noBorder=0xFFFFFFFE, noBackdrop=1;
  DwmSetWindowAttribute(hwnd_,33,&corner,sizeof(corner));
  DwmSetWindowAttribute(hwnd_,34,&noBorder,sizeof(noBorder));
  DwmSetWindowAttribute(hwnd_,38,&noBackdrop,sizeof(noBackdrop));
  const bool matte=EffectiveSettings()->GetBool("mattePanel");
  const auto compose=reinterpret_cast<SetComposition>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"SetWindowCompositionAttribute"));
  AccentPolicy policy={0,0,0,0};
  CompositionData data={19,&policy,sizeof(policy)};
  if(compose)compose(hwnd_,&data);
  native_blur_=ConfigureFrostedBackdrop(hwnd_,matte);
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
  Layout();
  RedrawWindow(hwnd_,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME);

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
  if(settings_dirty_&&id!=active_profile_id_)return;
  settings_preview_=nullptr;settings_loaded_=nullptr;settings_staged_=nullptr;
  const auto it = std::find_if(profiles_.begin(), profiles_.end(),
      [&id](const Profile& profile) { return profile.id == id; });
  if (it == profiles_.end()) return;
  CaptureThumbnail();
  active_profile_id_ = id;
  LoadProfileSettings();
  RefreshSettingsProfile();
  auto tab = std::find_if(tabs_.begin(), tabs_.end(),
      [&id](const Tab& item) { return !item.incognito && item.profile_id == id; });
  if (tab == tabs_.end()) NewTab();
  else {
    active_tab_id_ = tab->id;
    last_normal_active_[tab->profile_id]=tab->id;
    Layout();
    EmitState();
  }
}

bool BrowserWindow::IsSettingsUrl(const std::string& url) const {
  const auto canonical = FileUrl(std::filesystem::u8path(ExecutableDirectory()) / "ui" / "settings.html");
  return url == "soulu://settings" || url.rfind("soulu://settings/", 0) == 0 ||
      url == canonical || url.rfind(canonical + "#", 0) == 0 || url.rfind(canonical + "?", 0) == 0;
}
void BrowserWindow::OpenSettingsOverlay() {
  if (closing_) return;
  if (settings_overlay_) { FocusSettings(); return; }
  settings_previous_focus_ = GetFocus();
  settings_overlay_ = std::make_unique<SettingsOverlay>(hwnd_,
      [this] { FinishSettingsTransition(); }, [this] { GuardSettingsClose(kSettingsSession); });
  if (!settings_overlay_->Create()) {
    settings_overlay_.reset();
    TypographyMessageBox(hwnd_, L"Не удалось создать слой настроек Windows Composition.", L"Soulu", MB_OK | MB_ICONERROR);
    return;
  }
  BlockSettingsBackground(true);
  CefWindowInfo info;
  info.SetAsChild(settings_overlay_->hwnd(), CefRect(0, -800, 1020, 800));
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  info.style &= ~WS_VISIBLE;
  CefBrowserSettings config;
  const std::string theme = EffectiveSettings()->GetString("theme");
  const bool dark = theme == "dark" || (theme == "system" && IsWindowsDarkMode());
  config.background_color = dark ? CefColorSetARGB(255,20,27,39) : CefColorSetARGB(255,255,255,255);
  settings_profile_ = active_profile_id_;
  settings_browser_pending_ = true;
  const auto url = FileUrl(std::filesystem::u8path(ExecutableDirectory()) / "ui" / "settings.html");
  if (!CefBrowserHost::CreateBrowser(info, new BrowserClient(this, BrowserRole::kSettings, kSettingsSession),
      url + "?host=overlay", config, nullptr, ContextForNewTab(false))) {
    settings_browser_pending_ = false;
    BlockSettingsBackground(false); settings_overlay_.reset();
  }
  EmitState();
}
void BrowserWindow::AttachSettings(CefRefPtr<CefBrowser> browser) {
  settings_browser_pending_ = false;
  settings_browser_ = browser;
  if (!settings_overlay_ || closing_) { browser->GetHost()->CloseBrowser(true); return; }
  settings_overlay_->Attach(browser->GetHost()->GetWindowHandle());
  if (settings_overlay_->closing()) browser->GetHost()->CloseBrowser(true);
}
void BrowserWindow::FocusSettings() {
  if (settings_overlay_) settings_overlay_->Focus();
  if (settings_browser_ && settings_overlay_ && !settings_overlay_->closing())
    settings_browser_->GetHost()->SetFocus(true);
}
void BrowserWindow::BlockSettingsBackground(bool block) {
  if (block) {
    auto disable = [this](HWND child) {
      if (!child || !IsWindow(child)) return;
      if (!settings_input_state_.count(child)) settings_input_state_[child] = IsWindowEnabled(child) != FALSE;
      EnableWindow(child, FALSE);
    };
    if (surface_) disable(surface_->hwnd());
    for (auto& tab : tabs_) if (tab.browser) disable(tab.browser->GetHost()->GetWindowHandle());
  } else {
    for (const auto& [child, enabled] : settings_input_state_) if (IsWindow(child)) EnableWindow(child, enabled);
    settings_input_state_.clear();
  }
}
void BrowserWindow::CloseSettingsOverlay() {
  if (settings_overlay_) settings_overlay_->Close();
}
void BrowserWindow::FinishSettingsTransition() {
  if (settings_browser_) settings_browser_->GetHost()->CloseBrowser(true);
  else if (!settings_browser_pending_) SettingsClosed(nullptr);
}
void BrowserWindow::SettingsClosed(CefRefPtr<CefBrowser> browser) {
  if (browser && (!settings_browser_ || !settings_browser_->IsSame(browser))) return;
  settings_browser_ = nullptr; settings_session_id_ = 0; settings_dirty_ = false;
  settings_loaded_ = nullptr; settings_staged_ = nullptr;
  const bool all = settings_close_all_; settings_close_all_ = false;
  const auto pending = settings_pending_url_; settings_pending_url_.clear();
  ResetSettingsPreview();
  settings_overlay_.reset();
  BlockSettingsBackground(false);
  const HWND previous = settings_previous_focus_; settings_previous_focus_ = nullptr;
  if (IsWindow(previous) && IsWindowEnabled(previous)) SetFocus(previous);
  else if (auto* tab = ActiveTab(); tab && tab->browser) tab->browser->GetHost()->SetFocus(true);
  EmitState();
  if (all) CloseAll();
  else if (!pending.empty()) NewTab(pending);
}
void BrowserWindow::RefreshSettingsProfile() {
  if (!settings_browser_) return;
  settings_profile_ = active_profile_id_; settings_dirty_ = false;
  settings_loaded_ = nullptr; settings_staged_ = nullptr; settings_preview_ = nullptr;
  settings_browser_->GetMainFrame()->ExecuteJavaScript(
      "window.souluSettingsProfileChanged&&window.souluSettingsProfileChanged()",
      settings_browser_->GetMainFrame()->GetURL(), 0);
}

void BrowserWindow::OpenIncognitoLink(int source_id, CefRefPtr<CefBrowser> source,
                                       const std::string& url) {
  auto* tab = FindTab(source_id);
  if (!tab || !tab->browser || !tab->browser->IsSame(source) || closing_) return;
  // Reuse the existing memory-only incognito context and the same tab manager.
  NewTab(url, true);
}

void BrowserWindow::OpenTabFrom(int source_id, CefRefPtr<CefBrowser> source,
                                 const std::string& url, bool background) {
  auto* tab = FindTab(source_id);
  if (!tab || !tab->browser || !tab->browser->IsSame(source) || closing_) return;
  const bool incognito = tab->incognito;
  const std::string profile_id = tab->profile_id;
  NewTab(url, incognito, !background, source->GetHost()->GetRequestContext(), profile_id);
}

int BrowserWindow::PreparePopup(int source_id, const std::string& url,
                                bool background, CefWindowInfo& info) {
  auto* source = FindTab(source_id);
  if (!source || closing_) return 0;
  if (IsSettingsUrl(url)) { OpenSettingsOverlay(); return 0; }
  Tab tab;
  tab.id = next_tab_id_++;
  tab.url = url.empty() ? "about:blank" : url;
  tab.incognito = source->incognito;
  tab.profile_id = source->profile_id;
  tab.activate_on_attach = !background;
  if (!background) CaptureThumbnail();
  tabs_.push_back(tab);
  info.SetAsChild(hwnd_, CurrentGeometry().content);
  // Soulu owns the window and tab lifecycle. The default Chrome runtime
  // creates a Chrome Browser window even when a native parent is supplied.
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  // Keep the child hidden until AttachContent applies the selected tab's layout.
  info.style &= ~WS_VISIBLE;
  EmitState();
  return tab.id;
}

void BrowserWindow::AbortPopup(int tab_id) {
  auto* tab = FindTab(tab_id);
  if (!tab || tab->browser) return;
  tabs_.erase(std::remove_if(tabs_.begin(), tabs_.end(),
      [tab_id](const Tab& item) { return item.id == tab_id; }), tabs_.end());
  if (closing_ && !shell_ && !settings_browser_ && !settings_browser_pending_ && tabs_.empty()) FinishClose();
  EmitState();
}

void BrowserWindow::NewTab(const std::string& url, bool incognito,
                          bool foreground, CefRefPtr<CefRequestContext> context,
                          const std::string& profile_id) {
  InitializeProfiles();
  if (IsSettingsUrl(url)) { OpenSettingsOverlay(); return; }
  const int id = next_tab_id_++;
  Tab tab;
  tab.id = id;
  if (incognito && !private_page_settings_) private_page_settings_ = settings_->Copy(false);
  tab.url = url.empty() ? PageUrl("newTab", incognito ? private_page_settings_ : settings_) : url;
  if(!incognito && foreground && (profile_id.empty()||profile_id==active_profile_id_) && NeedsOnboarding() &&
      std::none_of(tabs_.begin(),tabs_.end(),[this](const Tab& t){return !t.incognito&&t.profile_id==active_profile_id_;}))
    tab.url="soulu://onboarding";
  tab.focus_address_on_attach = tab.url!="soulu://onboarding" && foreground && (url.empty() || tab.url=="about:blank" || tab.url=="soulu://home");
  tab.incognito = incognito;
  tab.profile_id = profile_id.empty() ? (incognito ? "__incognito__" : active_profile_id_) : profile_id;

  tabs_.push_back(tab);
  const int previous_active = active_tab_id_;
  if (foreground) { CaptureThumbnail(); active_tab_id_ = id; if(!incognito)last_normal_active_[tab.profile_id]=id; }

  CefWindowInfo info;
  info.SetAsChild(hwnd_, CurrentGeometry().content);
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  // OnAfterCreated commits current bounds/visibility, including changes that
  // occurred while browser creation was in flight.
  info.style &= ~WS_VISIBLE;
  CefBrowserSettings browser_settings;
  const bool dark = settings_->GetString("theme") == "dark" || (settings_->GetString("theme") == "system" && IsWindowsDarkMode());
  browser_settings.background_color = dark && tab.url!="soulu://onboarding" ? CefColorSetARGB(255,8,9,11) : CefColorSetARGB(255,250,250,250);
  const BrowserRole role = BrowserRole::kContent;
  const bool created = CefBrowserHost::CreateBrowser(
      info, new BrowserClient(this, role, id),
      InternalUrl(tab.url), browser_settings,
      [&](){auto extra=CefDictionaryValue::Create();extra->SetBool("souluIncognito",incognito);return extra;}(),
      context ? context : ContextForNewTab(incognito));
  if (!created) {
    AbortPopup(id);
    active_tab_id_ = previous_active;
    Layout();
    EmitState();
    return;
  }
  EmitState();
  if (tab.url!="soulu://onboarding" && foreground && (url.empty() || tab.url == "about:blank" || tab.url == "soulu://home")) FocusAddress();
}

void BrowserWindow::AttachShell(CefRefPtr<CefBrowser> browser) {
  shell_ = browser;
  surface_->Attach(browser);
  InitializeProfiles();
  if (NeedsOnboarding() || !RestoreSession()) NewTab(PageUrl("startup", settings_));
  CefCommandLine::ArgumentList arguments;
  CefCommandLine::GetGlobalCommandLine()->GetArguments(arguments);
  for(const auto& argument:arguments)OpenExternal(argument.ToString());
  Layout();
  if(!NeedsOnboarding())FocusAddress();
}

void BrowserWindow::AttachContent(int tab_id, CefRefPtr<CefBrowser> browser) {
  auto* tab = FindTab(tab_id);
  if (!tab || (tab->browser && !tab->browser->IsSame(browser))) {
    browser->GetHost()->CloseBrowser(true);
    return;
  }
  tab->browser = browser;
  if(tab->focus_address_on_attach && active_tab_id_==tab_id){
    tab->focus_address_on_attach=false;FocusAddress();
  }
  ApplySiteSound();
  if (closing_) {
    browser->GetHost()->CloseBrowser(true);
    return;
  }
  if (tab->activate_on_attach) {
    active_tab_id_ = tab_id;
    if(!tab->incognito)last_normal_active_[tab->profile_id]=tab_id;
    tab->activate_on_attach = false;
  }
  Layout();
  ShowWhenReady();
  EmitState();
}

void BrowserWindow::ShowWhenReady() {
  if (closing_ || !shell_frame_ready_ || IsWindowVisible(hwnd_)) return;
  const auto* tab = ActiveTab();
  if (!tab || !tab->browser) return;
  Layout();
  ShowWindow(hwnd_, SW_SHOW);
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
  auto* tab=FindTab(id);if(!tab)return;
  if(!tab->incognito&&tab->profile_id!=active_profile_id_){
    if(settings_dirty_){GuardSettingsClose(settings_session_id_);return;}
    settings_preview_=nullptr;settings_loaded_=nullptr;settings_staged_=nullptr;
    active_profile_id_=tab->profile_id;LoadProfileSettings();RefreshSettingsProfile();
  }
  if (active_tab_id_ != id) CaptureThumbnail();
  active_tab_id_ = id;
  if(!tab->incognito)last_normal_active_[tab->profile_id]=id;
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
  const bool incognito = it->incognito;
  const std::string profile = it->profile_id;
  tabs_.erase(it);
  if (active) {
    active_tab_id_ = 0;
    ReplaceLastTab(incognito, profile);
  }
  ReleaseIncognito();
  Layout();
  EmitState();
}

void BrowserWindow::BrowserClosed(CefRefPtr<CefBrowser> browser, int tab_id,
                                  bool shell) {

  if (shell) { if (surface_) surface_->Detach(); shell_ = nullptr; }
  else {
    auto* tab = FindTab(tab_id);
    // A popup/DevTools browser must never remove its opener's tab.
    if (!tab || !tab->browser || !tab->browser->IsSame(browser)) return;
    const bool incognito = tab->incognito;
    const std::string profile = tab->profile_id;
    tabs_.erase(std::remove_if(tabs_.begin(), tabs_.end(),
        [tab_id](const Tab& tab) { return tab.id == tab_id; }), tabs_.end());
    if (active_tab_id_ == tab_id) {
      active_tab_id_ = 0;
      ReplaceLastTab(incognito, profile);
    }
    ReleaseIncognito();
  }
  if (closing_ && !shell_ && !settings_browser_ && !settings_browser_pending_ && tabs_.empty()) FinishClose();
  else { Layout(); EmitState(); }
}

void BrowserWindow::FocusAddress() {
  if (settings_overlay_) { FocusSettings(); return; }
  if (!shell_ || !shell_->GetMainFrame()) return;
  if (surface_) surface_->Focus();
  const std::string script =
      "setTimeout(()=>window.__souluEmit&&window.__souluEmit('focusAddress',null),0)";
  shell_->GetMainFrame()->ExecuteJavaScript(
      script, shell_->GetMainFrame()->GetURL(), 0);
}

void BrowserWindow::Navigate(const std::string& value) {
  std::string url = NormalizeAddress(value);
  if (IsSettingsUrl(url)) { OpenSettingsOverlay(); return; }
  auto* tab = ActiveTab();
  if (!tab) return;
  if (settings_->GetString("addressOpenMode") == "newIfOccupied" &&
      tab->url != "about:blank" && tab->url != "soulu://home" && tab->url != url) {
    NewTab(url);
    return;
  }
  if (tab->browser) tab->browser->GetMainFrame()->LoadURL(InternalUrl(url));
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
  UpdateHistory(id);
  EmitState();
}
void BrowserWindow::UpdateAddress(int id, const std::string& url) {
  if (auto* tab = FindTab(id)) {
    const std::string next = IsHistoryUi(url) ? "soulu://history" : url == InternalUrl("about:blank") ? "about:blank" : (url == InternalUrl("soulu://home") ? "soulu://home" : (IsOnboardingUi(url)?"soulu://onboarding":url));
    if (next != tab->url) {tab->thumbnail.clear();++tab->document_generation;tab->reader_active=false;tab->reader_article=nullptr;}
    tab->url = next;
  }
  Layout();
  EmitState();
}
void BrowserWindow::UpdateFavicon(int id, const std::string& url) {
  if (auto* tab = FindTab(id)) tab->favicon = url;
  UpdateHistory(id);
  EmitState();
}
void BrowserWindow::UpdateLoading(int id, bool loading, bool can_go_back) {
  if (auto* tab = FindTab(id)) {
    tab->loading = loading; tab->can_go_back = can_go_back;
    if(!loading)tab->main_loading=false;
  }
  if (!loading && overview_visible_ && id == active_tab_id_) CaptureThumbnail();
  EmitState();
}

void BrowserWindow::UpdateDownload(int tab_id, CefRefPtr<CefDownloadItem> item) {
  auto* tab=FindTab(tab_id);if(!tab)return;
  auto row = CefDictionaryValue::Create();
  row->SetInt("id", static_cast<int>(item->GetId()));
  row->SetString("filename", item->GetSuggestedFileName());
  row->SetDouble("receivedBytes", static_cast<double>(item->GetReceivedBytes()));
  row->SetDouble("totalBytes", static_cast<double>(item->GetTotalBytes()));
  row->SetString("state", item->IsComplete() ? "completed" : item->IsCanceled() ? "cancelled" : "progressing");
  row->SetString("profileId", tab->incognito?"__incognito__":tab->profile_id);
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

bool BrowserWindow::SaveBookmarks(CefRefPtr<CefListValue> rows) const {
  const auto path=UserDataDirectory()/L"bookmarks.json";
  const auto temp=UserDataDirectory()/L"bookmarks.json.tmp";
  {std::ofstream file(temp,std::ios::binary|std::ios::trunc); auto persistent=CefListValue::Create();
    for(size_t i=0;i<rows->GetSize();++i) {auto row=rows->GetDictionary(i);if(row && row->GetString("profileId")!="__incognito__") persistent->SetDictionary(persistent->GetSize(),row->Copy(false));}
    file<<CefWriteJSON(Wrap(persistent),JSON_WRITER_PRETTY_PRINT); file.flush(); if(!file.good()) return false;}
  return MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
}

bool BrowserWindow::BookmarksBarVisible() const {
  const auto mode=settings_->GetString("bookmarksBarMode").ToString();
  if(settings_->GetString("bookmarksBarPosition")=="hidden") return false;
  if(mode=="always") return true;
  if(mode=="auto") return bookmarks_auto_visible_;
  if(mode!="newTab" && mode!="home") return false;
  const auto* tab=const_cast<BrowserWindow*>(this)->ActiveTab();
  if(!tab) return false;
  if(mode=="newTab" && (tab->url.empty()||tab->url=="about:blank"))return true;
  const auto config=PageSettings(*tab);
  const auto expected=PageUrl(mode=="newTab"?"newTab":"home",config);
  if(tab->url==expected)return true;
  CefURLParts parts;
  return CefParseURL(expected,parts) && tab->url==CefString(&parts.spec).ToString();
}

void BrowserWindow::StoreThumbnail(int id, const std::string& url, const std::string& data) {
  auto* tab=FindTab(id);
  if(!tab || tab->url!=url || data.size()>2*1024*1024) return;
  tab->thumbnail=data;
  size_t bytes=0; for(const auto& item:tabs_) bytes+=item.thumbnail.size();
  for(auto& item:tabs_) {if(bytes<=24*1024*1024) break; if(item.id!=id) {bytes-=item.thumbnail.size();item.thumbnail.clear();}}
  if(overview_visible_) EmitState();
}

void BrowserWindow::CaptureThumbnail() {
  auto* tab=ActiveTab();
  if(!tab || !tab->browser || IsIconic(hwnd_)) return;
  HWND child=tab->browser->GetHost()->GetWindowHandle(); RECT r={}; GetClientRect(child,&r);
  if(!IsWindowVisible(child) || r.right<2 || r.bottom<2) return;
  const double dpi=GetDpiForWindow(hwnd_)/96.0;
  auto params=CefDictionaryValue::Create();params->SetString("format","jpeg");params->SetInt("quality",70);
  params->SetBool("fromSurface",true);params->SetBool("captureBeyondViewport",false);
  auto clip=CefDictionaryValue::Create();clip->SetDouble("x",0);clip->SetDouble("y",0);
  clip->SetDouble("width",r.right/dpi);clip->SetDouble("height",r.bottom/dpi);
  clip->SetDouble("scale",std::min(1.0,480.0*dpi/r.right));params->SetDictionary("clip",clip);
  tab->thumbnail_registration=tab->browser->GetHost()->AddDevToolsMessageObserver(new ThumbnailObserver(this,tab->id,tab->url));
  tab->browser->GetHost()->ExecuteDevToolsMethod(900001,"Page.captureScreenshot",params);
}

BrowserWindow::Geometry BrowserWindow::CurrentGeometry() const {
  RECT client = {}; GetClientRect(hwnd_, &client);
  const float scale = GetDpiForWindow(hwnd_) / 96.0f;
  const auto px = [scale](int value) { return static_cast<int>(std::round(value * scale)); };
  Geometry g = {};
  g.scale = scale;
  g.width = std::max(1L, client.right - client.left);
  g.height = std::max(1L, client.bottom - client.top);
  g.toolbar = px((EffectiveSettings()->GetString("layout") == "classic" ? 82 : 48) + (BookmarksBarVisible() ? 28 : 0));
  g.sidebar = sidebar_visible_ && bookmarks_sidebar_ ? px(276) : 0;
  g.panel = px(std::max(0, right_panel_width_));
  const auto* active=const_cast<BrowserWindow*>(this)->ActiveTab();
  g.shell_height = (active&&active->reader_active) || popover_visible_ || overview_visible_ || sidebar_visible_ || sidebar_motion_ || g.panel > 0 ? g.height :
      std::min(g.height, std::max(g.toolbar, px(suggestions_height_)));
  g.content = CefRect(g.sidebar, g.toolbar,
      std::max(1, g.width - g.sidebar - g.panel), std::max(1, g.height - g.toolbar));
  return g;
}

void BrowserWindow::Layout() {
  CEF_REQUIRE_UI_THREAD();
  if (!hwnd_ || IsIconic(hwnd_)) return;
  const auto g = CurrentGeometry();
  struct Position { HWND hwnd, after; int x, y, width, height; UINT flags; };
  std::vector<Position> positions;
  constexpr UINT flags = SWP_NOACTIVATE | SWP_NOCOPYBITS;
  const std::string profile = VisibleProfileId();
  for (auto& tab : tabs_) {
    if (!tab.browser) continue;
    HWND child = tab.browser->GetHost()->GetWindowHandle();
    const bool belongs = tab.incognito ? profile == "__incognito__" : tab.profile_id == profile;
    // The opaque shell overview covers the existing active view. Keeping that
    // view mapped lets an asynchronous compositor screenshot finish reliably.
    const bool visible = belongs && tab.id == active_tab_id_;
    positions.push_back({child, HWND_BOTTOM, g.content.x, g.content.y, g.content.width, g.content.height,
        flags | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW)});
  }
  if (surface_) {
    surface_->PrepareResize(g.width, g.shell_height, g.scale);
    positions.push_back({surface_->hwnd(), resize_border_ ? resize_border_ : HWND_TOP,
        0, 0, g.width, g.shell_height, flags | SWP_SHOWWINDOW});
  }
  if(resize_border_){
    if(!IsZoomed(hwnd_)){
      const int edge=static_cast<int>(std::round(6 * g.scale));
      HRGN ring=CreateRectRgn(0,0,g.width,g.height),inside=CreateRectRgn(edge,edge,g.width-edge,g.height-edge);
      CombineRgn(ring,ring,inside,RGN_DIFF);DeleteObject(inside);
      SetWindowRgn(resize_border_,ring,FALSE);
    }
    positions.push_back({resize_border_, HWND_TOP, 0, 0, g.width, g.height,
        flags | (IsZoomed(hwnd_) ? SWP_HIDEWINDOW : SWP_SHOWWINDOW)});
  }
  // Bridge events (for example suggestion updates) can request layout without
  // changing geometry. Do not invalidate or reposition those surfaces again.
  positions.erase(std::remove_if(positions.begin(), positions.end(), [this](const Position& p) {
    RECT actual = {};
    if (!GetWindowRect(p.hwnd, &actual)) return false;
    MapWindowPoints(nullptr, hwnd_, reinterpret_cast<POINT*>(&actual), 2);
    const bool visible = (GetWindowLongPtrW(p.hwnd, GWL_STYLE) & WS_VISIBLE) != 0;
    return actual.left == p.x && actual.top == p.y &&
        actual.right - actual.left == p.width && actual.bottom - actual.top == p.height &&
        visible == ((p.flags & SWP_SHOWWINDOW) != 0);
  }), positions.end());
  if (!positions.empty()) {
    HDWP batch = BeginDeferWindowPos(static_cast<int>(positions.size()));
    for (const auto& p : positions) {
      if (!batch) break;
      batch = DeferWindowPos(batch, p.hwnd, p.after, p.x, p.y, p.width, p.height, p.flags);
    }
    if (!batch || !EndDeferWindowPos(batch)) {
      // A failed deferred batch must not leave the hosts at previous bounds.
      for (const auto& p : positions)
        SetWindowPos(p.hwnd, p.after, p.x, p.y, p.width, p.height, p.flags);
    }
  }
  ResizeFrostedBackdrop(hwnd_, g.width, std::min(g.height, g.toolbar));
  if (surface_) surface_->CommitResize();
  if (settings_overlay_) { settings_overlay_->Layout(); BlockSettingsBackground(true); }
}

void BrowserWindow::ApplyContentTheme() {
  const bool dark = EffectiveSettings()->GetString("theme") == "dark" ||
      (EffectiveSettings()->GetString("theme") == "system" && IsWindowsDarkMode());
  for (auto& tab : tabs_) {
    if (!tab.browser || (tab.url != "about:blank" && tab.url != "soulu://home")) continue;
    auto frame = tab.browser->GetMainFrame();
    if (frame && frame->GetURL().ToString().find("/ui/start.html") != std::string::npos)
      frame->ExecuteJavaScript(std::string("document.body.dataset.theme='") + (dark ? "dark" : "light") + "';document.documentElement.style.background='" + (dark ? "#08090b" : "#fafafa") + "';", frame->GetURL(), 0);
  }
  RefreshHomePages();
}

CefRefPtr<CefDictionaryValue> BrowserWindow::State() const {
  auto state = CefDictionaryValue::Create();
  const auto client = CurrentGeometry();
  state->SetInt("clientHeight", static_cast<int>(std::ceil(client.height / client.scale)));
  state->SetBool("settingsOverlayOpen", settings_overlay_ != nullptr);
  state->SetBool("settingsOverlayReady", settings_overlay_ && settings_overlay_->ready());
  state->SetDouble("settingsOverlayProgress", settings_overlay_ ? settings_overlay_->progress() : 0);
  state->SetInt("settingsOverlayDuration", settings_overlay_ ? settings_overlay_->duration() : 260);
  state->SetInt("settingsOverlayTicks", settings_overlay_ ? settings_overlay_->ticks() : 0);
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
        ? (EffectiveSettings()->GetString("language") == "en" ? "Settings" : "Настройки")
        : tab.title);
    row->SetString("url", tab.url == "about:blank" ? "" :
        (is_settings ? "soulu://settings" : tab.url));
    row->SetString("label", tab.url == "about:blank" ? "" :
        (is_settings ? "Настройки Soulu" : tab.url));
    row->SetString("favicon", tab.favicon);
    if (overview_visible_) row->SetString("thumbnail", tab.thumbnail);
    row->SetBool("loading", tab.loading);
    row->SetBool("active", tab.id == active_tab_id_);
    row->SetBool("incognito", tab.incognito);
    list->SetDictionary(output_index++, row);
  }
  state->SetList("tabs", list);
  state->SetInt("activeTabId", active_tab_id_);
  state->SetBool("sidebarVisible", sidebar_visible_);
  state->SetBool("bookmarksSidebarVisible", sidebar_visible_ && bookmarks_sidebar_);
  state->SetBool("overviewVisible", overview_visible_);
  auto* active=const_cast<BrowserWindow*>(this)->ActiveTab();
  state->SetBool("readerActive",active&&active->reader_active);
  state->SetBool("bookmarksBarVisible", BookmarksBarVisible());
  state->SetList("bookmarks", ProfileBookmarks());
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
  update->SetString("soulu", kSouluVersion);
  update->SetString("recommended", kSouluVersion);
  update->SetString("cef", EngineVersion(0, 3));
  update->SetString("chromium", EngineVersion(4, 4));
  update->SetBool("available", false);
  update->SetBool("security", false);
  state->SetDictionary("update", update);
  state->SetDictionary("settings", EffectiveSettings()->Copy(false));
  if (auto* tab = const_cast<BrowserWindow*>(this)->ActiveTab()) {
    auto page = CefDictionaryValue::Create();
    const bool is_settings =
        tab->url.find("/ui/settings.html") != std::string::npos;
    page->SetString("title", is_settings
        ? (EffectiveSettings()->GetString("language") == "en" ? "Settings" : "Настройки")
        : tab->title);
    page->SetString("url", tab->url == "about:blank" ? "" :
        (is_settings ? "soulu://settings" : tab->url));
    page->SetString("label", tab->url == "about:blank" ? "" :
        (is_settings ? "Настройки Soulu" : tab->url));
    page->SetString("favicon", tab->favicon);
    page->SetBool("loading", tab->loading);
    page->SetBool("canGoBack", tab->can_go_back);
    page->SetInt("generation",tab->document_generation);
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
  if (settings_browser_ && settings_browser_->GetMainFrame())
    settings_browser_->GetMainFrame()->ExecuteJavaScript(script, settings_browser_->GetMainFrame()->GetURL(), 0);
}
void BrowserWindow::EmitState() { Emit("state", Wrap(State())); }

void BrowserWindow::RequestFind() { if (settings_overlay_) { FocusSettings(); return; } if(surface_)surface_->Focus();Emit("requestFind",EmptyValue()); }

void BrowserWindow::ReaderDocumentNavigation(int id) {
  if(auto* tab=FindTab(id)){++tab->document_generation;tab->main_loading=true;tab->reader_active=false;tab->reader_article=nullptr;}
  Layout();EmitState();
}
void BrowserWindow::ReaderDocumentLoaded(int id) {
  if(auto* tab=FindTab(id))tab->main_loading=false;
  EmitState();
}

CefRefPtr<CefDictionaryValue> BrowserWindow::ReaderPreferences(const Tab& tab) {
  const auto key=tab.incognito?"__incognito__":tab.profile_id;
  auto& prefs=reader_preferences_[key];if(prefs)return prefs->Copy(false);
  prefs=CefDictionaryValue::Create();prefs->SetString("theme","light");prefs->SetString("font","serif");
  prefs->SetInt("size",20);prefs->SetInt("width",1);prefs->SetInt("spacing",1);prefs->SetBool("images",true);
  // Validate persisted input through the same bounded schema as user edits.
  auto saved=tab.incognito?nullptr:ReadJson(ProfileRoot(key)/L"soulu-reader.json");
  if(saved&&saved->GetType()==VTYPE_DICTIONARY){auto d=saved->GetDictionary();
    const auto theme=d->GetString("theme").ToString(),font=d->GetString("font").ToString();
    if(theme=="light"||theme=="sepia"||theme=="dark")prefs->SetString("theme",theme);
    if(font=="sans"||font=="serif"||font=="system")prefs->SetString("font",font);
    if(d->HasKey("size"))prefs->SetInt("size",std::clamp(d->GetInt("size"),14,32));
    if(d->HasKey("width"))prefs->SetInt("width",std::clamp(d->GetInt("width"),0,2));
    if(d->HasKey("spacing"))prefs->SetInt("spacing",std::clamp(d->GetInt("spacing"),0,2));
    if(d->GetType("images")==VTYPE_BOOL)prefs->SetBool("images",d->GetBool("images"));
  }
  return prefs->Copy(false);
}

CefRefPtr<CefDictionaryValue> BrowserWindow::SiteSnapshot(int id) {
  auto result=CefDictionaryValue::Create();auto* tab=id?FindTab(id):ActiveTab();if(!tab)return result;
  result->SetInt("tabId",tab->id);result->SetString("url",tab->url);result->SetInt("generation",tab->document_generation);
  result->SetString("origin",WebOrigin(tab->url));result->SetString("domain",SiteDomain(tab->url));
  result->SetString("favicon",tab->favicon);result->SetBool("readerActive",tab->reader_active);
  auto entry=tab->browser?tab->browser->GetHost()->GetVisibleNavigationEntry():nullptr;
  auto ssl=entry?entry->GetSSLStatus():nullptr;
  result->SetBool("secureConnection",ssl&&ssl->IsSecureConnection()&&!CefIsCertStatusError(ssl->GetCertStatus()));
  result->SetBool("mainLoading",tab->main_loading);
  result->SetBool("readerAvailable",tab->reader_article!=nullptr&&!tab->main_loading);
  if(tab->reader_active&&tab->reader_article)result->SetDictionary("article",tab->reader_article->Copy(false));
  result->SetDictionary("preferences",ReaderPreferences(*tab));
  if(tab->browser)result->SetInt("zoom",static_cast<int>(std::round(100*std::pow(1.2,tab->browser->GetHost()->GetZoomLevel()))));
  auto policy=PolicyForTab(tab->id);if(policy)result->SetDictionary("rules",policy->Snapshot());
  return result;
}

void BrowserWindow::FinishReader(int id,const std::string& url,int generation,bool enter,
    CefRefPtr<CefDictionaryValue> article,CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  auto* tab=FindTab(id);
  if(!tab||tab->id!=active_tab_id_||tab->url!=url||tab->document_generation!=generation||tab->main_loading){
    callback->Failure(409,"Страница изменилась. Откройте меню заново.");return;}
  if(article&&(article->GetString("url")!=url||article->GetString("content").length()>1000000||
      article->GetString("content").empty()))article=nullptr;
  tab->reader_article=article?article->Copy(false):nullptr;
  if(enter&&!article){callback->Failure(422,"На этой странице не удалось выделить статью");return;}
  if(enter){tab->reader_active=true;overview_visible_=false;Layout();EmitState();if(surface_)surface_->Focus();}
  Reply(callback,SiteSnapshot());
}

bool BrowserWindow::HandleSiteAction(const std::string& action,CefRefPtr<CefValue> payload,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  if(action.rfind("browser.site.",0)!=0)return false;
  auto* tab=ActiveTab();auto data=payload&&payload->GetType()==VTYPE_DICTIONARY?payload->GetDictionary():nullptr;
  if(!tab||!tab->browser){callback->Failure(400,"Нет активной страницы");return true;}
  if(action=="browser.site.get"){Reply(callback,SiteSnapshot());return true;}
  // Every mutation and asynchronous result is bound to a particular document,
  // not whichever tab happens to be active when a bridge request arrives.
  if(!data||data->GetInt("tabId")!=tab->id||data->GetString("url")!=tab->url||
     data->GetInt("generation")!=tab->document_generation){callback->Failure(409,"Страница изменилась");return true;}
  if(action=="browser.site.reader.exit"){
    tab->reader_active=false;Layout();EmitState();Reply(callback,SiteSnapshot());return true;}
  if(action=="browser.site.reader.link"){
    const std::string url=data->GetString("target");
    if(!tab->reader_active||WebOrigin(url).empty()){callback->Failure(400,"Некорректная ссылка");return true;}
    const auto mode=data->GetString("mode").ToString();
    if(mode=="incognito")OpenIncognitoLink(tab->id,tab->browser,url);
    else if(mode=="background"||mode=="new")OpenTabFrom(tab->id,tab->browser,url,mode=="background");
    else if(mode=="current"){tab->reader_active=false;Layout();tab->browser->GetMainFrame()->LoadURL(url);EmitState();}
    else {callback->Failure(400,"Некорректное действие ссылки");return true;}
    ReplyEmpty(callback);return true;
  }
  if(action=="browser.site.reader.image"){
    const std::string target=data->GetString("target");auto policy=PolicyForTab(tab->id);
    if(!tab->reader_active||WebOrigin(target).empty()||
       BlockResource(tab->url,target,RT_IMAGE,policy&&policy->Blocking(tab->url))){callback->Failure(403,"Изображение недоступно");return true;}
    auto request=CefRequest::Create();request->SetURL(target);request->SetMethod("GET");
    request->SetFlags(UR_FLAG_DISABLE_CACHE|UR_FLAG_ALLOW_STORED_CREDENTIALS);
    CefRefPtr<ReaderImageClient> client=new ReaderImageClient(callback);
    auto pending=CefURLRequest::Create(request,client,tab->browser->GetHost()->GetRequestContext());
    if(!pending){callback->Failure(500,"Не удалось загрузить изображение");return true;}
    CefPostDelayedTask(TID_UI,new FunctionTask([client,pending]{client->Timeout(pending);}),15000);return true;
  }
  if(action=="browser.site.reader.preferences"){
    auto prefs=ReaderPreferences(*tab);auto changes=data->GetDictionary("preferences");
    if(!changes){callback->Failure(400,"Некорректные настройки чтения");return true;}
    CefDictionaryValue::KeyList keys;changes->GetKeys(keys);
    for(const auto& key:keys){auto value=changes->GetValue(key);const auto name=key.ToString();bool ok=false;
      if(name=="theme"||name=="font"){auto str=value->GetString().ToString();ok=value->GetType()==VTYPE_STRING&&
        (name=="theme"?(str=="light"||str=="sepia"||str=="dark"):(str=="sans"||str=="serif"||str=="system"));}
      else if(name=="images")ok=value->GetType()==VTYPE_BOOL;
      else if(name=="size"||name=="width"||name=="spacing")ok=value->GetType()==VTYPE_INT&&
        (name=="size"?(value->GetInt()>=14&&value->GetInt()<=32):(value->GetInt()>=0&&value->GetInt()<=2));
      if(!ok){callback->Failure(400,"Некорректные настройки чтения");return true;}
      prefs->SetValue(key,value->Copy());
    }
    const auto key=tab->incognito?"__incognito__":tab->profile_id;
    if(!tab->incognito&&!WriteJson(ProfileRoot(key)/L"soulu-reader.json",Wrap(prefs))){callback->Failure(500,"Настройки чтения не сохранены");return true;}
    reader_preferences_[key]=prefs;EmitState();Reply(callback,SiteSnapshot());return true;
  }
  const auto origin=WebOrigin(tab->url);
  if(origin.empty()){callback->Failure(400,"Доступно только для HTTP/HTTPS-сайтов");return true;}
  if(action=="browser.site.reader.probe"||action=="browser.site.reader.enter"){
    if(tab->main_loading){callback->Failure(409,"Дождитесь загрузки страницы");return true;}
    const auto ui=std::filesystem::u8path(ExecutableDirectory())/"ui";
    std::string script="(()=>{";
    for(const auto& path:{ui/"third_party"/"Readability.js",ui/"reader-extract.js"}){
      std::ifstream file(path,std::ios::binary);if(!file){callback->Failure(500,"Parser режима чтения недоступен");return true;}
      script+=std::string(std::istreambuf_iterator<char>(file),{});script+='\n';
    }
    // reader-extract's IIFE is the final expression, explicitly return it.
    const auto marker=script.rfind("(() => {");if(marker==std::string::npos){callback->Failure(500,"Parser недоступен");return true;}
    script.insert(marker,"return ");script+="})()";
    CefRefPtr<ReaderJob> job=new ReaderJob(this,tab->id,tab->url,tab->document_generation,
      action=="browser.site.reader.enter",script,callback);job->Start(tab->browser);return true;
  }
  if(action=="browser.site.zoom"){
    const auto command=data->GetString("command").ToString();auto host=tab->browser->GetHost();
    double level=host->GetZoomLevel();
    if(command=="reset")level=0;else if(command=="in")level+=1;else if(command=="out")level-=1;
    else {callback->Failure(400,"Некорректный масштаб");return true;}
    host->SetZoomLevel(std::clamp(level,-5.0,8.0));Reply(callback,SiteSnapshot());return true;
  }
  if(action=="browser.site.find"){RequestFind();ReplyEmpty(callback);return true;}
  if(action=="browser.site.clear"){
    const std::wstring text=L"Очистить хранилища сайта "+CefString(origin).ToWString()+
      L" в текущем профиле?\n\nБудут удалены localStorage, sessionStorage, IndexedDB, Cache Storage и service workers только этого origin. Cookies и HTTP-кэш сохранятся. Пароли, закладки и другие сайты не затрагиваются.";
    const int id=tab->id,generation=tab->document_generation;const auto url=tab->url;
    if(TypographyMessageBox(hwnd_,text.c_str(),L"Данные сайта",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES){callback->Success("{\"cleared\":false}");return true;}
    tab=ActiveTab();if(!tab||tab->id!=id||tab->url!=url||tab->document_generation!=generation){callback->Failure(409,"Страница изменилась");return true;}
    CefRefPtr<SiteStorageJob> job=new SiteStorageJob(callback);job->Start(tab->browser,origin);return true;
  }
  auto policy=PolicyForTab(tab->id);bool ok=false;
  if(action=="browser.site.permission")ok=policy&&policy->Set(tab->url,data->GetString("permission"),data->GetInt("value"));
  else if(action=="browser.site.blocking")ok=policy&&policy->SetBlocking(tab->url,data->GetInt("value"));
  else if(action=="browser.site.reset")ok=policy&&policy->ResetSite(tab->url);
  else {callback->Failure(400,"Неизвестное действие сайта");return true;}
  if(!ok){callback->Failure(500,"Правило сайта не сохранено");return true;}
  ApplySiteSound();SyncSitePolicy(tab->id,tab->url);Reply(callback,SiteSnapshot());return true;
}

void BrowserWindow::SetSetting(const std::string& key, CefRefPtr<CefValue> value) {
  if (key == "startPageMode" || key == "startPageUrl") {
    settings_->SetValue(key == "startPageMode" ? "startupMode" : "startupUrl", value->Copy());
  }
  settings_->SetValue(key, value->Copy());
  SaveSettings();
  if (key == "layout" || key.find("bookmarks") == 0) Layout();
  if (key == "theme" || key == "mattePanel") { ApplyWindowAppearance(); ApplyContentTheme(); }
}

void BrowserWindow::HandleBridge(const std::string& request,
                                 CefRefPtr<CefMessageRouterBrowserSide::Callback> callback, bool settings_source) {
  CEF_REQUIRE_UI_THREAD();
  auto parsed = CefParseJSON(request, JSON_PARSER_RFC);
  if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) {
    callback->Failure(400, "Invalid bridge request"); return;
  }
  auto root = parsed->GetDictionary();
  const std::string action = root->GetString("action");
  auto payload = root->GetValue("payload");
  if (action == "browser.settings.siteSnapshot" || action == "browser.settings.clearSite") {
    auto data = payload && payload->GetType() == VTYPE_DICTIONARY ? payload->GetDictionary() : nullptr;
    auto* tab = data ? FindTab(data->GetInt("tabId")) : nullptr;
    if (!settings_source || !settings_overlay_ || !tab || tab->incognito ||
        tab->profile_id != active_profile_id_ || !tab->browser || WebOrigin(tab->url).empty()) {
      callback->Failure(403,"Сайт недоступен в текущем профиле.");return;
    }
    if (action == "browser.settings.siteSnapshot") return Reply(callback,SiteSnapshot(tab->id));
    const int id = tab->id, generation = tab->document_generation;
    const auto url = tab->url, origin = WebOrigin(url);
    if (data->GetString("url") != url || data->GetInt("generation") != generation) {
      callback->Failure(409,"Страница изменилась.");return;
    }
    const std::wstring message = L"Очистить хранилища сайта " + CefString(origin).ToWString() +
      L"? Cookies, HTTP-кэш, пароли и закладки сохранятся.";
    if (TypographyMessageBox(settings_overlay_->hwnd(),message.c_str(),L"Данные сайта",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2) != IDYES) {
      callback->Success("{\"cleared\":false}");return;
    }
    tab = FindTab(id);
    if (!tab || tab->profile_id != active_profile_id_ || tab->url != url || tab->document_generation != generation) {
      callback->Failure(409,"Страница изменилась.");return;
    }
    CefRefPtr<SiteStorageJob> job = new SiteStorageJob(callback);job->Start(tab->browser,origin);return;
  }
  if(HandleSiteAction(action,payload,callback))return;

  if (action == "browser.state.get") return Reply(callback, State());
  if(action=="browser.test.pageShortcut"){
    wchar_t enabled[12]={};if(!GetEnvironmentVariableW(L"SOULU_UI_TEST_PORT",enabled,12)){callback->Failure(403,"Test mode required");return;}
    const int key=payload&&payload->GetType()==VTYPE_INT?payload->GetInt():0;
    if(key!='T'&&key!='L'&&key!=VK_HOME){callback->Failure(400,"Unsupported shortcut");return;}
    if(auto* tab=ActiveTab();tab&&tab->browser){
      CefKeyEvent event;event.type=KEYEVENT_RAWKEYDOWN;event.windows_key_code=key;
      event.modifiers=key==VK_HOME?EVENTFLAG_ALT_DOWN:EVENTFLAG_CONTROL_DOWN;
      tab->browser->GetHost()->SendKeyEvent(event);
    }
    ReplyEmpty(callback);return;
  }
  if(action=="browser.test.findShortcut"){
    wchar_t enabled[12]={};if(!GetEnvironmentVariableW(L"SOULU_UI_TEST_PORT",enabled,12)){callback->Failure(403,"Test mode required");return;}
    if(auto* tab=ActiveTab();tab&&tab->browser){
      CefKeyEvent event;event.type=KEYEVENT_RAWKEYDOWN;event.windows_key_code='F';event.modifiers=EVENTFLAG_CONTROL_DOWN;
      auto browser=tab->reader_active?shell_:tab->browser;browser->GetHost()->SendKeyEvent(event);
    }
    ReplyEmpty(callback);return;
  }
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
    result->SetInt("shellFrameRate", shell_ ? shell_->GetHost()->GetWindowlessFrameRate() : 0);
    result->SetInt("toolbarAlpha", surface_ ? surface_->toolbar_alpha() : 255);
    return Reply(callback, result);
  }
  if (action == "browser.navigate") Navigate(payload->GetString());
  else if (action == "browser.back") { if (auto* t = ActiveTab(); t && t->browser) t->browser->GoBack(); }
  else if (action == "browser.reload") {
    if (auto* t = ActiveTab(); t && t->browser) t->loading ? t->browser->StopLoad() : t->browser->Reload();
  }
  else if (action == "browser.newTab") NewTab("", VisibleProfileId()=="__incognito__");
  else if (action == "browser.home") {
    if (auto* tab=ActiveTab(); tab && tab->browser)
      tab->browser->GetMainFrame()->LoadURL(InternalUrl(PageUrl("home", PageSettings(*tab))));
  }
  else if (action == "browser.newIncognito") NewTab("", true);
  else if (action == "browser.profile.create") {
    if(settings_dirty_){callback->Failure(409,"Apply or cancel settings changes first");return;}
    std::string name = payload && payload->GetType() == VTYPE_STRING
        ? payload->GetString() : "Профиль";
    CreateProfile(name);
    settings_preview_=nullptr;settings_loaded_=nullptr;settings_staged_=nullptr;settings_dirty_=false;
    active_profile_id_ = profiles_.back().id;
    LoadProfileSettings();
    RefreshSettingsProfile();
    NewTab();
    return Reply(callback, State());
  }
  else if (action == "browser.profile.switch") {
    if(settings_dirty_){callback->Failure(409,"Apply or cancel settings changes first");return;}
    SwitchProfile(payload->GetString());
    return Reply(callback, State());
  }
  else if(action=="browser.profile.delete") {
    if(importing_){callback->Failure(409,"Wait for password import to finish");return;}
    const std::string id=payload&&payload->GetType()==VTYPE_STRING?payload->GetString():"";
    auto found=std::find_if(profiles_.begin(),profiles_.end(),[&](const Profile& p){return p.id==id;});
    if(found==profiles_.end()||profiles_.size()<2){callback->Failure(400,"Keep at least one profile");return;}
    const auto text=L"Удалить профиль «"+CefString(found->name).ToWString()+
      L"» и только его данные? Вкладки будут закрыты. Файлы удалятся после завершения Soulu.";
    if(TypographyMessageBox(hwnd_,text.c_str(),L"Удаление профиля",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return Reply(callback,State());
    auto pending=ReadJson(DataRoot()/L"soulu-delete-profiles.json");
    auto list=pending&&pending->GetType()==VTYPE_LIST?pending->GetList()->Copy():CefListValue::Create();
    list->SetString(list->GetSize(),id);
    if(!WriteJson(DataRoot()/L"soulu-delete-profiles.json",Wrap(list))){callback->Failure(500,"Unable to schedule deletion");return;}
    std::vector<int> close;for(const auto& tab:tabs_)if(tab.profile_id==id&&!tab.incognito)close.push_back(tab.id);
    profiles_.erase(found);policies_.erase(id);SaveProfiles();
    SwitchProfile(profiles_.front().id);
    for(int tab_id:close)CloseTab(tab_id);
    auto marks=CefListValue::Create();for(size_t i=0;i<bookmarks_->GetSize();++i){auto row=bookmarks_->GetDictionary(i);
      if(row&&row->GetString("profileId")!=id)marks->SetDictionary(marks->GetSize(),row->Copy(false));}
    bookmarks_=marks;SaveBookmarks(marks);return Reply(callback,State());
  }
  else if(action=="browser.import.sources")return Reply(callback,Wrap(DiscoverPasswordSources()));
  else if(action=="browser.import.browsers")return Reply(callback,Wrap(DiscoverImportBrowsers()));
  else if(action=="browser.import.passwords") {
    auto data=payload&&payload->GetType()==VTYPE_DICTIONARY?payload->GetDictionary():nullptr;
    if(!data||importing_||(!settings_source&&VisibleProfileId()=="__incognito__")) {callback->Failure(409,"Import is unavailable");return;}
    const std::string target=data->GetString("target"),source=data->GetString("source");
    if(std::none_of(profiles_.begin(),profiles_.end(),[&](const Profile& p){return p.id==target;})) {callback->Failure(400,"Unknown target profile");return;}
    importing_=true;CefRefPtr<BrowserWindow> self=this;
    CefPostTask(TID_FILE_BACKGROUND,new FunctionTask([self,callback,target,source](){
      auto report=ImportPasswords(source,target);
      CefPostTask(TID_UI,new FunctionTask([self,callback,report](){
        self->importing_=false;self->Reply(callback,report);
        if(self->close_after_import_)self->CloseAll();
      }));
    }));return;
  }
  else if(action.rfind("browser.sites.",0)==0){
    auto* tab=ActiveTab();auto policy=tab?PolicyForTab(tab->id):policies_[active_profile_id_];
    if(!policy){callback->Failure(400,"No profile policy");return;}
    if(action=="browser.sites.get")return Reply(callback,policy->Snapshot());
    auto data=payload&&payload->GetType()==VTYPE_DICTIONARY?payload->GetDictionary():nullptr;
    if(!data){callback->Failure(400,"Invalid site rule");return;}
    const std::string domain=data->GetString("domain");bool ok=false;
    if(action=="browser.sites.set")ok=policy->Set(domain,data->GetString("permission"),data->GetInt("value"));
    else if(action=="browser.sites.blocking")ok=policy->SetBlocking(domain,data->GetInt("value"));
    else if(action=="browser.sites.reset"){
      if(domain.empty()&&TypographyMessageBox(hwnd_,L"Сбросить все исключения разрешений сайтов этого профиля?",L"Soulu",MB_YESNO|MB_DEFBUTTON2)!=IDYES)return Reply(callback,policy->Snapshot());
      ok=policy->Reset(domain);
    }
    if(!ok){callback->Failure(400,"Rule could not be saved");return;}
    ApplySiteSound();return Reply(callback,policy->Snapshot());
  }
  else if (action == "browser.update.check") {
    auto update = CefDictionaryValue::Create();
    update->SetString("soulu", kSouluVersion);
    update->SetString("recommended", kSouluVersion);
    update->SetString("cef", EngineVersion(0, 3));
    update->SetString("chromium", EngineVersion(4, 4));
    update->SetBool("available", false);
    update->SetBool("security", false);
    return Reply(callback, update);
  }
  else if (action == "browser.switchTab") SwitchTab(payload->GetInt());
  else if (action == "browser.closeTab") CloseTab(payload->GetInt());
  else if (action == "browser.bookmarks.sidebar" || action == "browser.toggleSidebar") {
    const bool tab_toggle = action == "browser.toggleSidebar";
    const bool show_bookmarks = !tab_toggle && payload && payload->GetType()==VTYPE_BOOL && payload->GetBool();
    const bool closing_tabs = !tab_toggle && !show_bookmarks && sidebar_visible_ && !bookmarks_sidebar_;
    if (tab_toggle) {
      sidebar_visible_ = bookmarks_sidebar_ || !sidebar_visible_; bookmarks_sidebar_ = false;
    } else {
      sidebar_visible_ = show_bookmarks;
      if (!closing_tabs) bookmarks_sidebar_ = true;
    }
    if (tab_toggle || closing_tabs) {
      sidebar_motion_ = true; const auto generation = ++sidebar_motion_generation_;
      if (shell_) shell_->GetHost()->SetWindowlessFrameRate(motion::kShellMotionFrameRate);
      CefRefPtr<BrowserWindow> self = this;
      // Keep the OSR host mapped throughout exit, including Escape/overview.
      // A bounded deadline handles frontend interruption or failure.
      CefPostDelayedTask(TID_UI, new FunctionTask([self,generation] {
        if (self->sidebar_motion_generation_ != generation) return;
        self->sidebar_motion_ = false;
        if (self->shell_) self->shell_->GetHost()->SetWindowlessFrameRate(motion::kShellIdleFrameRate);
        self->Layout();
      }), motion::kStructuralMs);
    }
    Layout(); EmitState();
  }
  else if (action == "browser.popover") { popover_visible_ = payload->GetBool(); Layout(); }
  else if (action == "browser.setRightPanel") { right_panel_width_ = payload->GetInt(); Layout(); }
  else if (action == "browser.setSuggestionsHeight") { suggestions_height_ = payload->GetInt(); Layout(); }
  else if (action == "browser.find") {
    if (auto* t = ActiveTab(); t && t->browser) {
      auto browser=t->reader_active?shell_:t->browser;
      const auto options=payload->GetDictionary();
      const std::string text=options ? options->GetString("text").ToString() : payload->GetString().ToString();
      const bool forward=!options || options->GetBool("forward");
      if(text.empty()){browser->GetHost()->StopFinding(true);if(shell_)shell_->GetHost()->StopFinding(true);find_text_.clear();find_browser_id_=0;}
      else {const bool next=find_text_==text&&find_browser_id_==browser->GetIdentifier();
        browser->GetHost()->Find(text,forward,false,next);find_text_=text;find_browser_id_=browser->GetIdentifier();}
    }
  }
  else if (action == "browser.downloads.get") return Reply(callback, Wrap(ProfileDownloads()));
  else if (action == "browser.bookmarks.get") return Reply(callback, Wrap(ProfileBookmarks()));
  else if (action == "browser.overview") {
    const bool show = payload->GetBool();
    if (show && !overview_visible_) CaptureThumbnail();
    overview_visible_ = show; Layout(); EmitState();
  }
  else if (action == "browser.bookmarks.auto") {
    bookmarks_auto_visible_ = payload->GetBool(); Layout(); EmitState();
  }
  else if (action == "browser.openTab") {
    auto args = payload->GetDictionary();
    if (auto* tab = ActiveTab()) OpenTabFrom(tab->id, tab->browser, args->GetString("url"), args->GetBool("background"));
  }
  else if (action == "browser.bookmarks.replace") {
    if (!payload || payload->GetType() != VTYPE_LIST || payload->GetList()->GetSize() > 20000) {
      callback->Failure(400, "Invalid bookmarks"); return;
    }
    auto incoming = payload->GetList();
    auto merged = CefListValue::Create();
    const auto profile = VisibleProfileId();
    for (size_t i=0; i<bookmarks_->GetSize(); ++i) {
      auto row=bookmarks_->GetDictionary(i);
      if (row && row->GetString("profileId") != profile) merged->SetDictionary(merged->GetSize(), row->Copy(false));
    }
    std::unordered_map<int,CefRefPtr<CefDictionaryValue>> index;
    for (size_t i=0; i<incoming->GetSize(); ++i) {
      auto row=incoming->GetDictionary(i);
      if (!row || row->GetInt("id") <= 0 || index.find(row->GetInt("id")) != index.end()) {
        callback->Failure(400, "Invalid bookmark id"); return;
      }
      index.emplace(row->GetInt("id"),row);
      auto copy=row->Copy(false); copy->SetString("profileId",profile);
      merged->SetDictionary(merged->GetSize(),copy);
    }
    for (size_t i=0; i<incoming->GetSize(); ++i) {
      auto row=incoming->GetDictionary(i); int parent=row->GetInt("parentId");
      std::vector<int> ancestors={row->GetInt("id")};
      while(parent) {
        if(std::find(ancestors.begin(),ancestors.end(),parent)!=ancestors.end()) {callback->Failure(400,"Folder cycle"); return;}
        if(ancestors.size()>64) {callback->Failure(400,"Folder hierarchy too deep");return;}
        ancestors.push_back(parent); CefRefPtr<CefDictionaryValue> folder;
        auto found=index.find(parent); if(found!=index.end()) folder=found->second;
        if(!folder || folder->GetString("type")!="folder") {callback->Failure(400,"Invalid folder");return;}
        parent=folder->GetInt("parentId");
      }
    }
    if(!SaveBookmarks(merged)) {callback->Failure(500,"Could not save bookmarks");return;}
    bookmarks_=merged; EmitState(); return Reply(callback,Wrap(ProfileBookmarks()));
  }
  else if (action == "browser.bookmarks.add") {
    if (auto* t = ActiveTab(); t && t->url != "about:blank") {
      auto next=bookmarks_->Copy(); int id=1; bool exists=false;
      for(size_t i=0;i<next->GetSize();++i) {auto row=next->GetDictionary(i); id=std::max(id,row->GetInt("id")+1); if(row->GetString("profileId")==VisibleProfileId() && row->GetString("url")==t->url) exists=true;}
      if(!exists) {
        auto mark=CefDictionaryValue::Create(); mark->SetInt("id",id); mark->SetString("type","url");
        mark->SetString("title",t->title);mark->SetString("url",t->url);mark->SetString("favicon",t->favicon);
        mark->SetString("profileId",VisibleProfileId());mark->SetInt("parentId",0);mark->SetInt("order",static_cast<int>(next->GetSize()));
        mark->SetDouble("createdAt",static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()));next->SetDictionary(next->GetSize(),mark);
        if(!SaveBookmarks(next)) {callback->Failure(500,"Could not save bookmarks");return;} bookmarks_=next; EmitState();
      }
    }
    return Reply(callback,Wrap(ProfileBookmarks()));
  }
  else if (action == "browser.bookmarks.remove") {
    auto next=bookmarks_->Copy();
    for(size_t i=0;i<next->GetSize();++i) if(next->GetDictionary(i)->GetString("profileId")==VisibleProfileId() && next->GetDictionary(i)->GetInt("id")==payload->GetInt()) {next->Remove(i);break;}
    if(!SaveBookmarks(next)) {callback->Failure(500,"Could not save bookmarks");return;} bookmarks_=next; EmitState();
    return Reply(callback,Wrap(ProfileBookmarks()));
  }
  else if (action == "browser.bookmarks.open") Navigate(payload->GetString());
  else if (action == "browser.settings.openWindow") OpenSettingsOverlay();
  else if (action == "browser.settings.get") return Reply(callback, EffectiveSettings()->Copy(false));
  else if (action == "browser.settings.set") {
    if (payload && payload->GetType() == VTYPE_DICTIONARY) {
      auto patch = payload->GetDictionary();
      if(!ValidatePagePatch(patch)){callback->Failure(400,"Invalid page mode or HTTP(S) URL");return;}
      CefDictionaryValue::KeyList keys; patch->GetKeys(keys);
      for (const auto& key : keys) SetSetting(key, patch->GetValue(key));
      RefreshHomePages();
      Emit("settings", Wrap(settings_->Copy(false)));
      EmitState();
    }
    return Reply(callback, settings_->Copy(false));
  }
  else if (action == "browser.history.open") { OpenHistory(payload && payload->GetType()==VTYPE_BOOL && payload->GetBool()); }
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
    if (overview_visible_) row->SetString("thumbnail", tab.thumbnail);
        result->SetDictionary(out++, row);
      }
    }
    for (size_t i = 0; i < bookmarks_->GetSize() && out < 8; ++i) {
      auto mark = bookmarks_->GetDictionary(i);
      if (!mark || mark->GetString("type") == "folder") continue;
      if (!mark || mark->GetString("profileId") != visible_profile) continue;
      std::string hay = mark->GetString("title").ToString() + " " + mark->GetString("url").ToString();
      std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
      if (hay.find(lower) != std::string::npos) { auto row = mark->Copy(false); row->SetString("source", "bookmark"); result->SetDictionary(out++, row); }
    }
    if(visible_profile!="__incognito__"&&!query.empty()){
      auto visits=HistoryFor(visible_profile)->Query(query,0,HistoryNow()+1,0,8);
      if(visits)for(size_t i=0;i<visits->GetSize()&&out<8;++i){auto row=visits->GetDictionary(i);row->SetString("source","history");result->SetDictionary(out++,row);}
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
  else if (action.rfind("browser.passwords.",0)==0) {
    if(importing_&&action!="browser.passwords.get"){callback->Failure(409,"Wait for import to finish");return;}
    if(!settings_source&&VisibleProfileId()=="__incognito__") {callback->Failure(403,"Passwords are unavailable in incognito");return;}
    PasswordVault vault(active_profile_id_);
    if(action=="browser.passwords.get")return Reply(callback,Wrap(vault.List()));
    auto data=payload&&payload->GetType()==VTYPE_DICTIONARY?payload->GetDictionary():nullptr;
    bool ok=false;
    if(action=="browser.passwords.add"&&data){
      std::string secret=data->GetString("password");
      ok=vault.Put(data->GetString("origin"),data->GetString("username"),secret,true);
      if(!secret.empty())SecureZeroMemory(secret.data(),secret.size());
    }else if(action=="browser.passwords.remove"&&payload&&payload->GetType()==VTYPE_STRING)ok=vault.Remove(payload->GetString());
    else if(action=="browser.passwords.reveal"&&payload&&payload->GetType()==VTYPE_STRING){
      std::string secret;if(!vault.Reveal(payload->GetString(),secret)){callback->Failure(500,"Unable to decrypt credential");return;}
      auto v=CefValue::Create();v->SetString(secret);Reply(callback,v);
      if(!secret.empty())SecureZeroMemory(secret.data(),secret.size());return;
    }
    else if(action=="browser.passwords.copy"&&data){
      std::string secret;
      if(data->GetString("field")=="username"){
        auto rows=vault.List();for(size_t i=0;i<rows->GetSize();++i){auto row=rows->GetDictionary(i);
          if(row&&row->GetString("id")==data->GetString("id")){secret=row->GetString("username");ok=true;break;}}
        if(!ok){callback->Failure(400,"Credential not found");return;}ok=false;
      }else if(data->GetString("field")!="password"||!vault.Reveal(data->GetString("id"),secret)){
        callback->Failure(500,"Unable to decrypt credential");return;}
      std::wstring wide=CefString(secret).ToWString();
      if(!secret.empty())SecureZeroMemory(secret.data(),secret.size());
      HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(wide.size()+1)*sizeof(wchar_t));
      if(memory){void* buffer=GlobalLock(memory);
        if(buffer){memcpy(buffer,wide.c_str(),(wide.size()+1)*sizeof(wchar_t));GlobalUnlock(memory);
          if(OpenClipboard(hwnd_)){EmptyClipboard();if(SetClipboardData(CF_UNICODETEXT,memory)){memory=nullptr;ok=true;}CloseClipboard();}}
        if(memory)GlobalFree(memory);}
      if(!wide.empty())SecureZeroMemory(wide.data(),wide.size()*sizeof(wchar_t));
      if(ok){DWORD sequence=GetClipboardSequenceNumber();HWND hwnd=hwnd_;
        CefPostDelayedTask(TID_UI,new FunctionTask([sequence,hwnd](){
          if(GetClipboardSequenceNumber()==sequence&&OpenClipboard(hwnd)){
            if(GetClipboardSequenceNumber()==sequence)EmptyClipboard();CloseClipboard();}
        }),30000);}
    }
    if(!ok){callback->Failure(400,"Credential could not be saved or removed");return;}
    return Reply(callback,Wrap(vault.List()));
  }
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
  else if (action == "window.beginDrag") {
    POINT cursor = {}; GetCursorPos(&cursor); ReleaseCapture();
    SendMessage(hwnd_, payload->GetInt() == 2 ? WM_NCLBUTTONDBLCLK : WM_NCLBUTTONDOWN,
                HTCAPTION, MAKELPARAM(cursor.x, cursor.y));
  }
  else if (action == "window.toolbarMenu") {
    POINT point = {}; GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Настройки");
    AppendMenuW(menu, MF_STRING, 2, L"Загрузки");
    AppendMenuW(menu, MF_STRING, 5, L"История\tCtrl+H");
    AppendMenuW(menu, MF_STRING, 6, L"Очистить данные браузера…\tCtrl+Shift+Delete");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"Новая вкладка");
    AppendMenuW(menu, MF_STRING, 4, L"Новая вкладка инкогнито");
    const int command = TypographyTrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                       point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (command == 5) OpenHistory();
    else if (command == 6) OpenHistory(true);
    else if (command == 1) OpenSettingsOverlay();
    else if (command == 2) Emit("openDownloads", EmptyValue());
    else if (command == 3) NewTab();
    else if (command == 4) NewTab("", true);
  }
  else if (action == "browser.pageMenu") {
    POINT point = {}; GetCursorPos(&point);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Копировать адрес");
    AppendMenuW(menu, MF_STRING, 3, L"Найти на странице");
    auto* current=ActiveTab();auto policy=current?PolicyForTab(current->id):nullptr;
    if(current&&policy&&!WebOrigin(current->url).empty())AppendMenuW(menu,MF_STRING,4,
      policy->Blocking(current->url)?L"Отключить блокировку рекламы на этом сайте":L"Включить блокировку рекламы на этом сайте");
    const int command = TypographyTrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
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
    else if(command==4){
      if(auto* tab=ActiveTab();tab&&tab->browser){auto rules=PolicyForTab(tab->id);
        if(rules&&rules->SetBlocking(tab->url,rules->Blocking(tab->url)?0:1))tab->browser->Reload();}
    }
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
  if(clearing_data_){close_after_clear_=true;return;}
  if (settings_overlay_) { settings_close_all_ = true; GuardSettingsClose(kSettingsSession, true); return; }
  if(importing_){close_after_import_=true;return;}
  if (closing_) return;
  SaveSession();
  closing_ = true;
  // Destroying an Alloy child can synchronously call BrowserClosed and erase
  // tabs_. Close a snapshot so no browser is skipped by iterator invalidation.
  std::vector<CefRefPtr<CefBrowser>> browsers;
  for (const auto& tab : tabs_) if (tab.browser) browsers.push_back(tab.browser);
  if (shell_) browsers.push_back(shell_);
  for (const auto& browser : browsers) browser->GetHost()->CloseBrowser(true);
  if (!shell_ && tabs_.empty()) FinishClose();
}

void BrowserWindow::CookieStoreFlushed() {
  CEF_REQUIRE_UI_THREAD();
  if (pending_cookie_flushes_ > 0) --pending_cookie_flushes_;
  if (flushing_cookies_ && pending_cookie_flushes_ == 0 && hwnd_)
    DestroyWindow(hwnd_);
}

void BrowserWindow::FinishClose() {
  CEF_REQUIRE_UI_THREAD();
  if (flushing_cookies_) return;
  flushing_cookies_ = true;
  std::vector<CefRefPtr<CefCookieManager>> managers;
  managers.push_back(CefCookieManager::GetGlobalManager(nullptr));
  for (const auto& profile : profiles_)
    managers.push_back(profile.context->GetCookieManager(nullptr));
  pending_cookie_flushes_ = static_cast<int>(managers.size());
  // Keep CEF's message loop and contexts alive until their writes complete.
  for (const auto& manager : managers) {
    if (!manager || !manager->FlushStore(new CookieFlushCompletion(this)))
      CookieStoreFlushed();
  }
}

LRESULT CALLBACK BrowserWindow::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if(TypographyMenuMessage(message,lparam))return TRUE;
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
      if (wparam) {
        // A borderless maximized client must not extend into the invisible
        // thick-frame margin outside the monitor work area.
        if (IsZoomed(hwnd)) {
          auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
          MONITORINFO monitor = {sizeof(monitor)};
          if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
            IntersectRect(&params->rgrc[0], &params->rgrc[0], &monitor.rcWork);
        }
        return 0;
      }
      break;
    case WM_GETMINMAXINFO: {
      auto* sizes = reinterpret_cast<MINMAXINFO*>(lparam);
      const UINT dpi = GetDpiForWindow(hwnd);
      sizes->ptMinTrackSize = {MulDiv(620, dpi, 96), MulDiv(420, dpi, 96)};
      return 0;
    }
    case WM_NCHITTEST: {
      if (IsZoomed(hwnd)) return HTCLIENT;
      const LRESULT hit = DefWindowProc(hwnd, message, wparam, lparam);
      if (hit != HTCLIENT) return hit;
      RECT r = {}; GetWindowRect(hwnd, &r);
      const int x = GET_X_LPARAM(lparam), y = GET_Y_LPARAM(lparam), edge = MulDiv(7, GetDpiForWindow(hwnd), 96);
      const bool left = x < r.left + edge, right = x >= r.right - edge;
      const bool top = y < r.top + edge, bottom = y >= r.bottom - edge;
      if (top && left) return HTTOPLEFT; if (top && right) return HTTOPRIGHT;
      if (bottom && left) return HTBOTTOMLEFT; if (bottom && right) return HTBOTTOMRIGHT;
      if (left) return HTLEFT; if (right) return HTRIGHT;
      if (top) return HTTOP; if (bottom) return HTBOTTOM;
      return HTCLIENT;
    }
    case WM_ERASEBKGND: {
      const std::string theme = self->EffectiveSettings()->GetString("theme");
      const bool dark = theme == "dark" ||
          (theme == "system" && IsWindowsDarkMode());
      RECT client = {};
      GetClientRect(hwnd, &client);
      HBRUSH background = CreateSolidBrush(dark ? RGB(8, 9, 11) : RGB(255, 255, 255));
      FillRect(reinterpret_cast<HDC>(wparam), &client,
          self->EffectiveSettings()->GetBool("mattePanel") ? static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)) : background);
      DeleteObject(background);
      return 1;
    }
    case WM_SIZE: self->Layout(); if (self->settings_overlay_ && IsIconic(hwnd)) self->settings_overlay_->Layout(); return 0;
    case WM_SYSCOMMAND:
      if ((wparam & 0xFFF0) == SC_MOVE || (wparam & 0xFFF0) == SC_SIZE) {
        // CEF 154 disables nestable Chromium work by default. Win32's move/
        // resize modal loop must continue processing renderer resize/paint.
        // Keep the owner alive if a close task runs in that loop.
        CefRefPtr<BrowserWindow> keep_alive(self);
        CefScopedSetNestableTasksAllowed allow_tasks;
        return DefWindowProc(hwnd, message, wparam, lparam);
      }
      break;
    case WM_ENTERSIZEMOVE:
      if (self->shell_) self->shell_->GetHost()->NotifyMoveOrResizeStarted();
      for (auto& tab : self->tabs_)
        if (tab.browser) tab.browser->GetHost()->NotifyMoveOrResizeStarted();
      return 0;
    case WM_EXITSIZEMOVE: self->Layout(); return 0;
    case WM_DPICHANGED: {
      const auto* rect = reinterpret_cast<const RECT*>(lparam);
      SetWindowPos(hwnd, nullptr, rect->left, rect->top,
                   rect->right - rect->left, rect->bottom - rect->top,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
      self->Layout();
      return 0;
    }
    case ShellSurface::kFirstFrame:
      self->shell_frame_ready_ = true;
      self->ShowWhenReady();
      return 0;
    case WM_MOVE: if (self->shell_) self->shell_->GetHost()->NotifyMoveOrResizeStarted();
      if (self->settings_overlay_) self->settings_overlay_->Layout(); break;
    case WM_SETFOCUS: if (self->settings_overlay_) { self->FocusSettings(); return 0; } break;
    case WM_DWMCOMPOSITIONCHANGED: self->ApplyWindowAppearance(); return 0;
    case WM_SETTINGCHANGE: self->ApplyWindowAppearance(); self->ApplyContentTheme(); break;
    case WM_CLOSE: TypographyCancelOwnedDialogs(hwnd);self->CloseAll();return 0;
    case WM_DESTROY: ReleaseFrostedBackdrop(hwnd); CefQuitMessageLoop(); return 0;
    case WM_NCDESTROY:
      SetWindowLongPtr(hwnd, GWLP_USERDATA, 0); self->hwnd_ = nullptr; self->Release(); break;
  }
  return DefWindowProc(hwnd, message, wparam, lparam);
}
}

