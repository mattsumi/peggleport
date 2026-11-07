#pragma once
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pp {

namespace online { class Rendezvous; }

class LobbyServer {
 public:
  struct Limits {
    size_t maxConns = 256;
    size_t maxPerIp = 16;
    size_t maxRooms = 64;
    size_t maxRoomsPerIp = 3;
    int handshakeMs = 10000;
    int pipeIdleMs = 15 * 60 * 1000;
    int hostIdleMs = 2 * 60 * 60 * 1000;
    size_t maxOutBuf = 1 << 20;
  };
  Limits limits;
  bool enableUdp = true;
  int udpAuxOffset = 1;
  std::string udpError;

  LobbyServer();
  ~LobbyServer();
  LobbyServer(const LobbyServer&) = delete;
  LobbyServer& operator=(const LobbyServer&) = delete;
  bool listen(int port, std::string* err);
  int port() const { return port_; }
  void poll();
  size_t rooms() const { return rooms_.size(); }
  size_t pipes() const;
  size_t connections() const { return conns_.size(); }
  size_t refused() const { return refused_; }
  online::Rendezvous* rendezvous() const { return rdv_.get(); }
  bool udpActive() const { return rdv_ != nullptr; }

 private:
  struct Conn;
  struct Room {
    std::string code, token, name, level;
    int players = 2;
    bool party = false;
    std::string hash;
    Conn* host = nullptr;
    int joined = 0;
    std::string hostIp;
  };
  intptr_t listen_ = -1;
  int port_ = 0;
  std::unique_ptr<online::Rendezvous> rdv_;
  bool listenTcp(int port, std::string* err);
  std::vector<std::unique_ptr<Conn>> conns_;
  std::map<std::string, Room> rooms_;
  std::map<uint32_t, Conn*> pending_;
  size_t refused_ = 0;

  void line(Conn& c, const std::string& l);
  void reply(Conn& c, const std::string& s, bool closeAfter = false);
  std::string newCode();
  void drop(Conn& c);
};

}
