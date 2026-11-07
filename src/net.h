#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <utility>

#include "match.h"
#include "online/direct.h"

namespace pp {

struct MatchInfo {
  int players = 2;
  bool party = false;
  uint32_t seed = 4357;
  int boardWidth = 650;
  int master[4]{0, 0, 0, 0};
  std::string level;
  uint64_t contentHash = 0;
  std::string missingMaster;
};

class Session {
 public:
  struct Seat {
    std::string name;
    bool connected = false, ready = false;
  };
  Session();
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  bool host(int port, const MatchInfo& mi, std::string* err);
  bool join(const std::string& host, int port, std::string* err);
  bool hostViaLobby(const std::string& lobbyHost, int lobbyPort, const std::string& roomName, const MatchInfo& mi, std::string* err);
  bool joinViaLobby(const std::string& lobbyHost, int lobbyPort, const std::string& code, std::string* err);
  static bool listRooms(const std::string& lobbyHost, int lobbyPort, std::vector<std::string>* lines, std::string* err);
  const std::string& roomCode() const { return roomCode_; }
  int boundPort() const { return port_; }
  void poll();
  void leave();

  void setName(const std::string& n) { name_ = n; }
  bool autoStart = true;
  void hostStart();
  bool allReady() const;
  const std::vector<Seat>& roster() const { return roster_; }
  void requestRematch();
  bool takeRematch(uint32_t* seed);
  int rematchVotes() const { return (int)rematchVotes_.size(); }
  bool udpEnabled = true;
  online::DirectParams directTemplate;
  bool direct() const;
  std::string linkNote() const;
  uint64_t streamRetransmits() const;

  bool isHost() const { return host_; }
  bool welcomed() const { return welcomed_; }
  bool started() const { return started_; }
  int localPlayer() const { return local_; }
  int connectedPeers() const;
  const MatchInfo& info() const { return mi_; }

  void sendReady(uint64_t contentHash);
  void sendShot(const ShotCmd& c);
  std::vector<ShotCmd> takeShots();
  void reportHash(int board, int shotIndex, uint64_t h);

  bool desynced() const { return desync_; }
  bool failed() const { return !error.empty(); }
  std::string error, desyncMsg;
  std::string notice;
  int hashesCompared = 0;

 private:
  struct Peer;
  std::vector<Peer*> peers_;
  Peer* lobbyCtl_ = nullptr;
  std::string lobbyHost_, roomCode_, lobbyToken_;
  int lobbyPort_ = 0;
  intptr_t listen_ = -1;
  int port_ = 0;
  bool host_ = false, welcomed_ = false, started_ = false, desync_ = false;
  int local_ = 0;
  uint32_t seq_ = 1;
  MatchInfo mi_;
  std::deque<ShotCmd> in_;
  std::string name_;
  std::vector<Seat> roster_;
  std::string rosterSig_;
  std::set<int> rematchVotes_;
  std::deque<uint32_t> rematchSeeds_;
  bool left_ = false;
  std::map<std::pair<int, int>, uint64_t> mine_;
  std::map<std::pair<int, int>, std::vector<uint64_t>> theirs_;

  void handle(Peer& p, uint8_t type, const std::vector<uint8_t>& body);
  void send(Peer& p, uint8_t type, const std::vector<uint8_t>& body);
  void broadcast(uint8_t type, const std::vector<uint8_t>& body, const Peer* except = nullptr);
  void compare(std::pair<int, int> key);
  void flush(Peer& p);
  void refreshRoster();
  void tryRematch();
  void resetForRematch(uint32_t seed);
  void startDirect(Peer& p, const std::string& pairKey, int auxPort);
  std::string who(const Peer& p) const;
  std::vector<uint8_t> helloBody() const;
};

}
