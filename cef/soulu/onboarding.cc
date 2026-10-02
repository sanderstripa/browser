#include "examples/soulu/browser_window.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include <shellapi.h>
#include <algorithm>
#include <functional>

namespace soulu {
namespace {
class OnboardingTask final : public CefTask {
 public:
  explicit OnboardingTask(std::function<void()> fn):fn_(std::move(fn)){}
  void Execute() override {fn_();}
 private:
  std::function<void()> fn_;
  IMPLEMENT_REFCOUNTING(OnboardingTask);
};
CefRefPtr<CefValue> Value(CefRefPtr<CefDictionaryValue> d) {
  auto v=CefValue::Create();v->SetDictionary(d);return v;
}
bool RegisterDefaultBrowser() {
  wchar_t path[32768]={};if(!GetModuleFileNameW(nullptr,path,32768))return false;
  const std::wstring exe=path,command=L"\""+exe+L"\" \"%1\"";
  auto set=[](const wchar_t* key,const wchar_t* name,const std::wstring& value){
    HKEY handle=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,key,0,nullptr,0,KEY_SET_VALUE,nullptr,&handle,nullptr)!=ERROR_SUCCESS)return false;
    const bool ok=RegSetValueExW(handle,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS;
    RegCloseKey(handle);return ok;
  };
  return set(L"Software\\Classes\\Soulu.Url",nullptr,L"Soulu Web Link")&&
      set(L"Software\\Classes\\Soulu.Url",L"URL Protocol",L"")&&
      set(L"Software\\Classes\\Soulu.Url\\DefaultIcon",nullptr,exe+L",0")&&
      set(L"Software\\Classes\\Soulu.Url\\shell\\open\\command",nullptr,command)&&
      set(L"Software\\Soulu\\Capabilities",L"ApplicationName",L"Soulu")&&
      set(L"Software\\Soulu\\Capabilities",L"ApplicationDescription",L"Soulu browser")&&
      set(L"Software\\Soulu\\Capabilities",L"ApplicationIcon",exe+L",0")&&
      set(L"Software\\Soulu\\Capabilities\\URLAssociations",L"http",L"Soulu.Url")&&
      set(L"Software\\Soulu\\Capabilities\\URLAssociations",L"https",L"Soulu.Url")&&
      set(L"Software\\RegisteredApplications",L"Soulu",L"Software\\Soulu\\Capabilities");
}
}
void BrowserWindow::OpenExternal(const std::string& url) {
  if(!current_)return;
  CefURLParts parts;
  if(!CefParseURL(url,parts)||!((CefString(&parts.scheme)=="http")||(CefString(&parts.scheme)=="https"))||
      CefString(&parts.host).empty()||!CefString(&parts.username).empty()||!CefString(&parts.password).empty())return;
  if(current_->NeedsOnboarding())current_->pending_external_url_=url;
  else current_->NewTab(url);
  ShowWindow(current_->hwnd_,SW_RESTORE);SetForegroundWindow(current_->hwnd_);
}
bool BrowserWindow::IsOnboardingUi(const std::string& url) const {
  return url==InternalUrl("soulu://onboarding");
}
CefRefPtr<CefDictionaryValue> BrowserWindow::OnboardingState() const {
  auto flow=settings_->GetDictionary("onboarding");
  return flow?flow->Copy(false):CefDictionaryValue::Create();
}
bool BrowserWindow::NeedsOnboarding() const {
  return OnboardingState()->GetString("status")=="not_started";
}
bool BrowserWindow::SaveOnboarding(CefRefPtr<CefDictionaryValue> flow) {
  auto next=settings_->Copy(false);next->SetDictionary("onboarding",flow->Copy(false));
  if(!WriteJson(ProfileRoot(active_profile_id_)/L"soulu-settings.json",Value(next)))return false;
  settings_=next;return true;
}
void BrowserWindow::HandleOnboardingBridge(int id,const std::string& request,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  auto* tab=FindTab(id);
  if(!tab||!tab->browser||tab->incognito||tab->profile_id!=active_profile_id_||
      id!=active_tab_id_||!IsOnboardingUi(tab->browser->GetMainFrame()->GetURL())||!NeedsOnboarding()) {
    callback->Failure(403,"Active normal first-run document required");return;
  }
  auto json=CefParseJSON(request,JSON_PARSER_RFC);
  if(!json||json->GetType()!=VTYPE_DICTIONARY){callback->Failure(400,"Invalid request");return;}
  auto root=json->GetDictionary();const std::string action=root->GetString("action");
  auto payload=root->GetDictionary("payload");auto flow=OnboardingState();
  auto save=[&](){if(!SaveOnboarding(flow)){callback->Failure(500,"Unable to save first-run progress");return false;}return true;};
  if(action=="onboarding.get") {
    auto result=CefDictionaryValue::Create();result->SetDictionary("flow",flow);
    auto browsers=DiscoverImportBrowsers(),sources=DiscoverPasswordSources(),eligible=CefListValue::Create();
    for(size_t i=0;i<sources->GetSize();++i){auto source=sources->GetDictionary(i);
      for(size_t n=0;n<browsers->GetSize();++n){auto browser=browsers->GetDictionary(n);
        if(browser->GetBool("installed")&&browser->GetString("browser")==source->GetString("browser")){
          auto row=source->Copy(false);row->SetString("icon",browser->GetString("icon"));
          eligible->SetDictionary(eligible->GetSize(),row);break;
        }}
    }
    result->SetList("sources",eligible);result->SetDictionary("policy",policies_[active_profile_id_]->Snapshot());
    Reply(callback,result);return;
  }
  if(action=="onboarding.progress") {
    if(!payload){callback->Failure(400,"Progress required");return;}
    const int step=payload->GetInt("step");
    if(step<1||step>6){callback->Failure(400,"Invalid step");return;}
    // Only draft choices are writable from JS; outcomes belong to native code.
    flow->SetInt("step",step);
    for(const auto* key:{"source","passwords","adblock","askPermissions"})
      if(payload->HasKey(key)){
        if((std::string(key)=="source"&&payload->GetType(key)!=VTYPE_STRING)||
           (std::string(key)!="source"&&payload->GetType(key)!=VTYPE_BOOL)){
          callback->Failure(400,"Invalid choice");return;
        }
        flow->SetValue(key,payload->GetValue(key)->Copy());
      }
    if(save())Reply(callback,flow);return;
  }
  if(action=="onboarding.import") {
    if(importing_){callback->Failure(409,"Import in progress");return;}
    if(flow->GetDictionary("importReport")){Reply(callback,flow);return;}
    // Revalidate detected ∩ supported at the time of the operation.
    const std::string source=flow->GetString("source");bool eligible=false;
    auto sources=DiscoverPasswordSources(),browsers=DiscoverImportBrowsers();
    for(size_t i=0;i<sources->GetSize();++i){auto row=sources->GetDictionary(i);
      if(row->GetString("id")!=source)continue;
      for(size_t n=0;n<browsers->GetSize();++n){auto b=browsers->GetDictionary(n);
        if(b->GetBool("installed")&&b->GetString("browser")==row->GetString("browser"))eligible=true;}}
    if(!eligible||!flow->GetBool("passwords")){callback->Failure(400,"Select an available password source");return;}
    // Persist intent before importing. A crash never automatically retries it.
    if(flow->GetBool("importStarted")){callback->Failure(409,"Previous import may have completed. Continue or use Settings to retry.");return;}
    flow->SetBool("importStarted",true);if(!save())return;
    importing_=true;const std::string target=active_profile_id_;CefRefPtr<BrowserWindow> self=this;
    CefPostTask(TID_FILE_BACKGROUND,new OnboardingTask([self,callback,target,source](){
      auto report=ImportPasswords(source,target);
      CefPostTask(TID_UI,new OnboardingTask([self,callback,target,report](){
        // Profile switches cannot redirect completion into another profile.
        auto config=ReadJson(ProfileRoot(target)/L"soulu-settings.json");
        bool saved=false;
        if(config&&config->GetType()==VTYPE_DICTIONARY){auto d=config->GetDictionary()->Copy(false);
          auto f=d->GetDictionary("onboarding");
          if(f){f->SetDictionary("importReport",report->Copy(false));saved=WriteJson(ProfileRoot(target)/L"soulu-settings.json",Value(d));
            if(saved&&target==self->active_profile_id_)self->settings_=d;}}
        self->importing_=false;
        if(saved)self->Reply(callback,report);else callback->Failure(500,"Import ran but its report could not be saved. Continue without retrying.");
        if(self->close_after_import_)self->CloseAll();
      }));
    }));return;
  }
  if(importing_){callback->Failure(409,"Wait for import to finish");return;}
  if(action=="onboarding.privacy") {
    auto policy=policies_[active_profile_id_];
    bool ok=policy->SetBlocking("",flow->GetBool("adblock")?1:0);
    // Off means deny sensitive access, never blanket allow.
    for(const auto* name:{"camera","microphone","geolocation","notifications"})
      ok=policy->Set("",name,flow->GetBool("askPermissions")?1:2)&&ok;
    if(!ok){callback->Failure(500,"Some privacy settings could not be saved. Review Settings.");return;}
    flow->SetBool("privacyApplied",true);if(save())Reply(callback,flow);return;
  }
  if(action=="onboarding.vpn") {
    if(!payload||payload->GetString("link").empty()||payload->GetString("link").size()>16384){callback->Failure(400,"VPN key required");return;}
    auto native=CefDictionaryValue::Create();native->SetString("action","save_profile");
    native->SetString("id",flow->GetString("vpnId"));native->SetString("name","Soulu VPN");
    native->SetString("url",payload->GetString("link"));
    // The existing helper is the authority for protocol validation and storage.
    auto saved=SendVpnHelper(native);
    if(!saved->GetBool("ok")){Reply(callback,saved);return;}
    std::string vpnId=saved->GetString("id");if(vpnId.empty())vpnId=saved->GetString("profileId");
    vpn_settings_->SetString("lastProfileId",vpnId);
    for(const auto* key:{"link","protocol","address","region"})vpn_settings_->SetString(key,payload->GetString(key));
    SaveSettings();flow->SetBool("vpnAdded",true);flow->SetString("vpnId",vpnId);
    if(save())Reply(callback,saved);return;
  }
  if(action=="onboarding.default") {
    auto result=CefDictionaryValue::Create();
    if(!RegisterDefaultBrowser()){callback->Failure(500,"Unable to register Soulu with Windows Default Apps");return;}
    SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
    const auto opened=reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd_,L"open",L"ms-settings:defaultapps?registeredAppUser=Soulu",nullptr,nullptr,SW_SHOWNORMAL));
    result->SetBool("ok",opened>32);result->SetString("message","Выбери Soulu в стандартных приложениях Windows и назначь его для HTTP и HTTPS.");
    Reply(callback,result);return;
  }
  if(action=="onboarding.finish") {
    flow->SetString("status",payload&&payload->GetBool("skip")?"skipped":"completed");
    if(!save())return;
    const auto destination=pending_external_url_.empty()?PageUrl("startup",settings_):pending_external_url_;
    pending_external_url_.clear();
    tab->browser->GetMainFrame()->LoadURL(InternalUrl(destination));EmitState();ReplyEmpty(callback);return;
  }
  callback->Failure(400,"Unknown onboarding action");
}
}
