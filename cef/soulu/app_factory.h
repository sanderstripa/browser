#pragma once

#include "include/cef_app.h"

namespace soulu {
CefRefPtr<CefApp> CreateBrowserApp();
CefRefPtr<CefApp> CreateRendererApp();
CefRefPtr<CefApp> CreateOtherApp();
}
