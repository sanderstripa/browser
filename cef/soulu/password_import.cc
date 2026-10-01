#include "examples/soulu/profile_data.h"
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "include/cef_parser.h"
#include "examples/soulu/third_party/sqlite3.h"

namespace soulu {
namespace {
struct Source {std::string id,browser,name;std::filesystem::path root,state;};
std::filesystem::path Environment(const wchar_t* name) {
  wchar_t buffer[32768]={};auto n=GetEnvironmentVariableW(name,buffer,32768);
  return n&&n<32768?std::filesystem::path(buffer):std::filesystem::path();
}
std::filesystem::path InstalledBrowser(const std::string& browser) {
  const std::wstring executable=browser=="Chrome"?L"chrome.exe":browser=="Edge"?L"msedge.exe":L"firefox.exe";
  for(auto hive:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE}){
    wchar_t buffer[32768]={};DWORD size=sizeof(buffer);
    auto key=L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\"+executable;
    if(RegGetValueW(hive,key.c_str(),nullptr,RRF_RT_REG_SZ,nullptr,buffer,&size)==ERROR_SUCCESS){
      std::filesystem::path path(buffer);std::error_code error;
      if(path.is_absolute()&&std::filesystem::is_regular_file(path,error))return path;
    }
  }
  const std::wstring relative=browser=="Chrome"?L"Google/Chrome/Application/chrome.exe":
    browser=="Edge"?L"Microsoft/Edge/Application/msedge.exe":L"Mozilla Firefox/firefox.exe";
  for(auto name:{L"ProgramW6432",L"PROGRAMFILES",L"PROGRAMFILES(X86)",L"LOCALAPPDATA"}){
    auto root=Environment(name);std::error_code error;
    if(!root.empty()&&std::filesystem::is_regular_file(root/relative,error))return root/relative;
  }return {};
}
std::vector<Source> Sources() {
  std::vector<Source> out;std::error_code error;
  auto local=Environment(L"LOCALAPPDATA"),roaming=Environment(L"APPDATA");
  if(local.empty()||roaming.empty())return out;
  for(auto browser:{std::string("Chrome"),std::string("Edge")}) {
    auto root=local/(browser=="Chrome"?L"Google/Chrome/User Data":L"Microsoft/Edge/User Data");
    if(!std::filesystem::is_directory(root,error))continue;
    auto state=ReadJson(root/L"Local State");CefRefPtr<CefDictionaryValue> cache;
    if(state&&state->GetType()==VTYPE_DICTIONARY){auto profile=state->GetDictionary()->GetDictionary("profile");
      if(profile)cache=profile->GetDictionary("info_cache");}
    for(const auto& entry:std::filesystem::directory_iterator(root,error)) {
      auto filename=entry.path().filename().string();
      if(filename!="Default"&&filename.rfind("Profile ",0)!=0)continue;
      if(!std::filesystem::is_regular_file(entry.path()/L"Login Data",error))continue;
      // Stable catalog IDs, not arbitrary paths accepted from the UI.
      std::string name=filename;
      if(cache){auto item=cache->GetDictionary(filename);if(item&&!item->GetString("name").empty())name=item->GetString("name");}
      out.push_back({browser+":"+filename,browser,name,entry.path(),root/L"Local State"});
    }
  }
  auto firefox=roaming/L"Mozilla/Firefox";
  std::ifstream ini(firefox/L"profiles.ini");std::string line,section;std::map<std::string,std::string> values;
  auto append=[&](){
    if(section.rfind("Profile",0)!=0||!values.count("Path"))return;
    auto path=std::filesystem::u8path(values["Path"]);
    if(values["IsRelative"]!="0")path=firefox/path;
    // Profiles may legitimately use an absolute path, but must be owned by
    // this user's registered profiles.ini and readable without elevation.
    if(!std::filesystem::is_regular_file(path/L"logins.json",error))return;
    out.push_back({"Firefox:"+section,"Firefox",values["Name"],path,{}});
  };
  while(std::getline(ini,line)){
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    if(line.size()>1&&line[0]=='['&&line.back()==']'){append();values.clear();section=line.substr(1,line.size()-2);}
    else {auto eq=line.find('=');if(eq!=std::string::npos)values[line.substr(0,eq)]=line.substr(eq+1);}}
  append();return out;
}

// Holds read handles denying write/delete while making one consistent snapshot.
// Source SQLite is never opened: it cannot create a source WAL/SHM or checkpoint.
class Snapshot {
 public:
  Snapshot():root_(Environment(L"TEMP")/std::filesystem::u8path("soulu-import-"+RandomId())) {
    if(root_.parent_path().empty())throw std::runtime_error("Temporary directory unavailable");
    std::filesystem::create_directories(root_);
  }
  ~Snapshot(){for(auto handle:handles_)CloseHandle(handle);std::error_code e;std::filesystem::remove_all(root_,e);}
  bool Copy(const std::filesystem::path& source,const std::vector<std::wstring>& files) {
    std::vector<std::pair<std::wstring,HANDLE>> opened;
    for(const auto& name:files){
      auto path=source/name;std::error_code e;
      if(!std::filesystem::exists(path,e))continue;
      auto h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
      if(h==INVALID_HANDLE_VALUE)return false;
      handles_.push_back(h);opened.push_back({name,h});
    }
    for(const auto& item:opened){
      LARGE_INTEGER size={};if(!GetFileSizeEx(item.second,&size)||size.QuadPart>256LL*1024*1024)return false;
      std::ofstream file(root_/item.first,std::ios::binary);char buffer[65536];DWORD count=0;
      while(ReadFile(item.second,buffer,sizeof(buffer),&count,nullptr)&&count){file.write(buffer,count);if(!file)return false;}
      if(!file||file.tellp()!=size.QuadPart)return false;
    }
    // Keep handles until import completes so browser writes cannot invalidate it.
    return true;
  }
  const std::filesystem::path& root() const{return root_;}
 private:
  std::filesystem::path root_;std::vector<HANDLE> handles_;
};
std::string Decode(const std::string& value) {
  auto bytes=CefBase64Decode(value);if(!bytes)return "";std::string result(bytes->GetSize(),'\0');
  bytes->GetData(result.data(),result.size(),0);return result;
}
void Wipe(std::string& value){if(!value.empty())SecureZeroMemory(value.data(),value.size());value.clear();}
bool Unprotect(const std::string& encrypted,std::string& result) {
  DATA_BLOB input={static_cast<DWORD>(encrypted.size()),reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.data()))},output={};
  bool ok=CryptUnprotectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)!=0;
  if(ok)result.assign(reinterpret_cast<char*>(output.pbData),output.cbData);
  if(output.pbData){SecureZeroMemory(output.pbData,output.cbData);LocalFree(output.pbData);}return ok;
}
bool ChromiumSecret(const std::string& blob,const std::string& key,std::string& result,bool& protected_record) {
  // v20 App-Bound Encryption and every unknown version are explicitly unsupported.
  if(blob.rfind("v20",0)==0||(blob.size()>2&&blob[0]=='v'&&blob.substr(0,3)!="v10"&&blob.substr(0,3)!="v11")){
    protected_record=true;return false;}
  if(blob.rfind("v10",0)!=0&&blob.rfind("v11",0)!=0)return Unprotect(blob,result);
  if(blob.size()<31||key.size()!=32){protected_record=true;return false;}
  BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_KEY_HANDLE handle=nullptr;
  bool ok=false;
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_AES_ALGORITHM,nullptr,0)>=0 &&
     BCryptSetProperty(algorithm,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
       sizeof(BCRYPT_CHAIN_MODE_GCM),0)>=0 &&
     BCryptGenerateSymmetricKey(algorithm,&handle,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())),32,0)>=0) {
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce=reinterpret_cast<PUCHAR>(const_cast<char*>(blob.data()+3));info.cbNonce=12;
    info.pbTag=reinterpret_cast<PUCHAR>(const_cast<char*>(blob.data()+blob.size()-16));info.cbTag=16;
    result.resize(blob.size()-31);ULONG count=0;
    ok=BCryptDecrypt(handle,reinterpret_cast<PUCHAR>(const_cast<char*>(blob.data()+15)),
      static_cast<ULONG>(blob.size()-31),&info,nullptr,0,reinterpret_cast<PUCHAR>(result.data()),
      static_cast<ULONG>(result.size()),&count,0)>=0;
    if(ok)result.resize(count);else Wipe(result);
  }
  if(handle)BCryptDestroyKey(handle);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);return ok;
}
// NSS is loaded only from an installed Firefox directory, with DLL dependencies
// restricted to that directory and System32. No PATH/current-directory lookup.
class Nss {
  struct Item {int type;unsigned char* data;unsigned int length;};
  using Init=int(*)(const char*);using Shutdown=int(*)();
  using Decrypt=int(*)(Item*,Item*,void*);using Free=void(*)(Item*,int);
 public:
  bool Open(const std::filesystem::path& profile) {
    auto firefox=InstalledBrowser("Firefox");if(firefox.empty())return false;
    auto dll=firefox.parent_path()/L"nss3.dll";
    library_=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!library_)return false;
    auto init=reinterpret_cast<Init>(GetProcAddress(library_,"NSS_Init"));
    shutdown_=reinterpret_cast<Shutdown>(GetProcAddress(library_,"NSS_Shutdown"));
    decrypt_=reinterpret_cast<Decrypt>(GetProcAddress(library_,"PK11SDR_Decrypt"));
    free_=reinterpret_cast<Free>(GetProcAddress(library_,"SECITEM_FreeItem"));
    auto path="sql:"+CefString(profile.wstring()).ToString();
    active_=init&&shutdown_&&decrypt_&&free_&&init(path.c_str())==0;return active_;
  }
  ~Nss(){if(active_)shutdown_();if(library_)FreeLibrary(library_);}
  bool Read(const std::string& encoded,std::string& result) {
    if(!active_)return false;auto bytes=Decode(encoded);if(bytes.empty())return false;
    Item input={0,reinterpret_cast<unsigned char*>(bytes.data()),static_cast<unsigned int>(bytes.size())},output={};
    bool ok=decrypt_(&input,&output,nullptr)==0;
    if(ok)result.assign(reinterpret_cast<char*>(output.data),output.length);
    if(output.data){SecureZeroMemory(output.data,output.length);free_(&output,0);}return ok;
  }
 private:
  HMODULE library_=nullptr;Shutdown shutdown_=nullptr;Decrypt decrypt_=nullptr;Free free_=nullptr;bool active_=false;
};
}

CefRefPtr<CefListValue> DiscoverPasswordSources() {
  auto rows=CefListValue::Create();
  for(const auto& source:Sources()){
    auto row=CefDictionaryValue::Create();row->SetString("id",source.id);
    row->SetString("browser",source.browser);row->SetString("name",source.name);
    row->SetString("status",source.browser=="Firefox"?"NSS / primary-password profiles may be protected":"DPAPI / AES-GCM; App-Bound records unsupported");
    rows->SetDictionary(rows->GetSize(),row);
  }return rows;
}
CefRefPtr<CefListValue> DiscoverImportBrowsers() {
  auto rows=CefListValue::Create();auto sources=Sources();
  for(const auto browser:{std::string("Chrome"),std::string("Edge"),std::string("Firefox")}){
    auto row=CefDictionaryValue::Create();row->SetString("browser",browser);
    row->SetBool("installed",!InstalledBrowser(browser).empty());
    row->SetInt("profiles",static_cast<int>(std::count_if(sources.begin(),sources.end(),[&](const Source& s){return s.browser==browser;})));
    rows->SetDictionary(rows->GetSize(),row);
  }return rows;
}
CefRefPtr<CefDictionaryValue> ImportPasswords(const std::string& source_id,const std::string& target) {
  auto report=CefDictionaryValue::Create();int imported=0,skipped=0,failed=0,protected_count=0;
  report->SetString("status","error");
  auto finish=[&](){report->SetInt("imported",imported);report->SetInt("skipped",skipped);
    report->SetInt("failed",failed);report->SetInt("protected",protected_count);return report;};
  try {
    if(!ValidProfileId(target)){report->SetString("message","Invalid target profile");return finish();}
    auto sources=Sources();auto source=std::find_if(sources.begin(),sources.end(),[&](const Source& s){return s.id==source_id;});
    if(source==sources.end()){report->SetString("message","Source profile is no longer available");return finish();}
    Snapshot snapshot;PasswordVault vault(target);
    auto store=[&](const std::string& origin,const std::string& user,std::string& secret){
      auto site=WebOrigin(origin);
      if(site.empty()||secret.empty())++skipped;
      else if(vault.Contains(site,user))++skipped;
      else if(vault.Put(site,user,secret,false))++imported;
      else ++failed;
      Wipe(secret);
    };
    if(source->browser=="Firefox") {
      if(!snapshot.Copy(source->root,{L"logins.json",L"key4.db",L"key4.db-wal",L"cert9.db",L"cert9.db-wal",L"pkcs11.txt"})){
        report->SetString("message","Close Firefox and retry: source files are locked");return finish();}
      auto json=ReadJson(snapshot.root()/L"logins.json");auto logs=json&&json->GetType()==VTYPE_DICTIONARY?json->GetDictionary()->GetList("logins"):nullptr;
      if(!logs){report->SetString("message","Invalid Firefox login store");return finish();}
      // Do not load source pkcs11.txt: it can register arbitrary external modules.
      std::error_code error;std::filesystem::remove(snapshot.root()/L"pkcs11.txt",error);
      Nss nss;bool ready=nss.Open(snapshot.root());
      for(size_t i=0;i<logs->GetSize();++i){auto row=logs->GetDictionary(i);if(!row){++failed;continue;}
        std::string user,secret;
        if(ready&&nss.Read(row->GetString("encryptedUsername"),user)&&nss.Read(row->GetString("encryptedPassword"),secret))
          store(row->GetString("hostname"),user,secret);
        else{++protected_count;Wipe(secret);}Wipe(user);
      }
    } else {
      if(!snapshot.Copy(source->root,{L"Login Data",L"Login Data-wal"}) ||
         !snapshot.Copy(source->state.parent_path(),{L"Local State"})){
        report->SetString("message","Close the source browser and retry: password store is locked");return finish();}
      std::string key;auto state=ReadJson(snapshot.root()/L"Local State");
      if(state&&state->GetType()==VTYPE_DICTIONARY){auto crypto=state->GetDictionary()->GetDictionary("os_crypt");
        if(crypto){auto wrapped=Decode(crypto->GetString("encrypted_key"));
          if(wrapped.rfind("DPAPI",0)==0)Unprotect(wrapped.substr(5),key);}}
      sqlite3* database=nullptr;
      auto path=CefString((snapshot.root()/L"Login Data").wstring()).ToString();
      if(sqlite3_open_v2(path.c_str(),&database,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK){
        if(database)sqlite3_close(database);Wipe(key);report->SetString("message","Unable to read the copied password store");return finish();}
      sqlite3_db_config(database,SQLITE_DBCONFIG_DEFENSIVE,1,nullptr);
      sqlite3_db_config(database,SQLITE_DBCONFIG_TRUSTED_SCHEMA,0,nullptr);
      sqlite3_limit(database,SQLITE_LIMIT_LENGTH,1024*1024);
      sqlite3_stmt* query=nullptr;
      int prepared=sqlite3_prepare_v2(database,"SELECT origin_url,username_value,password_value,blacklisted_by_user FROM logins",-1,&query,nullptr);
      if(prepared!=SQLITE_OK){sqlite3_close(database);Wipe(key);report->SetString("message","Unsupported password store schema");return finish();}
      auto text=[&](int col){auto p=sqlite3_column_text(query,col);return p?std::string(reinterpret_cast<const char*>(p)):std::string();};
      int status;
      while((status=sqlite3_step(query))==SQLITE_ROW){
        if(sqlite3_column_int(query,3)){++skipped;continue;}
        auto data=sqlite3_column_blob(query,2);int size=sqlite3_column_bytes(query,2);
        if(!data||size<=0){++skipped;continue;}
        std::string blob(static_cast<const char*>(data),size),secret;bool protected_record=false;
        if(ChromiumSecret(blob,key,secret,protected_record))store(text(0),text(1),secret);
        else if(protected_record)++protected_count;else ++failed;
      }
      if(status!=SQLITE_DONE)++failed;
      sqlite3_finalize(query);sqlite3_close(database);Wipe(key);
    }
    report->SetString("status",protected_count||failed?"partial":"ok");
    report->SetString("message",protected_count?"Protected or unsupported records were skipped; no protection bypass was attempted":"Local import complete");
  } catch(const std::exception&) {report->SetString("message","Import failed; source data was not modified");}
  return finish();
}
}
