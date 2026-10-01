#include "examples/soulu/browser_client.h"

#include <string>
#include <cstring>

#include "examples/soulu/browser_window.h"
#include "include/wrapper/cef_helpers.h"

namespace soulu {
namespace {
enum LinkCommand {
  kLinkForeground = MENU_ID_USER_FIRST,
  kLinkBackground,
  kLinkIncognito,
  kLinkSave,
  kLinkCopy,
};

bool DownloadableLink(const std::string& url) {
  return url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
}

class BridgeHandler final : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit BridgeHandler(CefRefPtr<BrowserWindow> owner) : owner_(owner) {}
  bool OnQuery(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, int64_t,
               const CefString& request, bool,
               CefRefPtr<Callback> callback) override {
    if (!frame->IsMain()) return false;
    const std::string url = frame->GetURL();
    if (!owner_->IsTrustedUi(url)) {
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
    : owner_(owner), role_(role), tab_id_(tab_id),
      policy_(role!=BrowserRole::kShell?owner->PolicyForTab(tab_id):nullptr) {}

CefRefPtr<CefResourceRequestHandler> BrowserClient::GetResourceRequestHandler(
    CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,CefRefPtr<CefRequest>,
    bool navigation,bool download,const CefString&,bool&) {
  return role_!=BrowserRole::kShell&&!navigation&&!download?this:nullptr;
}
BrowserClient::ReturnValue BrowserClient::OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,CefRefPtr<CefRequest> request,CefRefPtr<CefCallback>) {
  if(!browser||!frame||!policy_)return RV_CONTINUE;
  auto main=browser->GetMainFrame();if(!main)return RV_CONTINUE;
  const std::string top=main->GetURL();
  return BlockResource(top,request->GetURL(),request->GetResourceType(),policy_->Blocking(top))?RV_CANCEL:RV_CONTINUE;
}
bool BrowserClient::OnRequestMediaAccessPermission(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame>,
    const CefString& origin,uint32_t requested,CefRefPtr<CefMediaAccessCallback> callback) {
  CEF_REQUIRE_UI_THREAD();uint32_t allowed=0;
  if(requested&CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE)
    if(owner_->AllowSite(tab_id_,origin,"camera"))allowed|=CEF_MEDIA_PERMISSION_DEVICE_VIDEO_CAPTURE;
  if(requested&CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE)
    if(owner_->AllowSite(tab_id_,origin,"microphone"))allowed|=CEF_MEDIA_PERMISSION_DEVICE_AUDIO_CAPTURE;
  // Screen capture is a separate permission, deliberately never inferred from camera access.
  callback->Continue(allowed);return true;
}
bool BrowserClient::OnShowPermissionPrompt(CefRefPtr<CefBrowser>,uint64_t,
    const CefString& origin,uint32_t requested,CefRefPtr<CefPermissionPromptCallback> callback) {
  CEF_REQUIRE_UI_THREAD();uint32_t handled=0;bool allowed=true;
  const std::pair<uint32_t,const char*> supported[]={
    {CEF_PERMISSION_TYPE_GEOLOCATION,"geolocation"},{CEF_PERMISSION_TYPE_NOTIFICATIONS,"notifications"},
    {CEF_PERMISSION_TYPE_CAMERA_STREAM,"camera"},{CEF_PERMISSION_TYPE_MIC_STREAM,"microphone"},
    {CEF_PERMISSION_TYPE_MULTIPLE_DOWNLOADS,"downloads"}};
  uint32_t known=0;for(const auto& item:supported)known|=item.first;
  if(requested&~known)return false;
  for(const auto& [flag,name]:supported)if(requested&flag){handled|=flag;
    allowed=owner_->AllowSite(tab_id_,origin,name)&&allowed;}
  callback->Continue(allowed&&handled==requested&&handled!=0?CEF_PERMISSION_RESULT_ACCEPT:CEF_PERMISSION_RESULT_DENY);
  return true;
}
void BrowserClient::OnLoadEnd(CefRefPtr<CefBrowser>,CefRefPtr<CefFrame> frame,int) {
  if(frame->IsMain()&&role_!=BrowserRole::kShell)owner_->ApplySiteSound();
}
bool BrowserClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,bool,bool) {
  CEF_REQUIRE_UI_THREAD();if(router_)router_->OnBeforeBrowse(browser,frame);
  if(role_!=BrowserRole::kShell&&frame->IsMain())owner_->SyncSitePolicy(tab_id_,request->GetURL());
  return false;
}

void BrowserClient::OnBeforeContextMenu(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
    CefRefPtr<CefContextMenuParams> params, CefRefPtr<CefMenuModel> model) {
  CEF_REQUIRE_UI_THREAD();
  if (role_ != BrowserRole::kContent || params->GetLinkUrl().empty()) return;
  model->Clear();
  model->AddItem(kLinkForeground, "Открыть ссылку в новой вкладке");
  model->AddItem(kLinkBackground, "Открыть ссылку в фоновой вкладке");
  model->AddItem(kLinkIncognito, "Открыть ссылку в режиме инкогнито");
  const bool web_link = DownloadableLink(params->GetLinkUrl());
  model->SetEnabled(kLinkForeground, web_link);
  model->SetEnabled(kLinkBackground, web_link);
  model->SetEnabled(kLinkIncognito, web_link);
  if (DownloadableLink(params->GetLinkUrl()))
    model->AddItem(kLinkSave, "Сохранить ссылку как…");
  model->AddItem(kLinkCopy, "Копировать адрес ссылки");
}

bool BrowserClient::RunContextMenu(CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams> params,
    CefRefPtr<CefMenuModel> model, CefRefPtr<CefRunContextMenuCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (role_ != BrowserRole::kContent || params->GetLinkUrl().empty()) return false;
  HMENU menu = CreatePopupMenu();
  if (!menu) { callback->Cancel(); return true; }
  for (size_t i = 0; i < model->GetCount(); ++i) {
    const auto label = model->GetLabelAt(i).ToWString();
    AppendMenuW(menu, MF_STRING | (model->IsEnabledAt(i) ? 0 : MF_GRAYED),
                model->GetCommandIdAt(i), label.c_str());
  }
  POINT point = {params->GetXCoord(), params->GetYCoord()};
  ClientToScreen(browser->GetHost()->GetWindowHandle(), &point);
  const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
      point.x, point.y, 0, owner_->hwnd(), nullptr);
  DestroyMenu(menu);
  if (command) callback->Continue(command, EVENTFLAG_NONE);
  else callback->Cancel();
  return true;
}

bool BrowserClient::OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame>, CefRefPtr<CefContextMenuParams> params,
    int command, EventFlags) {
  CEF_REQUIRE_UI_THREAD();
  if (role_ != BrowserRole::kContent || params->GetLinkUrl().empty()) return false;
  const std::string url = params->GetLinkUrl();
  switch (command) {
    case kLinkForeground:
    case kLinkBackground:
      if (DownloadableLink(url))
        owner_->OpenTabFrom(tab_id_, browser, url, command == kLinkBackground);
      return true;
    case kLinkIncognito:
      if (DownloadableLink(url)) owner_->OpenIncognitoLink(tab_id_, browser, url);
      return true;
    case kLinkSave:
      if (DownloadableLink(url)) browser->GetHost()->StartDownload(url);
      return true;
    case kLinkCopy: {
      const std::wstring wide = params->GetLinkUrl().ToWString();
      HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));
      if (!memory) return true;
      void* buffer = GlobalLock(memory);
      if (!buffer) { GlobalFree(memory); return true; }
      memcpy(buffer, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t));
      GlobalUnlock(memory);
      if (OpenClipboard(owner_->hwnd())) {
        EmptyClipboard();
        if (SetClipboardData(CF_UNICODETEXT, memory)) memory = nullptr;
        CloseClipboard();
      }
      if (memory) GlobalFree(memory);
      return true;
    }
    default: return false;
  }
}

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
  if(role_!=BrowserRole::kShell&&source_process==PID_RENDERER&&
     message->GetName()=="soulu.credential.submit") {
    auto args=message->GetArgumentList();
    if(args->GetSize()==3 && args->GetType(0)==VTYPE_STRING&&args->GetType(1)==VTYPE_STRING&&args->GetType(2)==VTYPE_STRING)
      owner_->OfferCredential(tab_id_,frame,args->GetString(0),args->GetString(1),args->GetString(2));
    return true;
  }
  return router_ && router_->OnProcessMessageReceived(browser, frame, source_process, message);
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (popup_opener_) {
    popup_opener_->pending_popups_.erase(opener_popup_id_);
    popup_opener_ = nullptr;
  }
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

bool BrowserClient::OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame>, const CefString& url, WindowOpenDisposition disposition,
    bool) {
  CEF_REQUIRE_UI_THREAD();
  if (role_ == BrowserRole::kShell) return false;
  switch (disposition) {
    case CEF_WOD_NEW_FOREGROUND_TAB:
    case CEF_WOD_NEW_BACKGROUND_TAB:
    case CEF_WOD_NEW_WINDOW:
    case CEF_WOD_NEW_POPUP:
      owner_->OpenTabFrom(tab_id_, browser, url,
                         disposition == CEF_WOD_NEW_BACKGROUND_TAB);
      return true;
    default:
      return false;
  }
}

bool BrowserClient::OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>,
    int popup_id, const CefString& url, const CefString&,
    WindowOpenDisposition disposition, bool gesture, const CefPopupFeatures&,
    CefWindowInfo& info, CefRefPtr<CefClient>& client, CefBrowserSettings&,
    CefRefPtr<CefDictionaryValue>& extra_info, bool*) {
  CEF_REQUIRE_UI_THREAD();
  if (role_ == BrowserRole::kShell) return true;
  const bool popup_request=!gesture||disposition==CEF_WOD_NEW_POPUP||disposition==CEF_WOD_NEW_WINDOW;
  if(popup_request&&!owner_->AllowSite(tab_id_,browser->GetMainFrame()->GetURL(),"popups"))return true;
  const int id = owner_->PreparePopup(tab_id_, url,
      disposition == CEF_WOD_NEW_BACKGROUND_TAB, info);
  if (!id) return true;
  CefRefPtr<BrowserClient> popup_client =
      new BrowserClient(owner_, BrowserRole::kContent, id);
  popup_client->popup_opener_ = this;
  popup_client->opener_popup_id_ = popup_id;
  client = popup_client;
  pending_popups_[popup_id] = id;
  if(!extra_info)extra_info=CefDictionaryValue::Create();
  extra_info->SetBool("souluIncognito",owner_->IsIncognitoTab(tab_id_));
  // Let CEF create the real popup, retaining its opener and request context.
  return false;
}

void BrowserClient::OnBeforePopupAborted(CefRefPtr<CefBrowser>, int popup_id) {
  CEF_REQUIRE_UI_THREAD();
  auto it = pending_popups_.find(popup_id);
  if (it == pending_popups_.end()) return;
  owner_->AbortPopup(it->second);
  pending_popups_.erase(it);
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (role_ == BrowserRole::kShell) return false;
  // Alloy's default close targets the top-level parent. A tab must destroy
  // only its own child HWND, allowing OnBeforeClose to update the tab manager.
  // The Soulu host stays alive until every browser and cookie flush completes.
  const HWND child = browser->GetHost()->GetWindowHandle();
  if (child && IsWindow(child)) DestroyWindow(child);
  return true;
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (router_) {
    router_->RemoveHandler(bridge_.get());
    bridge_.reset();
    router_ = nullptr;
  }
  for (const auto& popup : pending_popups_) owner_->AbortPopup(popup.second);
  pending_popups_.clear();
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
  if (!loading && role_ != BrowserRole::kShell) owner_->ApplyContentTheme();
}

bool BrowserClient::OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefDownloadItem>,
                                     const CefString& suggested_name,
                                     CefRefPtr<CefBeforeDownloadCallback> callback) {
  const std::string site=browser->GetMainFrame()->GetURL();
  if(!owner_->AllowSite(tab_id_,site,"downloads")) {
    MessageBoxW(owner_->hwnd(),L"Загрузка заблокирована правилом сайта. Изменить правило можно в настройках сайтов.",L"Soulu",MB_OK|MB_ICONINFORMATION);
    return true;
  }
  callback->Continue(suggested_name, true);
  return true;
}

void BrowserClient::OnDownloadUpdated(CefRefPtr<CefBrowser>,
                                      CefRefPtr<CefDownloadItem> item,
                                      CefRefPtr<CefDownloadItemCallback>) {
  owner_->UpdateDownload(tab_id_,item);
}
}

