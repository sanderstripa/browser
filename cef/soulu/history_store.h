#pragma once
#include "examples/soulu/profile_data.h"

struct sqlite3;
namespace soulu {
// The single Soulu visit store, shared by History and address suggestions.
class HistoryStore {
 public:
  explicit HistoryStore(const std::string& profile);
  ~HistoryStore();
  bool Healthy() const { return db_ != nullptr; }
  std::string Add(const std::string& url, const std::string& title,
                  const std::string& favicon, double time);
  bool Update(const std::string& id, const std::string& title, const std::string& favicon);
  CefRefPtr<CefListValue> Query(const std::string& search, double begin,
                              double end, int offset, int limit);
  bool Remove(const std::string& id);
  bool Clear(double begin, double end);
 private:
  sqlite3* db_ = nullptr;
};
double HistoryNow();
}
