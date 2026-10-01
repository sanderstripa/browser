#include "examples/soulu/app_factory.h"
#include "include/wrapper/cef_message_router.h"
#include "include/cef_parser.h"
#include "include/cef_process_message.h"
#include <set>

namespace soulu {
class CredentialSubmit final : public CefV8Handler {
 public:
  bool Execute(const CefString&,CefRefPtr<CefV8Value>,const CefV8ValueList& args,
               CefRefPtr<CefV8Value>&,CefString&) override {
    if(args.size()!=2||!args[0]->IsString()||!args[1]->IsString())return true;
    auto context=CefV8Context::GetCurrentContext();if(!context||!context->GetFrame()->IsMain())return true;
    auto frame=context->GetFrame();CefURLParts parts;
    if(!CefParseURL(frame->GetURL(),parts)||(CefString(&parts.scheme)!="https"&&CefString(&parts.scheme)!="http"))return true;
    auto message=CefProcessMessage::Create("soulu.credential.submit");
    message->GetArgumentList()->SetString(0,args[0]->GetStringValue());
    message->GetArgumentList()->SetString(1,args[1]->GetStringValue());
    frame->SendProcessMessage(PID_BROWSER,message);return true;
  }
 private:IMPLEMENT_REFCOUNTING(CredentialSubmit);
};
class RendererApp final : public CefApp, public CefRenderProcessHandler {
 public:
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }
  void OnBrowserCreated(CefRefPtr<CefBrowser> browser,CefRefPtr<CefDictionaryValue> extra) override {
    if(extra&&extra->GetBool("souluIncognito"))private_browsers_.insert(browser->GetIdentifier());
  }
  void OnBrowserDestroyed(CefRefPtr<CefBrowser> browser) override {private_browsers_.erase(browser->GetIdentifier());}
  void OnWebKitInitialized() override {
    CefMessageRouterConfig config;
    router_ = CefMessageRouterRendererSide::Create(config);
  }
  void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override {
    router_->OnContextCreated(browser, frame, context);
    if(!frame->IsMain()||private_browsers_.count(browser->GetIdentifier()))return;CefURLParts parts;
    if(!CefParseURL(frame->GetURL(),parts)||(CefString(&parts.scheme)!="https"&&CefString(&parts.scheme)!="http"))return;
    context->GetGlobal()->SetValue("__souluCredentialSubmit",
      CefV8Value::CreateFunction("submit",new CredentialSubmit()),V8_PROPERTY_ATTRIBUTE_NONE);
    // The native hook is captured in a closure and removed from window. Only a
    // trusted form-submit event reads the submitted form, never all page fields.
    CefRefPtr<CefV8Value> result;CefRefPtr<CefV8Exception> error;
    context->Eval(R"JS((()=>{
      const send=window.__souluCredentialSubmit;delete window.__souluCredentialSubmit;
      document.addEventListener('submit',event=>{
        if(!event.isTrusted||event.defaultPrevented||!(event.target instanceof HTMLFormElement))return;
        const form=event.target;
        if(new URL(form.action||location.href,location.href).origin!==location.origin)return;
        const fields=[...form.elements];
        const passwords=fields.filter(el=>el instanceof HTMLInputElement&&el.type==='password'&&!el.disabled&&el.value);
        if(passwords.length!==1||passwords[0].autocomplete==='new-password')return;
        const user=fields.find(el=>el instanceof HTMLInputElement&&!el.disabled&&el.autocomplete==='username')||
          fields.find(el=>el instanceof HTMLInputElement&&!el.disabled&&(el.type==='email'||el.type==='text'));
        if(passwords[0].value.length>16384||(user&&user.value.length>4096))return;
        send(user?user.value:'',passwords[0].value);
      },true);
    })())JS",frame->GetURL(),0,result,error);
  }
  void OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override {
    router_->OnContextReleased(browser, frame, context);
  }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                CefProcessId source, CefRefPtr<CefProcessMessage> message) override {
    return router_->OnProcessMessageReceived(browser, frame, source, message);
  }
 private:
  std::set<int> private_browsers_;
  CefRefPtr<CefMessageRouterRendererSide> router_;
  IMPLEMENT_REFCOUNTING(RendererApp);
};

CefRefPtr<CefApp> CreateRendererApp() { return new RendererApp(); }
}
