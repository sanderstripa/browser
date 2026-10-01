#include "examples/soulu/profile_data.h"
#include <windows.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "include/cef_parser.h"
#include "include/internal/cef_types.h"
#include "examples/soulu/third_party/sqlite3.h"

namespace soulu {
namespace {
int checks=0;
void Check(bool passed,const char* name){++checks;if(!passed)throw std::runtime_error(name);}
std::string Bytes(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);std::stringstream data;data<<f.rdbuf();return data.str();}
std::string Protect(const std::string& plain){
  DATA_BLOB input={static_cast<DWORD>(plain.size()),reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))},output={};
  Check(CryptProtectData(&input,nullptr,nullptr,nullptr,nullptr,CRYPTPROTECT_UI_FORBIDDEN,&output)!=0,"fixture-dpapi");
  std::string blob(reinterpret_cast<char*>(output.pbData),output.cbData);LocalFree(output.pbData);return blob;
}
std::string EncryptFixture(const std::string& plain,const std::string& key,const char* version){
  BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_KEY_HANDLE handle=nullptr;
  Check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_AES_ALGORITHM,nullptr,0)>=0,"fixture-aes-provider");
  Check(BCryptSetProperty(algorithm,BCRYPT_CHAINING_MODE,reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),sizeof(BCRYPT_CHAIN_MODE_GCM),0)>=0,"fixture-gcm-mode");
  Check(BCryptGenerateSymmetricKey(algorithm,&handle,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<char*>(key.data())),32,0)>=0,"fixture-aes-key");
  unsigned char nonce[12]={},tag[16]={};BCryptGenRandom(nullptr,nonce,sizeof(nonce),BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;BCRYPT_INIT_AUTH_MODE_INFO(info);
  info.pbNonce=nonce;info.cbNonce=sizeof(nonce);info.pbTag=tag;info.cbTag=sizeof(tag);
  std::string encrypted(plain.size(),'\0');ULONG count=0;
  bool ok=BCryptEncrypt(handle,reinterpret_cast<PUCHAR>(const_cast<char*>(plain.data())),static_cast<ULONG>(plain.size()),
    &info,nullptr,0,reinterpret_cast<PUCHAR>(encrypted.data()),static_cast<ULONG>(encrypted.size()),&count,0)>=0;
  BCryptDestroyKey(handle);BCryptCloseAlgorithmProvider(algorithm,0);Check(ok,"fixture-aes-encrypt");
  return std::string(version)+std::string(reinterpret_cast<char*>(nonce),sizeof(nonce))+encrypted+std::string(reinterpret_cast<char*>(tag),sizeof(tag));
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
    std::string key(32,'\0');Check(BCryptGenRandom(nullptr,reinterpret_cast<PUCHAR>(key.data()),32,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"fixture-key-rng");
    auto wrapped=std::string("DPAPI")+Protect(key);auto crypto=CefDictionaryValue::Create();
    crypto->SetString("encrypted_key",CefBase64Encode(wrapped.data(),wrapped.size()));
    auto state=CefDictionaryValue::Create();state->SetDictionary("os_crypt",crypto);auto state_value=CefValue::Create();state_value->SetDictionary(state);
    Check(WriteJson(source.parent_path()/L"Local State",state_value),"fixture-local-state");
    add("imported",Protect(test));add("aes10",EncryptFixture(test,key,"v10"));add("aes11",EncryptFixture(test,key,"v11"));
    auto corrupted=EncryptFixture(test,key,"v10");corrupted.back()^=1;add("tampered",corrupted);
    add("protected","v20"+std::string(64,'x'));sqlite3_close(db);SecureZeroMemory(key.data(),key.size());
    auto before=Bytes(source/L"Login Data");
    auto result=ImportPasswords("Chrome:Default","test-two");
    Check(result->GetInt("imported")==3&&result->GetInt("protected")==1&&result->GetInt("failed")==1,"import-dpapi-aes-protected-tampered");
    Check(Bytes(source/L"Login Data")==before&&!std::filesystem::exists(source/L"Login Data-shm"),"source-database-unchanged");
    auto again=ImportPasswords("Chrome:Default","test-two");Check(again->GetInt("skipped")==3&&again->GetInt("imported")==0,"import-deduplication");
    PasswordVault imported("test-two");auto records=imported.List();
    Check(records->GetSize()==3&&imported.Reveal(records->GetDictionary(0)->GetString("id"),revealed)&&revealed==test,"import-restart-decrypt");
    Check(Bytes(ProfileRoot("test-two")/L"soulu-passwords.json").find(test)==std::string::npos,"import-target-encrypted");
    std::ofstream output(report);output<<"{\"passed\":true,\"checks\":"<<checks<<"}";return output?0:2;
  }catch(const std::exception& error){std::ofstream output(report);output<<"{\"passed\":false,\"failed_check\":\""<<error.what()<<"\"}";return 2;}
}
}
