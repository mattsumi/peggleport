#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "online/rudp.h"

namespace pp::online::steam {

bool compiledIn();
bool ensureInit();
void shutdown();
void pump();
void setPresence(const std::string& status, const std::string& connectLink);
void clearPresence();
bool takeJoinRequest(std::string* inviteLink);
void discardJoinRequest();
uint64_t identity();
bool padGlyphs(uint64_t handle, std::array<std::string, 4>* art);
enum class RoomState { Failed, Pending, Active, Busy };
RoomState roomState(const void* owner);
bool hostRoom(const void* owner, const std::string& invite, int capacity);
bool joinRoom(const void* owner, uint64_t lobby, uint64_t host, const std::string& invite, int capacity);
void updateRoom(const void* owner, int seated, bool started);
void leaveRoom(const void* owner);
uint64_t roomId(const void* owner);
bool roomMember(const void* owner, uint64_t peer);

class Link {
 public:
  ~Link();
  static std::unique_ptr<Link> open(const void* owner, uint64_t peer, const std::string& key, bool host);
  void poll();
  void write(const std::vector<uint8_t>& bytes);
  void read(std::vector<uint8_t>* bytes);
  bool accepts(uint64_t peer) const { return peer == peer_ && !failed_ && roomMember(owner_, peer); }
  bool belongs(const void* owner) const { return owner_ == owner; }
  void disconnect() { failed_ = true; }
  bool up() const { return up_ && !streamDead(); }
  bool failed() const { return failed_; }
  bool streamDead() const { return failed_ || stream_.dead(); }
  const Rudp& stream() const { return stream_; }
  static void retire(std::unique_ptr<Link> link);

 private:
  Link(const void* owner, uint64_t peer, const SipKey& key, bool host);
  const void* owner_;
  uint64_t peer_, since_, lastProof_ = 0;
  SipKey key_;
  Rudp stream_;
  bool host_, heard_ = false, up_ = false, failed_ = false;
  std::vector<std::vector<uint8_t>> packets_;
  void raw(const uint8_t* bytes, size_t size);
  friend void pump();
  friend void shutdown();
};

}
