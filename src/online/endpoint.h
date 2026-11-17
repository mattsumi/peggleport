#pragma once
#include <string>

namespace pp::online {

inline constexpr const char* publicLobby = "wss://peggleport-lobby.konekurosawa.org/lobby";

struct LobbyEndpoint {
  std::string host, path;
  int port = 7770;
  bool secure = false;
  std::string address() const;
};

bool parseEndpoint(const std::string& text, LobbyEndpoint* out);
std::string endpointAddress(const std::string& host, int port);
std::string preferredLobby(const std::string& saved);

}
