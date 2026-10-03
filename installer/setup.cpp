#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <thread>
#include <objidl.h>
#include <memory>
#include <filesystem>
#include <fstream>
#include "typography_metrics.h"
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
using namespace Gdiplus;
namespace {
// Reference panels are 524 x 381. Keep that compact rectangular proportion.
constexpr int kWidth=720,kHeight=524,kMain=1001,kMin=1002,kClose=1003,kLaunch=1004;
constexpr float kReferenceScale=524.0f/720.0f;
enum class Stage { Welcome, Installing, Finished, Error };
HWND window=nullptr,mainButton=nullptr,launchButton=nullptr,minButton=nullptr,closeButton=nullptr;
std::atomic<HANDLE> worker{nullptr};
std::atomic<bool> cancelling{false};
std::thread preparation;
HANDLE workerJob=nullptr;
Stage stage=Stage::Welcome;
bool launch=true;
bool keyboardFocus=false;
bool dragging=false;
POINT dragOrigin={};
RECT dragWindow={};
float scale=1;
HICON icon=nullptr,smallIcon=nullptr;
IStream* logoStream=nullptr;
Bitmap* logo=nullptr;
std::wstring payloadPath,installDir,errorText;
ULONG_PTR graphicsToken=0;
int Px(float v){return static_cast<int>(v*scale+.5f);}
void Round(GraphicsPath& path,RectF r,float radius){
 const float d=radius*2; path.AddArc(r.X,r.Y,d,d,180,90);path.AddArc(r.GetRight()-d,r.Y,d,d,270,90);
 path.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);path.AddArc(r.X,r.GetBottom()-d,d,d,90,90);path.CloseFigure();
}
// One private collection per real face. GDI+ sees each as a regular family,
// so Medium/SemiBold are selected by resource, never synthetic FontStyleBold.
std::unique_ptr<PrivateFontCollection> fontCollections[3];
std::unique_ptr<FontFamily> fontFamilies[3];
std::vector<BYTE> fontBytes[3];
std::unique_ptr<Font> textFonts[10];
bool LoadTypography(){
 for(int i=0;i<3;++i){
  HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(103+i),RT_RCDATA);
  HGLOBAL loaded=resource?LoadResource(nullptr,resource):nullptr;
  auto* bytes=loaded?static_cast<const BYTE*>(LockResource(loaded)):nullptr;
  DWORD length=resource?SizeofResource(nullptr,resource):0;
  if(!bytes||!length)return false;
  fontBytes[i].assign(bytes,bytes+length);
  fontCollections[i]=std::make_unique<PrivateFontCollection>();
  if(fontCollections[i]->AddMemoryFont(fontBytes[i].data(),length)!=Ok)return false;
  // Select the sole family from this exact static-face collection. GDI+ can
  // expose legacy or typographic family names depending on its font cache;
  // looking up "Onest Medium" by name is therefore not reliable.
  FontFamily families[1];INT found=0;
  if(fontCollections[i]->GetFamilies(1,families,&found)!=Ok||found!=1)return false;
  fontFamilies[i].reset(families[0].Clone());
  if(!fontFamilies[i])return false;
  if(fontFamilies[i]->GetLastStatus()!=Ok||!fontFamilies[i]->IsStyleAvailable(FontStyleRegular))return false;
 }
 return true;
}
void Text(Graphics& g,const wchar_t* text,soulu::typography::Metrics metrics,RectF rect,Color color){
 // The existing installer paints a 720-wide reference at 524 DIP. Compensate
 // that reference transform so token sizes/line boxes still match CSS DIP.
 const float size=metrics.size/kReferenceScale;
 const int face=metrics.weight==600?2:metrics.weight==500?1:0;
 const soulu::typography::Metrics roles[]={soulu::typography::display,soulu::typography::heading1,soulu::typography::heading2,soulu::typography::heading3,soulu::typography::bodyLarge,soulu::typography::body,soulu::typography::control,soulu::typography::compact,soulu::typography::compactControl,soulu::typography::caption};
 int role=0;for(int i=0;i<10;++i)if(roles[i].size==metrics.size&&roles[i].weight==metrics.weight){role=i;break;}
 if(!textFonts[role])textFonts[role]=std::make_unique<Font>(fontFamilies[face].get(),size,FontStyleRegular,UnitPixel);
 SolidBrush brush(color);StringFormat format;format.SetAlignment(StringAlignmentCenter);format.SetLineAlignment(StringAlignmentCenter);
 format.SetTrimming(StringTrimmingEllipsisCharacter);
 // Use the canonical line box for multiline descriptions, independent of the
 // font's default GDI+ paragraph spacing. Keep the existing centering geometry.
 std::wstring value(text);size_t start=0;std::vector<std::wstring> lines;
 do{size_t next=value.find(L'\n',start);lines.push_back(value.substr(start,next-start));if(next==std::wstring::npos)break;start=next+1;}while(true);
 const float line=metrics.lineHeight/kReferenceScale;
 const float top=rect.Y+(rect.Height-line*lines.size())/2;
 for(size_t i=0;i<lines.size();++i)g.DrawString(lines[i].c_str(),-1,textFonts[role].get(),RectF(rect.X,top+i*line,rect.Width,line),&format,&brush);
}
Bitmap* LoadPngResource(int id,IStream** keptStream){
 HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(id),RT_RCDATA);
 HGLOBAL source=resource?LoadResource(nullptr,resource):nullptr;
 const DWORD size=resource?SizeofResource(nullptr,resource):0;
 const void* bytes=source?LockResource(source):nullptr;
 if(!bytes||!size)return nullptr;
 HGLOBAL copy=GlobalAlloc(GMEM_MOVEABLE,size);
 if(!copy)return nullptr;
 void* target=GlobalLock(copy);memcpy(target,bytes,size);GlobalUnlock(copy);
 if(FAILED(CreateStreamOnHGlobal(copy,TRUE,keptStream))){GlobalFree(copy);return nullptr;}
 Bitmap* image=Bitmap::FromStream(*keptStream,FALSE);
 if(!image||image->GetLastStatus()!=Ok){delete image;(*keptStream)->Release();*keptStream=nullptr;return nullptr;}
 return image;
}
void Background(Graphics& g){
 // Airy icy-blue surface and the low sweeping translucent ribbons in Image A.
 LinearGradientBrush base(PointF(40,0),PointF(680,524),Color(255,247,252,255),Color(255,248,249,255));
 g.FillRectangle(&base,0,0,kWidth,kHeight);
 GraphicsPath glow;glow.AddEllipse(RectF(-80,-420,850,800));
 PathGradientBrush light(&glow);light.SetCenterColor(Color(82,197,234,255));
 Color edge(0,220,241,255);int count=1;light.SetSurroundColors(&edge,&count);g.FillPath(&light,&glow);
 GraphicsPath lowerGlow;lowerGlow.AddEllipse(RectF(-450,145,900,690));
 PathGradientBrush lowerLight(&lowerGlow);lowerLight.SetCenterColor(Color(120,186,221,255));
 lowerLight.SetSurroundColors(&edge,&count);g.FillPath(&lowerLight,&lowerGlow);
 if(stage!=Stage::Installing&&stage!=Stage::Error){
 GraphicsState ribbons=g.Save();if(stage==Stage::Finished)g.TranslateTransform(0,40);
 GraphicsPath ribbon;
 ribbon.AddBezier(PointF(-40,305),PointF(150,372),PointF(249,554),PointF(526,398));
 ribbon.AddBezier(PointF(526,398),PointF(638,337),PointF(678,304),PointF(760,274));
 ribbon.AddLine(PointF(760,274),PointF(760,554));ribbon.AddLine(PointF(760,554),PointF(-40,554));ribbon.CloseFigure();
 LinearGradientBrush wave(PointF(0,310),PointF(720,524),Color(110,198,235,255),Color(65,226,212,255));
 g.FillPath(&wave,&ribbon);
 GraphicsPath lower;
 lower.AddBezier(PointF(-30,429),PointF(178,352),PointF(263,469),PointF(415,452));
 lower.AddBezier(PointF(415,452),PointF(563,436),PointF(634,395),PointF(750,387));
 lower.AddLine(PointF(750,387),PointF(750,550));lower.AddLine(PointF(750,550),PointF(-30,550));lower.CloseFigure();
 LinearGradientBrush pale(PointF(0,392),PointF(710,530),Color(174,248,253,255),Color(135,213,237,255));
 g.FillPath(&pale,&lower);
 g.Restore(ribbons);
 }
 GraphicsPath border;Round(border,RectF(.5f,.5f,kWidth-1.0f,kHeight-1.0f),17);
 Pen line(Color(255,186,207,239),1);g.DrawPath(&line,&border);
}
void DrawControl(Graphics& g,int id,bool pressed,bool focused){
 const Color ink(255,10,19,65),muted(255,86,113,166);
 if(id==kMain){
   GraphicsPath pill;Round(pill,RectF(0,0,320,60),30);
   LinearGradientBrush blue(PointF(0,0),PointF(320,0),Color(255,0,80,255),Color(255,71,54,255));
   if(pressed){SolidBrush down(Color(255,27,74,230));g.FillPath(&down,&pill);}
   else{
     g.FillPath(&blue,&pill);
     LinearGradientBrush sheen(PointF(0,0),PointF(0,60),Color(145,125,211,255),Color(0,125,211,255));
     Color colors[]={Color(145,125,211,255),Color(40,125,211,255),Color(0,125,211,255)};
     REAL positions[]={0,.3f,1};sheen.SetInterpolationColors(colors,positions,3);g.FillPath(&sheen,&pill);
   }
   Pen highlight(Color(105,255,255,255),1);g.DrawPath(&highlight,&pill);
   Text(g,stage==Stage::Finished?L"Готово":stage==Stage::Error?L"Повторить":L"Установить →",soulu::typography::control,RectF(0,-1,320,60),Color(255,255,255,255));
   if(focused){GraphicsPath ring;Round(ring,RectF(4,4,312,52),26);Pen focus(Color(170,255,255,255),1);g.DrawPath(&focus,&ring);}
 }else if(id==kLaunch){
   GraphicsPath box;Round(box,RectF(4,8,26,26),4);
   if(launch){
     LinearGradientBrush blue(PointF(4,8),PointF(30,34),Color(255,45,157,255),Color(255,38,63,255));g.FillPath(&blue,&box);
     Pen check(Color(255,255,255,255),1.6f);g.DrawLine(&check,10.0f,21.0f,15.0f,26.0f);g.DrawLine(&check,15.0f,26.0f,25.0f,15.0f);
   }else{SolidBrush clear(Color(130,250,253,255));g.FillPath(&clear,&box);Pen outline(muted,1);g.DrawPath(&outline,&box);}
   Text(g,L"Запустить Soulu",soulu::typography::control,RectF(42,0,168,42),muted);
   if(focused){Pen focus(muted,1);g.DrawRectangle(&focus,1.0f,3.0f,218.0f,35.0f);}
 }else{
   Pen line(ink,1.3f);
   if(id==kMin)g.DrawLine(&line,16.0f,22.0f,32.0f,22.0f);
   else{g.DrawLine(&line,17.0f,15.0f,31.0f,29.0f);g.DrawLine(&line,31.0f,15.0f,17.0f,29.0f);}
   if(focused){Pen focus(muted,1);g.DrawRectangle(&focus,5.0f,5.0f,38.0f,34.0f);}
 }
}
void PaintContent(Graphics& g){
 g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.SetPixelOffsetMode(PixelOffsetModeHighQuality);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
 Background(g);
 const Color ink(255,10,19,65),muted(255,86,113,166);
 if(stage==Stage::Welcome||stage==Stage::Finished){
   // A single transparent production PNG, never a baked screen or old logo.
   if(logo)g.DrawImage(logo,RectF(276,46,168,168));
   if(stage==Stage::Welcome){
     Text(g,L"Soulu",soulu::typography::display,RectF(0,209,kWidth,67),ink);
     Text(g,L"Спокойный и умный браузер\nдля больших возможностей.",soulu::typography::bodyLarge,RectF(110,279,500,62),muted);
     Text(g,L"Быстро. Безопасно. Для того, что важно.",soulu::typography::body,RectF(80,460,560,35),muted);
   }else{
     Text(g,L"Всё готово",soulu::typography::heading1,RectF(0,220,kWidth,57),ink);
     Text(g,L"Браузер установлен. Можно начинать.",soulu::typography::bodyLarge,RectF(35,278,650,38),muted);
   }
 }else if(stage==Stage::Installing){
   Text(g,L"Установка Soulu",soulu::typography::heading1,RectF(0,212,kWidth,64),ink);
   Text(g,L"Это займёт всего несколько мгновений.",soulu::typography::bodyLarge,RectF(35,278,650,38),muted);
   // NSIS exposes completion/exit code, not byte progress. No fake percentage.
 }else{
   Text(g,L"Не удалось завершить установку",soulu::typography::heading2,RectF(35,207,650,58),ink);
   Text(g,errorText.c_str(),soulu::typography::body,RectF(75,274,570,66),muted);
 }
}
void Paint(HDC dc){
 Bitmap buffer(Px(kWidth),Px(kHeight),PixelFormat32bppPARGB);Graphics g(&buffer);
 g.ScaleTransform(scale,scale);PaintContent(g);
 Graphics screen(dc);screen.DrawImage(&buffer,0,0);
}
// Explicit diagnostics do not install, register, or launch the application.
int TypographyDiagnostics(const std::filesystem::path& directory){
 std::error_code error;std::filesystem::create_directories(directory,error);if(error)return 2;
 const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
 std::ofstream report(directory/L"installer-typography.json");
 report<<"{\"privateFaces\":3,\"syntheticBold\":false,\"dpi\":[";
 const int dpis[]={96,120,144,192};bool first=true;
 for(int dpi:dpis){
  scale=dpi/96.0f*kReferenceScale;
  if(!first)report<<",";first=false;report<<dpi;
  for(int value=0;value<4;++value){
   stage=static_cast<Stage>(value);errorText=L"Проверьте соединение и повторите попытку.";
   Bitmap buffer(Px(kWidth),Px(kHeight),PixelFormat32bppPARGB);Graphics g(&buffer);
   g.ScaleTransform(scale,scale);PaintContent(g);
   if(stage!=Stage::Installing){auto saved=g.Save();g.TranslateTransform(200,stage==Stage::Finished?358.0f:365.0f);DrawControl(g,kMain,false,false);g.Restore(saved);}
   if(stage==Stage::Finished){auto saved=g.Save();g.TranslateTransform(258,431);DrawControl(g,kLaunch,false,false);g.Restore(saved);}
   auto file=directory/(L"installer-"+std::to_wstring(dpi)+L"-"+std::to_wstring(value)+L".png");
   if(buffer.Save(file.c_str(),&png,nullptr)!=Ok)return 3;
  }
 }
 for(const auto& font:textFonts)if(font&&font->GetLastStatus()!=Ok)return 5;
 report<<"],\"passed\":true}";return report.good()?0:4;
}

void DrawButton(DRAWITEMSTRUCT* item){
 RECT bounds={};GetWindowRect(item->hwndItem,&bounds);
 POINT origin={bounds.left,bounds.top};ScreenToClient(window,&origin);
 Bitmap buffer(bounds.right-bounds.left,bounds.bottom-bounds.top,PixelFormat32bppPARGB);Graphics g(&buffer);
 g.ScaleTransform(scale,scale);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
 // Same backdrop under each control: toggling cannot leave a flat patch.
 GraphicsState saved=g.Save();g.TranslateTransform(-origin.x/scale,-origin.y/scale);Background(g);g.Restore(saved);
 DrawControl(g,item->CtlID,(item->itemState&ODS_SELECTED)!=0,keyboardFocus&&(item->itemState&ODS_FOCUS)!=0);
 Graphics output(item->hDC);output.DrawImage(&buffer,0,0);
}
HWND Button(int id,const wchar_t* text,int x,int y,int width,int height){
 HWND button=CreateWindowExW(0,L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
    Px(x),Px(y),Px(width),Px(height),window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
 return button;
}
void ShowStage(Stage next){
 stage=next;
 if(stage==Stage::Installing)ReleaseCapture();
 const bool finished=stage==Stage::Finished;
 SetWindowTextW(mainButton,finished?L"Готово":stage==Stage::Error?L"Повторить":L"Установить");
 SetWindowPos(mainButton,nullptr,Px(200),Px(finished?358:365),Px(320),Px(60),SWP_NOZORDER);
 ShowWindow(mainButton,stage==Stage::Installing?SW_HIDE:SW_SHOW);
 ShowWindow(launchButton,finished?SW_SHOW:SW_HIDE);
 InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);
 if(stage!=Stage::Installing)SetFocus(mainButton);
}
void CleanupWorker(bool cancel){
 cancelling.store(cancel);
 // Never close the job / delete the temporary payload while preparation is
 // still writing or assigning the suspended child process to that job.
 if(preparation.joinable())preparation.join();
 HANDLE process=worker.exchange(nullptr);
 if(process){if(cancel&&WaitForSingleObject(process,0)==WAIT_TIMEOUT)TerminateProcess(process,2);CloseHandle(process);}
 if(workerJob){CloseHandle(workerJob);workerJob=nullptr;}
 if(!payloadPath.empty())DeleteFileW(payloadPath.c_str());
}
void StartInstall(){
 cancelling.store(false);
 ShowStage(Stage::Installing);
 wchar_t temp[MAX_PATH]={},file[MAX_PATH]={};GetTempPathW(MAX_PATH,temp);GetTempFileNameW(temp,L"slu",0,file);DeleteFileW(file);
 payloadPath=std::wstring(file)+L".exe";
 workerJob=CreateJobObjectW(nullptr,nullptr);
 JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits={};
 limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
 if(!workerJob||!SetInformationJobObject(workerJob,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){
   CleanupWorker(false);errorText=L"Не удалось запустить установку. Попробуй ещё раз.";ShowStage(Stage::Error);return;
 }
 const HWND target=window;const HANDLE job=workerJob;
 const std::wstring path=payloadPath,folder=installDir;
 // Copying and Windows executable scanning can block too. Keep all payload
 // preparation off the UI thread, not just the extraction itself.
 preparation=std::thread([target,job,path,folder]{
   HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(100),RT_RCDATA);
   HGLOBAL bytes=resource?LoadResource(nullptr,resource):nullptr;
   const DWORD length=resource?SizeofResource(nullptr,resource):0;
   const void* data=bytes?LockResource(bytes):nullptr;
   HANDLE output=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY,nullptr);
   DWORD written=0;
   bool ok=data&&output!=INVALID_HANDLE_VALUE&&WriteFile(output,data,length,&written,nullptr)&&written==length;
   if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);
   if(ok&&!cancelling.load()){
     std::wstring command=L"\""+path+L"\" /S /D="+folder;
     STARTUPINFOW startup={sizeof(startup)};PROCESS_INFORMATION process={};
     ok=CreateProcessW(path.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup,&process)!=FALSE;
     if(ok){
       ok=AssignProcessToJobObject(job,process.hProcess)!=FALSE;
       if(ok){worker.store(process.hProcess);ResumeThread(process.hThread);}
       else{TerminateProcess(process.hProcess,2);CloseHandle(process.hProcess);}
       CloseHandle(process.hThread);
     }
   }
   if(!ok&&!cancelling.load())PostMessageW(target,WM_APP+2,0,0);
 });
 SetTimer(window,1,100,nullptr);
}
void Finish(){
 if(stage==Stage::Welcome||stage==Stage::Error){PostMessageW(window,WM_APP+1,0,0);return;}
 if(stage!=Stage::Finished)return;
 bool start=launch;std::wstring exe=installDir+L"\\Soulu.exe";
 DestroyWindow(window);
 if(start)ShellExecuteW(nullptr,L"open",exe.c_str(),nullptr,installDir.c_str(),SW_SHOWNORMAL);
}
void UpdateGeometry(){
 SetWindowPos(mainButton,nullptr,Px(200),Px(stage==Stage::Finished?358:365),Px(320),Px(60),SWP_NOZORDER);
 SetWindowPos(launchButton,nullptr,Px(258),Px(431),Px(220),Px(42),SWP_NOZORDER);
 SetWindowPos(minButton,nullptr,Px(590),Px(10),Px(48),Px(44),SWP_NOZORDER);
 SetWindowPos(closeButton,nullptr,Px(652),Px(10),Px(48),Px(44),SWP_NOZORDER);
 HRGN region=CreateRoundRectRgn(0,0,Px(kWidth)+1,Px(kHeight)+1,Px(34),Px(34));SetWindowRgn(window,region,TRUE);
 InvalidateRect(window,nullptr,FALSE);
}
LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){
  case WM_CREATE:window=hwnd;return 0;
  case WM_ERASEBKGND:return 1;
  case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);Paint(dc);EndPaint(hwnd,&ps);return 0;}
  case WM_DRAWITEM:DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
  case WM_DPICHANGED:{
    scale=HIWORD(wp)/96.0f*kReferenceScale;const RECT* suggested=reinterpret_cast<const RECT*>(lp);
    SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,Px(kWidth),Px(kHeight),SWP_NOZORDER|SWP_NOACTIVATE);
    UpdateGeometry();return 0;
  }
  case WM_NCHITTEST:return HTCLIENT;
  case WM_LBUTTONDOWN:
    dragging=true;GetCursorPos(&dragOrigin);GetWindowRect(hwnd,&dragWindow);SetCapture(hwnd);return 0;
  case WM_MOUSEMOVE:
    if(dragging&&(wp&MK_LBUTTON)){
      POINT cursor={};GetCursorPos(&cursor);
      SetWindowPos(hwnd,nullptr,dragWindow.left+cursor.x-dragOrigin.x,dragWindow.top+cursor.y-dragOrigin.y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    }return 0;
  case WM_LBUTTONUP:if(dragging){dragging=false;ReleaseCapture();}return 0;
  case WM_CAPTURECHANGED:dragging=false;return 0;
  case WM_COMMAND:
    if(HIWORD(wp)==BN_CLICKED){switch(LOWORD(wp)){
      case kMain:Finish();break;
      case kClose:SendMessageW(hwnd,WM_CLOSE,0,0);break;
      case kMin:ShowWindow(hwnd,SW_MINIMIZE);break;
      case kLaunch:launch=!launch;InvalidateRect(launchButton,nullptr,FALSE);UpdateWindow(launchButton);break;
    }}return 0;
  case WM_APP+1:if(stage!=Stage::Installing)StartInstall();return 0;
  case WM_APP+2:KillTimer(hwnd,1);CleanupWorker(false);errorText=L"Не удалось запустить установку. Попробуй ещё раз.";ShowStage(Stage::Error);return 0;
  case WM_TIMER:if(worker.load()&&WaitForSingleObject(worker.load(),0)==WAIT_OBJECT_0){
    DWORD code=1;GetExitCodeProcess(worker.load(),&code);KillTimer(hwnd,1);CleanupWorker(false);
    if(code==0&&GetFileAttributesW((installDir+L"\\Soulu.exe").c_str())!=INVALID_FILE_ATTRIBUTES)ShowStage(Stage::Finished);
    else{errorText=L"Установка не завершена. Закрой Soulu и повтори попытку.";ShowStage(Stage::Error);}
  }return 0;
  case WM_CLOSE:cancelling.store(true);DestroyWindow(hwnd);return 0;
  case WM_DESTROY:PostQuitMessage(0);return 0;
 }
 return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,wchar_t* arguments,int){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 GdiplusStartupInput input;GdiplusStartup(&graphicsToken,&input,nullptr);
 if(!LoadTypography()){MessageBoxW(nullptr,L"Bundled Onest resources are missing or invalid.",L"Soulu Setup",MB_ICONERROR);
  for(auto& family:fontFamilies)family.reset();for(auto& collection:fontCollections)collection.reset();
  GdiplusShutdown(graphicsToken);return 1;}
 logo=LoadPngResource(102,&logoStream);
 int count=0;wchar_t** args=CommandLineToArgvW(GetCommandLineW(),&count);
 if(args&&count==3&&std::wstring(args[1])==L"--typography-check"){
  int result=TypographyDiagnostics(args[2]);LocalFree(args);
  for(auto& font:textFonts)font.reset();for(auto& family:fontFamilies)family.reset();for(auto& collection:fontCollections)collection.reset();
  delete logo;logo=nullptr;if(logoStream){logoStream->Release();logoStream=nullptr;}
  GdiplusShutdown(graphicsToken);return result;
 }
 if(args)LocalFree(args);
 wchar_t local[MAX_PATH]={};GetEnvironmentVariableW(L"LOCALAPPDATA",local,MAX_PATH);installDir=std::wstring(local)+L"\\Programs\\Soulu";
 icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_DEFAULTCOLOR));
 smallIcon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_DEFAULTCOLOR));
 WNDCLASSEXW cls={sizeof(cls)};cls.lpfnWndProc=Proc;cls.hInstance=instance;cls.lpszClassName=L"SouluInstaller";
 cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hIcon=icon;cls.hIconSm=smallIcon;RegisterClassExW(&cls);
 scale=GetDpiForSystem()/96.0f*kReferenceScale;
 RECT work={};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
 window=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,L"Soulu Setup",WS_POPUP|WS_MINIMIZEBOX|WS_SYSMENU|WS_CLIPCHILDREN,
   work.left+(work.right-work.left-Px(kWidth))/2,work.top+(work.bottom-work.top-Px(kHeight))/2,Px(kWidth),Px(kHeight),nullptr,nullptr,instance,nullptr);
 HRGN region=CreateRoundRectRgn(0,0,Px(kWidth)+1,Px(kHeight)+1,Px(34),Px(34));SetWindowRgn(window,region,TRUE);
 DWORD corner=2;DwmSetWindowAttribute(window,33,&corner,sizeof(corner));
 mainButton=Button(kMain,L"Установить",200,365,320,60);
 minButton=Button(kMin,L"Свернуть",590,10,48,44);closeButton=Button(kClose,L"Закрыть",652,10,48,44);
 launchButton=Button(kLaunch,L"Запустить Soulu",258,431,220,42);ShowWindow(launchButton,SW_HIDE);
 ShowWindow(window,SW_SHOW);SetFocus(mainButton);UpdateWindow(window);
 MSG message;while(GetMessageW(&message,nullptr,0,0)>0){
   if(message.message==WM_KEYDOWN&&message.wParam==VK_TAB){keyboardFocus=true;InvalidateRect(mainButton,nullptr,FALSE);InvalidateRect(launchButton,nullptr,FALSE);}
   if(message.message==WM_LBUTTONDOWN)keyboardFocus=false;
   if(message.message==WM_KEYDOWN&&message.wParam==VK_RETURN){
     HWND focused=GetFocus();int id=GetDlgCtrlID(focused);
     if(id==kMain||id==kLaunch||id==kMin||id==kClose)SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(focused));
     else Finish();continue;
   }
   if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE){SendMessageW(window,WM_CLOSE,0,0);continue;}
   if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
 }
 CleanupWorker(true);if(icon)DestroyIcon(icon);if(smallIcon)DestroyIcon(smallIcon);delete logo;if(logoStream)logoStream->Release();for(auto& font:textFonts)font.reset();for(auto& family:fontFamilies)family.reset();for(auto& collection:fontCollections)collection.reset();GdiplusShutdown(graphicsToken);return 0;
}
