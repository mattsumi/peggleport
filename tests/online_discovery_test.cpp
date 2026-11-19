#include "online/rendezvous.h"
#include "online/udp.h"
#include "online/discovery.h"
#include "online/invite.h"
#include "sockcompat.h"
#include "net.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

using namespace pp::online;

class TcpLobby {
 public:
  sock_t socket = BAD_SOCK;
  int port = 0;
  std::atomic<bool> stop{false};
  std::atomic<int> requests{0};
  std::thread worker;

  explicit TcpLobby(bool stalled = false, std::string roomName = "network_room") {
    netStartup();
    socket = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(socket, (sockaddr*)&address, sizeof address) != 0 || ::listen(socket, 16) != 0) throw std::runtime_error("fixture cannot bind");
    socklen_t length = sizeof address;
    getsockname(socket, (sockaddr*)&address, &length);
    port = ntohs(address.sin_port);
    nonBlock(socket);
    worker = std::thread([this, stalled, roomName] {
      std::map<sock_t, std::string> clients;
      while (!stop) {
        sock_t client = accept(socket, nullptr, nullptr);
        if (client != BAD_SOCK) { nonBlock(client); clients.emplace(client, std::string()); }
        for (auto it = clients.begin(); it != clients.end();) {
          char buffer[128];
          int size = (int)recv(it->first, buffer, sizeof buffer, 0);
          if (size > 0) it->second.append(buffer, size);
          if (it->second == "LIST\n") {
            requests++;
            it->second.clear();
            if (!stalled) {
              std::string reply = "ROOM ABCDE 2 1 0 levels/test 1234 " + roomName + "\nEND\n";
              ::send(it->first, reply.data(), (int)reply.size(), MSG_NOSIGNAL);
            }
          }
          if (size == 0 || (size < 0 && !wouldBlock())) { closeSock(it->first); it = clients.erase(it); }
          else ++it;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      for (const auto& client : clients) closeSock(client.first);
    });
  }

  ~TcpLobby() { stop = true; if (worker.joinable()) worker.join(); closeSock(socket); }
  std::string address() const { return "127.0.0.1:" + std::to_string(port); }
};


static void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

static void udpDiscovery() {
  Rendezvous server;
  std::string error;
  require(server.listen(0, -1, &error), "rendezvous must bind a real UDP socket");
  auto client = openUdpPort(0, &error);
  require(client != nullptr, "discovery client must bind");
  uint8_t request[64] = {'P', 'R', 8, 1, 2, 3, 4, 5, 6, 7, 8};
  const auto destination = Endpoint::v4(127, 0, 0, 1, (uint16_t)server.port());
  require(client->sendTo(destination, request, sizeof request), "discovery request must send");
  uint8_t response[128]{};
  Endpoint from;
  int size = 0;
  const auto deadline = nowMs() + 500;
  while (!size && nowMs() < deadline) {
    server.poll(nowMs());
    size = client->recvFrom(&from, response, sizeof response);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  require(size == 21, "LAN discovery must answer a real socket with a bounded response");
  require(response[0] == 'P' && response[1] == 'R' && response[2] == 9, "discovery response must use its own opcode");
  require(std::memcmp(response + 3, request + 3, 8) == 0, "discovery response must echo the request nonce");
  require(((int)response[11] * 256 + response[12]) == server.port(), "discovery response must advertise the real listening port");
  require(size < (int)sizeof request, "discovery must not amplify requests");
  require(client->sendTo(destination, request, 11), "short discovery request must send");
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  server.poll(nowMs());
  require(client->recvFrom(&from, response, sizeof response) == 0, "short discovery requests must receive no response");
  server.limits.perIpBurst = 0;
  server.limits.perIpPerSec = 0;
  require(client->sendTo(destination, request, sizeof request), "rate-limit discovery request must send");
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  server.poll(nowMs());
  require(client->recvFrom(&from, response, sizeof response) == 0, "discovery must share rendezvous rate limits");
}

static void incrementalBrowsing() {
  TcpLobby local;
  TcpLobby global(true);
  TcpLobby configured;
  RoomBrowser browser(pp::Session::listRooms);
  BrowseOptions options;
  options.global = global.address();
  options.configured = configured.address();
  options.localPort = local.port;
  options.discoveryPort = local.port;
  options.broadcast = false;
  options.discoveryMs = 50;
  require(browser.start(options), "browser must start independent discovery workers");
  BrowseSnapshot snapshot;
  const auto deadline = nowMs() + 500;
  bool localVisible = false, globalPending = false;
  while (nowMs() < deadline && !localVisible) {
    snapshot = browser.snapshot();
    for (const auto& listing : snapshot.listings) {
      if (listing.source == LobbySource::Local && listing.ok && !listing.lines.empty()) localVisible = true;
      if (listing.source == LobbySource::Global && !listing.done) globalPending = true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  require(localVisible, "local rooms must appear before a slow global listing completes");
  require(globalPending, "global listing must run independently from local results");
  while (configured.requests == 0 && nowMs() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  require(configured.requests > 0 && global.requests > 0, "custom configuration must never suppress the global query");
  const auto cancelAt = nowMs();
  browser.cancel();
  require(nowMs() - cancelAt < 300, "cancellation must retire all workers promptly");
  require(!browser.snapshot().busy(), "cancelled browser must have no active workers");
}

static void lanDiscoveryRouting() {
  TcpLobby lan;
  TcpLobby global(true);
  Rendezvous server;
  std::string error;
  require(server.listen(lan.port, -1, &error), "LAN TCP fixture must have its real UDP rendezvous listener");
  RoomBrowser browser(pp::Session::listRooms);
  BrowseOptions options;
  options.localPort = global.port;
  options.global = "127.0.0.1:1";
  options.discoveryPort = lan.port;
  options.discoveryMs = 100;
  options.broadcast = false;
  browser.start(options);
  bool found = false;
  auto deadline = nowMs() + 500;
  while (nowMs() < deadline && !found) {
    server.poll(nowMs());
    for (const auto& listing : browser.snapshot().listings)
      if (listing.source == LobbySource::Local && listing.endpoint.port == lan.port && listing.ok && !listing.lines.empty()) found = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  require(found, "UDP discovery must route LIST to the server's advertised actual endpoint");
  require(server.stats.received == 1, "each refresh must send at most one loopback discovery request");
  browser.cancel();
}

static void endpointRouting() {
  TcpLobby first(false, "first_endpoint");
  TcpLobby second(false, "second_endpoint");
  TcpLobby stalled(true);
  RoomBrowser browser(pp::Session::listRooms);
  BrowseOptions options;
  options.localPort = first.port;
  options.global = second.address();
  options.configured = stalled.address();
  options.discoveryMs = 20;
  options.broadcast = false;
  browser.start(options);
  BrowseSnapshot snapshot;
  auto deadline = nowMs() + 500;
  while (nowMs() < deadline) {
    snapshot = browser.snapshot();
    int ready = 0;
    for (const auto& entry : snapshot.listings) ready += entry.ok ? 1 : 0;
    if (ready >= 2) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  LobbyEndpoint selected;
  require(parseEndpoint(second.address(), &selected), "selected fixture address must parse");
  auto targets = roomJoinTargets(snapshot, "ABCDE", &selected);
  require(targets.size() == 1 && targets.front().port == second.port, "identical codes must retain the clicked room's endpoint");
  auto manual = roomJoinTargets(snapshot, "ABCDE");
  require(manual.size() == 3, "manual room codes must search local, global and custom sources");
  require(manual.front().port == first.port, "discovered local matching rooms must be preferred for a bare code");
  LobbyEndpoint resolved;
  std::string error;
  std::atomic<bool> stop{false};
  require(findRoomEndpoint(pp::Session::listRooms, targets, "ABCDE", &resolved, &error, &stop), "selected endpoint must resolve through a real TCP room list");
  require(resolved.port == second.port, "same room code on another endpoint must not redirect the join");
  auto began = nowMs();
  require(findRoomEndpoint(pp::Session::listRooms, manual, "ABCDE", &resolved, &error, &stop), "manual code must resolve despite an unresponsive source");
  require(nowMs() - began < 500, "one slow endpoint must not block a successful code lookup");
  require(!findRoomEndpoint(pp::Session::listRooms, targets, "ZZZZZ", &resolved, &error, &stop), "unknown room codes must fail rather than join a different room");
  stop = true;
  require(!findRoomEndpoint(pp::Session::listRooms, targets, "ABCDE", &resolved, &error, &stop), "cancelled lookup must not choose an endpoint");
  browser.cancel();
}

static void localServerAliases() {
  TcpLobby local;
  TcpLobby global;
  Rendezvous server;
  std::string error;
  require(server.listen(local.port, -1, &error), "alias regression must bind its own ephemeral UDP server");
  RoomBrowser browser(pp::Session::listRooms);
  BrowseOptions options;
  options.localPort = local.port;
  options.discoveryPort = local.port;
  options.global = global.address();
  options.discoveryMs = 200;
  browser.start(options);
  BrowseSnapshot snapshot;
  const auto deadline = nowMs() + 1500;
  do {
    server.poll(nowMs());
    snapshot = browser.snapshot();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (snapshot.busy() && nowMs() < deadline);
  require(server.stats.received >= 2, "regression must receive both real loopback and IPv4 broadcast requests");
  int localRows = 0, globalRows = 0;
  LobbyEndpoint destination;
  for (const auto& listing : snapshot.listings) {
    for (const auto& line : listing.lines) if (listedRoomCode(line) == "ABCDE") {
      if (listing.source == LobbySource::Local) { localRows++; destination = listing.endpoint; }
      if (listing.source == LobbySource::Global) globalRows++;
    }
  }
  require(localRows == 1, "one real server reached over loopback and LAN must produce one local room row");
  require(globalRows == 1, "another server with identical room metadata must remain distinct");
  require(destination.host != "127.0.0.1" && destination.host != "::1", "LAN hosting and invites must prefer the reachable LAN address");
  LobbyEndpoint loopback;
  require(parseEndpoint(local.address(), &loopback), "loopback alias must parse");
  auto targets = roomJoinTargets(snapshot, "ABCDE", &loopback);
  require(targets.size() == 1 && targets.front().address() == destination.address(), "a previously selected loopback row must follow its server's LAN endpoint");
  std::atomic<bool> stop{false};
  LobbyEndpoint resolved;
  require(findRoomEndpoint(pp::Session::listRooms, targets, "ABCDE", &resolved, &error, &stop), "the preferred LAN endpoint must serve a real TCP room list");
  Invite invite;
  require(parseInvite(makeInvite(destination.host, destination.port, "ABCDE"), &invite), "LAN destination must produce a valid shareable invite");
  require(invite.host == destination.host && invite.host != loopback.host, "copied LAN invites must not point friends at their own loopback");
  browser.cancel();
}

static void blockingCancellation() {
  for (bool destroy : {false, true}) {
    struct State { std::atomic<bool> entered{false}, release{false}, finished{false}; };
    auto state = std::make_shared<State>();
    auto lifetime = std::make_shared<int>(0);
    std::weak_ptr<int> retired = lifetime;
    auto browser = std::make_unique<RoomBrowser>([state, lifetime](const std::string&, int, std::vector<std::string>*, std::string*, const std::atomic<bool>*) {
      state->entered = true;
      const auto deadline = nowMs() + 1000;
      while (!state->release && nowMs() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      state->finished = true;
      return false;
    });
    lifetime.reset();
    BrowseOptions options;
    options.global.clear();
    options.discoveryMs = 0;
    options.broadcast = false;
    browser->start(options);
    const auto deadline = nowMs() + 500;
    while (!state->entered && nowMs() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(state->entered, "blocking resolver fixture must enter its uncancellable call");
    const auto began = nowMs();
    if (destroy) browser.reset();
    else browser->cancel();
    const auto elapsed = nowMs() - began;
    const bool retained = !retired.expired();
    state->release = true;
    require(elapsed < 300, destroy ? "browser destruction must not wait for blocking DNS" : "browser cancellation must not wait for blocking DNS");
    require(retained, "retirement must retain callback ownership while the worker is blocked");
    if (browser) require(!browser->snapshot().busy(), "cancelled browser must discard the retired generation");
    browser.reset();
    const auto ended = nowMs() + 500;
    while (!retired.expired() && nowMs() < ended) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(state->finished && retired.expired(), "retired callbacks must be joined and their ownership released");
  }
}

static void blockingLookup() {
  for (bool cancel : {false, true}) {
    struct State { std::atomic<bool> entered{false}, release{false}, finished{false}, stop{false}; };
    auto state = std::make_shared<State>();
    RoomLister list = [state, cancel](const std::string&, int port, std::vector<std::string>* lines, std::string*, const std::atomic<bool>*) {
      const auto deadline = nowMs() + 1000;
      if (port == 1) {
        state->entered = true;
        while (!state->release && nowMs() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        state->finished = true;
        return false;
      }
      while (!state->entered && nowMs() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      if (cancel) state->stop = true;
      else *lines = {"ROOM ABCDE 2 1 0 levels/test 1234 network_room"};
      return !cancel;
    };
    LobbyEndpoint slow, fast, resolved;
    parseEndpoint("127.0.0.1:1", &slow);
    parseEndpoint("127.0.0.1:2", &fast);
    std::string error;
    const auto began = nowMs();
    const bool found = findRoomEndpoint(list, {slow, fast}, "ABCDE", &resolved, &error, &state->stop);
    const auto elapsed = nowMs() - began;
    state->release = true;
    require(elapsed < 300, cancel ? "manual lookup cancellation must not wait for blocking DNS" : "a matching room must not wait for another endpoint's blocking DNS");
    require(found != cancel, "manual lookup must distinguish a match from cancellation");
    require(cancel ? error == "cancelled" : resolved.port == fast.port, "manual lookup must preserve the matching endpoint or cancellation error");
    const auto deadline = nowMs() + 500;
    while (!state->finished && nowMs() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(state->finished, "retired lookup must finish with owned inputs after its caller returns");
  }
}

static void roomMetadata() {
  require(listedRoomCode("ROOM ABCDE 2 1 0 levels/test 1234 network_room") == "ABCDE", "complete room metadata must remain valid");
  require(listedRoomCode("ROOM ABCDE 4 4 1 levels/test 0123456789abcdef party room") == "ABCDE", "full party rooms and spaced names must remain valid");
  const std::vector<std::string> invalid = {
    "ROOM ABCDE", "ROOM ABCDE 2 1 0 levels/test 1234", "ROOM ABCDE 2 1 0 levels/test 1234   ",
    "ROOM ABCDE x 1 0 levels/test 1234 name", "ROOM ABCDE 1 1 0 levels/test 1234 name",
    "ROOM ABCDE 5 1 0 levels/test 1234 name", "ROOM ABCDE 2 -1 0 levels/test 1234 name",
    "ROOM ABCDE 2 0 0 levels/test 1234 name", "ROOM ABCDE 2 3 0 levels/test 1234 name",
    "ROOM ABCDE 2 1 2 levels/test 1234 name", "ROOM ABCDE 2 1 0levels/test 1234 name",
    "ROOM ABCDE 2 1 0 levels/test xyz name",
    "ROOM ABCDE 2 1 0 levels/test 12345678901234567 name", "ROOM ABCD0 2 1 0 levels/test 1234 name"
  };
  for (const auto& line : invalid) {
    if (!listedRoomCode(line).empty()) throw std::runtime_error("malformed room metadata accepted: " + line);
  }
  LobbyEndpoint target;
  require(parseEndpoint("127.0.0.1:7770", &target), "metadata lookup fixture must parse");
  RoomLister malformed = [](const std::string&, int, std::vector<std::string>* lines, std::string*, const std::atomic<bool>*) {
    *lines = {"ROOM ABCDE"};
    return true;
  };
  std::string error;
  require(!findRoomEndpoint(malformed, {target}, "ABCDE", nullptr, &error, nullptr), "incomplete room records must not resolve a manual join");
}

int main() {
  try {
    blockingLookup();
    std::cout << "PASS prompt manual lookup match and cancellation despite blocked sources\n";
    blockingCancellation();
    std::cout << "PASS nonblocking browser cancellation and destruction during uncancellable work\n";
    roomMetadata();
    std::cout << "PASS complete room metadata validation and malformed manual-join rejection\n";
    udpDiscovery();
    std::cout << "PASS UDP discovery nonce, actual port, non-amplification, malformed input and rate limit\n";
    incrementalBrowsing();
    std::cout << "PASS independent local/global/custom TCP listings and prompt cancellation\n";
    endpointRouting();
    std::cout << "PASS duplicate room-code endpoint routing, manual lookup, missing-room failure and cancellation\n";
    lanDiscoveryRouting();
    std::cout << "PASS UDP-discovered server TCP routing with global failure\n";
    localServerAliases();
    std::cout << "PASS real loopback/broadcast server deduplication, LAN routing and shareable invites\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
