#include "examples/soulu/frosted_backdrop.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <windows.ui.composition.interop.h>
#include <DispatcherQueue.h>
#include <dwmapi.h>
#include <map>
namespace soulu {
namespace {
using namespace winrt;
using namespace Windows::UI::Composition;
struct Backdrop {
  Windows::System::DispatcherQueueController queue{nullptr};
  Compositor compositor{nullptr};
  Desktop::DesktopWindowTarget target{nullptr};
  SpriteVisual visual{nullptr};
};
std::map<HWND,Backdrop> backdrops;
}
int BackdropCapabilities(){
  BOOL composition=FALSE;DwmIsCompositionEnabled(&composition);
  int flags=composition?1:0;
  try{if(winrt::Windows::UI::ViewManagement::UISettings().AdvancedEffectsEnabled())flags|=2;}catch(...){}
  if(GetSystemMetrics(SM_REMOTESESSION))flags|=4;
  HIGHCONTRASTW contrast={sizeof(contrast)};
  if(SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags&HCF_HIGHCONTRASTON))flags|=8;
  return flags;
}
bool ConfigureFrostedBackdrop(HWND window,bool enabled){
  try {
    auto& state=backdrops[window];
    if(!state.compositor){
      if(!Windows::System::DispatcherQueue::GetForCurrentThread()){
        DispatcherQueueOptions options={sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_NONE};
        check_hresult(CreateDispatcherQueueController(options,reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(put_abi(state.queue))));
      }
      state.compositor=Compositor();
      auto interop=state.compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
      check_hresult(interop->CreateDesktopWindowTarget(window,false,reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(put_abi(state.target))));
      state.visual=state.compositor.CreateSpriteVisual();
      state.visual.Brush(state.compositor.CreateHostBackdropBrush());
      state.target.Root(state.visual);
    }
    BOOL host=enabled?TRUE:FALSE;
    check_hresult(DwmSetWindowAttribute(window,17,&host,sizeof(host)));
    state.visual.IsVisible(enabled);
    return enabled;
  }catch(const hresult_error& error){
    wchar_t text[96]={};swprintf_s(text,L"Soulu backdrop error: 0x%08X\n",static_cast<unsigned>(error.code().value));
    OutputDebugStringW(text);
    return false;
  }
}
void ResizeFrostedBackdrop(HWND window,int width,int height){
  const auto found=backdrops.find(window);
  if(found!=backdrops.end()&&found->second.visual)
    found->second.visual.Size({static_cast<float>(width),static_cast<float>(height)});
}
void SetFrostedBackdropOpacity(HWND window,float opacity){
  const auto found=backdrops.find(window);
  if(found!=backdrops.end()&&found->second.visual)found->second.visual.Opacity(opacity);
}
void ReleaseFrostedBackdrop(HWND window){backdrops.erase(window);}
}
