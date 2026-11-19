#include <SDL3/SDL.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include "editor/model.h"
#include "physball.h"
#include "ui/character_dialog.h"
#include "ui/game_screen.h"
#include "ui/screens.h"
#include "ui/session.h"

using namespace pp;
using namespace pp::ui;
namespace fs = std::filesystem;

static int checks = 0, failures = 0;
static std::string testCase;

static bool check(bool pass, const std::string& name) {
  ++checks;
  if (!pass) { ++failures; std::cerr << "FAIL " << testCase << ": " << name << '\n'; }
  return pass;
}

static bool fixture(const fs::path& path, int width) {
  Level level;
  level.version = 0x42;
  level.mode = 1;
  for (float x : {-70.0f, 720.0f, 325.0f}) {
    Val entry;
    entry.k = VK::Ptr;
    entry.n = ed::newCircle(x, 250, 10, x == 325 ? 2 : 0, false);
    entry.n->setF("bouncy", 0.8f);
    level.entries.push_back(entry);
  }
  auto data = writeLevel(level);
  fs::create_directories(path.parent_path());
  std::ofstream dat(path, std::ios::binary);
  dat.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  auto side = path;
  side.replace_extension(".ini");
  std::ofstream ini(side);
  ini << "board_width = " << width << "\norange = 1\n";
  return static_cast<bool>(dat) && static_cast<bool>(ini);
}

static int terrain(const Board& b, float x) {
  for (int i = 0; i < static_cast<int>(b.objs.size()); ++i) {
    const auto& o = b.objs[i];
    if (!o.fromBase && o.shape == Shape::Circle && o.pegType == 0 && std::fabs(o.pos.x - x - (b.W - 650) * 0.5f) < 0.001f) return i;
  }
  return -1;
}

static void geometry(const Board& b, int authored, int display) {
  check(b.W == display && b.cfg.boardWidth == display, "display width preserved");
  check(b.cfg.authoredWidth == authored, "authored width comes from level, not display");
  for (float x : {-70.0f, 720.0f}) {
    int i = terrain(b, x);
    if (!check(i >= 0, "extension terrain retains translated position")) continue;
    const auto& o = b.objs[i];
    check(o.radius == 10 && o.y0 == 240 && o.y1 == 260, "extension geometry is not stretched");
    check(o.collidable == (authored > 650) && o.visible == (authored > 650), "authored terrain retained; classic off-field scenery culled");
  }
}

static void contacts(const Board& source, const Board& restored, int authored) {
  for (float x : {-70.0f, 720.0f}) {
    int index = terrain(source, x);
    if (!check(index >= 0 && terrain(restored, x) == index, "contact object identity preserved")) continue;
    Board a = source, b = restored;
    for (Board* p : {&a, &b}) {
      if (p->phase == Phase::Aim) p->fire(4.712389f);
      Ball ball;
      phys::ballSetPos(ball, p->objs[index].pos.x, p->objs[index].y0 - 18);
      ball.vel = {0, 15 - p->physics.gravityY};
      p->balls.assign(1, ball);
      p->events.clear();
    }
    bool hit = false, bounce = false, same = true;
    for (int tick = 0; tick < 8; ++tick) {
      a.step();
      b.step();
      same &= a.hash() == b.hash() && a.contactNotes() == b.contactNotes();
      for (auto [ball, object] : b.contactNotes()) hit |= object == index;
      bounce |= !b.balls.empty() && b.balls[0].vel.y < 0;
      a.events.clear();
      b.events.clear();
    }
    check(same, "restored contact trace and per-frame hashes match original");
    check(hit == (authored > 650) && bounce == (authored > 650), "real extension contact and rebound follow authored provenance");
  }
}

static void aiSnapshot(const SavedSession& session, const LevelBundle& bundle, int authored, int display) {
  SavedSession saved = session;
  saved.mode = static_cast<int>(GameMode::Duel);
  saved.cfg.duel = true;
  saved.cfg.players = 2;
  Board original;
  std::string err;
  if (!check(original.load(bundle.base, bundle.level, saved.cfg, &err), "AI snapshot source loads")) return;
  saved.log = {{0, 4, 0, 1, 0}};
  saved.frames = saved.tick = saved.shots = saved.score = 0;
  saved.ballsLeft = original.ballsLeft[0];
  saved.hash = original.hash();
  saved.duel.state = 1;
  ReadySession ready;
  if (!check(SavedSession::parse(saved.bytes(), ready.saved), "AI session serializer round-trip")) return;
  if (!check(ready.saved.rebuild(bundle, ready.board, ready.snap, ready.haveSnap, ready.aiSnap), "AI session rebuild succeeds")) return;
  geometry(ready.aiSnap, authored, display);
  check(ready.aiSnap.hash() == original.hash(), "AI snapshot retains exact starting hash");
  contacts(original, ready.aiSnap, authored);
}

static void rejectedSessions(Gfx& gfx, Vfs& vfs, Game game, const fs::path& dir) {
  App::Config cfg;
  cfg.game = game;
  cfg.noSave = true;
  cfg.noAudio = true;
  cfg.seed = 4357;
  cfg.userDir = dir / "userdata";
  PlayerInfo profile;
  profile.reset();
  profile.name = "Session Preservation Test";
  App app(gfx, vfs, nullptr, nullptr, cfg);
  app.curProfile = &profile;
  app.gameMode = GameMode::Adventure;
  app.curStage = app.curLevel = 0;
  GameScreen original(app, true, true);
  if (!check(original.ok(), "preservation source GameScreen loads")) return;
  original.keyDown(SDLK_RETURN, 0);
  Input release;
  release.type = Input::KeyUp;
  release.key = SDLK_RETURN;
  original.input(release);
  for (int i = 0; i < 12; ++i) { original.board().step(); original.board().events.clear(); }
  SavedSession usable;
  if (!check(original.fillSession(usable), "capture preservation source session")) return;
  usable.mode = static_cast<int>(GameMode::QuickPlay);
  app.showMainMenu();
  for (int i = 0; i < 100; ++i) app.tick();
  const auto profileBytes = profile.serialize();
  const fs::path path = app.profiles.savedGamePath(profile, GameMode::QuickPlay);
  if (!check(path.parent_path() == cfg.userDir && path.parent_path().parent_path() == dir, "rejected-session path is isolated scratch userdata")) return;
  if (!check(app.stages.levelByName(usable.level) != nullptr, "preservation level is in the real stage index")) return;
  SessionKeeper& keeper = sessionsOf(app);
  for (const std::string fault : {"board hash", "content hash", "invalid format"}) {
    SavedSession bad = usable;
    if (fault == "board hash") bad.hash ^= 1;
    if (fault == "content hash") bad.content ^= 1;
    auto bytes = bad.bytes();
    if (fault == "invalid format") bytes[0] ^= 1;
    SavedSession parsed;
    const bool parses = SavedSession::parse(bytes, parsed);
    check(parses == (fault != "invalid format"), fault + " fixture parser result");
    if (parses) {
      ReadySession rejected;
      check(!parsed.rebuild(original.bundle(), rejected.board, rejected.snap, rejected.haveSnap, rejected.aiSnap), fault + " fixture fails reconstruction");
    }
    for (bool menu : {false, true}) {
      if (!check(usable.save(path), "write usable lookup control")) return;
      app.cfg.noSave = false;
      const bool found = keeper.find(GameMode::QuickPlay);
      app.cfg.noSave = true;
      if (!check(found && keeper.found(), "usable lookup succeeds before rejection")) return;
      std::ofstream file(path, std::ios::binary);
      file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      file.close();
      if (!check(static_cast<bool>(file), "write rejected-session fixture")) return;
      bool accepted = false;
      if (menu) {
        app.showMainMenu();
        for (int i = 0; i < 100; ++i) app.tick();
        auto* mainMenu = dynamic_cast<MainMenu*>(app.screen());
        if (!check(mainMenu != nullptr, "real main menu opens")) return;
        app.cfg.noSave = false;
        mainMenu->onButton(1);
        app.cfg.noSave = true;
      } else {
        app.cfg.noSave = false;
        accepted = keeper.find(GameMode::QuickPlay);
        app.cfg.noSave = true;
      }
      check(!accepted && !keeper.found() && !keeper.resume(), fault + " cannot resume or retain a stale ready session");
      std::ifstream preserved(path, std::ios::binary);
      std::vector<uint8_t> after(bytes.size());
      preserved.read(reinterpret_cast<char*>(after.data()), static_cast<std::streamsize>(after.size()));
      check(preserved && after == bytes && preserved.peek() == std::char_traits<char>::eof(), fault + (menu ? " menu preserves original bytes" : " lookup preserves original bytes"));
      if (menu) {
        for (int i = 0; i < 100; ++i) app.tick();
        check(app.screen() && std::string(app.screen()->name()) == "levelscreen" && !app.getDialog(D_ContinueGame) && !dynamic_cast<GameScreen*>(app.screen()), "rejected Quick Play opens selection without continuing");
      }
      check(profile.serialize() == profileBytes, "rejected lookup/menu leaves player progress unchanged");
    }
  }
  if (!check(usable.save(path), "write explicit-discard control")) return;
  app.cfg.noSave = false;
  keeper.drop(GameMode::QuickPlay);
  app.cfg.noSave = true;
  check(!fs::exists(path) && !keeper.found(), "explicit discard still removes the session");
  check(!fs::exists(cfg.userDir / "users.dat") && !fs::exists(app.profiles.detailsPath(profile)), "rejection checks write no profile files");
}

static int diskVersion(const fs::path& path) {
  std::ifstream f(path, std::ios::binary);
  f.seekg(4);
  return f.get();
}

static void historicalRecordings(Gfx& gfx, Vfs vfs, const fs::path& root) {
  const fs::path fixtures = fs::path(__FILE__).parent_path() / "recordings";
  std::string mountError;
  if (!check(vfs.mountMod(fixtures, &mountError), "mount synthetic collision board")) return;
  App::Config cfg;
  cfg.game = Game::Deluxe;
  cfg.noSave = cfg.noAudio = cfg.noRoomDiscovery = true;
  cfg.userDir = root / "userdata";
  PlayerInfo profile;
  profile.reset();
  App app(gfx, vfs, nullptr, nullptr, cfg);
  app.curProfile = &profile;
  app.gameMode = GameMode::Adventure;
  struct Recording { const char* producer; SimRules rules; uint64_t frame71; };
  const Recording recordings[] = {
    {"classic", SimRules::Classic, 3974857467461232688ull}, {"hd", SimRules::HD, 2299244122164583032ull},
    {"full-stroke", SimRules::FullStroke, 277785596890327708ull}, {"fair-coin", SimRules::FairCoin, 277785596890327708ull}
  };
  for (const auto& [producer, rules, frame71] : recordings) {
    testCase = producer;
    ReadySession r;
    std::string err;
    if (!check(SavedSession::load(fixtures / (testCase + ".ppss"), r.saved) &&
               loadLevelBundle(vfs, r.saved.level, r.bundle, &err), "original producer checkpoint")) continue;
    auto rebuild = [&](const SavedSession& s) { return s.rebuild(r.bundle, r.board, r.snap, r.haveSnap, r.aiSnap); };
    auto invalid = r.saved;
    invalid.hash ^= 1;
    check(!rebuild(invalid), "no reconstruction accepts the wrong terminal hash");
    invalid = r.saved;
    ++invalid.score;
    check(!rebuild(invalid), "matching hash does not bypass summary validation");
    if (!check(rebuild(r.saved) && r.haveSnap && r.board.hash() == r.saved.hash, "original producer terminal hash")) continue;
    app.mouseX = static_cast<float>(75 + r.board.mouseX);
    app.mouseY = static_cast<float>(40 + r.board.mouseY);
    check(r.board.cfg.simRules == rules && r.snap.cfg.simRules == rules, "board and shot snapshot retain validated rules");
    GameScreen resumed(app, r);
    SavedSession next, decoded;
    if (!check(resumed.fillSession(next) && SavedSession::parse(next.bytes(), decoded) && rebuild(decoded), "resumed checkpoint reconstructs again")) continue;
    check(next.bytes()[4] == 4 && decoded.cfg.simRules == rules, "new checkpoint identifies its simulation");
    auto future = next.bytes();
    ++future[4];
    check(!SavedSession::parse(future, invalid), "unknown checkpoint format rejected");
    invalid = next;
    invalid.cfg.simRules = static_cast<SimRules>(42);
    check(!SavedSession::parse(invalid.bytes(), decoded), "unknown rules with valid checksum rejected");
    invalid.cfg.simRules = rules == SimRules::Classic ? SimRules::HD : SimRules::Classic;
    check(!rebuild(invalid), "tagged checkpoint never falls back to other rules");
    fs::path path;
    ReplayData replay;
    if (!check(resumed.saveReplay(true, &path, testCase) && ReplayData::load(path, replay), "export replay of resumed board")) continue;
    check(diskVersion(path) == 4 && replay.cfg.simRules == rules, "new replay identifies its simulation");
    GameScreen viewer(app, replay);
    for (int i = 0; i < r.saved.frames && viewer.replaying(); ++i) viewer.update();
    check(viewer.ok() && viewer.board().hash() == r.saved.hash, "viewer reproduces original producer checkpoint");
    for (int i = r.saved.frames; i < 71; ++i) {
      viewer.update();
      resumed.update();
    }
    check(viewer.board().hash() == frame71 && resumed.board().hash() == frame71, "resumed and exported shots continue on producer trajectory");
    for (const int at : {4, 8}) {
      replay.save(path);
      std::fstream bytes(path, std::ios::binary | std::ios::in | std::ios::out);
      bytes.seekp(at);
      bytes.put(42);
      bytes.close();
      check(!ReplayData::load(path, replay), "unknown replay format or rules rejected");
    }
    for (const char* name : {"untagged.pego", "untagged-physics.pego", "fair-coin.pego"}) {
      const bool unknown = std::string(name) != "fair-coin.pego";
      if (rules != (unknown ? SimRules::FullStroke : SimRules::FairCoin)) continue;
      ReplayData old;
      if (!check(ReplayData::load(fixtures / name, old), "original producer replay")) continue;
      check(old.cfg.simRules == (unknown ? SimRules::Unspecified : rules), "do not assign an origin to shared-format input");
      GameScreen preview(app, old);
      if (unknown) {
        const auto start = preview.board().hash();
        preview.update();
        check(app.getDialog(D_Tip) && preview.paused() && preview.board().hash() == start, "warn before unverified playback advances");
        app.killDialog(D_Tip);
      }
      for (int i = 0; i < r.saved.frames && preview.replaying(); ++i) preview.update();
      check(preview.ok() && preview.board().hash() == r.saved.hash, "shared format retains prior playback; v3 is exact");
    }
  }
}

static bool pickMaster(App& app, GameScreen& gs, int cell) {
  for (int i = 0; i < 50; ++i) app.tick();
  auto* picker = dynamic_cast<CharacterDialog*>(app.getDialog(D_Character));
  if (!check(picker && gs.paused(), "live master picker is open")) return false;
  float x, y, w, h;
  picker->cellRect(0, cell, &x, &y, &w, &h);
  Input click;
  click.type = Input::MouseDown; click.button = 1; click.clicks = 2;
  click.x = picker->x + x + w / 2; click.y = picker->y + y + h / 2;
  app.input(click);
  click.type = Input::MouseUp; app.input(click);
  for (int i = 0; i < 100 && gs.paused(); ++i) app.tick();
  return check(gs.ok() && !gs.paused() && !app.getDialog(D_Character) && app.curProfile->char1 == cell &&
               gs.board().cfg.master[0] == Board::masterByName(cell == 1 ? "jimmy" : "bjorn"), "confirmed master starts board");
}

static void fireShot(GameScreen& gs) {
  gs.keyDown(SDLK_RETURN, 0);
  Input up;
  up.type = Input::KeyUp; up.key = SDLK_RETURN;
  gs.input(up);
}

static bool diskRules(GameScreen& gs, bool legacy, const std::string& stem) {
  const auto path = gs.app.userDir / (stem + ".ppss");
  SavedSession save, parsed;
  if (!check(gs.fillSession(save) && save.save(path) && SavedSession::load(path, parsed), "re-save live session to scratch")) return false;
  fs::path replay;
  ReplayData read;
  if (!check(gs.saveReplay(true, &replay, stem) && ReplayData::load(replay, read), "write and load actual exported replay")) return false;
  const int sv = diskVersion(path), rv = diskVersion(replay);
  std::cout << " session=v" << sv << " replay=v" << rv << '\n';
  check(sv == 4 && parsed.cfg.legacyMissCoin() == legacy, "serialized session coin rules");
  check(rv == 4 && read.cfg.legacyMissCoin() == legacy, "serialized replay coin rules");
  App playback(gs.app.gfx, gs.app.vfs, nullptr, nullptr, gs.app.cfg);
  GameScreen viewer(playback, read);
  for (int i = 0; i < parsed.frames && viewer.replaying(); ++i) viewer.update();
  check(viewer.ok() && viewer.board().hash() == gs.board().hash(), "exported replay reproduces the live board");
  ReadySession ready;
  return check(parsed.rebuild(gs.bundle(), ready.board, ready.snap, ready.haveSnap, ready.aiSnap) && ready.board.hash() == gs.board().hash(), "re-saved shot reconstructs");
}

static void pickerRules(Gfx& gfx, Vfs& vfs, Game game, const fs::path& dir, bool legacy, bool restart) {
  const bool nights = game == Game::Nights;
  App::Config cfg;
  cfg.game = game;
  cfg.noSave = cfg.noAudio = cfg.noRoomDiscovery = true;
  cfg.widescreen = 0;
  cfg.seed = 11;
  cfg.userDir = dir / "userdata";
  PlayerInfo profile;
  profile.reset();
  profile.name = "Picker Test";
  profile.maxStage = nights ? 11 : 10;
  profile.advCompletions = 1;
  profile.storyPending = false;
  fs::path path;
  SavedSession before;
  {
    App source(gfx, vfs, nullptr, nullptr, cfg);
    source.curProfile = &profile;
    const auto* level = source.stages.levelByName(nights ? "Jimmy4" : "Level2");
    if (!check(level != nullptr, "stock Adventure level indexed")) return;
    profile.advStage = level->stage; profile.advLevel = level->index;
    source.mouseX = 400; source.mouseY = 420;
    source.setRandState(400461691u);
    source.startAdventureGame();
    auto* gs = dynamic_cast<GameScreen*>(source.screen());
    path = source.profiles.savedGamePath(profile, GameMode::Adventure);
    if (!check(path.parent_path() == cfg.userDir && gs && gs->ok() && gs->paused() && source.getDialog(D_Character), "savable stock Adventure picker")) return;
    source.cfg.noSave = false;
    const bool kept = sessionsOf(source).keep(*gs);
    source.cfg.noSave = true;
    if (!check(kept && SavedSession::load(path, before) && !before.cfg.legacyMissCoin() && before.flags == 1 && before.shots == 0 && before.cfg.seed == 3, "keeper persists an untouched picker")) return;
  }
  if (legacy) {
    before.cfg.simRules = SimRules::Unspecified;
    if (!check(before.save(path), "persist picker in legacy format")) return;
  }
  App app(gfx, vfs, nullptr, nullptr, cfg);
  app.curProfile = &profile;
  app.gameMode = GameMode::Adventure;
  app.mouseX = 400; app.mouseY = 420;
  app.cfg.noSave = false;
  const bool continued = sessionsOf(app).askToContinue(nights);
  app.cfg.noSave = true;
  auto* gs = dynamic_cast<GameScreen*>(app.screen());
  if (!check(continued && !fs::exists(path) && gs && gs->ok() && gs->board().cfg.legacyMissCoin() == legacy && gs->board().hash() == before.hash, "continued actual saved picker")) return;
  if (!pickMaster(app, *gs, 1)) return;
  Board& b = gs->board();
  check(b.cfg.legacyMissCoin() == legacy && b.cfg.seed == before.cfg.seed && gs->bundle().name == before.level, "same board keeps saved coin rule and seed");
  Input aim;
  aim.type = Input::MouseMove;
  aim.x = (gs->canvasWidth() - 800) / 2.0f + (nights ? 509.0f : 389.0f);
  aim.y = 135;
  app.input(aim);
  for (int i = 0; i < 50; ++i) app.tick();
  const int balls = b.ballsLeft[0];
  fireShot(*gs);
  int face = -1;
  for (int i = 0; i < 2000 && !b.shotIndex; ++i) {
    app.tick();
    if (face == -1 && b.coin.on) {
      face = b.coin.win;
      check(!b.cfg.coinFlipSeen && !(b.shotRand[1] & 1u) && b.pegsHitShot == 0 && b.shotScore == 0 && !b.bucketShot, "eligible miss distinguishes first legacy heads from fair tails");
    }
  }
  check(face == int(legacy) && b.shotIndex == 1 && b.score[0] == 0 && b.ballsLeft[0] == balls - 1 + face, "live coin face and ball count");
  std::cout << "PICKER " << testCase << " " << before.level << " face=" << face << " balls=" << b.ballsLeft[0] << " draw=" << b.shotRand[1];
  if (!diskRules(*gs, legacy, "confirmed")) return;
  if (restart) gs->restartLevel();
  else {
    const int shots[2][7] = {{562, -1060, -220, -80, 1560, -1260, 1380}, {-44, -960, -220, -180, -40, 320, 1340}};
    for (int notches : shots[nights]) {
      const int index = b.shotIndex;
      Input wheel;
      wheel.type = Input::Wheel; wheel.wheel = static_cast<float>(notches);
      app.input(wheel);
      for (int i = 0; i < 38; ++i) app.tick();
      fireShot(*gs);
      for (int i = 0; i < 5000 && b.shotIndex == index && !gs->levelDone(); ++i) app.tick();
    }
    for (int i = 0; i < 3000 && !gs->levelDone() && !b.orangeLeft; ++i) app.tick();
    if (!check(gs->levelDone() && b.won && b.orangeLeft == 0, "stock Adventure level won by recorded shots")) return;
    gs->keyDown(SDLK_RETURN, 0);
    for (int i = 0; i < 500 && !app.getDialog(D_EndLevel); ++i) app.tick();
    auto* end = dynamic_cast<EndLevelDialog*>(app.getDialog(D_EndLevel));
    if (!check(end && end->mode == EndLevelDialog::AdventureWin, "Adventure Win offers next level")) return;
    end->onButton(1001);
  }
  const std::string expected = restart ? before.level : nights ? "Jimmy5" : "Level3";
  if (!check(gs->ok() && !b.cfg.legacyMissCoin() && gs->bundle().name == expected, "fresh picker has the new attempt's rules and level")) return;
  if (!pickMaster(app, *gs, 0)) return;
  check(gs->seed != before.cfg.seed && gs->bundle().name == expected && !b.cfg.legacyMissCoin(), "new attempt gets a new seed and fair rules");
  fireShot(*gs);
  for (int i = 0; i < 20; ++i) app.tick();
  std::cout << "PICKER " << (restart ? "restart " : "next ") << gs->bundle().name;
  diskRules(*gs, false, "new-attempt");
}

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) return 2;
  const char* tmp = std::getenv("TMPDIR");
  if (!tmp || !*tmp) { std::cerr << "TMPDIR must name a scratch directory\n"; return 2; }
  const fs::path root = fs::path(tmp) / ("peggle-authored-session-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  if (!SDL_Init(0)) { std::cerr << SDL_GetError() << '\n'; return 2; }
  for (int arg = 1; arg < argc; ++arg) {
    const Game game = arg == 1 ? Game::Deluxe : Game::Nights;
    Vfs vfs;
    std::string err;
    if (!vfs.mountGame(argv[arg], &err)) { std::cerr << err << '\n'; return 2; }
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(1550, 600, SDL_PIXELFORMAT_RGBA32), SDL_DestroySurface);
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer(surface ? SDL_CreateSoftwareRenderer(surface.get()) : nullptr, SDL_DestroyRenderer);
    if (!renderer) { std::cerr << SDL_GetError() << '\n'; return 2; }
    Gfx gfx(renderer.get(), vfs);
    if (arg == 1) historicalRecordings(gfx, vfs, root / "history");
    for (int authored : {650, 917}) for (int display : {917, 1400}) {
      testCase = std::string(arg == 1 ? "Deluxe" : "Nights") + " A" + std::to_string(authored) + " D" + std::to_string(display);
      const fs::path dir = root / (std::to_string(arg) + "-" + std::to_string(authored) + "-" + std::to_string(display));
      const fs::path level = dir / "Authored.dat";
      if (!check(fixture(level, authored), "write scratch DAT and sidecar")) continue;
      App::Config cfg;
      cfg.game = game;
      cfg.noSave = true;
      cfg.noAudio = true;
      cfg.seed = 4357;
      cfg.boardWidth = display;
      cfg.userDir = dir / "userdata";
      PlayerInfo profile;
      profile.reset();
      profile.name = "Authored Session Test";
      App app(gfx, vfs, nullptr, nullptr, cfg);
      app.curProfile = &profile;
      app.gameMode = GameMode::Adventure;
      app.curStage = app.curLevel = 0;
      auto* stage = app.stages.stage(0);
      if (!check(stage && !stage->levels.empty(), "real game stages loaded")) continue;
      stage->levels[0].file = level.generic_string();
      app.mouseX = static_cast<float>(75 + display / 2);
      app.mouseY = 400;
      GameScreen original(app, true, true);
      if (!check(original.ok(), "original GameScreen loads: " + original.loadError)) continue;
      geometry(original.board(), authored, display);
      const Board initial = original.board();
      original.keyDown(SDLK_RETURN, 0);
      Input release;
      release.type = Input::KeyUp;
      release.key = SDLK_RETURN;
      original.input(release);
      for (int i = 0; i < 12; ++i) { original.board().step(); original.board().events.clear(); }
      SavedSession saved;
      if (!check(original.fillSession(saved), "capture actual in-flight GameScreen session")) continue;
      check(saved.frames == 12 && saved.shots > 0, "nonempty frame and shot history captured");
      const auto bytes = saved.bytes();
      check(bytes.size() > 8 && bytes[4] == 4 && bytes[5] == 0, "new session uses fair coin format");
      ReadySession ready;
      ready.bundle = original.bundle();
      if (!check(SavedSession::parse(bytes, ready.saved), "real session serializer round-trip")) continue;
      check(!ready.saved.cfg.legacyMissCoin() && ready.saved.cfg.authoredWidth == 650, "new session restores fair rule without serializing authored width");
      check(ready.saved.bytes() == bytes, "session reserialization is byte-identical");
      auto altered = ready.bundle;
      altered.contentHash ^= 1;
      check(!ready.saved.rebuild(altered, ready.board, ready.snap, ready.haveSnap, ready.aiSnap), "content validation is not bypassed");
      auto badHash = ready.saved;
      badHash.hash ^= 1;
      check(!badHash.rebuild(ready.bundle, ready.board, ready.snap, ready.haveSnap, ready.aiSnap), "board hash validation is not bypassed");
      bool rebuilt = ready.saved.rebuild(ready.bundle, ready.board, ready.snap, ready.haveSnap, ready.aiSnap);
      check(rebuilt, "saved session rebuild preserves validated hash");
      const fs::path path = app.profiles.savedGamePath(profile, GameMode::Adventure);
      if (!check(path.parent_path() == cfg.userDir && path.parent_path().parent_path() == dir, "keeper path is isolated scratch userdata")) return 2;
      if (!check(saved.save(path), "persist scratch session")) continue;
      SessionKeeper keeper(app);
      app.cfg.noSave = false;
      bool found = keeper.find(GameMode::Adventure);
      app.cfg.noSave = true;
      check(found && keeper.found(), "SessionKeeper accepts persisted authored session");
      check(fs::exists(path), "SessionKeeper does not delete usable session");
      aiSnapshot(saved, ready.bundle, authored, display);
      if (rebuilt) {
        geometry(ready.board, authored, display);
        check(ready.board.hash() == original.board().hash(), "restored board hash matches saved live board");
        check(ready.haveSnap && ready.snap.hash() == initial.hash(), "pre-shot snapshot restored exactly");
        geometry(ready.snap, authored, display);
        contacts(original.board(), ready.board, authored);
        contacts(initial, ready.snap, authored);
        GameScreen resumed(app, ready);
        check(resumed.ok(), "resumed GameScreen is usable");
        SavedSession again;
        if (check(resumed.fillSession(again), "resumed GameScreen re-saves")) {
          check(again.cfg.authoredWidth == authored, "resumed config retains provenance for replay and re-save consumers");
          ReadySession twice;
          twice.bundle = ready.bundle;
          check(again.save(path) && SavedSession::load(path, twice.saved) && twice.saved.rebuild(twice.bundle, twice.board, twice.snap, twice.haveSnap, twice.aiSnap), "re-saved session loads and rebuilds again");
          check(twice.board.hash() == resumed.board().hash(), "second restore preserves board hash");
        }
        fs::path resumedReplay;
        if (check(resumed.saveReplay(true, &resumedReplay, "resumed"), "resumed screen exports actual replay")) {
          ReplayData d;
          if (check(ReplayData::load(resumedReplay, d), "resumed replay loads")) {
            GameScreen viewer(app, d);
            check(viewer.ok(), "resumed replay viewer opens");
            geometry(viewer.board(), authored, display);
            contacts(initial, viewer.board(), authored);
          }
        }
        resumed.restartLevel(false);
        check(resumed.ok(), "resumed screen restarts successfully");
        geometry(resumed.board(), authored, display);
      }
      fs::path replayPath;
      if (check(original.saveReplay(true, &replayPath, "original"), "write actual replay file")) {
        for (int version : {4, 3, 1, 2}) {
          ReplayData replay;
          if (check(ReplayData::load(replayPath, replay), "real replay serializer round-trip")) {
            check(replay.cfg.authoredWidth == 650 && replay.cfg.boardWidth == display && replay.cfg.legacyMissCoin() == (version < 3), "replay preserves display and coin rule");
            check(replayPath.extension() == replayExt(arg - 1), "game-specific replay extension preserved");
            std::ifstream file(replayPath, std::ios::binary);
            file.seekg(4);
            check(file.get() == version, "replay format matches recorded rules");
            GameScreen viewer(app, replay);
            if (check(viewer.ok(), "replay GameScreen loads")) {
              if (version < 3) app.killDialog(D_Tip);
              geometry(viewer.board(), authored, display);
              contacts(initial, viewer.board(), authored);
              for (int i = 0; i < 12; ++i) viewer.update();
              check(viewer.board().hash() == original.board().hash(), "replayed input history matches saved live hash");
            }
            if (version == 4) {
              file.seekg(0);
              std::vector<char> data((std::istreambuf_iterator<char>(file)), {});
              data[4] = 3;
              data.erase(data.begin() + 8, data.begin() + 12);
              replayPath = dir / ("fair" + replayExt(arg - 1));
              std::ofstream legacy(replayPath, std::ios::binary);
              legacy.write(data.data(), static_cast<std::streamsize>(data.size()));
            } else if (version == 3) {
              replay.cfg.simRules = SimRules::Unspecified;
              replayPath = dir / ("legacy" + replayExt(arg - 1));
              check(replay.save(replayPath), "write legacy replay without physics changes");
            } else if (version == 1) {
              replay.cfg.physics.set(PhysicsParams::GravityY, initial.physics.gravityY);
              replayPath = dir / ("physics" + replayExt(arg - 1));
              check(replay.save(replayPath), "write physics-version replay");
            }
          }
        }
      }
      if (found && check(keeper.resume(), "persisted session resumes through SessionKeeper")) {
        auto* resumed = dynamic_cast<GameScreen*>(app.screen());
        if (check(resumed && resumed->ok(), "keeper installs usable resumed GameScreen")) {
          geometry(resumed->board(), authored, display);
          check(resumed->board().hash() == saved.hash, "keeper resume retains persisted hash");
          SavedSession again;
          check(resumed->fillSession(again) && again.cfg.authoredWidth == authored, "keeper resume preserves re-save provenance");
        }
      }
      check(!fs::exists(cfg.userDir / "users.dat") && !fs::exists(app.profiles.detailsPath(profile)), "no profile data written");
      std::cout << "CASE " << testCase << " frames=" << saved.frames << " hash=" << saved.hash << " rebuild=" << rebuilt << " keeper=" << found << '\n';
    }
    for (bool legacy : {true, false}) for (bool restart : {false, true}) {
      if (!legacy && !restart) continue;
      testCase = std::string(arg == 1 ? "Deluxe" : "Nights") + (legacy ? " v2" : " v3") + (restart ? " restart" : " next");
      pickerRules(gfx, vfs, game, root / testCase, legacy, restart);
    }
    testCase = std::string(arg == 1 ? "Deluxe" : "Nights") + " rejected sessions";
    rejectedSessions(gfx, vfs, game, root / ("rejected-" + std::to_string(arg)));
  }
  std::cout << "AUTHORED_SESSION checks=" << checks << " failures=" << failures << " scratch=" << root.generic_string() << '\n';
  SDL_Quit();
  return failures ? 1 : 0;
}
