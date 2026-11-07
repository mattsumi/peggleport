#pragma once
#include <string>
#include <vector>

namespace pp::online {

struct Invite {
  std::string host;
  int port = 7770;
  std::string code;
};

bool parseInvite(const std::string& url, Invite* out);
std::string makeInvite(const std::string& host, int port, const std::string& code);
bool findInviteArg(const std::vector<std::string>& args, Invite* out);
bool extractInvite(const std::string& text, Invite* out);
bool isInviteLike(const std::string& s);

std::string registrationCommandLine(const std::string& exePath);
bool registerUrlHandler(const std::string& exePath, std::string* report);
bool unregisterUrlHandler(std::string* report);
std::string macInfoPlistSnippet();
std::string selfExePath(const char* argv0);

}
