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
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"dwmapi.lib")
#pragma comment(lib,"shell32.lib")
#pragma comment(lib,"ole32.lib")
using namespace Gdiplus;
namespace {
constexpr int kWidth=900,kHeight=593,kMain=1001,kMin=1002,kClose=1003,kLaunch=1004;
enum class Stage { Welcome, Installing, Finished, Error };
HWND window=nullptr,mainButton=nullptr,launchButton=nullptr;
std::atomic<HANDLE> worker{nullptr};
HANDLE workerJob=nullptr;
Stage stage=Stage::Welcome;
bool launch=true;
float scale=1;
HICON icon=nullptr;
IStream* screenStreams[3]={};
Bitmap* screens[3]={};
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
Bitmap* CurrentScreen(){
 if(stage==Stage::Installing||stage==Stage::Error)return screens[1];
 return stage==Stage::Finished?screens[2]:screens[0];
}
void Paint(HDC dc){
 Bitmap buffer(Px(kWidth),Px(kHeight),PixelFormat32bppPARGB);Graphics g(&buffer);
 g.ScaleTransform(scale,scale);g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetInterpolationMode(InterpolationModeHighQualityBicubic);g.SetPixelOffsetMode(PixelOffsetModeHighQuality);g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
 if(Bitmap* image=CurrentScreen())g.DrawImage(image,RectF(0,0,kWidth,kHeight),0,0,image->GetWidth(),image->GetHeight(),UnitPixel);
 else{SolidBrush fallback(Color(255,247,250,255));g.FillRectangle(&fallback,0,0,kWidth,kHeight);}
 if(stage==Stage::Finished&&!launch){
   SolidBrush cover(Color(255,238,246,255));g.FillRectangle(&cover,360.0f,448.0f,29.0f,29.0f);
   GraphicsPath box;Round(box,RectF(363,451,24,24),5);Pen line(Color(255,66,104,164),1.4f);g.DrawPath(&line,&box);
 }
 if(stage==Stage::Error){
   SolidBrush panel(Color(236,247,250,255));GraphicsPath card;Round(card,RectF(235,194,430,170),22);g.FillPath(&panel,&card);
   Text(g,L"Не удалось завершить установку",26,RectF(255,216,390,42),Color(255,12,43,84));
   Text(g,errorText.c_str(),15,RectF(270,267,360,58),Color(255,78,99,135));
 }
 Graphics screen(dc);screen.DrawImage(&buffer,0,0);
}
void DrawButton(DRAWITEMSTRUCT* item){
 FillRect(item->hDC,&item->rcItem,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
}
HWND Button(int id,const wchar_t* text,int x,int y,int width,int height){
 HWND button=CreateWindowExW(WS_EX_LAYERED,L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,
    Px(x),Px(y),Px(width),Px(height),window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
 SetLayeredWindowAttributes(button,0,1,LWA_ALPHA);
 return button;
}
void ShowStage(Stage next){
 stage=next;
 const bool finished=stage==Stage::Finished;
 SetWindowTextW(mainButton,finished?L"Готово":stage==Stage::Error?L"Повторить":L"Установить");
 SetWindowPos(mainButton,nullptr,Px(291),Px(finished?367:380),Px(320),Px(64),SWP_NOZORDER);
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
      case kLaunch:launch=!launch;InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);break;
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
 screens[0]=LoadPngResource(102,&screenStreams[0]);
 screens[1]=LoadPngResource(103,&screenStreams[1]);
 screens[2]=LoadPngResource(104,&screenStreams[2]);
 WNDCLASSEXW cls={sizeof(cls)};cls.lpfnWndProc=Proc;cls.hInstance=instance;cls.lpszClassName=L"SouluInstaller";
 cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hIcon=icon;cls.hIconSm=icon;RegisterClassExW(&cls);
 scale=GetDpiForSystem()/96.0f;
 RECT work={};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
 window=CreateWindowExW(WS_EX_APPWINDOW,cls.lpszClassName,L"Soulu Setup",WS_POPUP|WS_MINIMIZEBOX|WS_SYSMENU,
   work.left+(work.right-work.left-Px(kWidth))/2,work.top+(work.bottom-work.top-Px(kHeight))/2,Px(kWidth),Px(kHeight),nullptr,nullptr,instance,nullptr);
 HRGN region=CreateRoundRectRgn(0,0,Px(kWidth)+1,Px(kHeight)+1,Px(28),Px(28));SetWindowRgn(window,region,TRUE);
 DWORD corner=2;DwmSetWindowAttribute(window,33,&corner,sizeof(corner));
 mainButton=Button(kMain,L"Установить",291,380,320,64);
 Button(kMin,L"Свернуть",780,15,46,40);Button(kClose,L"Закрыть",840,15,46,40);
 launchButton=Button(kLaunch,L"Запустить Soulu",350,444,220,40);ShowWindow(launchButton,SW_HIDE);
 ShowWindow(window,SW_SHOW);SetFocus(mainButton);UpdateWindow(window);
 MSG message;while(GetMessageW(&message,nullptr,0,0)>0){
   if(message.message==WM_KEYDOWN&&message.wParam==VK_RETURN){Finish();continue;}
   if(message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE){SendMessageW(window,WM_CLOSE,0,0);continue;}
   if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
 }
 CleanupWorker(false);if(icon)DestroyIcon(icon);for(int i=0;i<3;++i){delete screens[i];if(screenStreams[i])screenStreams[i]->Release();}GdiplusShutdown(graphicsToken);return 0;
}
