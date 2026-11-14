#include <cctype>
#include <cstddef>
#include <string>

#include "online/online_ui.h"

namespace pp::ui::online {

namespace {

bool starts(const std::string& s, const std::string& head) { return s.rfind(head, 0) == 0; }

bool ends(const std::string& s, const std::string& tail, std::string* rest) {
  if (s.size() <= tail.size() || s.compare(s.size() - tail.size(), tail.size(), tail) != 0) return false;
  *rest = s.substr(0, s.size() - tail.size());
  return true;
}

std::string shown(const std::string& s, size_t maxBytes) { return drawableText(cleanText(s, maxBytes)); }

std::string person(const std::string& raw) {
  std::string n = shown(raw, 32);
  if (!n.empty()) n[0] = (char)std::toupper((unsigned char)n[0]);
  return n;
}

const char* const kUnknownLobby = "The lobby gave an answer this game does not know. Press F10 to see it.";
const char* const kUnknownHost = "The host ended the connection. Press F10 to see why.";
const char* const kUnknown = "The connection stopped. Press F10 to see why.";

std::string roomName(const std::string& code) { return code.empty() ? std::string("The room") : "Room " + code; }

std::string lobbyAnswer(const std::string& l, Where where, const std::string& code, bool* known) {
  if (l == "ERR no such room") return (code.empty() ? std::string("No room has that code.") : "No room has the code " + code + ".") + " Check the code, or pick a room from the list.";
  if (l == "ERR room is full") return roomName(code) + " is full or already playing. Pick another room, or create your own.";
  if (l == "ERR host left") return "The host closed " + (code.empty() ? std::string("the room") : "room " + code) + " before you got in.";
  if (l == "ERR not allowed") return "The lobby did not let that player into the room.";
  if (l == "ERR lobby is full") return "This lobby has no space for another room right now. Try again later, or use another lobby.";
  if (l == "ERR too many rooms from your address") return "Your network already has as many rooms open on this lobby as it allows. Close one and try again.";
  if (l == "ERR bad HOST") return "The lobby did not accept this room. Pick another level and try again.";
  if (l == "ERR unknown command") return "This lobby server does not know that request. It may run an older version.";
  if (l == "connection closed") return roomName(code) + " did not answer. The host may have gone - refresh the list and try again.";
  if (l.empty()) return where == Where::Open ? "The lobby did not answer. Check the address, or try again in a moment." : "The lobby did not answer. Try again in a moment.";
  *known = false;
  return kUnknownLobby;
}

}

std::string troubleText(const std::string& raw, Where where, bool host, const std::string& lobby, const std::string& code, bool* known) {
  bool dummy = true;
  if (!known) known = &dummy;
  *known = true;
  std::string who;
  if (raw.empty()) return where == Where::Match ? "The connection was lost." : where == Where::Room ? "The room was closed." : "The lobby did not answer. Try again in a moment.";
  if (starts(raw, "cannot resolve ")) return "Could not find a server called \"" + shown(raw.substr(15), 40) + "\". Check the lobby address.";
  if (starts(raw, "no usable address for ")) return "Could not find a server called \"" + shown(raw.substr(22), 40) + "\". Check the lobby address.";
  if (starts(raw, "cannot connect to ")) return "Could not reach a lobby at " + lobby + ". Check the address, or the server may be offline.";
  if (raw == "lobby connection failed") return "The connection to the lobby broke. Try again.";
  if (raw == "no room list") return lobby + " did not answer like a lobby server. Check the address and the port.";
  if (raw == "lobby heartbeat unavailable") return "This lobby server needs an update before you can play a match. Use the matching version of pp_lobbyd.";
  if (starts(raw, "lobby connection lost: ")) return "The match ended because the host (" + shown(raw.substr(23), 24) + ") lost connection to the lobby. The cause is unknown.";
  if (starts(raw, "lobby player connection lost: ")) return "The match ended because the lobby connection for " + shown(raw.substr(30), 24) + " was lost. The cause is unknown.";
  if (raw == "lobby relay connection lost") return "The match ended because the lobby connection was lost. The cause is unknown.";
  if (raw == "lobby connection lost") return where == Where::Match ? "The match ended because the connection to the lobby was lost. The cause is unknown." : host ? "The connection to the lobby was lost, so your room is closed. Create it again." : "The connection to the lobby was lost. Rejoin the room to try again.";
  if (starts(raw, "lobby refused the room: ")) return lobbyAnswer(raw.substr(24), Where::Open, code, known);
  if (raw == "protocol version mismatch") return "The host uses a different version of PegglePort. You both need the same version to play together.";
  if (raw == "match is full") return roomName(code) + " is full or already playing. Pick another room, or create your own.";
  if (starts(raw, "content mismatch")) return "Your level or mod files differ from the host's, so the games would not stay in sync. Use the same mods as the host.";
  if (raw == "lost connection to the host") return "The connection to the host was lost.";
  if (raw == "the host closed the room") return "The host closed the room.";
  if (raw == "the host left the match") return "The host left the match.";
  if (starts(raw, "lobby: ")) return lobbyAnswer(raw.substr(7), where, code, known);
  if (ends(raw, " left the match", &who)) return person(who) + " left the match.";
  if (ends(raw, " left the room", &who)) return person(who) + " left the room.";
  if (ends(raw, " closed the room", &who)) return person(who) + " closed the room.";
  if (ends(raw, " lost connection", &who)) return person(who) + " lost the connection.";
  if (const size_t at = raw.find(" lost the direct link ("); at != std::string::npos && at > 0) return "The connection to " + shown(raw.substr(0, at), 32) + " was lost.";
  if (starts(raw, "the direct link to ") && ends(raw.substr(19), " could not be completed", &who)) return "The connection to " + shown(who, 32) + " could not be set up. Try again.";
  *known = false;
  return host || where == Where::List || where == Where::Open ? kUnknown : kUnknownHost;
}

std::string linkText(const std::string& note, bool brief) {
  const std::string relay = "Relayed through the lobby";
  if (!starts(note, relay + " - ")) return shown(note, 96);
  const std::string why = note.substr(relay.size() + 3);
  if (brief) return relay;
  if (starts(why, "symmetric NAT")) return relay + " - your router does not allow direct links";
  if (starts(why, "the rendezvous did not answer")) return relay + " - its UDP port did not answer";
  if (starts(why, "the other player never registered")) return relay + " - the other player could not set up a direct link";
  if (starts(why, "the NAT/firewall blocked")) return relay + " - a router or firewall blocked the direct link";
  if (why == "closed") return relay;
  return relay + " - trying a direct link...";
}

std::string probeText(const pp::online::LobbyProbe& r, const std::string& host) {
  if (!r.resolved) return "Could not find a server called \"" + shown(host, 40) + "\". Check the lobby address.";
  if (!r.opened) return "This computer could not open a UDP port for the check. Matches still run through the lobby.";
  if (!r.answered) return "The lobby did not answer over UDP. You can still play: matches run through the lobby, with a little more delay. On your own server, open UDP port " + std::to_string(r.port) + ".";
  if (!r.natKnown) return "The lobby answers over UDP. The router check did not answer, so a direct link is tried and the lobby is used if it fails.";
  if (r.samePort) return "Direct links should work: the lobby answers over UDP and your router keeps one port for every destination.";
  return "The lobby answers over UDP, but your router uses a different port for every destination, so direct links will not work. Matches run through the lobby instead.";
}

}
