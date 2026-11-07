#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>

#include "sockcompat.h"

namespace pp::mods {

class TlsStream {
 public:
  enum class Status { Ok, Idle, Closed, Cut, Failed };

  ~TlsStream();
  TlsStream(const TlsStream&) = delete;
  TlsStream& operator=(const TlsStream&) = delete;

  static std::unique_ptr<TlsStream> connect(sock_t s, const std::string& host, int timeoutMs, const std::atomic<bool>* stop, std::string* err);

  Status read(char* buf, size_t cap, size_t* got, int waitMs, std::string* err);
  bool writeAll(const char* data, size_t len, int idleMs, std::string* err);
  void shutdown();

 private:
  struct Impl;
  explicit TlsStream(std::unique_ptr<Impl> p);
  std::unique_ptr<Impl> p_;
};

}
