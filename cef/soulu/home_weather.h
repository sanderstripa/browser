#pragma once
#include <string>
#include "include/cef_values.h"

namespace soulu {
// A provider returns cached state immediately. Future network refreshes must
// run asynchronously; Home never waits for a provider or requests location.
class HomeWeatherProvider {
 public:
  virtual ~HomeWeatherProvider() = default;
  virtual CefRefPtr<CefDictionaryValue> Snapshot(const std::string& city) const = 0;
};
class UnconfiguredHomeWeather final : public HomeWeatherProvider {
 public:
  CefRefPtr<CefDictionaryValue> Snapshot(const std::string& city) const override {
    auto data=CefDictionaryValue::Create();
    data->SetString("status","unavailable");
    data->SetString("reason","provider-not-configured");
    data->SetString("city",city);
    return data;
  }
};
}
