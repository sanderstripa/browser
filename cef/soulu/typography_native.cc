#include "examples/soulu/typography_native.h"
#include "examples/soulu/typography_metrics.h"
#include "include/cef_app.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <tuple>
#include <vector>
#include <algorithm>
#include <iterator>
#include <memory>

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
  int Line()const{return Px(static_cast<int>(typography::body.lineHeight));}
};
thread_local std::vector<Dialog*> activeDialogs;
struct MenuText {
  // MSAAMENUINFO layout lets Windows expose owner-drawn labels to accessibility.
  DWORD signature=0xAA0DF00D,length=0;LPWSTR accessibleText=nullptr;
  HMENU menu;UINT position,type;ULONG_PTR data;UINT dpi;
  std::wstring label,shortcut,accessibleLabel;bool submenu;
};
thread_local std::vector<MenuText*> activeMenuText;
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
    const int available=std::max(d.Line(),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top)-d.Px(250));
    d.visibleLines=std::max(1,std::min(static_cast<int>(d.lines.size()),available/d.Line()));
    d.bodyHeight=d.visibleLines*d.Line();
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
    const bool two=confirm||d.prompt||(d.flags&MB_TYPEMASK)==MB_OKCANCEL;
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
    RECT title={d.Px(titleLeft),d.Px(20),d.Px(436),d.Px(20)+d.Px(static_cast<int>(typography::heading2.lineHeight))};
    std::wstring heading=d.title;SIZE extent={};GetTextExtentPoint32W(dc,heading.c_str(),static_cast<int>(heading.size()),&extent);
    if(extent.cx>title.right-title.left){
      do{if(!heading.empty())heading.pop_back();const auto shortened=heading+L"…";GetTextExtentPoint32W(dc,shortened.c_str(),static_cast<int>(shortened.size()),&extent);}while(!heading.empty()&&extent.cx>title.right-title.left);
      heading+=L"…";
    }
    DrawLine(dc,heading,title,typography::heading2,d.dpi);
    SelectObject(dc,Service().Get(typography::body,d.dpi));
    for(int i=0;i<d.visibleLines;++i){RECT line={d.Px(24),d.Px(58)+i*d.Line(),d.Px(436),d.Px(58)+(i+1)*d.Line()};
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
  CefScopedSetNestableTasksAllowed allow_tasks;
  int result=static_cast<int>(DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&definition.dialog,state.owner,Procedure,reinterpret_cast<LPARAM>(&state)));
  activeDialogs.erase(std::remove(activeDialogs.begin(),activeDialogs.end(),&state),activeDialogs.end());
  return result<0?IDCANCEL:result;
}
}
int TypographyMessageBox(HWND owner,const wchar_t* text,const wchar_t* title,UINT flags,const void* tag){
  Dialog state;state.owner=owner;state.text=text?text:L"";state.title=title?title:L"Soulu";state.flags=flags;state.tag=tag;return Show(state);
}
bool TypographyPrompt(HWND owner,const std::wstring& text,const std::wstring& initial,std::wstring& result,const void* tag){
  Dialog state;state.owner=owner;state.text=text;state.title=L"Soulu";state.initial=initial;state.prompt=true;state.flags=MB_OKCANCEL;
  state.tag=tag;if(Show(state)!=IDOK)return false;result=std::move(state.result);return true;
}
void TypographyCancelDialogs(const void* tag){
  if(!tag)return;auto dialogs=activeDialogs;
  for(auto* dialog:dialogs)if(dialog->tag==tag&&dialog->window)EndDialog(dialog->window,IDCANCEL);
}
void TypographyCancelOwnedDialogs(HWND owner){
  auto dialogs=activeDialogs;
  for(auto* dialog:dialogs)if(dialog->window&&(dialog->owner==owner||IsChild(owner,dialog->owner)||GetAncestor(dialog->owner,GA_ROOTOWNER)==owner))EndDialog(dialog->window,IDCANCEL);
}
int TypographyTrackPopupMenu(HMENU menu,UINT flags,int x,int y,int reserved,HWND owner,const RECT* bounds){
  if(!Service().ready)return 0;
  UINT dpi=GetDpiForWindow(owner);if(!dpi)dpi=96;
  std::vector<std::unique_ptr<MenuText>> entries;
  auto prepare=[&](auto&& recurse,HMENU current)->void{
    for(int i=0;i<GetMenuItemCount(current);++i){
      wchar_t label[2048]={};MENUITEMINFOW info={sizeof(info)};
      info.fMask=MIIM_FTYPE|MIIM_DATA|MIIM_STRING|MIIM_SUBMENU;info.dwTypeData=label;info.cch=2047;
      if(!GetMenuItemInfoW(current,i,TRUE,&info))continue;
      if(info.hSubMenu)recurse(recurse,info.hSubMenu);
      if(info.fType&(MFT_SEPARATOR|MFT_OWNERDRAW))continue;
      auto item=std::make_unique<MenuText>();item->menu=current;item->position=i;item->type=info.fType;item->data=info.dwItemData;
      item->dpi=dpi;item->label=label;item->submenu=info.hSubMenu!=nullptr;
      auto tab=item->label.find(L'\t');if(tab!=std::wstring::npos){item->shortcut=item->label.substr(tab+1);item->label.resize(tab);}
      item->accessibleLabel=label;item->length=static_cast<DWORD>(item->accessibleLabel.size());item->accessibleText=item->accessibleLabel.data();
      info.fMask=MIIM_FTYPE|MIIM_DATA;info.fType|=MFT_OWNERDRAW;info.dwItemData=reinterpret_cast<ULONG_PTR>(item.get());
      if(SetMenuItemInfoW(current,i,TRUE,&info)){activeMenuText.push_back(item.get());entries.push_back(std::move(item));}
    }
  };
  prepare(prepare,menu);
  const int result=static_cast<int>(TrackPopupMenu(menu,flags,x,y,reserved,owner,bounds));
  for(auto& item:entries){
    MENUITEMINFOW info={sizeof(info)};info.fMask=MIIM_FTYPE|MIIM_DATA;info.fType=item->type;info.dwItemData=item->data;
    SetMenuItemInfoW(item->menu,item->position,TRUE,&info);
    activeMenuText.erase(std::remove(activeMenuText.begin(),activeMenuText.end(),item.get()),activeMenuText.end());
  }
  return result;
}
bool TypographyMenuMessage(UINT message,LPARAM parameter){
  if(message!=WM_MEASUREITEM&&message!=WM_DRAWITEM)return false;
  auto* measure=reinterpret_cast<MEASUREITEMSTRUCT*>(parameter);
  auto* draw=reinterpret_cast<DRAWITEMSTRUCT*>(parameter);
  if((message==WM_MEASUREITEM?measure->CtlType:draw->CtlType)!=ODT_MENU)return false;
  auto* item=reinterpret_cast<MenuText*>(message==WM_MEASUREITEM?measure->itemData:draw->itemData);
  if(std::find(activeMenuText.begin(),activeMenuText.end(),item)==activeMenuText.end())return false;
  auto px=[&](int value){return MulDiv(value,item->dpi,96);};
  if(message==WM_MEASUREITEM){
    HDC dc=GetDC(nullptr);auto old=SelectObject(dc,Service().Get(typography::compactControl,item->dpi));SIZE label={},shortcut={};
    GetTextExtentPoint32W(dc,item->label.c_str(),static_cast<int>(item->label.size()),&label);
    GetTextExtentPoint32W(dc,item->shortcut.c_str(),static_cast<int>(item->shortcut.size()),&shortcut);
    measure->itemWidth=label.cx+shortcut.cx+px(item->shortcut.empty()?48:80);measure->itemHeight=px(static_cast<int>(typography::compactControl.lineHeight)+12);
    SelectObject(dc,old);ReleaseDC(nullptr,dc);return true;
  }
  HDC dc=draw->hDC;const bool selected=(draw->itemState&ODS_SELECTED)!=0,disabled=(draw->itemState&(ODS_DISABLED|ODS_GRAYED))!=0;
  FillRect(dc,&draw->rcItem,GetSysColorBrush(selected?COLOR_HIGHLIGHT:COLOR_MENU));
  auto background=SetBkMode(dc,TRANSPARENT);auto color=SetTextColor(dc,GetSysColor(disabled?COLOR_GRAYTEXT:selected?COLOR_HIGHLIGHTTEXT:COLOR_MENUTEXT));
  const int lineHeight=px(static_cast<int>(typography::compactControl.lineHeight));
  RECT line=draw->rcItem;line.left+=px(28);line.right-=px(20);line.top+=(line.bottom-line.top-lineHeight)/2;line.bottom=line.top+lineHeight;
  DrawLine(dc,item->label,line,typography::compactControl,item->dpi);
  if(!item->shortcut.empty()){
    auto old=SelectObject(dc,Service().Get(typography::compactControl,item->dpi));SIZE size={};
    GetTextExtentPoint32W(dc,item->shortcut.c_str(),static_cast<int>(item->shortcut.size()),&size);SelectObject(dc,old);
    line.left=line.right-size.cx;DrawLine(dc,item->shortcut,line,typography::compactControl,item->dpi);
  }
  if(draw->itemState&ODS_CHECKED){RECT check=draw->rcItem;check.left+=px(5);check.right=check.left+px(16);check.top+=(check.bottom-check.top-px(16))/2;check.bottom=check.top+px(16);DrawFrameControl(dc,&check,DFC_MENU,DFCS_MENUCHECK|(disabled?DFCS_INACTIVE:0));}
  if(item->submenu){RECT arrow=draw->rcItem;arrow.right-=px(4);arrow.left=arrow.right-px(12);arrow.top+=(arrow.bottom-arrow.top-px(12))/2;arrow.bottom=arrow.top+px(12);DrawFrameControl(dc,&arrow,DFC_MENU,DFCS_MENUARROW|(disabled?DFCS_INACTIVE:0));}
  SetTextColor(dc,color);SetBkMode(dc,background);return true;
}
}
