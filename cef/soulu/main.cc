#include <windows.h>

#include <filesystem>
#include <cstdlib>

#include "examples/soulu/app_factory.h"
#include "include/cef_command_line.h"

namespace {
std::filesystem::path SouluDataRoot() {
  wchar_t buffer[MAX_PATH] = {};
  DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
  std::filesystem::path root = size ? buffer : L".";
  // Chrome-style CEF requires disk profiles to be immediate children of
  // root_cache_path. BrowserWindow uses Soulu/User Data/Profiles/<id>.
  // Keep those paths unchanged and align the CEF root with their parent.
  return root / L"Soulu" / L"User Data" / L"Profiles";
}

std::wstring LocalDataPath() {
  return (SouluDataRoot() / L"CEF").wstring();
}
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int) {
  CefMainArgs main_args(instance);
  auto command_line = CefCommandLine::CreateCommandLine();
  command_line->InitFromString(GetCommandLineW());
  const auto process_type = command_line->GetSwitchValue("type");

  CefRefPtr<CefApp> app;
  if (process_type == "renderer") app = soulu::CreateRendererApp();
  else if (process_type.empty()) app = soulu::CreateBrowserApp();
  else app = soulu::CreateOtherApp();

  const int code = CefExecuteProcess(main_args, app, nullptr);
  if (code >= 0) return code;

  CefSettings settings;
  settings.no_sandbox = true;
  settings.windowless_rendering_enabled = true;
  // Expose DevTools only in the dedicated CI test process.
  wchar_t test_port[12] = {};
  if (GetEnvironmentVariableW(L"SOULU_UI_TEST_PORT", test_port, 12) > 0)
    settings.remote_debugging_port = _wtoi(test_port);
  settings.multi_threaded_message_loop = false;
  CefString(&settings.cache_path) = LocalDataPath();
  CefString(&settings.root_cache_path) = SouluDataRoot().wstring();
  settings.persist_session_cookies = 1;
  CefString(&settings.locale) = "ru-RU";
  CefString(&settings.accept_language_list) = "ru-RU,ru,en-US,en";
  if (!CefInitialize(main_args, settings, app, nullptr)) return 1;
  CefRunMessageLoop();
  CefShutdown();
  return 0;
}

