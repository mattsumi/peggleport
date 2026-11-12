#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "content.h"
#include "profile.h"
#include "sim.h"
#include "ui/replay.h"

namespace pp::ui {

class App;
class GameScreen;

struct SavedSession {
  static constexpr int kMaxFrames = 600000;
  static constexpr size_t kMaxEvents = 600000;
  static constexpr size_t kMaxBytes = 16u * 1024u * 1024u;

  int game = 0, mode = 0;
  std::string level;
  uint64_t content = 0;
  int stage = 0, levelNo = 0;
  int shots = 0, score = 0, ballsLeft = 0;
  int frames = 0, tick = 0;
  uint64_t hash = 0;
  BoardConfig cfg;
  std::vector<ReplayEvent> log;

  std::vector<uint8_t> bytes() const;
  static bool parse(const std::vector<uint8_t>& data, SavedSession& out);
  bool save(const std::filesystem::path& file) const;
  static bool load(const std::filesystem::path& file, SavedSession& out);
  bool rebuild(const LevelBundle& lb, Board& board, Board& snap, bool& haveSnap) const;
};

struct ReadySession {
  SavedSession saved;
  LevelBundle bundle;
  Board board, snap;
  bool haveSnap = false;
};

class SessionKeeper {
 public:
  explicit SessionKeeper(App& a) : app_(a) {}
  std::filesystem::path file(GameMode m) const;
  bool keep(const GameScreen& gs);
  bool find(GameMode m);
  void drop(GameMode m);
  bool resume();
  const ReadySession* found() const { return ready_.get(); }
  bool askToContinue(bool ask);
  bool askToLeave();
  void finishContinue(bool goOn);
  void startAsUsual();
  void backOut();

 private:
  App& app_;
  std::unique_ptr<ReadySession> ready_;
};

SessionKeeper& sessionsOf(App& a);

enum { D_ContinueGame = 45 };

}
