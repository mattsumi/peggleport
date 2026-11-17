#include "online/steam.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "online/invite.h"
#include "online/udp.h"

#ifdef PP_STEAM
#include <steam/steam_api.h>
#include <steam/isteamnetworkingmessages.h>

#include "log.h"

namespace pp::online::steam {
namespace {
constexpr int channel = 0x5050;
constexpr const char* format = "peggleport-1";
bool ok = false;
uint64_t retryAt = 0;
std::atomic<uint64_t> self{0};
std::string presenceStatus, presenceLink;
std::vector<Link*> links;
struct Ending { std::unique_ptr<Link> link; uint64_t until; };
std::vector<Ending> ending;

bool canonical(const std::string& text) {
  Invite invite;
  return parseInvite(text, &invite) && makeInvite(invite.host, invite.port, invite.code) == text;
}

bool member(uint64_t lobby, uint64_t id) {
  const int n = SteamMatchmaking()->GetNumLobbyMembers(CSteamID(lobby));
  if (n < 1 || n > 4) return false;
  for (int i = 0; i < n; ++i)
    if (SteamMatchmaking()->GetLobbyMemberByIndex(CSteamID(lobby), i).ConvertToUint64() == id) return true;
  return false;
}

struct Handler {
  const void* owner = nullptr;
  uint64_t lobby = 0, host = 0, pendingLobby = 0;
  uint64_t operationAt = 0;
  int capacity = 0, seated = 1, publishedOpen = -1, publishedSeated = -1;
  bool started = false, hosting = false, abandoned = false, leaving = false;
  std::string invite, pending;
  uint64_t pendingUntil = 0, pendingSource = 0;
  struct Operation {
    Handler* handler;
    uint64_t until = nowMs() + 10000, target = 0;
    bool abandoned = false, finished = false;
    CCallResult<Operation, LobbyCreated_t> creation;
    CCallResult<Operation, LobbyEnter_t> entry;
    explicit Operation(Handler* value) : handler(value) {}
    void created(LobbyCreated_t* event, bool failure) {
      finished = true;
      if (!abandoned) handler->created(event, failure);
      else if (!failure && event->m_eResult == k_EResultOK) SteamMatchmaking()->LeaveLobby(CSteamID(event->m_ulSteamIDLobby));
    }
    void entered(LobbyEnter_t* event, bool failure) {
      finished = true;
      if (!abandoned) handler->entered(event, failure);
      else if (!failure && event->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess) SteamMatchmaking()->LeaveLobby(CSteamID(event->m_ulSteamIDLobby));
    }
  };
  std::vector<std::unique_ptr<Operation>> operations;
  CCallResult<Handler, LobbyMatchList_t> lookup;
  uint64_t lookupAt = 0;
  bool refreshed = false;
  void discard() {
    lookup.Cancel();
    pending.clear();
    pendingSource = 0;
    refreshed = false;
  }
  void found(LobbyMatchList_t* event, bool failure) {
    if (failure || !pendingSource || nowMs() >= pendingUntil || event->m_nLobbiesMatching > 50) { discard(); return; }
    for (uint32_t i = 0; i < event->m_nLobbiesMatching; ++i) {
      if (SteamMatchmaking()->GetLobbyByIndex(int(i)).ConvertToUint64() != pendingSource) continue;
      pending = lobbyInvite(pendingSource);
      if (pending.empty()) discard();
      else refreshed = true;
      return;
    }
    discard();
  }
  void request() {
    if (!pendingSource || lookup.IsActive() || nowMs() < lookupAt || nowMs() >= pendingUntil) return;
    lookupAt = nowMs() + 1000;
    auto* api = SteamMatchmaking();
    api->AddRequestLobbyListStringFilter("pp_format", format, k_ELobbyComparisonEqual);
    api->AddRequestLobbyListStringFilter("pp_lobby", std::to_string(pendingSource).c_str(), k_ELobbyComparisonEqual);
    api->AddRequestLobbyListDistanceFilter(k_ELobbyDistanceFilterWorldwide);
    api->AddRequestLobbyListResultCountFilter(50);
    api->AddRequestLobbyListFilterSlotsAvailable(1);
    const auto call = api->RequestLobbyList();
    if (call == k_uAPICallInvalid) { discard(); return; }
    lookup.Set(call, this, &Handler::found);
  }
  bool draining() const {
    return std::any_of(operations.begin(), operations.end(), [](const auto& operation) {
      return !operation->finished && nowMs() < operation->until;
    });
  }
  void created(LobbyCreated_t* event, bool failure);
  void entered(LobbyEnter_t* event, bool failure);
  void stage(const std::string& text, uint64_t source = 0) {
    if (!canonical(text)) return;
    discard();
    pending = text;
    pendingSource = source;
    pendingUntil = nowMs() + 5000;
  }
  std::string lobbyInvite(uint64_t source) const {
    auto* api = SteamMatchmaking();
    CSteamID id(source);
    const std::string cap = api->GetLobbyData(id, "pp_capacity"), seats = api->GetLobbyData(id, "pp_seated");
    if (std::string(api->GetLobbyData(id, "pp_lobby")) != std::to_string(source) ||
        std::string(api->GetLobbyData(id, "pp_format")) != format ||
        std::string(api->GetLobbyData(id, "pp_open")) != "1" || cap.size() != 1 || cap[0] < '2' || cap[0] > '4' ||
        seats.size() != 1 || seats[0] < '1' || seats[0] >= cap[0]) return {};
    const std::string text = api->GetLobbyData(id, "pp_invite");
    return canonical(text) ? text : std::string();
  }
  bool valid() const {
    return lobby && SteamMatchmaking()->GetLobbyOwner(CSteamID(lobby)).ConvertToUint64() == host &&
        std::string(SteamMatchmaking()->GetLobbyData(CSteamID(lobby), "pp_format")) == format &&
        std::string(SteamMatchmaking()->GetLobbyData(CSteamID(lobby), "pp_invite")) == invite &&
        SteamMatchmaking()->GetLobbyMemberLimit(CSteamID(lobby)) == capacity && member(lobby, self);
  }
  void close() {
    discard();
    for (auto& operation : operations) operation->abandoned = true;
    operationAt = 0;
    pendingLobby = 0;
    clearPresence();
    if (lobby) {
      if (hosting && SteamMatchmaking()->GetLobbyOwner(CSteamID(lobby)).ConvertToUint64() == self) {
        SteamMatchmaking()->SetLobbyJoinable(CSteamID(lobby), false);
        SteamMatchmaking()->SetLobbyData(CSteamID(lobby), "pp_open", "0");
        SteamMatchmaking()->DeleteLobbyData(CSteamID(lobby), "pp_invite");
      }
      SteamMatchmaking()->LeaveLobby(CSteamID(lobby));
    }
    lobby = 0;
    publishedOpen = publishedSeated = -1;
    owner = nullptr;
    abandoned = true;
  }
  void publish() {
    if (!valid()) { close(); return; }
    if (!hosting) { setPresence(started ? "In a PegglePort match" : "In a PegglePort room", {}); return; }
    const bool joinable = !leaving && !started && seated >= 1 && seated < capacity &&
        SteamMatchmaking()->GetNumLobbyMembers(CSteamID(lobby)) < capacity;
    if (publishedSeated != seated) {
      if (!SteamMatchmaking()->SetLobbyData(CSteamID(lobby), "pp_seated", std::to_string(seated).c_str())) { close(); return; }
      publishedSeated = seated;
    }
    if (publishedOpen != int(joinable)) {
      if (!SteamMatchmaking()->SetLobbyJoinable(CSteamID(lobby), joinable) ||
          !SteamMatchmaking()->SetLobbyData(CSteamID(lobby), "pp_open", joinable ? "1" : "0")) { close(); return; }
      publishedOpen = int(joinable);
    }
    setPresence(started ? "In a PegglePort match" : "In a PegglePort room", joinable ? invite : std::string());
  }
  STEAM_CALLBACK(Handler, richJoin, GameRichPresenceJoinRequested_t);
  STEAM_CALLBACK(Handler, lobbyJoin, GameLobbyJoinRequested_t);
  STEAM_CALLBACK(Handler, lobbyData, LobbyDataUpdate_t);
  STEAM_CALLBACK(Handler, lobbyChat, LobbyChatUpdate_t);
  STEAM_CALLBACK(Handler, sessionRequest, SteamNetworkingMessagesSessionRequest_t);
  STEAM_CALLBACK(Handler, sessionFailed, SteamNetworkingMessagesSessionFailed_t);
};
std::unique_ptr<Handler> handler;

void Handler::created(LobbyCreated_t* event, bool failure) {
  operationAt = 0;
  if (failure || event->m_eResult != k_EResultOK) { close(); return; }
  lobby = event->m_ulSteamIDLobby;
  if (!owner || abandoned) { close(); return; }
  host = self;
  auto* api = SteamMatchmaking();
  if (!api->SetLobbyJoinable(CSteamID(lobby), false) ||
      !api->SetLobbyData(CSteamID(lobby), "pp_format", format) ||
      !api->SetLobbyData(CSteamID(lobby), "pp_lobby", std::to_string(lobby).c_str()) ||
      !api->SetLobbyData(CSteamID(lobby), "pp_capacity", std::to_string(capacity).c_str()) ||
      !api->SetLobbyData(CSteamID(lobby), "pp_invite", invite.c_str()) ||
      !api->SetLobbyMemberLimit(CSteamID(lobby), capacity)) { close(); return; }
  publish();
}

void Handler::entered(LobbyEnter_t* event, bool failure) {
  operationAt = 0;
  const uint64_t target = pendingLobby;
  pendingLobby = 0;
  if (failure || event->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) { close(); return; }
  lobby = event->m_ulSteamIDLobby;
  if (!owner || abandoned || lobby != target || !valid()) { close(); return; }
}

void Handler::richJoin(GameRichPresenceJoinRequested_t* event) {
  const char* end = std::find(std::begin(event->m_rgchConnect), std::end(event->m_rgchConnect), '\0');
  if (end != std::end(event->m_rgchConnect)) stage(std::string(event->m_rgchConnect, size_t(end - event->m_rgchConnect)));
}

void Handler::lobbyJoin(GameLobbyJoinRequested_t* event) {
  if (!event->m_steamIDLobby.IsLobby()) return;
  discard();
  pendingSource = event->m_steamIDLobby.ConvertToUint64();
  pendingUntil = nowMs() + 5000;
}

void Handler::lobbyData(LobbyDataUpdate_t* event) {
  if (event->m_ulSteamIDLobby == lobby && (!event->m_bSuccess || !valid())) close();
}

void Handler::lobbyChat(LobbyChatUpdate_t* event) {
  if (event->m_ulSteamIDLobby == lobby && !valid()) close();
}

void Handler::sessionRequest(SteamNetworkingMessagesSessionRequest_t* event) {
  const uint64_t id = event->m_identityRemote.GetSteamID64();
  const bool expected = std::any_of(links.begin(), links.end(), [id](const Link* link) { return link->accepts(id); });
  if (expected) SteamNetworkingMessages()->AcceptSessionWithUser(event->m_identityRemote);
  else SteamNetworkingMessages()->CloseSessionWithUser(event->m_identityRemote);
}

void Handler::sessionFailed(SteamNetworkingMessagesSessionFailed_t* event) {
  for (auto* link : links) if (link->accepts(event->m_info.m_identityRemote.GetSteamID64())) link->disconnect();
}
}

bool compiledIn() { return true; }
uint64_t identity() { return self.load(std::memory_order_relaxed); }

bool ensureInit() {
  if (ok) return true;
  if (nowMs() < retryAt) return false;
  retryAt = nowMs() + 5000;
  if (!SteamAPI_IsSteamRunning() || !SteamAPI_Init()) return false;
  if (!SteamUser() || !SteamUser()->BLoggedOn() || !SteamMatchmaking() || !SteamNetworkingMessages() || !SteamFriends()) {
    SteamAPI_Shutdown();
    return false;
  }
  self = SteamUser()->GetSteamID().ConvertToUint64();
  ok = true;
  handler = std::make_unique<Handler>();
  pp::log::info("net", "Steam is connected");
  return true;
}

void shutdown() {
  retryAt = 0;
  if (!ok) return;
  handler->close();
  for (auto* link : links) {
    SteamNetworkingIdentity id;
    id.SetSteamID64(link->peer_);
    SteamNetworkingMessages()->CloseSessionWithUser(id);
    link->failed_ = true;
  }
  ending.clear();
  handler.reset();
  ok = false;
  self = 0;
  SteamAPI_Shutdown();
}

void setPresence(const std::string& status, const std::string& connectLink) {
  if (!ok) return;
  const std::string link = canonical(connectLink) && connectLink.size() < 256 ? connectLink : std::string();
  if (status != presenceStatus && SteamFriends()->SetRichPresence("status", status.c_str())) presenceStatus = status;
  if (link != presenceLink && SteamFriends()->SetRichPresence("connect", link.empty() ? nullptr : link.c_str())) presenceLink = link;
}
void clearPresence() {
  if (ok) SteamFriends()->ClearRichPresence();
  presenceStatus.clear();
  presenceLink.clear();
}
void discardJoinRequest() { if (ok && handler) handler->discard(); }
bool takeJoinRequest(std::string* inviteLink) {
  if (!ok || !handler) return false;
  if (nowMs() >= handler->pendingUntil) { handler->discard(); return false; }
  if (!inviteLink) return false;
  if (handler->pendingSource) {
    if (!handler->refreshed) {
      handler->request();
      return false;
    }
    if (handler->lobbyInvite(handler->pendingSource) != handler->pending) { handler->discard(); return false; }
  }
  if (handler->pending.empty()) return false;
  *inviteLink = std::move(handler->pending);
  handler->discard();
  return true;
}

RoomState roomState(const void* owner) {
  if (!ok || !owner) return RoomState::Failed;
  if (!handler->owner) return handler->draining() ? RoomState::Busy : RoomState::Failed;
  if (handler->owner != owner || handler->leaving) return RoomState::Busy;
  return handler->operationAt ? RoomState::Pending : RoomState::Active;
}

bool hostRoom(const void* owner, const std::string& invite, int capacity) {
  if (!ok || !owner || handler->owner || handler->draining() || handler->operations.size() >= 4 || !canonical(invite) || capacity < 2 || capacity > 4) return false;
  handler->owner = owner;
  handler->invite = invite;
  handler->capacity = capacity;
  handler->seated = 1;
  handler->started = false;
  handler->hosting = true;
  handler->abandoned = false;
  handler->leaving = false;
  handler->operationAt = nowMs();
  const auto call = SteamMatchmaking()->CreateLobby(k_ELobbyTypeInvisible, capacity);
  if (call == k_uAPICallInvalid) { handler->operationAt = 0; handler->close(); return false; }
  auto operation = std::make_unique<Handler::Operation>(handler.get());
  operation->creation.Set(call, operation.get(), &Handler::Operation::created);
  handler->operations.push_back(std::move(operation));
  return true;
}

bool joinRoom(const void* owner, uint64_t lobby, uint64_t host, const std::string& invite, int capacity) {
  if (!ok || capacity < 2 || capacity > 4 || !owner || handler->owner || handler->draining() || handler->operations.size() >= 4 || !CSteamID(lobby).IsLobby() || !CSteamID(host).BIndividualAccount() || !canonical(invite)) return false;
  if (std::any_of(handler->operations.begin(), handler->operations.end(), [lobby](const auto& operation) {
    return !operation->finished && operation->target == lobby;
  })) return false;
  handler->owner = owner;
  handler->pendingLobby = lobby;
  handler->host = host;
  handler->invite = invite;
  handler->capacity = capacity;
  handler->hosting = false;
  handler->abandoned = false;
  handler->leaving = false;
  handler->operationAt = nowMs();
  const auto call = SteamMatchmaking()->JoinLobby(CSteamID(lobby));
  if (call == k_uAPICallInvalid) { handler->operationAt = 0; handler->close(); return false; }
  auto operation = std::make_unique<Handler::Operation>(handler.get());
  operation->target = lobby;
  operation->entry.Set(call, operation.get(), &Handler::Operation::entered);
  handler->operations.push_back(std::move(operation));
  return true;
}

void updateRoom(const void* owner, int seated, bool started) {
  if (!ok || handler->owner != owner) return;
  handler->seated = seated;
  handler->started = started;
  if (handler->lobby) handler->publish();
}
void leaveRoom(const void* owner) {
  if (!ok || !owner || handler->owner != owner) return;
  handler->leaving = true;
  clearPresence();
  if (handler->lobby && handler->hosting) {
    SteamMatchmaking()->SetLobbyJoinable(CSteamID(handler->lobby), false);
    SteamMatchmaking()->SetLobbyData(CSteamID(handler->lobby), "pp_open", "0");
  }
  if (std::none_of(ending.begin(), ending.end(), [owner](const Ending& item) { return item.link->belongs(owner); })) handler->close();
}
uint64_t roomId(const void* owner) { return ok && owner && handler->owner == owner && handler->valid() ? handler->lobby : 0; }
bool roomMember(const void* owner, uint64_t peer) {
  return roomId(owner) && member(handler->lobby, peer) && (!handler->hosting ? peer == handler->host : peer != self);
}

Link::Link(const void* owner, uint64_t peer, const SipKey& key, bool host)
    : owner_(owner), peer_(peer), since_(nowMs()), key_(key), stream_(key, RudpConfig(), since_), host_(host) { links.push_back(this); }

std::unique_ptr<Link> Link::open(const void* owner, uint64_t peer, const std::string& key, bool host) {
  SipKey parsed;
  if (!ok || !owner || !peer || peer == self || !CSteamID(peer).BIndividualAccount() || !parseHexKey(key, &parsed) || links.size() >= 6) return nullptr;
  for (const auto* link : links) if (link->peer_ == peer) return nullptr;
  return std::unique_ptr<Link>(new Link(owner, peer, parsed, host));
}
Link::~Link() {
  links.erase(std::remove(links.begin(), links.end(), this), links.end());
  if (ok) {
    SteamNetworkingIdentity id;
    id.SetSteamID64(peer_);
    SteamNetworkingMessages()->CloseSessionWithUser(id);
  }
}
void Link::raw(const uint8_t* bytes, size_t size) {
  if (!ok || failed_ || size > 2048 || !roomMember(owner_, peer_)) return;
  SteamNetworkingIdentity id;
  id.SetSteamID64(peer_);
  const auto result = SteamNetworkingMessages()->SendMessageToUser(id, bytes, (uint32_t)size, k_nSteamNetworkingSend_UnreliableNoNagle, channel);
  if (result != k_EResultOK && result != k_EResultNoConnection) failed_ = true;
}
void Link::poll() {
  if (failed_) return;
  if (!ok || (nowMs() - since_ >= 6000 && !up_)) { failed_ = true; return; }
  if (!roomMember(owner_, peer_)) {
    if (up_ || handler->abandoned) failed_ = true;
    return;
  }
  const uint64_t now = nowMs();
  for (const auto& packet : packets_) {
    if (!Rudp::verify(key_, packet.data(), packet.size())) continue;
    if (packet.size() == 11 && packet[0] == 0x50 && packet[1] == uint8_t(host_ ? 1 : 0) && packet[2] <= 1) {
      heard_ = true;
      if (packet[2]) up_ = true;
    } else if (up_) stream_.onDatagram(packet.data(), packet.size(), now);
  }
  packets_.clear();
  if (now - lastProof_ >= 100 || !lastProof_) {
    lastProof_ = now;
    std::vector<uint8_t> packet{0x50, uint8_t(host_ ? 0 : 1), uint8_t(heard_ ? 1 : 0)};
    Rudp::tagged(key_, packet);
    raw(packet.data(), packet.size());
  }
  if (up_) stream_.update(now, [this](const uint8_t* bytes, size_t size) { raw(bytes, size); });
}
void Link::write(const std::vector<uint8_t>& bytes) {
  if (bytes.size() > (1u << 20) || stream_.queued() + bytes.size() > (1u << 20)) { failed_ = true; return; }
  stream_.write(bytes);
}
void Link::read(std::vector<uint8_t>* bytes) {
  if (!bytes) return;
  std::vector<uint8_t> received;
  stream_.read(&received);
  if (bytes->size() + received.size() > (1u << 20) + 4) { failed_ = true; return; }
  bytes->insert(bytes->end(), received.begin(), received.end());
}
void Link::retire(std::unique_ptr<Link> link) {
  if (link && ok) ending.push_back({std::move(link), nowMs() + 2500});
}

void pump() {
  if (!ok) return;
  if (handler->refreshed && handler->pendingSource) handler->pending.clear();
  handler->refreshed = false;
  if (handler->operationAt && nowMs() - handler->operationAt >= 10000) handler->close();
  SteamAPI_RunCallbacks();
  if (!SteamUser()->BLoggedOn()) { shutdown(); retryAt = nowMs() + 5000; return; }
  handler->operations.erase(std::remove_if(handler->operations.begin(), handler->operations.end(),
      [](const auto& operation) { return operation->finished; }), handler->operations.end());
  if (nowMs() >= handler->pendingUntil) handler->discard();
  if (handler->lobby && !handler->valid()) handler->close();
  for (int i = 0; i < 128; ++i) {
    SteamNetworkingMessage_t* message = nullptr;
    if (SteamNetworkingMessages()->ReceiveMessagesOnChannel(channel, &message, 1) <= 0) break;
    if (!message) break;
    const uint64_t peer = message->m_identityPeer.GetSteamID64();
    for (auto* link : links) {
      if (link->peer_ != peer || !roomMember(link->owner_, peer)) continue;
      if (message->m_cbSize >= 9 && message->m_cbSize <= 2048 && link->packets_.size() < 128) {
        const auto* data = static_cast<const uint8_t*>(message->m_pData);
        link->packets_.emplace_back(data, data + message->m_cbSize);
      }
      break;
    }
    message->Release();
  }
  for (auto it = ending.begin(); it != ending.end();) {
    it->link->poll();
    std::vector<uint8_t> ignored;
    it->link->read(&ignored);
    const auto& stream = it->link->stream();
    if (it->link->streamDead() || nowMs() >= it->until || (!stream.queued() && !stream.unacked())) it = ending.erase(it);
    else ++it;
  }
  if (handler->leaving && ending.empty()) handler->close();
}
}

#else
namespace pp::online::steam {
bool compiledIn() { return false; }
bool ensureInit() { return false; }
void shutdown() {}
void pump() {}
void setPresence(const std::string&, const std::string&) {}
void clearPresence() {}
bool takeJoinRequest(std::string*) { return false; }
void discardJoinRequest() {}
RoomState roomState(const void*) { return RoomState::Failed; }
uint64_t identity() { return 0; }
bool hostRoom(const void*, const std::string&, int) { return false; }
bool joinRoom(const void*, uint64_t, uint64_t, const std::string&, int) { return false; }
void updateRoom(const void*, int, bool) {}
void leaveRoom(const void*) {}
uint64_t roomId(const void*) { return 0; }
bool roomMember(const void*, uint64_t) { return false; }
Link::~Link() = default;
std::unique_ptr<Link> Link::open(const void*, uint64_t, const std::string&, bool) { return nullptr; }
void Link::poll() {}
void Link::write(const std::vector<uint8_t>&) {}
void Link::read(std::vector<uint8_t>*) {}
void Link::retire(std::unique_ptr<Link>) {}
}
#endif
