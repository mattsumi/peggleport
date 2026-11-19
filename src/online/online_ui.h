#pragma once
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "content.h"
#include "net.h"
#include "ui/app.h"

namespace pp::ui::online {

constexpr int kMenuButtonId = 90;

enum class Flow { Browse, Quick, Custom, Host, Join };

void addMainMenuButton(Panel& menu);
bool handleMenuButton(App& app, int id);
void tickMainMenu(App& app);
void setPendingInvite(const std::string& url);
void showOnline(App& app);
void showOnline(App& app, Flow flow, bool party);
const char* masterDisplayName(int i);
const char* masterKey(int i);
int masterTotal(bool nights);
std::string masterNameAt(bool nights, int i);
std::string masterKeyAt(bool nights, int i);
std::string masterNameForPower(int power);

struct LevelChoice {
  std::string file, title, group;
};
std::vector<LevelChoice> levelChoices(const App& app);
std::string levelTitle(const App& app, const std::string& file);
bool levelInstalled(const App& app, const std::string& file);
std::string levelLogName(const App& app, const std::string& file);
std::string cleanText(const std::string& s, size_t maxBytes);
std::string drawableText(const std::string& s);
std::string fitText(Gfx& g, const std::string& font, const std::string& s, float width);

enum class Where { List, Open, Join, Room, Match };
std::string troubleText(const std::string& raw, Where where, bool host, const std::string& lobby, const std::string& code, bool* known = nullptr);
std::string linkText(const std::string& note, bool brief);
std::string probeText(const pp::online::LobbyProbe& r, const std::string& host);

struct Prefs {
  std::string lobby;
  bool discord = false;
  int mode = 0;
  int master = 0;
  std::string masterMod;
  std::string level;
  void load(const std::filesystem::path& file);
  void save(const std::filesystem::path& file) const;
};

struct MatchLaunch {
  std::unique_ptr<Session> session;
  LevelBundle lb;
  MatchInfo info;
  std::string lobbyHost;
  int lobbyPort = 7770;
};
void startMatchScreen(App& app, MatchLaunch&& launch);

}
