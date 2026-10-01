#include "examples/soulu/profile_data.h"
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "include/cef_parser.h"
#include "include/internal/cef_types.h"

namespace soulu {
namespace {
CefRefPtr<CefValue> Value(CefRefPtr<CefListValue> rows) {
  auto v=CefValue::Create(); v->SetList(rows); return v;
}
CefRefPtr<CefValue> Value(CefRefPtr<CefDictionaryValue> row) {
  auto v=CefValue::Create(); v->SetDictionary(row); return v;
}
std::string Now() {
  return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
}
bool Secret(const std::string& input, const std::string& entropy,
            bool encrypt, std::string& output) {
  DATA_BLOB in={static_cast<DWORD>(input.size()),
    reinterpret_cast<BYTE*>(const_cast<char*>(input.data()))};
  DATA_BLOB extra={static_cast<DWORD>(entropy.size()),
    reinterpret_cast<BYTE*>(const_cast<char*>(entropy.data()))}, out={};
  bool ok=encrypt ? CryptProtectData(&in,L"Soulu credential",&extra,nullptr,
      nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)!=0 :
    CryptUnprotectData(&in,nullptr,&extra,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&out)!=0;
  if(ok) output.assign(reinterpret_cast<char*>(out.pbData),out.cbData);
  if(out.pbData) { SecureZeroMemory(out.pbData,out.cbData); LocalFree(out.pbData); }
  return ok;
}
bool PermissionName(const std::string& name) {
  static const std::string names[]={"geolocation","camera","microphone",
    "notifications","sound","popups","downloads"};
  return std::find(std::begin(names),std::end(names),name)!=std::end(names);
}
}

std::filesystem::path DataRoot() {
  wchar_t appdata[32768]={};
  auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",appdata,32768);
  if(!n || n>=32768) throw std::runtime_error("LOCALAPPDATA is unavailable");
  return std::filesystem::weakly_canonical(std::filesystem::path(appdata)/L"Soulu"/L"User Data");
}
bool ValidProfileId(const std::string& id) {
  if(id.empty() || id.size()>80 || id=="cef" || id=="__incognito__") return false;
  if(id=="con"||id=="prn"||id=="aux"||id=="nul"||
     (id.size()==4&&(id.rfind("com",0)==0||id.rfind("lpt",0)==0)&&id[3]>='1'&&id[3]<='9'))return false;
  return std::all_of(id.begin(),id.end(),[](unsigned char c){
    return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='-';
  });
}
std::filesystem::path ProfileRoot(const std::string& id) {
  if(!ValidProfileId(id)) throw std::runtime_error("Invalid profile ID");
  auto parent=DataRoot()/L"Profiles";
  auto candidate=parent/std::filesystem::u8path(id);
  auto attributes=GetFileAttributesW(candidate.c_str());
  if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))
    throw std::runtime_error("Profile directory is a reparse point");
  // Refuse symlinks/junctions redirecting profile data outside this parent.
  auto result=std::filesystem::weakly_canonical(candidate);
  if(result.parent_path()!=std::filesystem::weakly_canonical(parent))
    throw std::runtime_error("Profile directory redirects outside profile root");
  result.make_preferred(); return result;
}
std::string RandomId() {
  unsigned char bytes[16]={};
  if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)
    throw std::runtime_error("Random generator unavailable");
  const char* hex="0123456789abcdef"; std::string id;
  for(auto b:bytes) { id+=hex[b>>4];id+=hex[b&15]; } return id;
}
std::string WebOrigin(const std::string& input) {
  CefURLParts parts;
  if(!CefParseURL(input,parts)) return "";
  std::string scheme=CefString(&parts.scheme),host=CefString(&parts.host),port=CefString(&parts.port);
  std::transform(scheme.begin(),scheme.end(),scheme.begin(),::tolower);
  std::transform(host.begin(),host.end(),host.begin(),::tolower);
  if((scheme!="http"&&scheme!="https")||host.empty())return "";
  if((scheme=="http"&&port=="80")||(scheme=="https"&&port=="443"))port.clear();
  return scheme+"://"+host+(port.empty()?"":":"+port);
}
std::string SiteDomain(const std::string& input) {
  std::string url=input.find("://")==std::string::npos?"https://"+input:input;
  if(WebOrigin(url).empty()) return "";
  CefURLParts parts; if(!CefParseURL(url,parts)) return "";
  std::string host=CefString(&parts.host);
  std::transform(host.begin(),host.end(),host.begin(),::tolower);
  while(!host.empty()&&host.back()=='.')host.pop_back();
  return host;
}
CefRefPtr<CefValue> ReadJson(const std::filesystem::path& path) {
  std::error_code error;
  if(std::filesystem::file_size(path,error)>32*1024*1024 || error)return nullptr;
  std::ifstream file(path,std::ios::binary);std::stringstream text;text<<file.rdbuf();
  return CefParseJSON(text.str(),JSON_PARSER_RFC);
}
bool WriteJson(const std::filesystem::path& path,CefRefPtr<CefValue> value) {
  std::error_code error;std::filesystem::create_directories(path.parent_path(),error);
  if(error)return false;
  auto tmp=path;tmp+=L".new";
  {std::ofstream f(tmp,std::ios::binary|std::ios::trunc);
    f<<CefWriteJSON(value,JSON_WRITER_PRETTY_PRINT);f.flush();if(!f)return false;}
  return MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
PasswordVault::PasswordVault(const std::string& profile):profile_(profile),
  path_(ProfileRoot(profile)/L"soulu-passwords.json"),rows_(CefListValue::Create()) {
  if(!std::filesystem::exists(path_))return;
  auto saved=ReadJson(path_);
  healthy_=saved && saved->GetType()==VTYPE_LIST;
  if(healthy_)rows_=saved->GetList()->Copy();
}
CefRefPtr<CefListValue> PasswordVault::List() const {
  auto list=CefListValue::Create();
  for(size_t i=0;i<rows_->GetSize();++i){auto row=rows_->GetDictionary(i);if(!row)continue;
    auto copy=row->Copy(false);copy->Remove("secret");list->SetDictionary(list->GetSize(),copy);}
  return list;
}
bool PasswordVault::Contains(const std::string& origin,const std::string& username) const {
  for(size_t i=0;i<rows_->GetSize();++i){auto r=rows_->GetDictionary(i);
    if(r&&r->GetString("origin")==origin&&r->GetString("username")==username)return true;}
  return false;
}
bool PasswordVault::Put(const std::string& url,const std::string& username,
                        const std::string& password,bool replace) {
  auto origin=WebOrigin(url);
  if(!healthy_||origin.empty()||username.size()>4096||password.empty()||password.size()>16384)return false;
  auto next=rows_->Copy();size_t pos=next->GetSize();auto row=CefDictionaryValue::Create();
  for(size_t i=0;i<next->GetSize();++i){auto r=next->GetDictionary(i);
    if(r&&r->GetString("origin")==origin&&r->GetString("username")==username){
      if(!replace)return false;pos=i;row=r->Copy(false);break;}}
  if(!row->HasKey("id")){row->SetString("id",RandomId());row->SetString("created",Now());}
  const std::string id=row->GetString("id");std::string encrypted;
  if(!Secret(password,"Soulu/v1/"+profile_+"/"+id,true,encrypted))return false;
  row->SetString("secret",CefBase64Encode(encrypted.data(),encrypted.size()));
  row->SetString("profileId",profile_);row->SetString("origin",origin);
  row->SetString("username",username);row->SetString("updated",Now());
  next->SetDictionary(pos,row);
  if(!WriteJson(path_,Value(next)))return false;rows_=next;return true;
}
bool PasswordVault::Remove(const std::string& id) {
  if(!healthy_)return false;auto next=rows_->Copy();
  for(size_t i=0;i<next->GetSize();++i){auto r=next->GetDictionary(i);if(r&&r->GetString("id")==id){
    next->Remove(i);if(!WriteJson(path_,Value(next)))return false;rows_=next;return true;}}return false;
}
bool PasswordVault::Reveal(const std::string& id,std::string& output) const {
  for(size_t i=0;i<rows_->GetSize();++i){auto r=rows_->GetDictionary(i);if(!r||r->GetString("id")!=id)continue;
    auto data=CefBase64Decode(r->GetString("secret"));if(!data)return false;
    std::string blob(data->GetSize(),'\0');data->GetData(blob.data(),blob.size(),0);
    return Secret(blob,"Soulu/v1/"+profile_+"/"+id,false,output);}return false;
}

SitePolicy::SitePolicy(const std::string& profile):data_(CefDictionaryValue::Create()) {
  if(profile!="__incognito__")path_=ProfileRoot(profile)/L"soulu-site-rules.json";
  auto saved=path_.empty()?nullptr:ReadJson(path_);
  if(saved&&saved->GetType()==VTYPE_DICTIONARY)data_=saved->GetDictionary()->Copy(false);
  if(!data_->GetDictionary("defaults")) {
    auto defaults=CefDictionaryValue::Create();
    for(auto name:{"geolocation","camera","microphone","notifications","popups"})defaults->SetInt(name,1);
    defaults->SetInt("downloads",0);defaults->SetInt("sound",0);data_->SetDictionary("defaults",defaults);
  }
  if(!data_->GetDictionary("sites"))data_->SetDictionary("sites",CefDictionaryValue::Create());
  if(!data_->GetDictionary("blocking")){
    auto b=CefDictionaryValue::Create();b->SetBool("enabled",false);
    b->SetDictionary("sites",CefDictionaryValue::Create());data_->SetDictionary("blocking",b);}
}
int SitePolicy::Rule(const std::string& url,const std::string& name) const {
  std::lock_guard lock(mutex_);auto site=data_->GetDictionary("sites")->GetDictionary(SiteDomain(url));
  int value=site&&site->HasKey(name)?site->GetInt(name):data_->GetDictionary("defaults")->GetInt(name);
  return value>=0&&value<=2?value:2;
}
bool SitePolicy::Blocking(const std::string& url) const {
  std::lock_guard lock(mutex_);auto b=data_->GetDictionary("blocking");
  auto sites=b->GetDictionary("sites");auto domain=SiteDomain(url);
  return sites->HasKey(domain)?sites->GetBool(domain):b->GetBool("enabled");
}
CefRefPtr<CefDictionaryValue> SitePolicy::Snapshot() const {
  std::lock_guard lock(mutex_);return data_->Copy(false);
}
bool SitePolicy::Save() const {return path_.empty()||WriteJson(path_,Value(data_));}
bool SitePolicy::Set(const std::string& input,const std::string& name,int value) {
  if(!PermissionName(name)||value< -1||value>2||(input.empty()&&value<0))return false;
  auto domain=input.empty()?"":SiteDomain(input);if(!input.empty()&&domain.empty())return false;
  std::lock_guard lock(mutex_);auto old=data_->Copy(false);
  auto rules=data_->GetDictionary("defaults");
  if(!domain.empty()){auto sites=data_->GetDictionary("sites");rules=sites->GetDictionary(domain);
    if(!rules){rules=CefDictionaryValue::Create();sites->SetDictionary(domain,rules);
      // SetDictionary transfers ownership and invalidates a standalone input.
      rules=sites->GetDictionary(domain);}}
  if(value<0)rules->Remove(name);else rules->SetInt(name,value);
  if(!domain.empty()&&rules->GetSize()==0)data_->GetDictionary("sites")->Remove(domain);
  if(Save())return true;data_=old;return false;
}
bool SitePolicy::SetBlocking(const std::string& input,int value) {
  auto domain=input.empty()?"":SiteDomain(input);if((!input.empty()&&domain.empty())||value<0||value>2)return false;
  std::lock_guard lock(mutex_);auto old=data_->Copy(false);auto b=data_->GetDictionary("blocking");
  if(domain.empty())b->SetBool("enabled",value==1);
  else if(value==2)b->GetDictionary("sites")->Remove(domain);
  else b->GetDictionary("sites")->SetBool(domain,value==1);
  if(Save())return true;data_=old;return false;
}
bool SitePolicy::Reset(const std::string& input) {
  auto domain=input.empty()?"":SiteDomain(input);if(!input.empty()&&domain.empty())return false;
  std::lock_guard lock(mutex_);auto old=data_->Copy(false);
  auto sites=data_->GetDictionary("sites");if(domain.empty())sites->Clear();else sites->Remove(domain);
  if(Save())return true;data_=old;return false;
}
bool SitePolicy::ResetSite(const std::string& input) {
  auto domain=SiteDomain(input);if(domain.empty())return false;
  std::lock_guard lock(mutex_);auto old=data_->Copy(false);
  data_->GetDictionary("sites")->Remove(domain);
  data_->GetDictionary("blocking")->GetDictionary("sites")->Remove(domain);
  if(Save())return true;data_=old;return false;
}
bool BlockResource(const std::string& top,const std::string& url,int type,bool enabled) {
  if(!enabled||WebOrigin(top).empty()||WebOrigin(url).empty())return false;
  // Conservative host-only subset. Never filter documents, XHR, downloads,
  // WebSockets or worker requests; no path/substring guesses in auth flows.
  if(type!=RT_SCRIPT&&type!=RT_IMAGE&&type!=RT_STYLESHEET&&type!=RT_FONT_RESOURCE)return false;
  auto host=SiteDomain(url),site=SiteDomain(top);if(host.empty()||site.empty()||host==site)return false;
  static const std::string blocked[]={"doubleclick.net","googlesyndication.com",
    "googleadservices.com","adnxs.com","adsrvr.org","advertising.com","criteo.com","criteo.net"};
  for(const auto& domain:blocked)if(host==domain ||
    (host.size()>domain.size()&&host.ends_with("."+domain)))return true;
  return false;
}

std::vector<std::string> PendingProfileDeletions() {
  std::vector<std::string> result;auto pending=ReadJson(DataRoot()/L"soulu-delete-profiles.json");
  auto profiles=ReadJson(DataRoot()/L"profiles.json");
  if(!pending||pending->GetType()!=VTYPE_LIST||!profiles||profiles->GetType()!=VTYPE_LIST)return result;
  for(size_t i=0;i<pending->GetList()->GetSize();++i){
    const std::string id=pending->GetList()->GetString(i);if(!ValidProfileId(id))continue;
    bool exists=false;
    for(size_t n=0;n<profiles->GetList()->GetSize();++n){auto p=profiles->GetList()->GetDictionary(n);
      if(p&&p->GetString("id")==id)exists=true;}
    if(!exists)result.push_back(id);
  }return result;
}
void CleanupDeletedProfiles(const std::vector<std::string>& ids) {
  // Called after CefShutdown: no live Chromium profile databases remain.
  std::vector<std::string> retained;
  for(const auto& id:ids){
    try {auto root=ProfileRoot(id);std::error_code error;bool safe=true;
      if(std::filesystem::exists(root,error)){
        for(const auto& entry:std::filesystem::recursive_directory_iterator(root)){
          auto attributes=GetFileAttributesW(entry.path().c_str());
          if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)){safe=false;break;}}
        if(safe)std::filesystem::remove_all(root,error);
      }
      if(!safe||error)retained.push_back(id);
    }catch(const std::exception&){retained.push_back(id);}
  }
  // No CEF APIs after shutdown. IDs were validated before any path use.
  std::string json="[";
  for(const auto& id:retained){if(json.size()>1)json+=",";json+="\""+id+"\"";}json+="]";
  auto file=DataRoot()/L"soulu-delete-profiles.json",temp=file;temp+=L".new";
  {std::ofstream out(temp,std::ios::binary|std::ios::trunc);out<<json;out.flush();if(!out)return;}
  MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
}
