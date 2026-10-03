#pragma once
#include <functional>
#include "include/cef_request_context.h"
#include <windows.h>
namespace soulu {
// Uses Chromium's own profile-wide remover, not an origin list inferred from visits.
void ClearProfileWebData(HWND parent,CefRefPtr<CefRequestContext> context,
                         bool sites,bool cache,std::function<void(bool)> done);
}
