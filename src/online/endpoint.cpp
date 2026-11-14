#include "online/endpoint.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "sockcompat.h"

namespace pp::online {
namespace {

bool portNumber(const std::string& s, int* out) {
  if (s.empty() || s.size() > 5) return false;
  int n = 0;
  for (unsigned char c : s) {
    if (c < '0' || c > '9') return false;
    n = n * 10 + c - '0';
  }
  if (n < 1 || n > 65535) return false;
  *out = n;
  return true;
}

bool hostName(const std::string& s, bool scope = true) {
  if (s.empty() || s.size() > 253) return false;
  if (s.find(':') != std::string::npos) {
    std::string address = s;
    size_t percent = address.find('%');
    if (percent != std::string::npos) {
      if (!scope || percent + 1 == address.size()) return false;
      for (size_t i = percent + 1; i < address.size(); i++)
        if (!(std::isalnum((unsigned char)address[i]) || address[i] == '_' || address[i] == '-' || address[i] == '.')) return false;
      address.resize(percent);
    }
    in6_addr a{};
    return inet_pton(AF_INET6, address.c_str(), &a) == 1;
  }
  size_t label = 0;
  for (unsigned char c : s) {
    if (c == '.') {
      if (!label || label > 63) return false;
      label = 0;
    } else {
      if (!(std::isalnum(c) || c == '-' || c == '_')) return false;
      label++;
    }
  }
  return label <= 63 && (label || s.back() == '.');
}

}

std::string LobbyEndpoint::address() const {
  const std::string h = host.find(':') == std::string::npos ? host : "[" + host + "]";
  if (secure) return "wss://" + h + (port == 443 ? "" : ":" + std::to_string(port)) + path;
  return h + ":" + std::to_string(port);
}

bool parseEndpoint(const std::string& text, LobbyEndpoint* out) {
  if (text.empty() || text.size() > 1024) return false;
  for (unsigned char c : text) if (c < 32 || c >= 127) return false;
  size_t b = text.find_first_not_of(' '), e = text.find_last_not_of(' ');
  if (b == std::string::npos) return false;
  std::string s = text.substr(b, e - b + 1);
  LobbyEndpoint ep;
  std::string prefix = s.substr(0, 6);
  for (char& c : prefix) c = (char)std::tolower((unsigned char)c);
  if (prefix == "wss://") {
    ep.secure = true;
    ep.port = 443;
    s.erase(0, 6);
    size_t slash = s.find('/');
    ep.path = slash == std::string::npos ? "/lobby" : s.substr(slash);
    s = s.substr(0, slash);
    for (size_t i = 0; i < ep.path.size(); i++) {
      unsigned char c = (unsigned char)ep.path[i];
      if (c == '%') {
        if (i + 2 >= ep.path.size() || !std::isxdigit((unsigned char)ep.path[i + 1]) || !std::isxdigit((unsigned char)ep.path[i + 2])) return false;
        int n = std::stoi(ep.path.substr(i + 1, 2), nullptr, 16);
        if (n <= 32 || n == 127) return false;
        i += 2;
      } else if (!(std::isalnum(c) || std::string("/-._~!$&'()*+,;=:@").find((char)c) != std::string::npos)) return false;
    }
  }
  if (s.empty()) return false;
  if (s.front() == '[') {
    size_t rb = s.find(']');
    if (rb == std::string::npos) return false;
    ep.host = s.substr(1, rb - 1);
    if (ep.host.find(':') == std::string::npos) return false;
    if (rb + 1 != s.size() && (s[rb + 1] != ':' || !portNumber(s.substr(rb + 2), &ep.port))) return false;
  } else if (std::count(s.begin(), s.end(), ':') == 1) {
    size_t c = s.find(':');
    ep.host = s.substr(0, c);
    if (!portNumber(s.substr(c + 1), &ep.port)) return false;
  } else {
    if (ep.secure && s.find(':') != std::string::npos) return false;
    ep.host = s;
  }
  if (!hostName(ep.host, !ep.secure)) return false;
  if (out) *out = ep;
  return true;
}

std::string endpointAddress(const std::string& host, int port) {
  LobbyEndpoint ep;
  if (host.find("://") != std::string::npos) return parseEndpoint(host, &ep) && ep.secure ? ep.address() : std::string();
  ep.host = host;
  ep.port = port;
  return hostName(host) && port > 0 && port <= 65535 ? ep.address() : std::string();
}

std::string preferredLobby(const std::string& saved) {
  return saved.empty() || saved == "localhost:7770" ? std::string(publicLobby) : saved;
}

}
