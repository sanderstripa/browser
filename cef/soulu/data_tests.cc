#include "examples/soulu/profile_data.h"
#include <windows.h>
#include <wincrypt.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "include/cef_parser.h"
#include "include/internal/cef_types.h"
#include "examples/soulu/third_party/sqlite3.h"

namespace soulu {
namespace {
void Check(bool passed,const char* name){if(!passed)throw std::runtime_error(name);}
std::string Bytes(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);std::stringstream data;data<<f.rdbuf();return data.str();}
std::string Protect(const std::string& plain){
  DATA_BLOB input={static_cast<DWORD>(plain.size()),reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))},output={};
  Check(CryptProtectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)!=0,"fixture-dpapi");
  std::string blob(reinterpret_cast<char*>(output.pbData),output.cbData);LocalFree(output.pbData);return blob;
}
}
int RunDataSecurityTests(const std::filesystem::path& report) {
  try{
    const std::string test="soulu-test-secret-"+RandomId();
    Check(ValidProfileId("personal")&&!ValidProfileId("../personal")&&!ValidProfileId("CEF")&&!ValidProfileId("con"+std::string("/")),"profile-id-validation");
    Check(ProfileRoot("personal")==ProfileRoot("personal"),"canonical-profile-root");
    Check(SiteDomain("HTTPS://Example.COM:443/")=="example.com","domain-normalization");
    Check(WebOrigin("https://example.com:443/path")=="https://example.com","origin-normalization");
    PasswordVault first("test-one"),other("test-two");
    Check(first.Put("https://example.com/login","tester",test,true),"vault-save");
    auto list=first.List();Check(list->GetSize()==1&&!list->GetDictionary(0)->HasKey("secret"),"metadata-no-secret");
    const std::string id=list->GetDictionary(0)->GetString("id");std::string revealed;
    Check(!other.Reveal(id,revealed)&&other.List()->GetSize()==0,"vault-profile-isolation");
    PasswordVault restarted("test-one");Check(restarted.Reveal(id,revealed)&&revealed==test,"vault-reopen-decrypt");
    Check(!restarted.Put("https://example.com","tester","different",false),"import-does-not-overwrite");
    Check(restarted.Put("https://example.com","tester",test+"-updated",true)&&restarted.List()->GetSize()==1,"vault-update-deduplication");
    Check(Bytes(ProfileRoot("test-one")/L"soulu-passwords.json").find(test)==std::string::npos,"vault-no-plaintext-on-disk");
    Check(restarted.Remove(id)&&PasswordVault("test-one").List()->GetSize()==0,"vault-delete-persist");
    SitePolicy policy("test-one");
    Check(policy.Set("","camera",2)&&policy.Set("HTTPS://Example.com:443/","camera",0),"policy-write");
    Check(policy.Rule("https://example.com/path","camera")==0&&policy.Rule("https://other.com","camera")==2,"override-priority");
    Check(SitePolicy("test-one").Rule("https://example.com","camera")==0,"rules-restart");
    Check(SitePolicy("test-two").Rule("https://example.com","camera")==1,"rules-isolation");
    Check(policy.Set("example.com","camera",-1)&&policy.Rule("https://example.com","camera")==2,"remove-one-permission");
    Check(policy.Set("example.com","camera",0)&&policy.Reset("example.com")&&policy.Rule("https://example.com","camera")==2,"reset-domain");
    Check(policy.Set("example.com","camera",0)&&policy.Reset("")&&policy.Rule("https://example.com","camera")==2,"reset-all");
    Check(policy.SetBlocking("",1)&&policy.Blocking("https://example.com"),"blocking-global");
    Check(policy.SetBlocking("example.com",0)&&!policy.Blocking("https://example.com"),"blocking-exception");
    Check(policy.SetBlocking("example.com",2)&&policy.Blocking("https://example.com"),"blocking-inherit");
    Check(BlockResource("https://example.com","https://ads.doubleclick.net/a.js",RT_SCRIPT,true),"block-ad-script");
    Check(!BlockResource("https://example.com","https://doubleclick.net/login",RT_MAIN_FRAME,true)&&
      !BlockResource("https://example.com","https://doubleclick.net/auth",RT_XHR,true)&&
      !BlockResource("https://doubleclick.net","https://doubleclick.net/a.js",RT_SCRIPT,true)&&
      !BlockResource("https://example.com","https://notdoubleclick.net/a.js",RT_SCRIPT,true)&&
      !BlockResource("https://example.com","https://doubleclick.net/a.js",RT_SCRIPT,false),"blocking-safety-boundaries");
    auto source=DataRoot().parent_path().parent_path()/L"Google/Chrome/User Data/Default";
    std::filesystem::create_directories(source);sqlite3* db=nullptr;
    auto filename=CefString((source/L"Login Data").wstring()).ToString();
    Check(sqlite3_open(filename.c_str(),&db)==SQLITE_OK,"fixture-open");
    Check(sqlite3_exec(db,"CREATE TABLE logins(origin_url TEXT,username_value TEXT,password_value BLOB,blacklisted_by_user INTEGER)",nullptr,nullptr,nullptr)==SQLITE_OK,"fixture-schema");
    auto add=[&](const std::string& user,const std::string& secret){sqlite3_stmt* q=nullptr;
      Check(sqlite3_prepare_v2(db,"INSERT INTO logins VALUES('https://example.com',?,?,0)",-1,&q,nullptr)==SQLITE_OK,"fixture-prepare");
      sqlite3_bind_text(q,1,user.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_blob(q,2,secret.data(),static_cast<int>(secret.size()),SQLITE_TRANSIENT);
      Check(sqlite3_step(q)==SQLITE_DONE,"fixture-insert");sqlite3_finalize(q);};
    add("imported",Protect(test));add("protected","v20"+test);sqlite3_close(db);
    auto before=Bytes(source/L"Login Data");
    auto result=ImportPasswords("Chrome:Default","test-two");
    Check(result->GetInt("imported")==1&&result->GetInt("protected")==1&&result->GetInt("failed")==0,"import-dpapi-protected");
    Check(Bytes(source/L"Login Data")==before&&!std::filesystem::exists(source/L"Login Data-shm"),"source-database-unchanged");
    auto again=ImportPasswords("Chrome:Default","test-two");Check(again->GetInt("skipped")==1&&again->GetInt("imported")==0,"import-deduplication");
    PasswordVault imported("test-two");auto records=imported.List();
    Check(records->GetSize()==1&&imported.Reveal(records->GetDictionary(0)->GetString("id"),revealed)&&revealed==test,"import-restart-decrypt");
    Check(Bytes(ProfileRoot("test-two")/L"soulu-passwords.json").find(test)==std::string::npos,"import-target-encrypted");
    std::ofstream output(report);output<<"{\"passed\":true,\"checks\":27}";return output?0:2;
  }catch(const std::exception& error){std::ofstream output(report);output<<"{\"passed\":false,\"failed_check\":\""<<error.what()<<"\"}";return 2;}
}
}
