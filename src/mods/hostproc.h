#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace pp::mods {

enum class IoResult { Ok, Timeout, Closed };

class HostProcess {
 public:
  static std::unique_ptr<HostProcess> start(const std::filesystem::path& exe, std::string* err);
  ~HostProcess();
  IoResult send(const std::vector<uint8_t>& frame, int timeoutMs);
  IoResult receive(std::vector<uint8_t>& frame, int timeoutMs);
  void stop(bool force = false);
  bool running() const;
  std::string exitText();
  std::string problem() const;
  struct Impl;

 private:
  HostProcess() = default;
  std::unique_ptr<Impl> im_;
};

std::filesystem::path findModHost();

}
