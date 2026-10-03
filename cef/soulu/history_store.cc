#include "examples/soulu/history_store.h"
#include "examples/soulu/third_party/sqlite3.h"
#include <windows.h>
#include <chrono>

namespace soulu {
namespace {
std::string Fold(const std::string& text) {
  const auto wide=CefString(text).ToWString();
  if(wide.empty())return "";
  int size=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,wide.data(),
      static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr,0);
  if(!size)return text;
  std::wstring result(size,L'\0');
  LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,wide.data(),
      static_cast<int>(wide.size()),result.data(),size,nullptr,nullptr,0);
  return CefString(result).ToString();
}
struct Statement {
  sqlite3_stmt* value=nullptr;
  Statement(sqlite3* db,const char* sql){if(db)sqlite3_prepare_v2(db,sql,-1,&value,nullptr);}
  ~Statement(){sqlite3_finalize(value);}
  void Text(int index,const std::string& text){sqlite3_bind_text(value,index,text.data(),static_cast<int>(text.size()),SQLITE_TRANSIENT);}
};
std::string Text(sqlite3_stmt* s,int col){auto p=sqlite3_column_text(s,col);return p?reinterpret_cast<const char*>(p):"";}
}
double HistoryNow(){return std::chrono::duration<double,std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();}
HistoryStore::HistoryStore(const std::string& profile) {
  const auto path=ProfileRoot(profile)/L"soulu-history.sqlite";
  std::error_code error;std::filesystem::create_directories(path.parent_path(),error);
  if(error||sqlite3_open_v2(CefString(path.wstring()).ToString().c_str(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){sqlite3_close(db_);db_=nullptr;return;}
  sqlite3_busy_timeout(db_,2000);
  if(sqlite3_exec(db_,"PRAGMA secure_delete=ON; CREATE TABLE IF NOT EXISTS visits(id TEXT PRIMARY KEY,url TEXT NOT NULL,title TEXT NOT NULL,favicon TEXT NOT NULL,visited REAL NOT NULL,search TEXT NOT NULL); CREATE INDEX IF NOT EXISTS visits_time ON visits(visited DESC,id DESC);",nullptr,nullptr,nullptr)!=SQLITE_OK){sqlite3_close(db_);db_=nullptr;}
}
HistoryStore::~HistoryStore(){sqlite3_close(db_);}
std::string HistoryStore::Add(const std::string& url,const std::string& title,const std::string& favicon,double time){
  if(WebOrigin(url).empty())return "";
  Statement s(db_,"INSERT INTO visits VALUES(?,?,?,?,?,?)");if(!s.value)return "";
  const auto id=RandomId();s.Text(1,id);s.Text(2,url);s.Text(3,title);s.Text(4,favicon);
  sqlite3_bind_double(s.value,5,time);s.Text(6,Fold(title+" "+url));
  return sqlite3_step(s.value)==SQLITE_DONE?id:"";
}
bool HistoryStore::Update(const std::string& id,const std::string& title,const std::string& favicon){
  Statement s(db_,"UPDATE visits SET title=?,favicon=?,search=? WHERE id=?");
  Statement get(db_,"SELECT url FROM visits WHERE id=?");if(!s.value||!get.value)return false;
  get.Text(1,id);if(sqlite3_step(get.value)!=SQLITE_ROW)return true;
  s.Text(1,title);s.Text(2,favicon);s.Text(3,Fold(title+" "+Text(get.value,0)));s.Text(4,id);
  return sqlite3_step(s.value)==SQLITE_DONE;
}
CefRefPtr<CefListValue> HistoryStore::Query(const std::string& search,double begin,double end,int offset,int limit){
  Statement s(db_,"SELECT id,url,title,favicon,visited FROM visits WHERE visited>=? AND visited<? AND instr(search,?)>0 ORDER BY visited DESC,id DESC LIMIT ? OFFSET ?");
  if(!s.value)return nullptr;
  sqlite3_bind_double(s.value,1,begin);sqlite3_bind_double(s.value,2,end);s.Text(3,Fold(search));
  sqlite3_bind_int(s.value,4,limit);sqlite3_bind_int(s.value,5,offset);
  auto rows=CefListValue::Create();int result;
  while((result=sqlite3_step(s.value))==SQLITE_ROW){auto row=CefDictionaryValue::Create();
    row->SetString("id",Text(s.value,0));row->SetString("url",Text(s.value,1));
    row->SetString("title",Text(s.value,2));row->SetString("favicon",Text(s.value,3));
    row->SetDouble("visited",sqlite3_column_double(s.value,4));rows->SetDictionary(rows->GetSize(),row);}
  return result==SQLITE_DONE?rows:nullptr;
}
bool HistoryStore::Remove(const std::string& id){Statement s(db_,"DELETE FROM visits WHERE id=?");if(!s.value)return false;s.Text(1,id);return sqlite3_step(s.value)==SQLITE_DONE;}
bool HistoryStore::Clear(double begin,double end){Statement s(db_,"DELETE FROM visits WHERE visited>=? AND visited<?");if(!s.value)return false;sqlite3_bind_double(s.value,1,begin);sqlite3_bind_double(s.value,2,end);return sqlite3_step(s.value)==SQLITE_DONE;}
}
