#include "online/websocket.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"

namespace pp::online {
namespace {

constexpr size_t maxMessage = 1 << 20;
using Clock = std::chrono::steady_clock;

std::string base64(const unsigned char* data, size_t len) {
  unsigned char out[128];
  size_t n = 0;
  if (mbedtls_base64_encode(out, sizeof out, &n, data, len) != 0) return {};
  return std::string((const char*)out, n);
}

std::string lower(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t"), e = s.find_last_not_of(" \t");
  return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

bool token(const std::string& s, const std::string& wanted) {
  std::istringstream in(lower(s));
  std::string item;
  while (std::getline(in, item, ',')) if (trim(item) == wanted) return true;
  return false;
}

bool upgrade(const std::string& response, const std::string& key) {
  std::istringstream in(response);
  std::string line;
  if (!std::getline(in, line) || line.rfind("HTTP/1.1 101 ", 0) != 0) return false;
  std::map<std::string, std::string> headers;
  while (std::getline(in, line) && line != "\r") {
    if (line.empty() || line.back() != '\r') return false;
    line.pop_back();
    size_t c = line.find(':');
    if (c == std::string::npos) return false;
    const std::string name = lower(line.substr(0, c));
    if (name.empty() || (headers.count(name) && (name == "connection" || name == "upgrade" || name.rfind("sec-websocket-", 0) == 0))) return false;
    headers[name] = trim(line.substr(c + 1));
  }
  const std::string source = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  unsigned char digest[20];
  if (mbedtls_sha1((const unsigned char*)source.data(), source.size(), digest) != 0) return false;
  return lower(headers["upgrade"]) == "websocket" && token(headers["connection"], "upgrade") && headers["sec-websocket-accept"] == base64(digest, sizeof digest) &&
         !headers.count("sec-websocket-extensions") && !headers.count("sec-websocket-protocol");
}

bool utf8(const uint8_t* data, size_t len) {
  size_t i = 0;
  while (i < len) {
    uint32_t c = data[i++], min = 0;
    int n = 0;
    if (c < 128) continue;
    if (c >= 0xc2 && c <= 0xdf) { n = 1; c &= 31; min = 0x80; }
    else if (c >= 0xe0 && c <= 0xef) { n = 2; c &= 15; min = 0x800; }
    else if (c >= 0xf0 && c <= 0xf4) { n = 3; c &= 7; min = 0x10000; }
    else return false;
    if (i + (size_t)n > len) return false;
    while (n--) {
      uint8_t b = data[i++];
      if ((b & 0xc0) != 0x80) return false;
      c = (c << 6) | (b & 63);
    }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
  }
  return true;
}

}

std::unique_ptr<WebSocket> WebSocket::connect(sock_t socket, const LobbyEndpoint& ep, const std::atomic<bool>* stop, std::string* err) {
  if (!ep.secure) { *err = "WebSocket: secure endpoint required"; return nullptr; }
  auto ws = std::unique_ptr<WebSocket>(new WebSocket);
  ws->tls_ = mods::TlsStream::connect(socket, ep.host, 5000, stop, err);
  if (!ws->tls_) return nullptr;
  unsigned char nonce[16];
  if (!mods::TlsStream::randomBytes(nonce, sizeof nonce)) { *err = "WebSocket: random source unavailable"; return nullptr; }
  const std::string key = base64(nonce, sizeof nonce);
  const std::string host = ep.host.find(':') == std::string::npos ? ep.host : "[" + ep.host + "]";
  const std::string request = "GET " + ep.path + " HTTP/1.1\r\nHost: " + host + (ep.port == 443 ? "" : ":" + std::to_string(ep.port)) +
    "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key + "\r\nSec-WebSocket-Version: 13\r\n\r\n";
  if (!ws->tls_->writeAll(request.data(), request.size(), 5000, err)) return nullptr;
  std::string response;
  const auto deadline = Clock::now() + std::chrono::seconds(5);
  for (;;) {
    size_t end = response.find("\r\n\r\n");
    if (end != std::string::npos) {
      if (end + 4 > 8192 || !upgrade(response.substr(0, end + 4), key)) { *err = "WebSocket: invalid upgrade response"; return nullptr; }
      ws->wire_.assign(response.begin() + end + 4, response.end());
      ws->tls_->clearCancellation();
      return ws;
    }
    if (response.size() >= 8192) { *err = "WebSocket: upgrade response too large"; return nullptr; }
    if (stop && stop->load()) { *err = "WebSocket: connection cancelled"; return nullptr; }
    if (Clock::now() >= deadline) { *err = "WebSocket: upgrade timed out"; return nullptr; }
    char data[1024];
    size_t got = 0;
    const auto status = ws->tls_->read(data, sizeof data, &got, 20, err);
    if (status == mods::TlsStream::Status::Ok) response.append(data, got);
    else if (status != mods::TlsStream::Status::Idle) { if (err->empty()) *err = "WebSocket: upgrade connection closed"; return nullptr; }
  }
}

bool WebSocket::frame(uint8_t opcode, const char* data, size_t len) {
  if (queued_ + len + 14 > 65536) return false;
  unsigned char mask[4];
  if (!mods::TlsStream::randomBytes(mask, sizeof mask)) { error_ = "WebSocket: random source unavailable"; return false; }
  std::vector<uint8_t> f;
  f.reserve(len + 14);
  f.push_back(0x80 | opcode);
  if (len < 126) f.push_back(0x80 | (uint8_t)len);
  else {
    f.push_back(0x80 | 126);
    f.push_back((uint8_t)(len >> 8)); f.push_back((uint8_t)len);
  }
  f.insert(f.end(), mask, mask + 4);
  for (size_t i = 0; i < len; i++) f.push_back((uint8_t)data[i] ^ mask[i & 3]);
  queued_ += f.size();
  outgoing_.push_back(std::move(f));
  return true;
}

bool WebSocket::poll() {
  for (int i = 0; i < 16 && !outgoing_.empty(); i++) {
    auto& f = outgoing_.front();
    size_t sent = 0;
    auto status = tls_->writeSome((const char*)f.data() + offset_, f.size() - offset_, &sent, &error_);
    if (status == mods::TlsStream::Status::Idle) break;
    if (status != mods::TlsStream::Status::Ok) { if (error_.empty()) error_ = "WebSocket: sending failed"; return false; }
    offset_ += sent;
    queued_ -= sent;
    if (offset_ == f.size()) { outgoing_.pop_front(); offset_ = 0; }
  }
  return error_.empty();
}

int WebSocket::write(const char* data, size_t len) {
  if (!poll() || closing_ || closed_) return -2;
  if (len == 0) return 0;
  size_t n = std::min<size_t>(len, 16384);
  if (queued_ + n + 14 > 49152) return -1;
  if (!frame(2, data, n)) return error_.empty() ? -1 : -2;
  poll();
  return (int)n;
}

bool WebSocket::fail(const std::string& error, uint16_t code) {
  error_ = "WebSocket: " + error;
  if (!closing_) {
    const char body[2] = {(char)(code >> 8), (char)code};
    frame(8, body, sizeof body);
    closing_ = true;
  }
  return false;
}

bool WebSocket::parse() {
  if (wire_.size() < 2) return true;
  uint8_t first = wire_[0], second = wire_[1], opcode = first & 15;
  bool fin = (first & 128) != 0, control = (opcode & 8) != 0;
  if ((first & 0x70) || (second & 0x80) || !(opcode == 0 || opcode == 2 || opcode == 8 || opcode == 9 || opcode == 10)) return fail("invalid frame");
  if (control && (!fin || (second & 127) > 125)) return fail("invalid control frame");
  uint64_t len = second & 127;
  size_t header = 2;
  if (len == 126 || len == 127) {
    size_t bytes = len == 126 ? 2 : 8;
    if (wire_.size() < header + bytes) return true;
    len = 0;
    for (size_t i = 0; i < bytes; i++) len = (len << 8) | wire_[header++];
    if ((bytes == 2 && len < 126) || (bytes == 8 && (len < 65536 || (wire_[2] & 128)))) return fail("invalid frame length");
  }
  if (!control) {
    if ((opcode == 0) != fragmented_) return fail("invalid continuation");
    if (len > maxMessage || message_.size() > maxMessage - (size_t)len) return fail("message too large", 1009);
  }
  if (wire_.size() - header < len) return true;
  const uint8_t* body = wire_.data() + header;
  if (opcode == 8) {
    if (len == 1) return fail("invalid close frame");
    if (len >= 2) {
      int code = (int)body[0] * 256 + body[1];
      if (!((code >= 1000 && code <= 1014 && code != 1004 && code != 1005 && code != 1006) || (code >= 3000 && code <= 4999))) return fail("invalid close code");
      if (!utf8(body + 2, (size_t)len - 2)) return fail("invalid close reason", 1007);
    }
    if (!closing_ && !frame(8, (const char*)body, (size_t)len)) return fail("control queue full", 1008);
    closing_ = closed_ = true;
  } else if (opcode == 9) {
    if (!closing_ && !frame(10, (const char*)body, (size_t)len)) return fail("control queue full", 1008);
  } else if (!control) {
    message_.insert(message_.end(), body, body + len);
    fragmented_ = !fin;
  }
  wire_.erase(wire_.begin(), wire_.begin() + header + (size_t)len);
  return true;
}

int WebSocket::read(char* data, size_t cap) {
  if (!cap) return -1;
  poll();
  for (int i = 0; i < 64; i++) {
    if (!fragmented_ && received_ < message_.size()) {
      size_t n = std::min(cap, message_.size() - received_);
      std::memcpy(data, message_.data() + received_, n);
      received_ += n;
      if (received_ == message_.size()) { message_.clear(); received_ = 0; }
      return (int)n;
    }
    if (!error_.empty()) return -2;
    if (closed_) return 0;
    size_t before = wire_.size();
    if (!parse()) { poll(); continue; }
    if (wire_.size() < before) { poll(); continue; }
    if (wire_.size() >= maxMessage + 14) { fail("receive buffer full", 1009); continue; }
    char buf[8192];
    size_t got = 0;
    auto status = tls_->read(buf, std::min(sizeof buf, maxMessage + 14 - wire_.size()), &got, 0, &error_);
    if (status == mods::TlsStream::Status::Ok) wire_.insert(wire_.end(), buf, buf + got);
    else if (status == mods::TlsStream::Status::Idle) return -1;
    else { if (error_.empty()) error_ = "WebSocket: connection closed without a close frame"; return -2; }
  }
  return -1;
}

bool WebSocket::close() {
  poll();
  if (!closing_) {
    const char body[2] = {3, (char)232};
    if (!frame(8, body, sizeof body)) return false;
    closing_ = true;
  }
  return poll();
}

}
