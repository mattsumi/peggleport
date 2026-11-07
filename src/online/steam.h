#pragma once
#include <string>

namespace pp::online::steam {

bool compiledIn();
bool ensureInit();
void shutdown();
void pump();
void setPresence(const std::string& status, const std::string& connectLink);
void clearPresence();
bool takeJoinRequest(std::string* inviteLink);

}
