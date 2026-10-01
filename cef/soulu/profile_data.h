#pragma once
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include "include/cef_values.h"

namespace soulu {
std::filesystem::path DataRoot();
bool ValidProfileId(const std::string& id);
std::filesystem::path ProfileRoot(const std::string& id);
std::string WebOrigin(const std::string& url);
std::string SiteDomain(const std::string& url);
std::string RandomId();
bool WriteJson(const std::filesystem::path&, CefRefPtr<CefValue>);
CefRefPtr<CefValue> ReadJson(const std::filesystem::path&);

// All secrets cross the disk boundary only after user-scope DPAPI encryption.
class PasswordVault {
 public:
  explicit PasswordVault(const std::string& profile);
  CefRefPtr<CefListValue> List() const;
  bool Put(const std::string& origin, const std::string& username,
           const std::string& password, bool replace);
  bool Remove(const std::string& id);
  bool Reveal(const std::string& id, std::string& secret) const;
  bool Contains(const std::string& origin, const std::string& username) const;
 private:
  std::string profile_;
  std::filesystem::path path_;
  CefRefPtr<CefListValue> rows_;
  bool healthy_ = true;
};

// Separate persistent models for permissions and content-blocker exceptions.
// IO-thread filters read immutable snapshots under this lock, never tab state.
class SitePolicy {
 public:
  explicit SitePolicy(const std::string& profile);
  int Rule(const std::string& url, const std::string& permission) const;
  bool Blocking(const std::string& url) const;
  CefRefPtr<CefDictionaryValue> Snapshot() const;
  bool Set(const std::string& domain, const std::string& permission, int value);
  bool SetBlocking(const std::string& domain, int value);
  bool Reset(const std::string& domain);
 private:
  bool Save() const;
  mutable std::mutex mutex_;
  std::filesystem::path path_;
  CefRefPtr<CefDictionaryValue> data_;
};
bool BlockResource(const std::string& top_url, const std::string& url,
                   int resource_type, bool enabled);
CefRefPtr<CefListValue> DiscoverPasswordSources();
CefRefPtr<CefDictionaryValue> ImportPasswords(const std::string& source_id,
                                              const std::string& target);
std::vector<std::string> PendingProfileDeletions();
void CleanupDeletedProfiles(const std::vector<std::string>& ids);
}
