#include "examples/soulu/browser_window.h"
#include "examples/soulu/clear_data.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"
#include <cmath>
#include <limits>

namespace soulu {
bool BrowserWindow::IsHistoryUi(const std::string& url) const {return url==InternalUrl("soulu://history");}
std::shared_ptr<HistoryStore> BrowserWindow::HistoryFor(const std::string& profile){
  auto& store=histories_[profile];if(!store)store=std::make_shared<HistoryStore>(profile);return store;
}
void BrowserWindow::ResetHistoryVisit(int id){if(auto* tab=FindTab(id)){tab->history_visit.clear();tab->favicon.clear();}}
void BrowserWindow::RecordHistory(int id){
  auto* tab=FindTab(id);
  if(!tab||tab->incognito||!tab->browser||!tab->history_visit.empty())return;
  const std::string url=tab->browser->GetMainFrame()->GetURL();
  if(WebOrigin(url).empty()||!PageSettings(*tab)->GetBool("saveHistory"))return;
  tab->history_visit=HistoryFor(tab->profile_id)->Add(url,tab->title,tab->favicon,HistoryNow());
}
void BrowserWindow::UpdateHistory(int id){auto* tab=FindTab(id);if(tab&&!tab->incognito&&!tab->history_visit.empty())
  HistoryFor(tab->profile_id)->Update(tab->history_visit,tab->title,tab->favicon);}
void BrowserWindow::OpenHistory(bool clear){
  CEF_REQUIRE_UI_THREAD();
  // Private mode never exposes a normal profile's history or clearing controls.
  if(auto* tab=ActiveTab();tab&&tab->incognito){NewTab("soulu://history",true);return;}
  for(const auto& tab:tabs_)if(!tab.incognito&&tab.profile_id==active_profile_id_&&
      tab.browser&&IsHistoryUi(tab.browser->GetMainFrame()->GetURL())){
    const int id=tab.id;auto frame=tab.browser->GetMainFrame();SwitchTab(id);
    if(clear)frame->ExecuteJavaScript("window.souluHistoryClear&&window.souluHistoryClear()",frame->GetURL(),0);
    return;
  }
  // A separate internal URL parameter is unnecessary: initialize via a per-tab flag.
  NewTab("soulu://history");
  if(clear)if(auto* tab=ActiveTab())history_clear_tabs_.insert(tab->id);
}
void BrowserWindow::HandleHistoryBridge(int id,const std::string& request,
    CefRefPtr<CefMessageRouterBrowserSide::Callback> callback){
  CEF_REQUIRE_UI_THREAD();auto* tab=FindTab(id);
  if(!tab||!tab->browser||!IsHistoryUi(tab->browser->GetMainFrame()->GetURL())){callback->Failure(403,"History document required");return;}
  auto parsed=CefParseJSON(request,JSON_PARSER_RFC);
  if(!parsed||parsed->GetType()!=VTYPE_DICTIONARY){callback->Failure(400,"Invalid request");return;}
  auto root=parsed->GetDictionary();const std::string action=root->GetString("action");auto payload=root->GetDictionary("payload");
  if(action=="history.state"){
    auto result=HomeState(*tab);auto config=PageSettings(*tab);
    result->SetString("profile",tab->incognito?"":tab->profile_id);
    result->SetBool("groupDays",config->GetBool("historyGroupDays"));
    result->SetString("filter",config->GetString("historyDefaultFilter"));
    result->SetBool("saveHistory",config->GetBool("saveHistory"));
    result->SetBool("openClear",history_clear_tabs_.erase(id)>0);Reply(callback,result);return;
  }
  if(tab->incognito){callback->Failure(403,"История и очистка недоступны в инкогнито.");return;}
  auto store=HistoryFor(tab->profile_id);
  if(!store->Healthy()){callback->Failure(500,"Хранилище истории недоступно.");return;}
  if(action=="history.query"){
    const double begin=payload?payload->GetDouble("begin"):0,end=payload?payload->GetDouble("end"):0;
    const int offset=payload?payload->GetInt("offset"):0;
    if(!payload||!std::isfinite(begin)||!std::isfinite(end)||begin<0||end<=begin||offset<0||payload->GetString("search").length()>4096){callback->Failure(400,"Invalid history query");return;}
    auto rows=store->Query(payload->GetString("search"),begin,end,offset,101);
    if(!rows){callback->Failure(500,"Не удалось прочитать историю.");return;}
    auto result=CefDictionaryValue::Create();result->SetBool("more",rows->GetSize()>100);
    if(rows->GetSize()>100)rows->Remove(100);result->SetList("rows",rows);Reply(callback,result);return;
  }
  if(action=="history.remove"){
    if(!payload||payload->GetType("id")!=VTYPE_STRING){callback->Failure(400,"Visit ID required");return;}
    if(clearing_data_){callback->Failure(409,"Дождитесь окончания очистки.");return;}
    if(!store->Remove(payload->GetString("id"))){callback->Failure(500,"Не удалось удалить запись.");return;}
    ReplyEmpty(callback);return;
  }
  if(action=="history.open"){
    const std::string url=payload?payload->GetString("url"):"";
    if(WebOrigin(url).empty()){callback->Failure(400,"Web URL required");return;}
    ReplyEmpty(callback);
    if(payload->GetBool("newTab"))NewTab(url,false,true,tab->browser->GetHost()->GetRequestContext(),tab->profile_id);
    else tab->browser->GetMainFrame()->LoadURL(url);return;
  }
  if(action=="history.clear"){
    if(clearing_data_){callback->Failure(409,"Очистка уже выполняется.");return;}
    if(!payload||payload->GetType("history")!=VTYPE_BOOL||payload->GetType("sites")!=VTYPE_BOOL||payload->GetType("cache")!=VTYPE_BOOL){callback->Failure(400,"Categories required");return;}
    const std::string range=payload->GetString("range");double duration=0;
    if(range=="15m")duration=15*60*1000.;else if(range=="hour")duration=60*60*1000.;
    else if(range=="day")duration=24*60*60*1000.;else if(range=="week")duration=7*24*60*60*1000.;
    else if(range=="month")duration=28*24*60*60*1000.;else if(range!="all"){callback->Failure(400,"Invalid range");return;}
    const bool history=payload->GetBool("history"),sites=payload->GetBool("sites"),cache=payload->GetBool("cache");
    if(!history&&!sites&&!cache){callback->Failure(400,"Select data to clear");return;}
    if((sites||cache)&&!payload->GetBool("webAllTimeAcknowledged")){callback->Failure(400,"Подтвердите очистку данных сайтов и кэша за всё время.");return;}
    const double end=HistoryNow(),begin=duration?end-duration:0;
    if(history&&!store->Clear(begin,end+1)){callback->Failure(500,"Не удалось очистить историю.");return;}
    auto result=CefDictionaryValue::Create();result->SetBool("historyCleared",history);
    result->SetBool("webDataCleared",false);result->SetBool("ok",true);
    if(!sites&&!cache){Reply(callback,result);return;}
    clearing_data_=true;CefRefPtr<BrowserWindow> self=this;
    auto context=tab->browser->GetHost()->GetRequestContext();
    ClearProfileWebData(hwnd_,context,sites,cache,[self,result,callback](bool ok){
      self->clearing_data_=false;result->SetBool("ok",ok);result->SetBool("webDataCleared",ok);
      if(!ok)result->SetString("error","Не удалось подтвердить очистку данных сайтов/кэша. История очищена только если была выбрана. Повторите очистку.");
      self->Reply(callback,result);
      if(self->close_after_clear_){self->close_after_clear_=false;self->CloseAll();}
    });return;
  }
  callback->Failure(400,"Unknown history action");
}
}
