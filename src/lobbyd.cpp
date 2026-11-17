#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <string>

#include "lobby.h"
#include "online/rendezvous.h"

int main(int argc, char** argv) {
  int port = 7770;
  pp::LobbyServer srv;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--proxy-local")) srv.proxyLocal = true;
    else if (!std::strcmp(argv[i], "--no-udp")) srv.enableUdp = false;
    else if (!std::strcmp(argv[i], "--aux-offset") && i + 1 < argc) srv.udpAuxOffset = std::atoi(argv[++i]);
    else if (argv[i][0] != '-') port = std::atoi(argv[i]);
    else {
      std::fprintf(stderr, "usage: pp_lobbyd [port] [--no-udp] [--aux-offset N] [--proxy-local]\n");
      return 2;
    }
  }
  std::string err;
  if (!srv.listen(port, &err)) {
    std::fprintf(stderr, "%s\n", err.c_str());
    return 1;
  }
  std::printf("pp_lobbyd listening on port %d (tcp)", srv.port());
  if (srv.udpActive()) std::printf(", udp rendezvous on %d (nat probe port %d)\n", srv.rendezvous()->port(), srv.rendezvous()->auxPort());
  else std::printf(", udp off%s%s\n", srv.udpError.empty() ? "" : ": ", srv.udpError.c_str());
  std::fflush(stdout);
  size_t lastRooms = 0, lastPipes = 0, lastPairs = 0;
  auto lastStats = std::chrono::steady_clock::now();
  for (;;) {
    srv.poll();
    size_t pairs = srv.udpActive() ? srv.rendezvous()->pairs() : 0;
    if (srv.rooms() != lastRooms || srv.pipes() != lastPipes || pairs != lastPairs) {
      lastRooms = srv.rooms();
      lastPipes = srv.pipes();
      lastPairs = pairs;
      std::printf("rooms=%zu active_links=%zu udp_pairs=%zu\n", lastRooms, lastPipes, lastPairs);
      std::fflush(stdout);
    }
    if (srv.udpActive() && std::chrono::steady_clock::now() - lastStats > std::chrono::minutes(10)) {
      lastStats = std::chrono::steady_clock::now();
      auto& st = srv.rendezvous()->stats;
      std::printf("udp: received=%llu answered=%llu rate_limited=%llu bad_cookie=%llu unknown_pair=%llu paired=%llu\n", (unsigned long long)st.received, (unsigned long long)st.answered,
                  (unsigned long long)st.rateLimited, (unsigned long long)st.badCookie, (unsigned long long)st.unknownPair, (unsigned long long)st.paired);
      std::fflush(stdout);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}
