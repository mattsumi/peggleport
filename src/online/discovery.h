#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "online/endpoint.h"

namespace pp::online {

enum class LobbySource { Local, Global, Custom, Invite };

struct LobbyListing {
  LobbyEndpoint endpoint;
  LobbySource source = LobbySource::Global;
  bool done = false, ok = false;
  std::vector<std::string> lines;
  std::string error;
  std::string serverId;
  std::vector<std::string> aliases;
};

struct BrowseSnapshot {
  std::vector<LobbyListing> listings;
  bool discovering = false;
  uint64_t revision = 0;
  bool busy() const;
};

struct BrowseOptions {
  std::string configured, invited;
  std::string global = publicLobby;
  int discoveryPort = 7770, localPort = 7770, discoveryMs = 450;
  bool broadcast = true;
};

using RoomLister = std::function<bool(const std::string&, int, std::vector<std::string>*, std::string*, const std::atomic<bool>*)>;

void retireOnlineWork(std::shared_ptr<void> work);
std::string listedRoomCode(const std::string& line);
std::vector<LobbyEndpoint> roomJoinTargets(const BrowseSnapshot& snapshot, const std::string& code, const LobbyEndpoint* selected = nullptr);
bool findRoomEndpoint(const RoomLister& list, const std::vector<LobbyEndpoint>& targets, const std::string& code, LobbyEndpoint* endpoint, std::string* error, const std::atomic<bool>* stop);

class RoomBrowser {
 public:
  explicit RoomBrowser(RoomLister list);
  ~RoomBrowser();
  bool start(const BrowseOptions& options);
  BrowseSnapshot snapshot() const;
  void cancel();

 private:
  struct Impl;
  RoomLister list_;
  std::unique_ptr<Impl> impl_;
};

}
