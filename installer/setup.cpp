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
#include <atomic>
#include <thread>
#include <objidl.h>
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
using namespace Gdiplus;
namespace {
constexpr int kWidth=600,kHeight=400,kMain=1001,kMin=1002,kClose=1003,kLaunch=1004;
enum class Stage { Welcome, Installing, Finished, Error };
HWND window=nullptr,mainButton=nullptr,launchButton=nullptr;
std::atomic<HANDLE> worker{nullptr};
HANDLE workerJob=nullptr;
Stage stage=Stage::Welcome;
bool launch=true;
float scale=1;
HICON icon=nullptr;
IStream* logoStream=nullptr;
Bitmap* logo=nullptr;
std::wstring payloadPath,installDir,errorText;
ULONG_PTR graphicsToken=0;
int Px(float v){return static_cast<int>(v*scale+.5f);}
void Round(GraphicsPath& path,RectF r,float radius){
 const float d=radius*2; path.AddArc(r.X,r.Y,d,d,180,90);path.AddArc(r.GetRight()-d,r.Y,d,d,270,90);
 path.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);path.AddArc(r.X,r.GetBottom()-d,d,d,90,90);path.CloseFigure();
}
void Text(Graphics& g,const wchar_t* text,float size,RectF rect,Color color, bool bold=false){
 FontFamily family(L"Segoe UI");Font font(&family,size,bold?FontStyleBold:FontStyleRegular,UnitPixel);
 SolidBrush brush(color);StringFormat format;format.SetAlignment(StringAlignmentCenter);format.SetLineAlignment(StringAlignmentCenter);
 g.DrawString(text,-1,&font,rect,&format,&brush);
}
void Background(Graphics& g){
 SolidBrush base(Color(255,250,249,248));g.FillRectangle(&base,0,0,kWidth,kHeight);
 LinearGradientBrush wash(Point(0,0),Point(kWidth,kHeight),Color(255,255,255,255),Color(255,246,246,247));
 g.FillRectangle(&wash,0,0,kWidth,kHeight);
 GraphicsPath wave;wave.AddBezier(-20,320,125,215,110,395,335,358);wave.AddBezier(335,358,455,338,510,285,630,255);wave.AddLine(630,255,630,420);wave.AddLine(630,420,-20,420);wave.CloseFigure();
 SolidBrush shade(Color(24,152,167,185));g.FillPath(&shade,&wave);
 GraphicsPath wave2;wave2.AddBezier(-20,345,160,260,202,421,391,363);wave2.AddBezier(391,363,502,334,530,321,630,303);wave2.AddLine(630,303,630,420);wave2.AddLine(630,420,-20,420);wave2.CloseFigure();
 SolidBrush light(Color(170,255,255,255));g.FillPath(&light,&wave2);
}
void Paint(HDC dc){
 Bitmap buffer(Px(kWidth),Px(kHeight),PixelFormat32bppPARGB);Graphics g(&buffer);
 g.ScaleTransform(scale,scale);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
 Background(g);
 if(stage==Stage::Installing){
   Text(g,L"Установка Soulu",27,RectF(40,159,520,42),Color(255,25,49,72));
   Text(g,L"Это займёт всего несколько мгновений.",14,RectF(40,203,520,26),Color(255,94,105,120));
 }else if(stage==Stage::Error){
   Text(g,L"Не удалось завершить установку",23,RectF(30,135,540,50),Color(255,25,49,72));
   Text(g,errorText.c_str(),13,RectF(45,190,510,58),Color(255,110,72,72));
 }else{
   const bool done=stage==Stage::Finished;
   const int size=done?72:86,y=done?48:46;
   if(logo){
     g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
     g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
     ImageAttributes attributes;attributes.SetWrapMode(WrapModeTileFlipXY);
     g.DrawImage(logo,RectF((kWidth-size)/2.0f,static_cast<float>(y),static_cast<float>(size),static_cast<float>(size)),0,0,static_cast<float>(logo->GetWidth()),static_cast<float>(logo->GetHeight()),UnitPixel,&attributes);
   }
   Text(g,done?L"Всё готово":L"Soulu",done?29:35,RectF(35,done?136:143,530,48),Color(255,23,49,74));
   Text(g,done?L"Браузер установлен. Можно начинать.":L"Спокойный и умный браузер\nдля больших возможностей.",15,RectF(45,done?190:195,510,48),Color(255,87,99,114));
   if(!done)Text(g,L"Быстро. Безопасно. Для того, что важно.",12,RectF(30,361,540,22),Color(255,109,117,128));
 }
 Graphics screen(dc);screen.DrawImage(&buffer,0,0);
}
void DrawButton(DRAWITEMSTRUCT* item){
 RECT r;GetWindowRect(item->hwndItem,&r);POINT p={r.left,r.top};ScreenToClient(window,&p);
 const float x=p.x/scale,y=p.y/scale,w=(r.right-r.left)/scale,h=(r.bottom-r.top)/scale;
 Graphics g(item->hDC);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);g.ScaleTransform(scale,scale);
 g.TranslateTransform(-x,-y);Background(g);g.TranslateTransform(x,y);
 bool pressed=(item->itemState&ODS_SELECTED)!=0;
 if(item->CtlID==kMain){
   GraphicsPath path;Round(path,RectF(1,1,w-2,h-2),22);
   SolidBrush fill(pressed?Color(255,42,65,86):Color(255,25,45,64));g.FillPath(&fill,&path);
   Text(g,stage==Stage::Finished?L"Готово":stage==Stage::Error?L"Повторить":L"Установить  →",15,RectF(0,0,w,h),Color(255,255,255,255));
   if(item->itemState&ODS_FOCUS){Pen line(Color(255,119,158,192),1);g.DrawPath(&line,&path);}
 }else if(item->CtlID==kLaunch){
   GraphicsPath box;Round(box,RectF(14,7,16,16),4);SolidBrush fill(launch?Color(255,30,54,77):Color(255,255,255,255));g.FillPath(&fill,&box);
   Pen border(Color(255,126,139,150),1);g.DrawPath(&border,&box);
   if(launch){Pen check(Color(255,255,255,255),1.6f);g.DrawLine(&check,18.0f,15.0f,21.0f,18.0f);g.DrawLine(&check,21.0f,18.0f,27.0f,11.0f);}
   Text(g,L"Запустить Soulu",12.5f,RectF(35,0,w-42,h),Color(255,79,92,108));
 }else{
   if(pressed){SolidBrush fill(Color(24,40,60,80));g.FillRectangle(&fill,0.0f,0.0f,w,h);}
   Pen pen(Color(255,42,61,81),1.2f);
   if(item->CtlID==kClose){g.DrawLine(&pen,w/2-4.5f,h/2-4.5f,w/2+4.5f,h/2+4.5f);g.DrawLine(&pen,w/2+4.5f,h/2-4.5f,w/2-4.5f,h/2+4.5f);}
   else g.DrawLine(&pen,w/2-5,h/2,w/2+5,h/2);
 }
}
HWND Button(int id,const wchar_t* text,int x,int y,int width,int height){
 return CreateWindowExW(0,L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
    Px(x),Px(y),Px(width),Px(height),window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
}
void ShowStage(Stage next){
 stage=next;
 const bool finished=stage==Stage::Finished;
 SetWindowTextW(mainButton,finished?L"Готово":stage==Stage::Error?L"Повторить":L"Установить");
 SetWindowPos(mainButton,nullptr,Px(190),Px(finished?268:284),Px(220),Px(46),SWP_NOZORDER);
 ShowWindow(mainButton,stage==Stage::Installing?SW_HIDE:SW_SHOW);
 ShowWindow(launchButton,finished?SW_SHOW:SW_HIDE);
 InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);
 if(stage!=Stage::Installing)SetFocus(mainButton);
}
void CleanupWorker(bool cancel){
 HANDLE process=worker.exchange(nullptr);
 if(process){if(cancel&&WaitForSingleObject(process,0)==WAIT_TIMEOUT)TerminateProcess(process,2);CloseHandle(process);}
 if(workerJob){CloseHandle(workerJob);workerJob=nullptr;}
 if(!payloadPath.empty())DeleteFileW(payloadPath.c_str());
}
void StartInstall(){
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
 std::thread([target,job,path,folder]{
   HRSRC resource=FindResourceW(nullptr,MAKEINTRESOURCEW(100),RT_RCDATA);
   HGLOBAL bytes=resource?LoadResource(nullptr,resource):nullptr;
   const DWORD length=resource?SizeofResource(nullptr,resource):0;
   const void* data=bytes?LockResource(bytes):nullptr;
   HANDLE output=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_TEMPORARY,nullptr);
   DWORD written=0;
   bool ok=data&&output!=INVALID_HANDLE_VALUE&&WriteFile(output,data,length,&written,nullptr)&&written==length;
   if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);
   if(ok){
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
   if(!ok)PostMessageW(target,WM_APP+2,0,0);
 }).detach();
 SetTimer(window,1,100,nullptr);
}
void Finish(){
 if(stage==Stage::Welcome||stage==Stage::Error){PostMessageW(window,WM_APP+1,0,0);return;}
 if(stage!=Stage::Finished)return;
 bool start=launch;std::wstring exe=installDir+L"\\Soulu.exe";
 DestroyWindow(window);
 if(start)ShellExecuteW(nullptr,L"open",exe.c_str(),nullptr,installDir.c_str(),SW_SHOWNORMAL);
}
LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
 switch(msg){
  case WM_CREATE:window=hwnd;return 0;
  case WM_ERASEBKGND:return 1;
  case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);Paint(dc);EndPaint(hwnd,&ps);return 0;}
  case WM_DRAWITEM:DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lp));return TRUE;
  case WM_NCHITTEST:{POINT p={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&p);HWND child=ChildWindowFromPointEx(hwnd,p,CWP_SKIPINVISIBLE|CWP_SKIPDISABLED);return child&&child!=hwnd?HTCLIENT:HTCAPTION;}
  case WM_COMMAND:
    if(HIWORD(wp)==BN_CLICKED){switch(LOWORD(wp)){
      case kMain:Finish();break;
      case kClose:SendMessageW(hwnd,WM_CLOSE,0,0);break;
      case kMin:ShowWindow(hwnd,SW_MINIMIZE);break;
      case kLaunch:launch=!launch;InvalidateRect(launchButton,nullptr,FALSE);break;
    }}return 0;
  case WM_APP+1:if(stage!=Stage::Installing)StartInstall();return 0;
  case WM_APP+2:KillTimer(hwnd,1);CleanupWorker(false);errorText=L"Не удалось запустить установку. Попробуй ещё раз.";ShowStage(Stage::Error);return 0;
  case WM_TIMER:if(worker.load()&&WaitForSingleObject(worker.load(),0)==WAIT_OBJECT_0){
    DWORD code=1;GetExitCodeProcess(worker.load(),&code);KillTimer(hwnd,1);CleanupWorker(false);
    if(code==0&&GetFileAttributesW((installDir+L"\\Soulu.exe").c_str())!=INVALID_FILE_ATTRIBUTES)ShowStage(Stage::Finished);
    else{errorText=L"Установка не завершена. Закрой Soulu и повтори попытку.";ShowStage(Stage::Error);}
  }return 0;
  case WM_CLOSE:CleanupWorker(true);DestroyWindow(hwnd);return 0;
  case WM_DESTROY:PostQuitMessage(0);return 0;
 }
 return DefWindowProcW(hwnd,msg,wp,lp);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,wchar_t*,int){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 GdiplusStartupInput input;GdiplusStartup(&graphicsToken,&input,nullptr);
 wchar_t local[MAX_PATH]={};GetEnvironmentVariableW(L"LOCALAPPDATA",local,MAX_PATH);installDir=std::wstring(local)+L"\\Programs\\Soulu";
 icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,256,256,LR_DEFAULTCOLOR));
 HRSRC resource=FindResourceW(instance,MAKEINTRESOURCEW(102),RT_RCDATA);
 if(resource){
   DWORD length=SizeofResource(instance,resource);HGLOBAL source=LoadResource(instance,resource);
   HGLOBAL copy=GlobalAlloc(GMEM_MOVEABLE,length);
   if(copy){void* data=GlobalLock(copy);memcpy(data,LockResource(source),length);GlobalUnlock(copy);
     if(SUCCEEDED(CreateStreamOnHGlobal(copy,TRUE,&logoStream)))logo=Bitmap::FromStream(logoStream);
     else GlobalFree(copy);
   }
 }
 WNDCLASSEXW cls={sizeof(cls)};cls.lpfnWndProc=Proc;cls.hInstance=instance;cls.lpszClassName=L"SouluInstaller";
 cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hIcon=icon;cls.hIconSm=icon;RegisterClassExW(&cls);
 scale=GetDpiForSystem()/96.0f;
 RECT work={};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
 window=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,L"Soulu Setup",WS_POPUP|WS_MINIMIZEBOX|WS_SYSMENU,
   work.left+(work.right-work.left-Px(kWidth))/2,work.top+(work.bottom-work.top-Px(kHeight))/2,Px(kWidth),Px(kHeight),nullptr,nullptr,instance,nullptr);
 HRGN region=CreateRoundRectRgn(0,0,Px(kWidth)+1,Px(kHeight)+1,Px(20),Px(20));SetWindowRgn(window,region,TRUE);
 DWORD corner=2;DwmSetWindowAttribute(window,33,&corner,sizeof(corner));
 mainButton=Button(kMain,L"Установить",190,284,220,46);
 Button(kMin,L"Свернуть",514,10,36,32);Button(kClose,L"Закрыть",552,10,36,32);
 launchButton=Button(kLaunch,L"Запустить Soulu",210,322,180,30);ShowWindow(launchButton,SW_HIDE);
 ShowWindow(window,SW_SHOW);SetFocus(mainButton);UpdateWindow(window);
 MSG message;while(GetMessageW(&message,nullptr,0,0)>0){
   if(message.message==WM_KEYDOWN&&message.wParam==VK_RETURN){Finish();continue;}
   if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE){SendMessageW(window,WM_CLOSE,0,0);continue;}
   if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
 }
 CleanupWorker(false);if(icon)DestroyIcon(icon);delete logo;if(logoStream)logoStream->Release();GdiplusShutdown(graphicsToken);return 0;
}
