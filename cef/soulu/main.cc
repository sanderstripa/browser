#include <windows.h>

#include <filesystem>
#include <cstdlib>
#include <fstream>
#include "examples/soulu/engine_version.h"

#include "examples/soulu/app_factory.h"
#include "examples/soulu/profile_data.h"
#include "include/cef_command_line.h"
namespace soulu { int RunDataSecurityTests(const std::filesystem::path&); }

namespace {
std::filesystem::path SouluDataRoot() {
  // Chrome-style CEF requires disk profiles to be immediate children of
  // root_cache_path. BrowserWindow uses Soulu/User Data/Profiles/<id>.
  // Keep those paths unchanged and align the CEF root with their parent.
  auto root=soulu::DataRoot()/L"Profiles";root.make_preferred();return root;
}

std::wstring LocalDataPath() {
  return (SouluDataRoot() / L"CEF").wstring();
}
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int) {
  CefMainArgs main_args(instance);
  auto command_line = CefCommandLine::CreateCommandLine();
  command_line->InitFromString(GetCommandLineW());
  if(command_line->HasSwitch("data-security-test-report")){
    wchar_t enabled[12]={};
    if(!GetEnvironmentVariableW(L"SOULU_UI_TEST_PORT",enabled,12))return 2;
    wchar_t test_root[32768]={},local[32768]={},roaming[32768]={};
    if(!GetEnvironmentVariableW(L"SOULU_DATA_SECURITY_TEST_ROOT",test_root,32768)||
       !GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768)||
       !GetEnvironmentVariableW(L"APPDATA",roaming,32768)||
       std::wstring(test_root)!=local||std::wstring(test_root)!=roaming)return 2;
    return soulu::RunDataSecurityTests(std::filesystem::path(command_line->GetSwitchValue("data-security-test-report").ToWString()));
  }
  // CI probes the linked libcef before profile initialization or message loops.
  if (command_line->HasSwitch("engine-version-file")) {
    const std::filesystem::path output(
        command_line->GetSwitchValue("engine-version-file").ToWString());
    std::ofstream report(output);
    report << "{\"cef\":\"" << soulu::EngineVersion(0, 3)
           << "\",\"chromium\":\"" << soulu::EngineVersion(4, 4)
           << "\",\"cef_build\":\"" << soulu::RuntimeCEFBuild() << "\"}";
    report.close();
    return report && soulu::ApprovedEngine() ? 0 : 2;
  }
  if (!soulu::ApprovedEngine()) return 2;
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
  const auto deletions=soulu::PendingProfileDeletions();
  CefShutdown();
  soulu::CleanupDeletedProfiles(deletions);
  return 0;
}

