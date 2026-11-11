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

void addMainMenuButton(Panel& menu);
bool handleMenuButton(App& app, int id);
void tickMainMenu(App& app);
void setPendingInvite(const std::string& url);
void showOnline(App& app);
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
std::string cleanText(const std::string& s, size_t maxBytes);
std::string drawableText(const std::string& s);
std::string fitText(Gfx& g, const std::string& font, const std::string& s, float width);

struct Prefs {
  std::string lobby;
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
