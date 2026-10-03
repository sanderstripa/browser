#include "examples/soulu/clear_data.h"
#include "include/cef_client.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/cef_devtools_message_observer.h"

namespace soulu {
namespace {
class Later final:public CefTask {
 public:
  explicit Later(std::function<void()> run):run_(std::move(run)){}
  void Execute() override {run_();}
 private:std::function<void()> run_;IMPLEMENT_REFCOUNTING(Later);
};
// This auxiliary document has no Soulu bridge. Only an exact Chromium WebUI
// URL may execute the fixed, allowlisted clearing request. No page-supplied JS.
class ClearJob final:public CefClient,public CefLifeSpanHandler,
                     public CefLoadHandler,public CefDevToolsMessageObserver {
 public:
  ClearJob(bool sites,bool cache,std::function<void(bool)> done):sites_(sites),cache_(cache),done_(std::move(done)){}
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override{return this;}
  CefRefPtr<CefLoadHandler> GetLoadHandler() override{return this;}
  void Start(HWND parent,CefRefPtr<CefRequestContext> context){
    parent_=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!parent_){Finish(false);return;}
    CefWindowInfo info;info.SetAsChild(parent_,CefRect(0,0,1,1));
    info.runtime_style=CEF_RUNTIME_STYLE_ALLOY;info.style&=~WS_VISIBLE;
    CefBrowserSettings settings;
    creating_=true;
    if(!CefBrowserHost::CreateBrowser(info,this,"chrome://settings/",settings,nullptr,context)){creating_=false;Finish(false);return;}
    CefRefPtr<ClearJob> self=this;
    CefPostDelayedTask(TID_UI,new Later([self]{self->Finish(false);}),60000);
  }
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {creating_=false;browser_=browser;if(finished_)browser->GetHost()->CloseBrowser(true);}
  void OnBeforeClose(CefRefPtr<CefBrowser>) override {registration_=nullptr;browser_=nullptr;if(parent_){DestroyWindow(parent_);parent_=nullptr;}Complete(finished_&&result_);}
  void OnLoadEnd(CefRefPtr<CefBrowser> browser,CefRefPtr<CefFrame> frame,int) override {
    if(!done_||finished_||!frame->IsMain()||started_)return;
    if(frame->GetURL()!="chrome://settings/"){Finish(false);return;}
    started_=true;registration_=browser->GetHost()->AddDevToolsMessageObserver(this);
    std::string types="[";
    if(sites_)types+="\"browser.clear_data.cookies\"";
    if(cache_)types+=(sites_?",":"")+std::string("\"browser.clear_data.cache\"");
    types+="]";
    auto params=CefDictionaryValue::Create();
    params->SetString("expression","(async()=>{const {sendWithPromise}=await import('chrome://resources/js/cr.js'); await sendWithPromise('initializeClearBrowsingData'); await sendWithPromise('clearBrowsingData',"+types+",4);return true;})()");
    params->SetBool("awaitPromise",true);params->SetBool("returnByValue",true);
    message_=browser->GetHost()->ExecuteDevToolsMethod(0,"Runtime.evaluate",params);
    if(!message_)Finish(false);
  }
  void OnDevToolsMethodResult(CefRefPtr<CefBrowser>,int id,bool success,const void* result,size_t size) override {
    if(id!=message_||!done_)return;
    auto parsed=success?CefParseJSON(std::string(static_cast<const char*>(result),size),JSON_PARSER_RFC):nullptr;
    auto data=parsed&&parsed->GetType()==VTYPE_DICTIONARY?parsed->GetDictionary():nullptr;
    auto value=data?data->GetDictionary("result"):nullptr;
    Finish(data&&!data->HasKey("exceptionDetails")&&value&&value->GetBool("value"));
  }
 private:
  void Complete(bool ok){if(!done_)return;auto done=std::move(done_);done_=nullptr;done(ok);}
  void Finish(bool ok){if(finished_||!done_)return;finished_=true;result_=ok;
    registration_=nullptr;if(browser_){browser_->GetHost()->CloseBrowser(true);return;}
    if(creating_)return;
    if(parent_){DestroyWindow(parent_);parent_=nullptr;}Complete(ok);}
  HWND parent_=nullptr;bool sites_,cache_,started_=false,creating_=false,finished_=false,result_=false;int message_=0;
  std::function<void(bool)> done_;CefRefPtr<CefBrowser> browser_;CefRefPtr<CefRegistration> registration_;
  IMPLEMENT_REFCOUNTING(ClearJob);
};
}
void ClearProfileWebData(HWND parent,CefRefPtr<CefRequestContext> context,bool sites,bool cache,std::function<void(bool)> done){
  CefRefPtr<ClearJob> job=new ClearJob(sites,cache,std::move(done));job->Start(parent,context);
}
}
