#include "online/discovery.h"
#include "online/rendezvous.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <sstream>
#include <thread>

namespace pp::online {

void retireOnlineWork(std::shared_ptr<void> work) {
  struct Reaper {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::shared_ptr<void>> pending;
    bool stop = false;
    std::thread thread{[this] {
      std::unique_lock<std::mutex> lock(mutex);
      for (;;) {
        ready.wait(lock, [this] { return stop || !pending.empty(); });
        if (pending.empty()) return;
        auto work = std::move(pending.front());
        pending.pop_front();
        lock.unlock();
        work.reset();
        lock.lock();
      }
    }};
    ~Reaper() {
      { std::lock_guard<std::mutex> lock(mutex); stop = true; }
      ready.notify_one();
      thread.join();
    }
  };
  static Reaper reaper;
  if (!work) return;
  { std::lock_guard<std::mutex> lock(reaper.mutex); reaper.pending.push_back(std::move(work)); }
  reaper.ready.notify_one();
}

bool BrowseSnapshot::busy() const {
  return discovering || std::any_of(listings.begin(), listings.end(), [](const LobbyListing& entry) { return !entry.done; });
}

std::string listedRoomCode(const std::string& line) {
  std::istringstream input(line);
  std::string tag, code, players, joined, party, level, hash, name;
  if (!(input >> tag >> code >> players >> joined >> party >> level >> hash) || !std::getline(input >> std::ws, name) || name.empty()) return {};
  if (players.size() != 1 || players[0] < '2' || players[0] > '4' || joined.size() != 1 || joined[0] < '1' || joined[0] > players[0] ||
      (party != "0" && party != "1") || hash.size() > 16 || hash.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return {};
  return tag == "ROOM" && code.size() == 5 && code.find_first_not_of("ABCDEFGHJKLMNPQRSTUVWXYZ23456789") == std::string::npos ? code : std::string();
}

std::vector<LobbyEndpoint> roomJoinTargets(const BrowseSnapshot& snapshot, const std::string& code, const LobbyEndpoint* selected) {
  if (selected) {
    for (const auto& listing : snapshot.listings)
      if (std::find(listing.aliases.begin(), listing.aliases.end(), selected->address()) != listing.aliases.end()) return {listing.endpoint};
    return {*selected};
  }
  auto listings = snapshot.listings;
  std::stable_sort(listings.begin(), listings.end(), [&code](const LobbyListing& a, const LobbyListing& b) {
    auto rank = [&code](const LobbyListing& entry) {
      bool match = std::any_of(entry.lines.begin(), entry.lines.end(), [&code](const std::string& line) { return listedRoomCode(line) == code; });
      return (match ? 0 : 4) + (int)entry.source;
    };
    return rank(a) < rank(b);
  });
  std::vector<LobbyEndpoint> targets;
  for (const auto& listing : listings) targets.push_back(listing.endpoint);
  return targets;
}

bool findRoomEndpoint(const RoomLister& list, const std::vector<LobbyEndpoint>& targets, const std::string& code, LobbyEndpoint* endpoint, std::string* error, const std::atomic<bool>* stop) {
  if (stop && *stop) { if (error) *error = "cancelled"; return false; }
  struct Lookup {
    RoomLister list;
    std::string code;
    std::atomic<bool> cancel{false};
    std::atomic<size_t> finished{0};
    std::mutex mutex;
    bool found = false;
    LobbyEndpoint result;
    std::vector<std::thread> workers;
    ~Lookup() { cancel = true; for (auto& worker : workers) if (worker.joinable()) worker.join(); }
  };
  auto lookup = std::make_shared<Lookup>();
  lookup->list = list;
  lookup->code = code;
  try {
    for (size_t i = 0; i < targets.size() && i < 16; i++) {
      lookup->workers.emplace_back([work = lookup.get(), target = targets[i]] {
        std::vector<std::string> lines;
        std::string ignored;
        try {
          if (work->list(target.secure ? target.address() : target.host, target.port, &lines, &ignored, &work->cancel)) {
            for (const auto& line : lines) if (listedRoomCode(line) == work->code) {
              std::lock_guard<std::mutex> lock(work->mutex);
              if (!work->found && !work->cancel) { work->result = target; work->found = true; }
              break;
            }
          }
        } catch (const std::exception&) {}
        work->finished++;
      });
    }
    while (lookup->finished < lookup->workers.size() && !(stop && *stop)) {
      { std::lock_guard<std::mutex> lock(lookup->mutex); if (lookup->found) break; }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  } catch (const std::exception&) {}
  lookup->cancel = true;
  bool found;
  {
    std::lock_guard<std::mutex> lock(lookup->mutex);
    found = lookup->found && !(stop && *stop);
    if (found && endpoint) *endpoint = lookup->result;
  }
  retireOnlineWork(std::move(lookup));
  if (!found && error) *error = stop && *stop ? "cancelled" : "ERR no such room";
  return found;
}

struct RoomBrowser::Impl {
  RoomLister list;
  std::atomic<bool> stop{false};
  mutable std::mutex mutex;
  BrowseSnapshot snapshot;
  std::vector<std::thread> workers;
  std::thread discovery;

  explicit Impl(RoomLister lister) : list(std::move(lister)) {}

  void discover(const BrowseOptions& options) {
    auto port = openUdpPort(0, nullptr, true);
    if (port) {
      std::vector<uint8_t> request(rdv::kMinRequest, 0);
      request[0] = rdv::kMagic0; request[1] = rdv::kMagic1; request[2] = rdv::kDiscover;
      const std::string nonce = randomHex(4);
      std::copy(nonce.begin(), nonce.end(), request.begin() + 3);
      const auto number = (uint16_t)std::clamp(options.discoveryPort, 1, 65535);
      port->sendTo(Endpoint::v4(127, 0, 0, 1, number), request.data(), request.size());
      if (options.broadcast) port->sendTo(Endpoint::v4(255, 255, 255, 255, number), request.data(), request.size());
      const auto deadline = nowMs() + (uint64_t)std::clamp(options.discoveryMs, 0, 1500);
      size_t received = 0;
      while (!stop && nowMs() < deadline && received < 64) {
        uint8_t response[128];
        Endpoint from;
        int size = port->recvFrom(&from, response, sizeof response);
        if (size > 0) {
          received++;
          if (size != 21 || response[0] != rdv::kMagic0 || response[1] != rdv::kMagic1 || response[2] != rdv::kDiscovered ||
              !std::equal(request.begin() + 3, request.begin() + 11, response + 3)) continue;
          from.port = (uint16_t)((unsigned)response[11] * 256 + response[12]);
          if (from.valid()) launch(from.str(), LobbySource::Local, std::string((const char*)response + 13, 8));
        } else std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    }
    std::lock_guard<std::mutex> lock(mutex);
    snapshot.discovering = false;
    snapshot.revision++;
  }

  void launch(const std::string& address, LobbySource source, const std::string& serverId = {}) {
    LobbyEndpoint endpoint;
    if (stop || !parseEndpoint(address, &endpoint)) return;
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& entry : snapshot.listings) if (entry.endpoint.address() == endpoint.address()) {
      if (!serverId.empty() && entry.serverId != serverId) { entry.serverId = serverId; snapshot.revision++; }
      return;
    }
    if (snapshot.listings.size() >= 16) return;
    const size_t index = snapshot.listings.size();
    snapshot.listings.push_back({endpoint, source});
    snapshot.listings.back().serverId = serverId;
    snapshot.revision++;
    try {
      workers.emplace_back([this, endpoint, index] {
        std::vector<std::string> lines;
        std::string error;
        bool ok = false;
        try {
          ok = list(endpoint.secure ? endpoint.address() : endpoint.host, endpoint.port, &lines, &error, &stop);
        } catch (const std::exception& failure) { error = failure.what(); }
        std::lock_guard<std::mutex> lock(mutex);
        auto& entry = snapshot.listings[index];
        entry.done = true;
        entry.ok = ok && !stop;
        if (entry.ok) entry.lines = std::move(lines);
        entry.error = std::move(error);
        snapshot.revision++;
      });
    } catch (const std::exception& failure) {
      snapshot.listings[index].done = true;
      snapshot.listings[index].error = failure.what();
    }
  }

  ~Impl() {
    stop = true;
    if (discovery.joinable()) discovery.join();
    for (auto& worker : workers) if (worker.joinable()) worker.join();
  }
};
RoomBrowser::RoomBrowser(RoomLister list) : list_(std::move(list)) { retireOnlineWork({}); }
RoomBrowser::~RoomBrowser() { cancel(); }
bool RoomBrowser::start(const BrowseOptions& options) {
  if (snapshot().busy()) return false;
  cancel();
  impl_ = std::make_unique<Impl>(list_);
  impl_->launch("127.0.0.1:" + std::to_string(options.localPort), LobbySource::Local);
  impl_->launch(options.global, LobbySource::Global);
  impl_->launch(options.configured, LobbySource::Custom);
  impl_->launch(options.invited, LobbySource::Invite);
  impl_->snapshot.discovering = true;
  try {
    impl_->discovery = std::thread([impl = impl_.get(), options] {
      try { impl->discover(options); }
      catch (const std::exception&) {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->snapshot.discovering = false;
        impl->snapshot.revision++;
      }
    });
  } catch (const std::exception&) { impl_->snapshot.discovering = false; }
  return true;
}
BrowseSnapshot RoomBrowser::snapshot() const {
  if (!impl_) return {};
  std::lock_guard<std::mutex> lock(impl_->mutex);
  auto result = impl_->snapshot;
  result.listings.clear();
  auto loopback = [](const std::string& host) { return host.rfind("127.", 0) == 0 || host == "::1" || host == "localhost"; };
  for (auto entry : impl_->snapshot.listings) {
    entry.aliases.push_back(entry.endpoint.address());
    auto same = std::find_if(result.listings.begin(), result.listings.end(), [&entry](const LobbyListing& prior) { return !entry.serverId.empty() && prior.serverId == entry.serverId; });
    if (same == result.listings.end()) { result.listings.push_back(std::move(entry)); continue; }
    same->aliases.push_back(entry.endpoint.address());
    if (entry.ok && (!same->ok || (loopback(same->endpoint.host) && !loopback(entry.endpoint.host)))) same->endpoint = entry.endpoint;
    if (!same->ok && entry.ok) { same->lines = std::move(entry.lines); same->error.clear(); }
    same->ok = same->ok || entry.ok;
    same->done = same->done && entry.done;
  }
  return result;
}
void RoomBrowser::cancel() {
  if (!impl_) return;
  impl_->stop = true;
  retireOnlineWork(std::shared_ptr<Impl>(std::move(impl_)));
}

}
