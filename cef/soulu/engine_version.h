#pragma once

#include <string>
#include "include/cef_version_info.h"

// Refuse compilation against any engine other than the approved distribution.
static_assert(CEF_VERSION_MAJOR == 154 && CEF_VERSION_MINOR == 0 &&
              CEF_VERSION_PATCH == 32, "Unsupported CEF headers");
static_assert(CHROME_VERSION_MAJOR == 154 && CHROME_VERSION_MINOR == 0 &&
              CHROME_VERSION_BUILD == 8037 && CHROME_VERSION_PATCH == 58,
              "Unsupported Chromium headers");

namespace soulu {
inline std::string EngineVersion(int first, int count) {
  std::string version;
  for (int i = first; i < first + count; ++i) {
    if (i != first) version += ".";
    version += std::to_string(cef_version_info(i));
  }
  return version;
}
inline bool ApprovedEngine() {
  return EngineVersion(0, 3) == "154.0.32" &&
         EngineVersion(4, 4) == "154.0.8037.58";
}
}
