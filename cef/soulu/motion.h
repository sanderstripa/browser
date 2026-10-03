#pragma once

namespace soulu::motion {
inline constexpr int kMicroMs = 140;
inline constexpr int kSurfaceMs = 180;
inline constexpr int kStructuralMs = 260;
inline constexpr int kReducedOverlayMs = 90;
inline constexpr int kSidebarCloseMs = 180;
// CSS cubic-bezier(.22,1,.36,1), sampled by monotonic native timers.
inline float EaseOut(float x) {
  float lo = 0, hi = 1, t = x;
  for (int i = 0; i < 16; ++i) {
    t = (lo + hi) / 2;
    const float v = 3*(1-t)*(1-t)*t*.22f + 3*(1-t)*t*t*.36f + t*t*t;
    if (v < x) lo = t; else hi = t;
  }
  return 1 - (1-t)*(1-t)*(1-t);
}
}
