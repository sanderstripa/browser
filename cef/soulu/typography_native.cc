#include "examples/soulu/typography_native.h"
#include "examples/soulu/typography_metrics.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <tuple>
#include <vector>
#include <algorithm>
#include <iterator>

namespace soulu {
namespace {
struct Fonts {
  std::vector<std::vector<char>> bytes;
  std::vector<HANDLE> resources;
  std::map<std::tuple<int,int,int>,HFONT> cache;
  bool ready=false;
  Fonts(){
    wchar_t module[32768]={};GetModuleFileNameW(nullptr,module,32768);
    const auto root=std::filesystem::path(module).parent_path()/L"ui"/L"fonts";
    for(const wchar_t* name:{L"Onest-Regular.ttf",L"Onest-Medium.ttf",L"Onest-SemiBold.ttf"}){
      std::ifstream input(root/name,std::ios::binary);
      if(!input)return;
      bytes.emplace_back(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
      if(bytes.back().empty())return;
      DWORD count=0;HANDLE resource=AddFontMemResourceEx(bytes.back().data(),static_cast<DWORD>(bytes.back().size()),nullptr,&count);
      if(!resource||!count)return;
      resources.push_back(resource);
    }
    ready=true;
  }
  HFONT Get(typography::Metrics role,UINT dpi){
    if(!ready)return nullptr;
    auto key=std::make_tuple(static_cast<int>(role.size),role.weight,static_cast<int>(dpi));
    auto& font=cache[key];
    if(!font)font=CreateFontW(-MulDiv(static_cast<int>(role.size),dpi,96),0,0,0,role.weight,
        FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_TT_ONLY_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        DEFAULT_PITCH|FF_DONTCARE,role.weight==600?L"Onest SemiBold":role.weight==500?L"Onest Medium":L"Onest");
    return font;
  }
  ~Fonts(){for(auto& item:cache)if(item.second)DeleteObject(item.second);for(auto resource:resources)RemoveFontMemResourceEx(resource);}
};
Fonts& Service(){static Fonts fonts;return fonts;}

struct Dialog {
  HWND owner=nullptr,window=nullptr,edit=nullptr;
  UINT dpi=96,flags=0;
  bool prompt=false;
  const void* tag=nullptr;
  std::wstring text,title,initial,result;
  std::vector<std::wstring> lines;
  int bodyHeight=0,clientWidth=0,clientHeight=0,scroll=0,visibleLines=1;
  int Px(int value)const{return MulDiv(value,dpi,96);}
};
thread_local std::vector<Dialog*> activeDialogs;
std::vector<std::wstring> Wrap(HDC dc,const std::wstring& text,int width){
  std::vector<std::wstring> lines;
  size_t position=0;
  while(position<text.size()){
    size_t end=text.find(L'\n',position);if(end==std::wstring::npos)end=text.size();
    if(end==position){lines.emplace_back();++position;continue;}
    int fit=0;SIZE measured={};
    GetTextExtentExPointW(dc,text.data()+position,static_cast<int>(end-position),width,&fit,nullptr,&measured);
    size_t length=std::max(1,fit);
    if(position+length<end){
      auto space=text.rfind(L' ',position+length);
      if(space!=std::wstring::npos&&space>position)length=space-position;
      // Keep a surrogate pair together in unbroken Unicode domains/emoji.
      if(text[position+length-1]>=0xd800&&text[position+length-1]<=0xdbff)length=length==1?2:length-1;
    }
    lines.push_back(text.substr(position,length));position+=length;
    while(position<end&&text[position]==L' ')++position;
    if(position==end&&end<text.size())++position;
  }
  if(lines.empty())lines.emplace_back();return lines;
}
void DrawLine(HDC dc,const std::wstring& text,RECT bounds,typography::Metrics role,UINT dpi,bool center=false){
  auto old=SelectObject(dc,Service().Get(role,dpi));
  // GDI's WinAscent is larger than Onest's typographic ascent. Position by
  // hhea metrics so explicit line boxes match CEF instead of clipping at the
  // bottom of a default Win32 DrawText box.
  const float em=static_cast<float>(MulDiv(static_cast<int>(role.size),dpi,96));
  const int baseline=bounds.top+static_cast<int>((bounds.bottom-bounds.top-em*(typography::onestAscent+typography::onestDescent))/2+em*typography::onestAscent+.5f);
  auto align=SetTextAlign(dc,TA_BASELINE|(center?TA_CENTER:TA_LEFT));
  ExtTextOutW(dc,center?(bounds.left+bounds.right)/2:bounds.left,baseline,ETO_CLIPPED,&bounds,text.c_str(),static_cast<UINT>(text.size()),nullptr);
  SetTextAlign(dc,align);SelectObject(dc,old);
}
INT_PTR CALLBACK Procedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam){
  auto* dialog=reinterpret_cast<Dialog*>(GetWindowLongPtrW(window,DWLP_USER));
  if(message==WM_INITDIALOG){
    dialog=reinterpret_cast<Dialog*>(lParam);SetWindowLongPtrW(window,DWLP_USER,lParam);dialog->window=window;
    auto& d=*dialog;d.dpi=GetDpiForWindow(d.owner?d.owner:window);if(!d.dpi)d.dpi=96;
    d.clientWidth=d.Px(460);
    HDC dc=GetDC(window);auto old=SelectObject(dc,Service().Get(typography::body,d.dpi));
    d.lines=Wrap(dc,d.text,d.Px(412));SelectObject(dc,old);ReleaseDC(window,dc);
    MONITORINFO monitor={sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(d.owner?d.owner:window,MONITOR_DEFAULTTONEAREST),&monitor);
    const int available=std::max(d.Px(20),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top)-d.Px(250));
    d.visibleLines=std::max(1,std::min(static_cast<int>(d.lines.size()),available/d.Px(20)));
    d.bodyHeight=d.visibleLines*d.Px(20);
    if(d.visibleLines<static_cast<int>(d.lines.size())){
      SetWindowLongW(window,GWL_STYLE,GetWindowLongW(window,GWL_STYLE)|WS_VSCROLL);
      SetScrollRange(window,SB_VERT,0,static_cast<int>(d.lines.size())-d.visibleLines,FALSE);
    }
    d.clientHeight=d.Px(d.prompt?168:118)+d.bodyHeight;
    RECT bounds={0,0,d.clientWidth,d.clientHeight};
    AdjustWindowRectExForDpi(&bounds,GetWindowLongW(window,GWL_STYLE),FALSE,GetWindowLongW(window,GWL_EXSTYLE),d.dpi);
    RECT owner={};GetWindowRect(d.owner?d.owner:GetDesktopWindow(),&owner);
    SetWindowPos(window,nullptr,owner.left+(owner.right-owner.left-(bounds.right-bounds.left))/2,
        owner.top+(owner.bottom-owner.top-(bounds.bottom-bounds.top))/2,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOZORDER);
    SetWindowTextW(window,d.title.c_str());
    if(d.prompt){
      d.edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",d.initial.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,
          d.Px(24),d.Px(62)+d.bodyHeight,d.Px(412),d.Px(32),window,reinterpret_cast<HMENU>(1001),nullptr,nullptr);
      SendMessageW(d.edit,WM_SETFONT,reinterpret_cast<WPARAM>(Service().Get(typography::body,d.dpi)),FALSE);
    }
    const bool confirm=(d.flags&MB_TYPEMASK)==MB_YESNO;
    const bool two=confirm||d.prompt;
    const bool english=PRIMARYLANGID(GetUserDefaultUILanguage())!=LANG_RUSSIAN;
    const int accept=confirm?IDYES:IDOK,reject=confirm?IDNO:IDCANCEL;
    auto button=[&](const wchar_t* label,int id,int x,bool primary){
      HWND child=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|(primary?BS_DEFPUSHBUTTON:BS_PUSHBUTTON),
          d.Px(x),d.clientHeight-d.Px(56),d.Px(104),d.Px(34),window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
      SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(Service().Get(typography::control,d.dpi)),FALSE);return child;
    };
    const bool defaultReject=two&&(d.flags&MB_DEFMASK)==MB_DEFBUTTON2;
    HWND yes=button(confirm?(english?L"Yes":L"Да"):L"OK",accept,two?220:332,!defaultReject);
    HWND no=two?button(confirm?(english?L"No":L"Нет"):(english?L"Cancel":L"Отмена"),reject,332,defaultReject):nullptr;
    SendMessageW(window,DM_SETDEFID,defaultReject?reject:accept,0);
    if(d.edit){SetFocus(d.edit);SendMessageW(d.edit,EM_SETSEL,0,-1);}else SetFocus(defaultReject?no:yes);
    return FALSE;
  }
  if(!dialog)return FALSE;
  if(message==WM_PAINT){
    PAINTSTRUCT paint={};HDC dc=BeginPaint(window,&paint);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,GetSysColor(COLOR_WINDOWTEXT));
    auto& d=*dialog;auto old=SelectObject(dc,Service().Get(typography::heading2,d.dpi));
    int titleLeft=24;LPCWSTR icon=nullptr;
    switch(d.flags&MB_ICONMASK){case MB_ICONERROR:icon=IDI_ERROR;break;case MB_ICONWARNING:icon=IDI_WARNING;break;case MB_ICONQUESTION:icon=IDI_QUESTION;break;case MB_ICONINFORMATION:icon=IDI_INFORMATION;break;}
    if(icon){DrawIconEx(dc,d.Px(24),d.Px(20),LoadIconW(nullptr,icon),d.Px(24),d.Px(24),0,nullptr,DI_NORMAL);titleLeft=58;}
    RECT title={d.Px(titleLeft),d.Px(20),d.Px(436),d.Px(48)};
    std::wstring heading=d.title;SIZE extent={};GetTextExtentPoint32W(dc,heading.c_str(),static_cast<int>(heading.size()),&extent);
    if(extent.cx>title.right-title.left){
      do{if(!heading.empty())heading.pop_back();const auto shortened=heading+L"…";GetTextExtentPoint32W(dc,shortened.c_str(),static_cast<int>(shortened.size()),&extent);}while(!heading.empty()&&extent.cx>title.right-title.left);
      heading+=L"…";
    }
    DrawLine(dc,heading,title,typography::heading2,d.dpi);
    SelectObject(dc,Service().Get(typography::body,d.dpi));
    for(int i=0;i<d.visibleLines;++i){RECT line={d.Px(24),d.Px(58)+i*d.Px(20),d.Px(436),d.Px(78)+i*d.Px(20)};
      DrawLine(dc,d.lines[d.scroll+i],line,typography::body,d.dpi);}
    SelectObject(dc,old);EndPaint(window,&paint);return TRUE;
  }
  if(message==WM_VSCROLL||message==WM_MOUSEWHEEL){
    auto& d=*dialog;int next=d.scroll;
    if(message==WM_MOUSEWHEEL)next-=GET_WHEEL_DELTA_WPARAM(wParam)/WHEEL_DELTA*3;
    else switch(LOWORD(wParam)){case SB_LINEUP:--next;break;case SB_LINEDOWN:++next;break;case SB_PAGEUP:next-=d.visibleLines;break;case SB_PAGEDOWN:next+=d.visibleLines;break;case SB_THUMBTRACK:next=HIWORD(wParam);break;}
    d.scroll=std::clamp(next,0,static_cast<int>(d.lines.size())-d.visibleLines);SetScrollPos(window,SB_VERT,d.scroll,TRUE);InvalidateRect(window,nullptr,FALSE);return TRUE;
  }
  if(message==WM_COMMAND){
    int id=LOWORD(wParam);
    if(id==IDOK||id==IDYES||id==IDNO||id==IDCANCEL){
      if(dialog->edit&&(id==IDOK||id==IDYES)){int length=GetWindowTextLengthW(dialog->edit);std::wstring value(length+1,L'\0');GetWindowTextW(dialog->edit,value.data(),length+1);value.resize(length);dialog->result=std::move(value);}
      EndDialog(window,id);return TRUE;
    }
  }
  if(message==WM_CLOSE){EndDialog(window,(dialog->flags&MB_TYPEMASK)==MB_YESNO?IDNO:IDCANCEL);return TRUE;}
  return FALSE;
}
int Show(Dialog& state){
  if(!Service().ready){
    // Last-resort OS error only when the required product resources are broken.
    MessageBoxW(state.owner,L"Bundled Onest resources are missing or invalid.",L"Soulu",MB_OK|MB_ICONERROR);
    return IDCANCEL;
  }
  // Zero controls in the template: children receive private fonts before show.
  struct Template {DLGTEMPLATE dialog;WORD menu=0,windowClass=0,title=0;} definition{};
  definition.dialog.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME;
  definition.dialog.cx=300;definition.dialog.cy=140;
  activeDialogs.push_back(&state);
  int result=static_cast<int>(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&definition.dialog,state.owner,Procedure,reinterpret_cast<LPARAM>(&state)));
  activeDialogs.erase(std::remove(activeDialogs.begin(),activeDialogs.end(),&state),activeDialogs.end());
  return result<0?IDCANCEL:result;
}
}
int TypographyMessageBox(HWND owner,const wchar_t* text,const wchar_t* title,UINT flags,const void* tag){
  Dialog state;state.owner=owner;state.text=text?text:L"";state.title=title?title:L"Soulu";state.flags=flags;state.tag=tag;return Show(state);
}
bool TypographyPrompt(HWND owner,const std::wstring& text,const std::wstring& initial,std::wstring& result,const void* tag){
  Dialog state;state.owner=owner;state.text=text;state.title=L"Soulu";state.initial=initial;state.prompt=true;state.flags=MB_OKCANCEL|MB_DEFBUTTON2;
  state.tag=tag;if(Show(state)!=IDOK)return false;result=std::move(state.result);return true;
}
void TypographyCancelDialogs(const void* tag){
  if(!tag)return;auto dialogs=activeDialogs;
  for(auto* dialog:dialogs)if(dialog->tag==tag&&dialog->window)EndDialog(dialog->window,IDCANCEL);
}
}
