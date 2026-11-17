#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "mods/tls.h"
#include "online/endpoint.h"
#include "sockcompat.h"

namespace pp::online {

class WebSocket {
 public:
  static std::unique_ptr<WebSocket> connect(sock_t socket, const LobbyEndpoint& endpoint, const std::atomic<bool>* stop, std::string* err);
  int read(char* data, size_t cap);
  int write(const char* data, size_t len);
  bool poll();
  bool close();
  bool drained() const { return outgoing_.empty(); }
  const std::string& error() const { return error_; }

 private:
  std::unique_ptr<mods::TlsStream> tls_;
  std::vector<uint8_t> wire_, message_;
  std::deque<std::vector<uint8_t>> outgoing_;
  size_t offset_ = 0, received_ = 0, queued_ = 0;
  bool fragmented_ = false, closing_ = false, closed_ = false;
  std::string error_;
  bool frame(uint8_t opcode, const char* data, size_t len);
  bool parse();
  bool fail(const std::string& error, uint16_t code = 1002);
};

}
