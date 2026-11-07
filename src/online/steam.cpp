#include "online/steam.h"

#ifdef PP_STEAM
#include <steam/steam_api.h>
#include <utility>
#include <string>

#include "log.h"

namespace pp::online::steam {

namespace {
struct JoinHandler {
  std::string pending;
  STEAM_CALLBACK(JoinHandler, onJoin, GameRichPresenceJoinRequested_t);
};
void JoinHandler::onJoin(GameRichPresenceJoinRequested_t* p) { pending = p->m_rgchConnect; }
JoinHandler* g_handler = nullptr;
bool g_ok = false, g_tried = false;
}

bool compiledIn() { return true; }

bool ensureInit() {
  if (g_tried) return g_ok;
  g_tried = true;
  g_ok = SteamAPI_Init();
  if (g_ok) {
    g_handler = new JoinHandler;
    pp::log::info("net", "Steam is connected: friends can join through invites");
  } else {
    pp::log::warn("net", "Steam is not available: invites through Steam are off");
  }
  return g_ok;
}

void shutdown() {
  if (!g_ok) return;
  delete g_handler;
  g_handler = nullptr;
  SteamAPI_Shutdown();
  g_ok = false;
}

void pump() {
  if (g_ok) SteamAPI_RunCallbacks();
}

void setPresence(const std::string& status, const std::string& connectLink) {
  if (!g_ok) return;
  SteamFriends()->SetRichPresence("status", status.c_str());
  SteamFriends()->SetRichPresence("steam_display", "#StatusFull");
  SteamFriends()->SetRichPresence("connect", connectLink.c_str());
}

void clearPresence() {
  if (g_ok) SteamFriends()->ClearRichPresence();
}

bool takeJoinRequest(std::string* inviteLink) {
  if (!g_ok || !g_handler || g_handler->pending.empty()) return false;
  *inviteLink = std::move(g_handler->pending);
  g_handler->pending.clear();
  return true;
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
}

#endif
