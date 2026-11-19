#include "audio.h"
#include "content.h"
#include "match.h"
#include "ui/session.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace pp;
using namespace pp::ui;
namespace fs = std::filesystem;

static void require(bool ok, const char* what) {
  if (!ok) throw std::runtime_error(what);
}

static Board board(const LevelBundle& lb, const BoardConfig& cfg) {
  Board b;
  std::string error;
  require(b.load(lb.base, lb.level, cfg, &error), "board load");
  return b;
}

static void aim(Board& b, int delay = 50) {
  b.gunWheel(b.cfg.nights ? 602 : -69);
  for (int i = 0; i < delay; ++i) b.step();
}

static void sound(Audio& audio, const Event& event, const Board& b, const char* expected) {
  std::array<float, 8192> actual{}, reference{};
  audio.stopSounds();
  audio.onEvents({event}, b);
  audio.render(actual.data(), static_cast<int>(actual.size() / 2));
  audio.stopSounds();
  audio.play(expected);
  audio.render(reference.data(), static_cast<int>(reference.size() / 2));
  require(actual == reference && std::any_of(actual.begin(), actual.end(), [](float x) { return x != 0; }), expected);
}

struct Shot {
  int spin = -1, flip = -1, bucket = 0, penalty = 0;
};

static Shot shoot(Board& b, Audio* audio = nullptr) {
  const int index = b.shotIndex, balls = b.ballsLeft[b.cur];
  Shot shot;
  b.gunClick();
  for (int i = 0; i < 2000 && b.shotIndex == index && b.phase != Phase::Over; ++i) {
    b.step();
    for (const auto& e : b.events) {
      if (e.type == Event::FreeBall) ++shot.bucket;
      if (e.type == Event::Penalty) ++shot.penalty;
      if (e.type == Event::CoinSpin) {
        require(shot.spin == -1 && b.coin.on && b.coin.win == (e.a == 1) && b.coin.angle == e.b, "spin visual state");
        require(b.pegsHitShot == 0 && b.shotScore == 0 && !b.bucketShot && b.ballsLeft[b.cur] == balls - 1, "genuine total miss");
        shot.spin = e.a;
        if (audio) sound(*audio, e, b, "COINSPIN");
      }
      if (e.type == Event::CoinFlip) {
        require(shot.flip == -1 && shot.spin == e.a && !b.coin.on, "one matching toss result");
        require(b.ballsLeft[b.cur] == balls - 1 + e.a && (b.banner == "Free Ball!" && b.bannerTimer > 0) == (e.a == 1), "ball and award banner");
        shot.flip = e.a;
        if (audio) sound(*audio, e, b, e.a ? "FREEBALL" : "COINSPIN_NO");
      }
    }
    b.events.clear();
  }
  require(b.shotIndex != index && shot.spin == shot.flip, "shot resolved");
  return shot;
}

static void peers(const LevelBundle& lb, unsigned seed) {
  BoardConfig cfg = lb.cfg;
  cfg.seed = seed;
  Match local, late;
  std::string err;
  for (Match* m : {&local, &late}) require(m->init(lb.base, lb.level, cfg, 2, true, &err), "Peg Party init");
  const ShotCmd fire{1, 0, 0, cfg.nights ? 5.77f : 4.6f, 0};
  local.submit(fire);
  for (int i = 0; i < 20; ++i) { local.step(); late.step(); }
  late.submit(fire);
  int face = -1;
  for (int i = 0; i < 1500 && local.boards[0].shotIndex == 0; ++i) {
    local.step(); late.step();
    require(local.boards[0].hash() == late.boards[0].hash(), "late peer diverged");
    for (const auto& e : local.boards[0].events) if (e.type == Event::CoinFlip) face = e.a;
    local.boards[0].events.clear(); late.boards[0].events.clear();
  }
  const auto a = local.drainResolved(), b = late.drainResolved();
  require(face == (seed == 1) && late.rollbacks == 1 && a.size() == 1 && b.size() == 1 && a[0].hash == b[0].hash, "resolved Peg Party toss");
}

static void formats(const LevelBundle& lb, const fs::path& path, bool legacy, bool physics) {
  auto cfg = lb.cfg;
  cfg.seed = 3;
  cfg.coinFlipSeen = false;
  cfg.legacyMissCoin = legacy;
  if (physics) cfg.physics.set(PhysicsParams::MaxSpeed, 14.5);
  auto original = board(lb, cfg);
  aim(original);
  const std::vector<ReplayEvent> log{{0, 2, cfg.nights ? 602 : -69, 0, 0}, {original.tick, 1, 0, 0, original.frames}};
  require(shoot(original).flip == legacy, "seed 3 distinguishes old and fair rules");
  ReplayData replay;
  replay.cfg = cfg; replay.level = lb.name; replay.game = cfg.nights; replay.log = log; replay.shots = 1;
  ReplayData read;
  require(replay.save(path) && ReplayData::load(path, read), "scratch replay round-trip");
  require(read.cfg.physics == cfg.physics && read.cfg.legacyMissCoin == legacy && !read.cfg.coinFlipSeen && read.log.size() == 2, "replay preserves rules");
  auto playback = board(lb, read.cfg);
  playback.gunWheel(read.log[0].a);
  while (playback.tick < read.log[1].tick) playback.step();
  require(shoot(playback).flip == legacy && playback.hash() == original.hash(), "replayed miss hash");
  SavedSession session, parsed;
  session.level = lb.name; session.game = cfg.nights; session.mode = static_cast<int>(GameMode::Adventure);
  session.hist.rng = 1; session.cfg = cfg; session.content = lb.contentHash; session.shots = 1; session.log = log;
  session.frames = original.frames; session.tick = original.tick; session.hash = original.hash(); session.ballsLeft = original.ballsLeft[0];
  const auto bytes = session.bytes();
  require(SavedSession::parse(bytes, parsed) && parsed.cfg.legacyMissCoin == legacy && parsed.bytes() == bytes, "session preserves rules on re-save");
  ReadySession ready;
  require(parsed.rebuild(lb, ready.board, ready.snap, ready.haveSnap, ready.aiSnap) && ready.haveSnap && ready.board.hash() == original.hash(), "session reconstructs miss");
}

int main(int argc, char** argv) try {
  if (argc != 4) return 2;
  const fs::path scratch = argv[3];
  for (int g = 0; g < 2; ++g) {
    Vfs v;
    LevelBundle lb;
    std::string error;
    require(v.mountGame(argv[g + 1], &error) && loadLevelBundle(v, g ? "Jimmy4" : "Level2", lb, &error), "mount edition and load stock level");
    lb.cfg.nights = g != 0;
    Audio audio(v);
    for (bool seen : {false, true}) for (int balls : {1, 10}) for (unsigned seed : {1u, 3u, 5u, 8u}) {
      auto cfg = lb.cfg;
      cfg.seed = seed; cfg.coinFlipSeen = seen; cfg.balls = balls;
      auto b = board(lb, cfg);
      aim(b);
      const bool win = seed == 1 || seed == 8;
      require(shoot(b, (!seen && balls == 1) ? &audio : nullptr).flip == win, "fair toss, regardless of first tip or last ball");
      require(b.result.shots == 1 && b.result.freeBallTotal() == 0 && b.score[0] == 0 && b.ballsLeft[0] == balls - 1 + win, "no bucket or score award");
      const bool over = balls == 1 && !win;
      require(b.result.finished == over && b.phase == (over ? Phase::Over : Phase::Aim) && (!over || (!b.result.won && b.result.ballsLeft == 0)), "last ball result");
    }
    auto cfg = lb.cfg;
    cfg.seed = 3; cfg.coinFlipSeen = false;
    auto repeated = board(lb, cfg);
    aim(repeated);
    std::string sequence;
    for (int i = 0; i < (g ? 6 : 2); ++i) {
      const auto shot = shoot(repeated);
      sequence += shot.flip == 1 ? 'H' : shot.flip == 0 ? 'T' : shot.bucket == 1 ? 'B' : '?';
    }
    require(sequence == (g ? "TBBTTH" : "TH"), "repeated same-level tosses");
    auto bucket = board(lb, cfg);
    aim(bucket, g ? 250 : 185);
    const auto caught = shoot(bucket);
    require(caught.bucket == 1 && caught.spin == -1 && bucket.result.freeBalls[0] == 1 && bucket.ballsLeft[0] == 10, "bucket catch is guaranteed, not a coin");
    cfg.ch.active = cfg.ch.noFreeballs = true; cfg.ch.scoreReq = 1000000;
    auto challenge = board(lb, cfg);
    aim(challenge);
    require(shoot(challenge).spin == -1 && challenge.ballsLeft[0] == 9, "no-free-balls challenge");
    cfg = lb.cfg; cfg.duel = true; cfg.players = 2;
    auto duel = board(lb, cfg);
    aim(duel);
    const auto miss = shoot(duel);
    require(miss.penalty == 1 && miss.spin == -1, "duel miss is a penalty");
    for (unsigned seed : {1u, 3u}) peers(lb, seed);
    for (bool legacy : {false, true}) for (bool physics : {false, true}) formats(lb, scratch / ("miss-coin" + replayExt(g)), legacy, physics);
    std::cout << lb.name << " first/seen and last/normal pass; sequence=" << sequence << "; mixer, bucket, challenge, duel, rollback, replay and session pass\n";
  }
  return 0;
} catch (const std::exception& e) {
  std::cerr << "FAIL miss coin toss: " << e.what() << '\n';
  return 1;
}
