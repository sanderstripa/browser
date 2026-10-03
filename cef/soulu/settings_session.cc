#include "examples/soulu/browser_window.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>

namespace soulu {
namespace {
CefRefPtr<CefValue> Value(CefRefPtr<CefDictionaryValue> d) {
  auto v=CefValue::Create();v->SetDictionary(d);return v;
}
bool Same(CefRefPtr<CefDictionaryValue> a,CefRefPtr<CefDictionaryValue> b) {
  return a&&b&&a->IsEqual(b);
}
bool PreviewKey(const std::string& key) {
  return key=="theme"||key=="mattePanel"||key=="layout"||key=="language"||
    key=="addressPosition"||key=="downloadsMode"||key=="extensionsPosition"||
    key=="vpnToolbarVisible"||key=="showSidebar"||key=="showBack"||
    key=="showFavorites"||key=="showNewTab"||key=="showDownloads";
}
bool ValidReader(CefRefPtr<CefDictionaryValue> d) {
  if(!d)return false;
  const std::string theme=d->GetString("theme"),font=d->GetString("font");
  return (theme=="light"||theme=="sepia"||theme=="dark")&&
    (font=="system"||font=="sans"||font=="serif")&&d->GetType("images")==VTYPE_BOOL&&
    d->GetType("size")==VTYPE_INT&&d->GetInt("size")>=14&&d->GetInt("size")<=32&&
    d->GetType("width")==VTYPE_INT&&d->GetInt("width")>=0&&d->GetInt("width")<=2&&
    d->GetType("spacing")==VTYPE_INT&&d->GetInt("spacing")>=0&&d->GetInt("spacing")<=2;
}
}

CefRefPtr<CefDictionaryValue> BrowserWindow::EffectiveSettings() const {
  if(!settings_preview_||settings_profile_!=active_profile_id_)return settings_;
  auto next=settings_->Copy(false);CefDictionaryValue::KeyList keys;settings_preview_->GetKeys(keys);
  for(const auto& key:keys)next->SetValue(key,settings_preview_->GetValue(key)->Copy());
  return next;
}
CefRefPtr<CefDictionaryValue> BrowserWindow::SettingsSnapshot() {
  auto data=CefDictionaryValue::Create();
  data->SetString("profile",active_profile_id_);
  data->SetDictionary("settings",settings_->Copy(false));
  auto policy=policies_[active_profile_id_];
  if(!policy)policies_[active_profile_id_]=policy=std::make_shared<SitePolicy>(active_profile_id_);
  data->SetDictionary("rules",policy->Snapshot());
  Tab profile;profile.profile_id=active_profile_id_;
  data->SetDictionary("reader",ReaderPreferences(profile));
  data->SetDictionary("vpn",vpn_settings_->Copy(false));
  return data;
}
void BrowserWindow::ResetSettingsPreview() {
  settings_preview_=nullptr;ApplyWindowAppearance();ApplyContentTheme();Layout();EmitState();
}
bool BrowserWindow::GuardSettingsClose(int id,bool all) {
  if(!settings_dirty_||!settings_tab_||(!all&&id!=settings_tab_))return false;
  auto* tab=FindTab(settings_tab_);if(!tab||!tab->browser)return false;
  settings_close_all_=all;
  tab->browser->GetMainFrame()->ExecuteJavaScript(
    "window.souluSettingsRequestClose&&window.souluSettingsRequestClose()",tab->browser->GetMainFrame()->GetURL(),0);
  SwitchTab(settings_tab_);return true;
}
bool BrowserWindow::GuardSettingsNavigation(int id,const std::string& url) {
  if(id!=settings_tab_)return false;
  if(settings_dirty_){settings_pending_url_=url;return GuardSettingsClose(id);}
  ResetSettingsPreview();settings_tab_=0;settings_loaded_=nullptr;settings_staged_=nullptr;
  return false;
}

// Each canonical store commits atomically through its existing writer. If a
// later group fails, the reply includes the actual persisted snapshot; the UI
// keeps the remaining draft instead of claiming success or losing edits.
bool BrowserWindow::ApplySettingsSession(std::string& error) {
  if(!settings_loaded_||!settings_staged_||settings_profile_!=active_profile_id_) {
    error="Профиль изменился. Откройте настройки заново.";return false;
  }
  auto config=settings_staged_->GetDictionary("settings");
  auto reader=settings_staged_->GetDictionary("reader");
  auto rules=settings_staged_->GetDictionary("rules");
  auto vpn=settings_staged_->GetDictionary("vpn");
  if(!config||!rules||!vpn||!ValidatePagePatch(config)||!ValidReader(reader)) {
    error="Проверьте адреса страниц и настройки чтения.";return false;
  }
  // Rebase only edited keys on the live dictionary, retaining onboarding and
  // home data changed outside this view. Refuse concurrent edits to a key.
  auto next=settings_->Copy(false),base=settings_loaded_->GetDictionary("settings");
  CefDictionaryValue::KeyList keys;config->GetKeys(keys);
  for(const auto& key:keys){auto changed=config->GetValue(key),before=base->GetValue(key);
    if(before&&before->IsEqual(changed))continue;
    auto current=settings_->GetValue(key);
    if(before&&current&&!before->IsEqual(current)&&!current->IsEqual(changed)){
      error="Настройка изменена в другом окне: "+key.ToString();return false;}
    if(!current||current->GetType()!=changed->GetType()){
      error="Некорректное значение: "+key.ToString();return false;}
    next->SetValue(key,changed->Copy());
  }
  const std::pair<const char*,std::vector<std::string>> enums[]={
    {"theme",{"system","light","dark"}},{"language",{"ru","en"}},
    {"layout",{"compact","classic"}},{"searchEngine",{"google","yandex","bing","duckduckgo"}},
    {"addressPosition",{"left","center"}},{"extensionsPosition",{"left","right"}},
    {"downloadsMode",{"always","dynamic"}},{"addressOpenMode",{"current","newIfOccupied"}},
    {"bookmarksBarMode",{"always","newTab","home","auto","never"}},
    {"bookmarksBarPosition",{"above","hidden"}}};
  for(const auto& [key,allowed]:enums){const std::string value=next->GetString(key);
    if(std::find(allowed.begin(),allowed.end(),value)==allowed.end()){
      error="Некорректное значение: "+std::string(key);return false;}}
  auto homePatch=CefDictionaryValue::Create();
  for(const char* key:{"homeShortcuts","homeWeatherCity","homeShowLogo","homeShowSearch",
      "homeShowWeather","homeShowShortcuts","homeShowBackground"}){
    auto changed=config->GetValue(key),before=base->GetValue(key);
    if(changed&&(!before||!before->IsEqual(changed)))homePatch->SetValue(key,changed->Copy());
  }
  if(!ValidateHomePatch(homePatch,next,error))return false;
  for(const char* kind:{"startup","newTab","home"}){
    const std::string mode=std::string(kind)+"Mode",url=std::string(kind)+"Url";
    if(next->GetString(mode)=="custom"&&next->GetString(url).empty()){
      error="Укажите HTTP/HTTPS-адрес страницы.";return false;
    }
  }
  const auto download=next->GetString("downloadPath").ToWString();
  std::error_code folderError;
  if(!download.empty()&&!std::filesystem::is_directory(download,folderError)){
    error="Папка загрузок не существует или недоступна.";return false;
  }
  if(!Same(next,settings_)){
    if(!WriteJson(ProfileRoot(settings_profile_)/L"soulu-settings.json",Value(next))){
      error="Не удалось сохранить настройки профиля.";return false;}
    settings_=next;
  }
  auto policy=policies_[settings_profile_];
  if(!Same(rules,settings_loaded_->GetDictionary("rules"))){
    if(!Same(policy->Snapshot(),settings_loaded_->GetDictionary("rules"))&&!Same(policy->Snapshot(),rules)){
      error="Веб-сайты: правила изменены в другом окне.";return false;}
    if(!policy->Replace(rules)){error="Веб-сайты: правила не сохранены.";return false;}
    ApplySiteSound();
    for(const auto& tab:tabs_)if(tab.profile_id==settings_profile_&&!tab.incognito)SyncSitePolicy(tab.id,tab.url);
  }
  if(!Same(reader,settings_loaded_->GetDictionary("reader"))){
    auto live=reader_preferences_[settings_profile_];
    if(live&&!Same(live,settings_loaded_->GetDictionary("reader"))&&!Same(live,reader)){
      error="Режим чтения: настройки изменены в другом окне.";return false;}
    if(!WriteJson(ProfileRoot(settings_profile_)/L"soulu-reader.json",Value(reader))){
      error="Режим чтения: настройки не сохранены.";return false;}
    reader_preferences_[settings_profile_]=reader->Copy(false);
  }
  if(!Same(vpn,settings_loaded_->GetDictionary("vpn"))){
    if(!Same(vpn_settings_,settings_loaded_->GetDictionary("vpn"))&&!Same(vpn_settings_,vpn)){
      error="VPN: конфигурация изменена в другом окне.";return false;}
    const std::string protocol=vpn->GetString("protocol"),link=vpn->GetString("link");
    if((protocol!="vless"&&protocol!="sudoku")||link.rfind(protocol+"://",0)!=0){
      error="VPN: проверьте ключ подключения.";return false;}
    auto command=CefDictionaryValue::Create();command->SetString("action","save_profile");
    command->SetString("id",vpn_settings_->GetString("lastProfileId"));
    command->SetString("name",vpn->GetString("region").empty()?"Soulu VPN":vpn->GetString("region"));
    command->SetString("country",vpn->GetString("region"));command->SetString("url",link);
    auto saved=SendVpnHelper(command);
    if(!saved->GetBool("ok")){error="VPN: "+saved->GetString("error").ToString();return false;}
    auto old=vpn_settings_;vpn_settings_=vpn->Copy(false);
    std::string id=saved->GetString("id");if(id.empty())id=saved->GetString("profileId");
    if(!id.empty())vpn_settings_->SetString("lastProfileId",id);
    if(!SaveSettings()){vpn_settings_=old;error="VPN: не удалось сохранить конфигурацию Soulu.";return false;}
  }
  return true;
}

bool BrowserWindow::HandleSettingsBridge(int id,const std::string& request,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback) {
  auto parsed=CefParseJSON(request,JSON_PARSER_RFC);
  if(!parsed||parsed->GetType()!=VTYPE_DICTIONARY)return false;
  auto root=parsed->GetDictionary();const std::string action=root->GetString("action");
  if(action.rfind("settings.",0)!=0)return false;
  auto* tab=FindTab(id);
  if(!tab||!tab->browser||tab->incognito||tab->profile_id!=active_profile_id_||
      tab->browser->GetMainFrame()->GetURL().ToString().find("/ui/settings.html")==std::string::npos){
    callback->Failure(403,"Настройки доступны в обычном активном профиле.");return true;}
  auto payload=root->GetDictionary("payload");
  if(action=="settings.capabilities"){
    auto result=CefDictionaryValue::Create();bool is_default=true;
    for(const wchar_t* scheme:{L"http",L"https"}){
      wchar_t progid[256]={};DWORD size=sizeof(progid);
      const std::wstring key=std::wstring(L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\")+scheme+L"\\UserChoice";
      if(RegGetValueW(HKEY_CURRENT_USER,key.c_str(),L"ProgId",RRF_RT_REG_SZ,nullptr,progid,&size)!=ERROR_SUCCESS||
          std::wstring(progid)!=L"Soulu.Url")is_default=false;
    }
    result->SetBool("defaultBrowser",is_default);Reply(callback,result);return true;
  }
  if(action=="settings.begin"){
    if(settings_tab_&&settings_tab_!=id&&settings_dirty_){callback->Failure(409,"Закройте другое окно настроек.");return true;}
    ResetSettingsPreview();settings_tab_=id;settings_profile_=active_profile_id_;
    settings_dirty_=false;settings_loaded_=SettingsSnapshot();settings_staged_=settings_loaded_->Copy(false);
    Reply(callback,settings_loaded_->Copy(false));return true;
  }
  if(action=="settings.defaultBrowser"){
    if(!OpenDefaultBrowserSettings())callback->Failure(500,"Не удалось открыть настройки Windows.");else ReplyEmpty(callback);
    return true;
  }
  if(action=="settings.chooseFolder"){
    BROWSEINFOW info={};info.hwndOwner=hwnd_;info.lpszTitle=L"Папка загрузок";
    info.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    auto item=SHBrowseForFolderW(&info);wchar_t path[MAX_PATH]={};
    if(item){SHGetPathFromIDListW(item,path);CoTaskMemFree(item);}
    auto result=CefValue::Create();result->SetString(CefString(path));Reply(callback,result);return true;
  }
  if(action=="settings.openFolder"){
    const auto folder=settings_->GetString("downloadPath").ToWString();
    PWSTR standard=nullptr;std::wstring path=folder;
    if(path.empty()&&SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads,0,nullptr,&standard))){path=standard;CoTaskMemFree(standard);}
    if(path.empty()||!std::filesystem::is_directory(path)||reinterpret_cast<INT_PTR>(
       ShellExecuteW(hwnd_,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL))<=32)
      callback->Failure(500,"Не удалось открыть папку загрузок.");else ReplyEmpty(callback);
    return true;
  }
  if(id!=settings_tab_||settings_profile_!=active_profile_id_||!settings_loaded_){
    callback->Failure(409,"Откройте настройки заново.");return true;}
  if(action=="settings.stage"){
    if(!payload||!payload->GetDictionary("settings")||!payload->GetDictionary("rules")||
        !payload->GetDictionary("reader")||!payload->GetDictionary("vpn")){
      callback->Failure(400,"Некорректные настройки.");return true;}
    settings_staged_=payload->Copy(false);settings_dirty_=!Same(settings_staged_,settings_loaded_);
    settings_preview_=CefDictionaryValue::Create();auto config=payload->GetDictionary("settings");
    CefDictionaryValue::KeyList keys;config->GetKeys(keys);
    for(const auto& key:keys)if(PreviewKey(key))settings_preview_->SetValue(key,config->GetValue(key)->Copy());
    ApplyWindowAppearance();ApplyContentTheme();Layout();EmitState();ReplyEmpty(callback);return true;
  }
  if(action=="settings.apply"){
    std::string error;const bool ok=ApplySettingsSession(error);
    settings_loaded_=SettingsSnapshot();
    if(ok){settings_staged_=settings_loaded_->Copy(false);settings_dirty_=false;ResetSettingsPreview();RefreshHomePages();}
    else settings_dirty_=!Same(settings_staged_,settings_loaded_);
    auto result=CefDictionaryValue::Create();result->SetBool("ok",ok);result->SetString("error",error);
    result->SetDictionary("persisted",settings_loaded_->Copy(false));Reply(callback,result);return true;
  }
  if(action=="settings.cancel"){
    settings_loaded_=SettingsSnapshot();settings_staged_=settings_loaded_->Copy(false);settings_dirty_=false;
    ResetSettingsPreview();Reply(callback,settings_loaded_->Copy(false));return true;
  }
  if(action=="settings.close"){
    if(settings_dirty_){GuardSettingsClose(id);ReplyEmpty(callback);return true;}
    ResetSettingsPreview();const auto url=settings_pending_url_;const bool all=settings_close_all_;
    settings_tab_=0;settings_pending_url_.clear();settings_close_all_=false;
    ReplyEmpty(callback);
    if(all)CloseAll();else if(!url.empty())tab->browser->GetMainFrame()->LoadURL(url);else CloseTab(id);
    return true;
  }
  if(action=="settings.abortClose"){
    settings_pending_url_.clear();settings_close_all_=false;ReplyEmpty(callback);return true;
  }
  callback->Failure(400,"Неизвестное действие настроек.");return true;
}
}
