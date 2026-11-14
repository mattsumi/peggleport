#include <SDL3/SDL.h>
#include <cstdint>
#include <utility>
#include <memory>
#include <vector>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <cctype>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>
#include <thread>
#include <system_error>

#include "log.h"
#include "online/content_hash.h"
#include "online/direct.h"
#include "online/endpoint.h"
#include "online/invite.h"
#include "online/online_ui.h"
#include "online/steam.h"
#include "gamefind.h"

namespace pp::ui::online {

using pp::online::Invite;
using pp::online::extractInvite;
using pp::online::makeInvite;

static std::string g_pendingInvite;
void setPendingInvite(const std::string& url) { g_pendingInvite = url; }

void Prefs::load(const std::filesystem::path& file) {
  std::ifstream f(file);
  bool savedAddress = false;
  std::string l;
  while (std::getline(f, l)) {
    auto eq = l.find('=');
    if (eq == std::string::npos) continue;
    std::string k = l.substr(0, eq), v = l.substr(eq + 1);
    if (!v.empty() && v.back() == '\r') v.pop_back();
    if (k == "lobbyset") savedAddress = v == "1";
    else if (k == "lobby") lobby = v;
    else if (k == "mode") mode = std::clamp(std::atoi(v.c_str()), 0, 2);
    else if (k == "master") master = std::clamp(std::atoi(v.c_str()), 0, 10);
    else if (k == "mastermod") masterMod = v;
    else if (k == "level") level = v;
    else if (k == "discord") discord = v == "1";
  }
  if (!savedAddress) lobby = pp::online::preferredLobby(lobby);
}
void Prefs::save(const std::filesystem::path& file) const {
  std::ofstream f(file);
  f << "lobbyset=1\nlobby=" << lobby << "\nmode=" << mode << "\nmaster=" << master << "\nlevel=" << level << "\n";
  f << "discord=" << (discord ? 1 : 0) << "\n";
  if (!masterMod.empty()) f << "mastermod=" << masterMod << "\n";
}

static void splitHostPort(const std::string& in, std::string* host, int* port) {
  pp::online::LobbyEndpoint endpoint;
  if (!pp::online::parseEndpoint(in, &endpoint)) { host->clear(); *port = 0; return; }
  *host = endpoint.secure ? endpoint.address() : endpoint.host;
  *port = endpoint.port;
}

namespace {

class NetEdit : public EditBox {
 public:
  void draw(Gfx& g) override {
    if (Tex* box = g.img("IMAGE_DLG_TEXTBOX")) g.drawBox(box, 0, 0, w, h);
    else g.fill(0, 0, w, h, 0x000000, 0.6f);
    float asc = g.fontAscent(font);
    const float shift = textShift(g);
    g.pushClip(textPad - 1, 0, w - 2 * textPad + 2, h);
    g.drawString(font, textPad - shift, (h + asc * 0.8f) / 2, value, 0x14204a);
    if (focused && (tick / 40) % 2 == 0) g.fill(textPad - shift + g.textWidth(font, value.substr(0, caret)), 5, 1, h - 10, 0x14204a);
    g.popClip();
  }
};

struct RoomRow {
  std::string code, level, name, hash, levelShown, nameShown;
  int players = 2, joined = 1;
  bool party = false, haveLevel = true;
};

constexpr float kColCode = 52, kColGame = 122, kColSeats = 200, kColLevel = 248, kColRoom = 386, kRowEnd = 532;
constexpr float kDlgX = 130, kDlgY = 100, kDlgW = 540, kDlgH = 340, kDlgPad = 24;
constexpr float kPickL = 250, kPickR = kDlgX + kDlgW - kDlgPad, kPickMid = (kPickL + kPickR) / 2, kPickGap = 8;
constexpr int kArrowHalf = 6;
constexpr int kHoldTicks = 1000, kNoticeTicks = 800;
constexpr float kAddrWidth = 300;

float capsWidth(const Tex* t) { return t ? 2.0f * std::floor(t->lw() / 3.0f) : 0.0f; }

class ArrowButton : public Button {
 public:
  bool left = true;
  void draw(Gfx& g) override {
    Button::draw(g);
    if (!visible || !image) return;
    const float cx = std::floor(w / 2), cy = std::floor(h / 2) - 1;
    auto shape = [&](float ox, float oy, uint32_t rgb, float a) {
      for (int i = -kArrowHalf; i <= kArrowHalf; i++) {
        const float len = (float)(kArrowHalf + 1 - std::abs(i)) * 2.0f;
        const float x0 = left ? cx + (float)(kArrowHalf + 1) - len : cx - (float)(kArrowHalf + 1);
        g.fill(x0 + ox, cy + (float)i + oy, len, 1, rgb, a);
      }
    };
    shape(1, 1, 0x2a0e00, 0.7f * alpha);
    shape(0, 0, over ? 0xffffff : 0xfff2c4, alpha);
  }
};

struct Job {
  std::thread th;
  std::atomic<bool> done{false}, stop{false};
  bool ok = false;
  std::string err;
  std::vector<std::string> lines;
  std::string text;
  ~Job() { stop = true; if (th.joinable()) th.join(); }
};

enum Ids { B_Refresh = 1, B_Join, B_Paste, B_Create, B_Back, B_Copy, B_Ready, B_Start, B_Leave, B_Go, B_Cancel, B_Mode, B_LvlPrev, B_LvlNext, B_MasPrev, B_MasNext, B_Test, B_Register, C_Discord, E_Lobby = 100, E_Code };

#if defined(__APPLE__)
constexpr bool kUrlRegistration = false;
#else
constexpr bool kUrlRegistration = true;
#endif

class OnlineScreen : public Screen {
 public:
  OnlineScreen(App& a, const std::string& invite, bool fromDiscord = false) : Screen(a) {
    prefsFile_ = a.userDir.parent_path() / "online.ini";
    if (!a.cfg.noSave) prefs_.load(prefsFile_);
    levels_ = levelChoices(a);
    for (size_t i = 0; i < levels_.size(); i++)
      if (levels_[i].file == prefs_.level) { levelIdx_ = (int)i; break; }
    mode_ = prefs_.mode;
    master_ = prefs_.master;
    nMasters_ = masterTotal(a.game == Game::Nights);
    master_ = std::clamp(master_, 0, (a.game == Game::Nights ? 11 : 10) - 1);
    if (!prefs_.masterMod.empty()) {
      const auto& mods = masters::all();
      for (size_t k = 0; k < mods.size(); k++)
        if (mods[k].key() == prefs_.masterMod) master_ = (a.game == Game::Nights ? 11 : 10) + (int)k;
    }

    auto edit = [&](int id, float x, float y, float w, size_t maxChars, float pad, float room) {
      auto e = std::make_unique<NetEdit>();
      e->id = id;
      e->gfx = &a.gfx;
      e->font = e->widthFont = "PLAIN";
      e->maxChars = maxChars;
      e->textPad = pad;
      e->clipText = true;
      e->maxWidth = room;
      e->x = x; e->y = y - 4; e->w = w; e->h = 36;
      return add(std::move(e));
    };
    const float joinW = std::max(94.0f, capsWidth(a.gfx.img("IMAGE_DLG_BUTTON3")));
    lobbyEdit_ = edit(E_Lobby, 150, 82, 390, 1024, 14, 360);
    lobbyEdit_->value = prefs_.lobby.empty() ? pp::online::publicLobby : prefs_.lobby;
    lobbyEdit_->caret = (int)lobbyEdit_->value.size();
    lobbyEdit_->onEnter = [this] { if (!job_) { held_ = false; startList(); } };
    codeEdit_ = edit(E_Code, 556, 176, 204 - joinW, 5, 10, 204 - joinW - 20);
    codeEdit_->onEnter = [this] { onButton(B_Join); };
    focusW = lobbyEdit_;
    lobbyEdit_->focused = true;

    auto btn = [&](int id, const char* label, float x, float y, float w) {
      Button* b = patchButton(id, label, "IMAGE_DLG_BUTTON3", x, y);
      b->w = std::max(w, capsWidth(b->image));
      return b;
    };
    auto arrow = [&](int id, bool left, float y) {
      auto b = std::make_unique<ArrowButton>();
      b->id = id;
      b->kind = Button::Kind::Patch;
      b->image = a.gfx.img("IMAGE_DLG_BROWNBUTTON1");
      if (!b->image) b->image = a.gfx.img("IMAGE_DLG_BUTTON3");
      b->tickRef = &updateCount;
      b->left = left;
      b->w = std::max(40.0f, capsWidth(b->image));
      b->h = b->image ? b->image->lh() / 3 : 34.0f;
      b->x = left ? kPickL : kPickR - b->w;
      b->y = y;
      return add(std::move(b));
    };
    bRefresh_ = btn(B_Refresh, "Refresh", 556, 80, 204);
    bJoin_ = btn(B_Join, "Join", 760 - joinW, 172, joinW);
    bPaste_ = btn(B_Paste, "Paste Invite", 556, 224, 204);
    bCreate_ = btn(B_Create, "Create Room", 556, 276, 204);
    bTest_ = btn(B_Test, "Test connection", 556, bCreate_->y + bCreate_->h + 14, 204);
    bRegister_ = btn(B_Register, "Register invite links", 556, bTest_->y + bTest_->h + 8, 204);
    for (Button* b : {bTest_, bRegister_})
      if (a.gfx.textWidth(b->font, b->label) > b->w - 20) b->font = "PLAIN";
    bBack_ = btn(B_Back, "Main Menu", 40, 520, 180);
    bCopy_ = btn(B_Copy, "Copy Invite", 40, 520, 180);
    bReady_ = btn(B_Ready, "Ready", 300, 520, 200);
    bStart_ = btn(B_Start, "Start Match", 300, 520, 200);
    bLeave_ = btn(B_Leave, "Leave Room", 580, 520, 180);
    bMode_ = btn(B_Mode, "", kPickL, 160, kPickR - kPickL);
    bLvlPrev_ = arrow(B_LvlPrev, true, 214);
    bLvlNext_ = arrow(B_LvlNext, false, 214);
    bMasPrev_ = arrow(B_MasPrev, true, 268);
    bMasNext_ = arrow(B_MasNext, false, 268);
    pickW_ = bLvlNext_->x - (bLvlPrev_->x + bLvlPrev_->w) - 2 * kPickGap;
    bGo_ = btn(B_Go, "Open Room", kDlgX + kDlgW / 2 - 155, 390, 150);
    bCancel_ = btn(B_Cancel, "Cancel", kDlgX + kDlgW / 2 + 5, 390, 150);
    auto discord = std::make_unique<Checkbox>();
    discord->id = C_Discord;
    discord->image = a.gfx.img("IMAGE_DLG_BUTTON1");
    discord->label = "Discord activity";
    discord->font = "PLAIN";
    discord->checked = a.discordEnabled();
    discord->x = 40; discord->y = 20; discord->w = 230;
    discord->h = discord->image ? discord->image->lh() / 4 : 34;
    discordCheck_ = add(std::move(discord));
    layout();
    if (!invite.empty()) {
      if (fromDiscord) applyDiscordInvite(invite);
      else applyInvite(invite, false);
    }
    else startList();
  }

  ~OnlineScreen() override {
    if (job_) {
      job_->stop = true;
      auto pending = std::make_shared<std::pair<std::unique_ptr<Job>, std::unique_ptr<Session>>>(std::move(job_), std::move(sess_));
      try {
        std::thread([pending] {
          pending->first.reset();
          if (pending->second) pending->second->leave();
        }).detach();
      } catch (const std::system_error&) {
        pending->first.reset();
        pp::log::warn("net", "could not finish closing the lobby connection in the background");
      }
    }
    savePrefs();
    if (sess_) sess_->leave();
  }

  const char* name() const override { return "online"; }

  void savePrefs() {
    prefs_.discord = app.discordEnabled();
    if (pp::online::parseEndpoint(lobbyEdit_->value, nullptr)) prefs_.lobby = lobbyEdit_->value;
    prefs_.mode = mode_;
    const int nb = app.game == Game::Nights ? 11 : 10;
    prefs_.master = std::min(master_, nb - 1);
    prefs_.masterMod = master_ >= nb ? masterKeyAt(app.game == Game::Nights, master_) : std::string();
    if (!levels_.empty()) prefs_.level = levels_[levelIdx_].file;
    if (!app.cfg.noSave) {
      std::error_code ec;
      std::filesystem::create_directories(prefsFile_.parent_path(), ec);
      prefs_.save(prefsFile_);
    }
  }

  void checkboxChanged(int id, bool checked) override {
    if (id != C_Discord) return;
    app.setDiscordEnabled(checked);
    if (!checked) discardDiscordInvite();
    if (app.cfg.noSave) return;
    auto saveFlag = [&](const std::filesystem::path& file) {
      Prefs prefs;
      prefs.load(file);
      prefs.discord = checked;
      std::error_code ec;
      std::filesystem::create_directories(file.parent_path(), ec);
      prefs.save(file);
    };
    saveFlag(prefsFile_);
    if (app.bothGames) {
      const auto dir = app.cfg.otherUserDir.empty() ? defaultUserDir(otherGame(app.game)) : app.cfg.otherUserDir;
      saveFlag(dir.parent_path() / "online.ini");
    }
  }

  void discardDiscordInvite() {
    if (!discordInviteUntil_) return;
    discordInviteUntil_ = 0;
    if (codeEdit_->value == discordInviteCode_) {
      setCode("");
      release();
    }
    discordInviteCode_.clear();
  }

  void discordPresence(pp::online::Discord::Presence& presence) const override {
    presence.state = room_ ? "Waiting in an online room" : create_ ? "Creating an online room" : "Browsing online rooms";
    if (!room_ || !sess_ || job_ || !isHost_ || !sess_->isHost() || !sess_->welcomed() || sess_->failed() || sess_->started()) return;
    const int capacity = sess_->info().players;
    const auto& roster = sess_->roster();
    int seated = 0;
    for (const auto& seat : roster) seated += seat.connected ? 1 : 0;
    if (capacity < 2 || capacity > 4 || seated < 1 || seated >= capacity) return;
    const std::string invite = makeInvite(lobbyHost_, lobbyPort_, roomCode_);
    if (invite.size() > 128) {
      if (!discordLongInvite_) pp::log::warn("net", "This lobby address is too long for a Discord invite. Copy Invite is still available.");
      discordLongInvite_ = true;
      return;
    }
    if (!pp::online::parseInvite(invite, nullptr)) return;
    presence.partyId = invite;
    presence.joinSecret = invite;
    presence.partySize = seated;
    presence.partyMax = capacity;
    presence.joinable = true;
  }

  void say(const std::string& s, int kind = 0) { status_ = s; statusKind_ = kind; held_ = false; }

  void hold(const std::string& s, int kind, bool untilAction = false) {
    status_ = s;
    statusKind_ = kind;
    held_ = !s.empty();
    heldTimed_ = !untilAction;
    heldAt_ = app.updateCount();
  }

  void release() {
    if (listKnown_) say(listLine_, listKind_);
    else if (held_ || statusKind_ != 0) say("");
  }

  void setCode(const std::string& code) {
    codeEdit_->value = code;
    codeEdit_->caret = (int)code.size();
    codeSeen_ = code;
  }

  std::string addrShown(const std::string& host, int port) {
    const std::string a = pp::online::endpointAddress(host, port);
    return fitText(app.gfx, "PLAIN", cleanText(a, 300), kAddrWidth);
  }

  std::string codeShown(const std::string& code) const { return drawableText(cleanText(code, 8)); }

  std::string trouble(const std::string& raw, Where where) {
    bool known = true;
    std::string text = troubleText(raw, where, isHost_, jobAddr_, codeShown(roomCode_), &known);
    if (!known) pp::log::warn("net", "no plain message for this answer: " + cleanText(raw, 240));
    return text;
  }

  std::string playerName() const { return app.curProfile && !app.curProfile->name.empty() ? app.curProfile->name : "Player"; }

  void applyDiscordInvite(const std::string& text) {
    applyInvite(text, false);
    nonDiscordInvite_ = false;
    discordInviteCode_ = codeEdit_->value;
    discordInviteUntil_ = SDL_GetTicks() + 5000;
  }

  void applyInvite(const std::string& text, bool pasted) {
    discordInviteUntil_ = 0;
    discordInviteCode_.clear();
    Invite iv;
    if (!extractInvite(text, &iv)) {
      hold(pasted ? "There is no invite link on the clipboard. Copy the whole link (it starts with peggleport://) and press Paste Invite again."
                  : "That invite link is not complete. Ask your friend to send it again.", 1);
      return;
    }
    nonDiscordInvite_ = true;
    lobbyEdit_->value = pp::online::endpointAddress(iv.host, iv.port);
    lobbyEdit_->caret = (int)lobbyEdit_->value.size();
    setCode(iv.code);
    hold("Invite for room " + iv.code + " on " + addrShown(iv.host, iv.port) + ". Press Join to connect.", 2, true);
    startList();
  }

  void startList() {
    if (job_ || room_ || create_) return;
    std::string host;
    int port;
    splitHostPort(lobbyEdit_->value, &host, &port);
    if (host.empty()) { say("Enter a valid lobby address: wss://host/lobby or host:port.", 1); return; }
    job_ = std::make_unique<Job>();
    jobKind_ = List;
    jobAddr_ = addrShown(host, port);
    Job* j = job_.get();
    j->th = std::thread([j, host, port] {
      j->ok = Session::listRooms(host, port, &j->lines, &j->err, &j->stop);
      j->done = true;
    });
    if (!held_) say("Contacting " + jobAddr_ + " ...");
  }

  void startProbe() {
    if (job_ || room_ || create_) return;
    std::string host;
    int port;
    splitHostPort(lobbyEdit_->value, &host, &port);
    if (host.empty()) { say("Enter a valid lobby address: wss://host/lobby or host:port.", 1); return; }
    job_ = std::make_unique<Job>();
    jobKind_ = Probe;
    jobAddr_ = addrShown(host, port);
    Job* j = job_.get();
    j->th = std::thread([j, host, port] {
      if (host.rfind("wss://", 0) == 0) {
        j->ok = Session::listRooms(host, port, &j->lines, &j->err, &j->stop);
        j->text = j->ok ? "Secure lobby connection is working. Matches use the relay." : troubleText(j->err, Where::List, false, host, "");
        j->done = true;
        return;
      }
      bool reachable = false;
      pp::online::LobbyProbe seen;
      pp::online::probeLobbyUdp(host, port, &reachable, &seen);
      j->text = probeText(seen, host);
      j->ok = reachable;
      j->done = true;
    });
    say("Testing the connection to " + jobAddr_ + " ...");
  }

  void startRegister() {
    if (job_ || room_ || create_) return;
    job_ = std::make_unique<Job>();
    jobKind_ = Register;
    Job* j = job_.get();
    j->th = std::thread([j] {
      std::string report;
      j->ok = pp::online::registerUrlHandler(pp::online::selfExePath(nullptr), &report);
      pp::log::write(j->ok ? pp::log::Level::Info : pp::log::Level::Warn, "net", "registering invite links: " + report);
      if (!j->ok) j->text = "Invite links could not be registered. Press F10 for the details.";
      else if (report.find("xdg-mime was not available") != std::string::npos) j->text = "The entry for invite links was written, but xdg-mime is missing, so links may not open the game yet. F10 shows the command to run.";
      else j->text = "Invite links are registered. Opening one now starts PegglePort.";
      j->done = true;
    });
    say("Registering invite links ...");
  }

  void finishReport(const Job& j) { hold(j.text, j.ok ? 2 : 1, true); }

  void finishList() {
    rooms_.clear();
    listKnown_ = true;
    lastRefresh_ = app.updateCount();
    const bool show = !held_ || (heldTimed_ && app.updateCount() - heldAt_ >= kHoldTicks);
    if (!job_->ok) {
      listLine_ = trouble(job_->err, Where::List);
      listKind_ = 1;
      if (show) say(listLine_, listKind_);
      return;
    }
    for (auto& l : job_->lines) {
      std::istringstream is(l);
      std::string tag;
      RoomRow r;
      int party = 0;
      is >> tag >> r.code >> r.players >> r.joined >> party >> r.level >> r.hash;
      std::getline(is >> std::ws, r.name);
      if (tag != "ROOM" || !plainCode(r.code)) continue;
      r.party = party != 0;
      r.players = std::clamp(r.players, 2, 4);
      r.joined = std::clamp(r.joined, 0, r.players);
      r.haveLevel = levelInstalled(app, r.level);
      for (auto& c : r.name) if (c == '_') c = ' ';
      r.levelShown = fitText(app.gfx, "PLAIN", r.haveLevel ? levelTitle(app, r.level) : cleanText(r.level, 64), kColRoom - kColLevel - 8);
      r.nameShown = r.haveLevel ? fitText(app.gfx, "PLAIN", cleanText(r.name, 40), kRowEnd - kColRoom) : std::string("level not installed");
      rooms_.push_back(r);
    }
    listLine_ = rooms_.empty() ? "Connected. No open rooms - create one and send your friends the invite." : std::to_string(rooms_.size()) + " open room(s). Click one to select it, double-click to join.";
    listKind_ = 0;
    if (show) say(listLine_, listKind_);
  }

  static bool plainCode(const std::string& code) {
    if (code.size() != 5) return false;
    for (unsigned char c : code)
      if (c <= 32 || c >= 127) return false;
    return true;
  }

  bool loadLevel(const std::string& name, LevelBundle* lb, std::string* err) { return loadLevelBundle(app.vfs, name, *lb, err); }

  const std::string& roomLevelTitle(const std::string& file) {
    if (!roomTitleSet_ || file != roomTitleFor_) {
      roomTitleFor_ = file;
      roomTitle_ = file.empty() ? std::string("?") : levelTitle(app, file);
      roomTitleSet_ = true;
    }
    return roomTitle_;
  }

  void startHost() {
    app.clearDiscord();
    discardDiscordInvite();
    nonDiscordInvite_ = false;
    discordLongInvite_ = false;
    if (levels_.empty()) { hold("No levels were found in the game files.", 1); return; }
    std::string host, err;
    int port;
    splitHostPort(lobbyEdit_->value, &host, &port);
    if (host.empty()) { say("Enter a valid lobby address: wss://host/lobby or host:port.", 1); return; }
    LevelBundle lb;
    if (!loadLevel(levels_[levelIdx_].file, &lb, &err)) {
      pp::log::error("net", "cannot load the level " + levelLogName(app, levels_[levelIdx_].file) + " for a room: " + err);
      hold("This level could not be loaded. Pick another one.", 1);
      return;
    }
    int players = mode_ == 0 ? 2 : mode_ + 2;
    MatchInfo mi;
    mi.players = players;
    mi.party = mode_ != 0;
    mi.seed = app.rand();
    mi.boardWidth = lb.cfg.boardWidth;
    for (int i = 0; i < 4; i++) mi.master[i] = lb.cfg.master[i];
    for (int i = 0; i < players; i++) mi.master[i] = Board::masterByName(masterKeyAt(app.game == Game::Nights, (master_ + i) % nMasters_));
    mi.level = lb.name;
    mi.contentHash = pp::online::matchContentHash(lb);
    lb_ = lb;
    hostInfo_ = mi;
    isHost_ = true;
    sess_ = std::make_unique<Session>();
    sess_->setName(playerName());
    sess_->autoStart = false;
    lobbyHost_ = host;
    lobbyPort_ = port;
    job_ = std::make_unique<Job>();
    jobKind_ = Host;
    jobAddr_ = addrShown(host, port);
    Job* j = job_.get();
    Session* sp = sess_.get();
    std::string roomName = playerName() + "'s " + (mi.party ? "party" : "duel");
    j->th = std::thread([j, sp, host, port, roomName, mi] {
      j->ok = sp->hostViaLobby(host, port, roomName, mi, &j->err, &j->stop);
      j->done = true;
    });
    savePrefs();
    say("Opening a room on " + jobAddr_ + " ...");
    layout();
  }

  void startJoin(const std::string& code) {
    app.clearDiscord();
    discordInviteUntil_ = 0;
    discordInviteCode_.clear();
    discordLongInvite_ = false;
    if (code.size() != 5) { say("A room code has 5 letters or digits. Type it, or click a room in the list.", 1); return; }
    std::string host;
    int port;
    splitHostPort(lobbyEdit_->value, &host, &port);
    if (host.empty()) { say("Enter a valid lobby address: wss://host/lobby or host:port.", 1); return; }
    nonDiscordInvite_ = false;
    isHost_ = false;
    sess_ = std::make_unique<Session>();
    sess_->setName(playerName());
    lobbyHost_ = host;
    lobbyPort_ = port;
    roomCode_ = code;
    job_ = std::make_unique<Job>();
    jobKind_ = Join;
    jobAddr_ = addrShown(host, port);
    Job* j = job_.get();
    Session* sp = sess_.get();
    j->th = std::thread([j, sp, host, port, code] {
      j->ok = sp->joinViaLobby(host, port, code, &j->err, &j->stop);
      j->done = true;
    });
    savePrefs();
    say("Joining room " + codeShown(code) + " ...");
  }

  void enterRoom() {
    app.clearDiscord();
    say("");
    seated_ = 0;
    room_ = true;
    if (isHost_) pp::online::steam::setPresence("In a PegglePort room", makeInvite(lobbyHost_, lobbyPort_, sess_->roomCode()));
    guestChecked_ = readySent_ = false;
    if (isHost_) roomCode_ = sess_->roomCode();
    layout();
  }

  void leaveRoom(const std::string& msg, int kind = 0) {
    app.clearDiscord();
    discardDiscordInvite();
    pp::online::steam::clearPresence();
    if (sess_) sess_->leave();
    sess_.reset();
    room_ = false;
    if (kind == 1) hold(msg, kind);
    else say(msg, kind);
    layout();
    startList();
  }

  void layout() {
    bool browse = !room_ && !create_;
    lobbyEdit_->visible = codeEdit_->visible = browse;
    for (Button* b : {bRefresh_, bJoin_, bPaste_, bCreate_, bTest_}) b->visible = browse;
    bRegister_->visible = browse && kUrlRegistration;
    bBack_->visible = browse;
    bCopy_->visible = bReady_->visible = bStart_->visible = bLeave_->visible = room_;
    for (Button* b : {bMode_, bLvlPrev_, bLvlNext_, bMasPrev_, bMasNext_, bGo_, bCancel_}) b->visible = create_;
    if (room_) {
      bCopy_->x = 40; bLeave_->x = 580;
      bReady_->visible = !isHost_;
      bStart_->visible = isHost_;
    }
    if (create_) bMode_->label = mode_ == 0 ? "Duel (2 players)" : mode_ == 1 ? "Peg Party (3 players)" : "Peg Party (4 players)";
    if (browse) focusW = lobbyEdit_, lobbyEdit_->focused = true, codeEdit_->focused = false;
  }

  void onButton(int id) override {
    if (job_) return;
    switch (id) {
      case B_Refresh: held_ = false; startList(); break;
      case B_Test: startProbe(); break;
      case B_Register: startRegister(); break;
      case B_Join: startJoin(codeEdit_->value); break;
      case B_Paste: {
        char* t = SDL_GetClipboardText();
        std::string text = t ? t : "";
        SDL_free(t);
        applyInvite(text, true);
        break;
      }
      case B_Create: create_ = true; release(); layout(); break;
      case B_Back: app.showMainMenu(); break;
      case B_Cancel: create_ = false; layout(); break;
      case B_Go: create_ = false; layout(); startHost(); break;
      case B_Mode: mode_ = (mode_ + 1) % 3; layout(); break;
      case B_LvlPrev: if (!levels_.empty()) levelIdx_ = (levelIdx_ + (int)levels_.size() - 1) % (int)levels_.size(); break;
      case B_LvlNext: if (!levels_.empty()) levelIdx_ = (levelIdx_ + 1) % (int)levels_.size(); break;
      case B_MasPrev: master_ = (master_ + nMasters_ - 1) % nMasters_; break;
      case B_MasNext: master_ = (master_ + 1) % nMasters_; break;
      case B_Copy: {
        std::string url = makeInvite(lobbyHost_, lobbyPort_, roomCode_);
        SDL_SetClipboardText(url.c_str());
        copiedAt_ = app.updateCount();
        break;
      }
      case B_Ready:
        if (sess_ && guestChecked_ && !readySent_) {
          sess_->sendReady(pp::online::matchContentHash(lb_));
          readySent_ = true;
        }
        break;
      case B_Start:
        app.clearDiscord();
        if (sess_) sess_->hostStart();
        break;
      case B_Leave: leaveRoom("You left the room."); break;
    }
  }

  void keyDown(int key, int mod) override {
    if (key == SDLK_ESCAPE) {
      if (room_) onButton(B_Leave);
      else if (create_) onButton(B_Cancel);
      else app.showMainMenu();
    } else if (key == SDLK_TAB && !room_ && !create_) {
      bool toCode = focusW == lobbyEdit_;
      lobbyEdit_->focused = !toCode;
      codeEdit_->focused = toCode;
      focusW = toCode ? static_cast<Widget*>(codeEdit_) : lobbyEdit_;
    } else if (key == SDLK_V && (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) && !room_ && !create_) {
      char* t = SDL_GetClipboardText();
      std::string text = t ? t : "";
      SDL_free(t);
      Invite iv;
      if (extractInvite(text, &iv)) applyInvite(text, true);
      else if (focusW) focusW->text(text.substr(0, 1024));
    }
  }

  void update() override {
    Panel::update();
    discordCheck_->checked = app.discordEnabled();
    if (discordInviteUntil_ && (!app.discordEnabled() || SDL_GetTicks() >= discordInviteUntil_)) discardDiscordInvite();
    if (!nonDiscordInvite_ && !room_ && !create_ && !job_ && !app.anyDialog() && !app.moving()) {
      std::string invite = app.takeDiscordInvite();
      if (!invite.empty()) {
        applyDiscordInvite(invite);
      }
    }
    pp::online::steam::pump();
    std::string joinLink;
    if (!room_ && !create_ && !job_ && pp::online::steam::takeJoinRequest(&joinLink)) applyInvite(joinLink, false);
    std::string up;
    for (char c : codeEdit_->value) up.push_back((char)std::toupper((unsigned char)c));
    if (up != codeEdit_->value) codeEdit_->value = up;
    if (codeEdit_->value != codeSeen_) {
      codeSeen_ = codeEdit_->value;
      if (!job_ && !room_ && !create_) release();
    }
    if (job_ && job_->done) {
      std::unique_ptr<Job> j = std::move(job_);
      if (j->th.joinable()) j->th.join();
      if (jobKind_ == List) { job_ = std::move(j); finishList(); job_.reset(); }
      else if (jobKind_ == Probe || jobKind_ == Register) finishReport(*j);
      else if (!j->ok) {
        sess_.reset();
        hold(trouble(j->err, jobKind_ == Host ? Where::Open : Where::Join), 1);
        startList();
      } else enterRoom();
    }
    if (!job_ && !room_ && !create_ && !sess_ && app.updateCount() - lastRefresh_ > 600 && lastRefresh_ >= 0) startList();
    if (sess_ && room_) tickRoom();
    if (bBack_->visible) bBack_->enabled = !job_;
  }

  void tickRoom() {
    sess_->poll();
    int seated = 0;
    for (const auto& seat : sess_->roster()) seated += seat.connected ? 1 : 0;
    if (seated != seated_) app.clearDiscord();
    if (!status_.empty() && (seated > seated_ || app.updateCount() - noticeAt_ >= kNoticeTicks)) say("");
    seated_ = seated;
    if (!sess_->notice.empty()) {
      say(trouble(sess_->notice, Where::Room));
      noticeAt_ = app.updateCount();
      sess_->notice.clear();
    }
    if (sess_->failed()) { leaveRoom(trouble(sess_->error, Where::Room), 1); return; }
    if (!isHost_ && sess_->welcomed() && !guestChecked_) {
      std::string err;
      const MatchInfo& mi = sess_->info();
      LevelBundle lb;
      pp::log::info("net", "the host chose the level " + levelLogName(app, mi.level));
      if (!mi.missingMaster.empty()) {
        leaveRoom("The host picked the Master \"" + drawableText(cleanText(mi.missingMaster, 64)) + "\" from a mod that you do not have, so you cannot join this room.", 1);
        return;
      }
      if (!loadLevel(mi.level, &lb, &err)) {
        pp::log::error("net", "cannot load the host's level " + levelLogName(app, mi.level) + ": " + cleanText(err, 160));
        leaveRoom("You do not have the level \"" + drawableText(cleanText(mi.level, 64)) + "\" that the host chose. It may come from a mod or from the other Peggle game.", 1);
        return;
      }
      if (pp::online::matchContentHash(lb) != mi.contentHash) {
        leaveRoom("Your copy of \"" + drawableText(levelTitle(app, mi.level)) + "\" (or your mods) differs from the host's, so the game would not stay in sync.", 1);
        return;
      }
      lb_ = lb;
      guestChecked_ = true;
    }
    if (sess_->started()) {
      app.clearDiscord();
      MatchLaunch L;
      L.info = isHost_ ? hostInfo_ : sess_->info();
      L.lb = lb_;
      L.lobbyHost = lobbyHost_;
      L.lobbyPort = lobbyPort_;
      L.session = std::move(sess_);
      room_ = false;
      startMatchScreen(app, std::move(L));
      return;
    }
    bStart_->enabled = isHost_ && sess_->allReady();
    bReady_->enabled = guestChecked_ && !readySent_;
    bStart_->alpha = bStart_->enabled ? 1.0f : 0.5f;
    bReady_->alpha = bReady_->enabled ? 1.0f : 0.5f;
    bReady_->label = readySent_ ? "Ready - waiting" : "Ready";
    bCopy_->label = app.updateCount() - copiedAt_ < 150 ? "Copied!" : "Copy Invite";
  }

  bool input(const Input& in) override {
    if (!room_ && !create_ && (focusW == lobbyEdit_ || focusW == codeEdit_) &&
        ((in.type == Input::Text && !in.text.empty()) ||
         (in.type == Input::KeyDown && (in.key == SDLK_BACKSPACE || in.key == SDLK_DELETE ||
          (in.key == SDLK_V && (in.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))))))) {
      nonDiscordInvite_ = true;
      discordInviteUntil_ = 0;
      discordInviteCode_.clear();
    }
    bool used = Panel::input(in);
    if (!used && in.type == Input::MouseDown && in.button == 1 && !room_ && !create_ && !job_) {
      int i = rowAt(in.x, in.y);
      if (i >= 0) {
        nonDiscordInvite_ = true;
        discordInviteUntil_ = 0;
        discordInviteCode_.clear();
        setCode(rooms_[i].code);
        release();
        if (in.clicks >= 2) onButton(B_Join);
        used = true;
      }
    }
    return used;
  }

  int rowAt(float x, float y) const {
    if (x < 40 || x > 530 || y < 166) return -1;
    int i = (int)((y - 166) / 26);
    return i >= 0 && i < (int)rooms_.size() && i < 10 ? i : -1;
  }

  void panel(Gfx& g, float x, float y, float w, float h, const std::string& title) {
    g.fill(x, y, w, h, 0x000000, 0.5f);
    g.frame(x, y, w, h, 0xffd605, 0.8f);
    if (!title.empty()) g.drawString("OVERLOAD16OUTLINE", x + w / 2, y + 20, title, 0xffd605, 1, Align::Center);
  }

  void drawLayer(Gfx& g) override {
    g.fill(0, 0, 800, 600, 0x0a1230);
    g.fill(0, 0, 800, 70, 0x16224d, 1);
    g.fill(0, 70, 800, 2, 0xffd605, 0.7f);
    g.drawString("OVERLOAD24GRAD1", 400, 46, "Play Online", 0xffffff, 1, Align::Center);
    if (room_) { drawRoom(g); }
    else {
      g.drawString("PLAIN", 40, 101, "Lobby server", 0xffffff, 1);
      panel(g, 40, 130, 500, 330, "Open Rooms");
      g.drawString("PLAIN", kColCode, 160, "Code", 0xaaaaff, 1);
      g.drawString("PLAIN", kColGame, 160, "Game", 0xaaaaff, 1);
      g.drawString("PLAIN", kColSeats, 160, "Seats", 0xaaaaff, 1);
      g.drawString("PLAIN", kColLevel, 160, "Level", 0xaaaaff, 1);
      g.drawString("PLAIN", kColRoom, 160, "Room", 0xaaaaff, 1);
      for (int i = 0; i < (int)rooms_.size() && i < 10; i++) {
        const RoomRow& r = rooms_[i];
        float y = (float)(166 + i * 26);
        bool sel = codeEdit_->value == r.code;
        if (sel) g.fill(44, y, 492, 24, 0xffd605, 0.25f);
        uint32_t col = r.haveLevel && r.joined < r.players ? 0xffffff : 0x888888;
        char seats[16];
        std::snprintf(seats, sizeof seats, "%d/%d", r.joined, r.players);
        g.drawString("PLAIN", kColCode, y + 17, r.code, col);
        g.drawString("PLAIN", kColGame, y + 17, r.party ? "Peg Party" : "Duel", col);
        g.drawString("PLAIN", kColSeats, y + 17, seats, col);
        g.drawString("PLAIN", kColLevel, y + 17, r.levelShown, col);
        g.drawString("PLAIN", kColRoom, y + 17, r.nameShown, col);
      }
      g.drawString("PLAIN", 556, 166, "Join with a room code", 0xffffff, 1);
      if (job_) g.drawString("PLAIN", 556, 450, "working...", 0xffe070);
      drawStatus(g, 40, 484);
    }
    if (create_) drawCreate(g);
  }

  void drawStatus(Gfx& g, float x, float y) {
    uint32_t col = statusKind_ == 1 ? 0xff8080 : statusKind_ == 2 ? 0x90ff90 : 0xffe070;
    bool report = (held_ || statusKind_ != 0) && !room_ && !create_;
    float bx = report ? 236 : x, bw = report ? 524.0f : 720.0f, yy = report ? 470 : y;
    for (size_t p = 0; p <= status_.size();) {
      size_t e = status_.find('\n', p);
      std::string line = status_.substr(p, e == std::string::npos ? std::string::npos : e - p);
      p = e == std::string::npos ? status_.size() + 1 : e + 1;
      if (!line.empty()) yy += g.drawWrapped("PLAIN", {bx, yy, bw, 40}, line, col);
    }
  }

  void drawCreate(Gfx& g) {
    const float labelX = kDlgX + kDlgPad;
    g.fill(0, 72, 800, 528, 0x000000, 0.6f);
    g.fill(kDlgX, kDlgY, kDlgW, kDlgH, 0x0a1230, 1);
    panel(g, kDlgX, kDlgY, kDlgW, kDlgH, "Create a Room");
    g.drawString("PLAIN", labelX, 182, "Game", 0xffffff, 1);
    g.drawString("PLAIN", labelX, 236, "Level", 0xffffff, 1);
    g.drawString("PLAIN", labelX, 290, "Master", 0xffffff, 1);
    if (levels_.empty()) g.drawString("PLAIN", kPickMid, 237, "-", 0xffff88, 1, Align::Center);
    else {
      const LevelChoice& lc = levels_[levelIdx_];
      g.drawString("PLAIN", kPickMid, 237, fitText(g, "PLAIN", lc.title, pickW_), 0xffff88, 1, Align::Center);
      g.drawString("PLAIN", kPickMid, 257, fitText(g, "PLAIN", lc.group, pickW_), 0xaaaaaa, 1, Align::Center);
    }
    g.drawString("PLAIN", kPickMid, 291, fitText(g, "PLAIN", masterNameAt(app.game == Game::Nights, master_), pickW_), 0xffff88, 1, Align::Center);
    if (const auto& mods = masters::all(); !mods.empty()) {
      const int nb = app.game == Game::Nights ? 11 : 10;
      std::string line = std::to_string(master_ + 1) + " of " + std::to_string(nMasters_);
      uint32_t col = 0xaaaaaa;
      if (master_ >= nb && master_ - nb < (int)mods.size()) {
        const ModMaster& m = mods[(size_t)(master_ - nb)];
        line = m.powerName + "  -  mod " + m.modId;
        col = 0x88ff88;
      }
      g.drawString("PLAIN", kPickMid, 311, fitText(g, "PLAIN", line, kPickR - kPickL), col, 1, Align::Center);
    }
    g.drawWrapped("PLAIN", {labelX, 330, kDlgW - 2 * kDlgPad, 40}, "Friends join with the room code or the invite link. You can start once everybody is ready.", 0xcccccc);
  }

  void drawRoom(Gfx& g) {
    panel(g, 40, 100, 720, 400, isHost_ ? "Your Room" : "Room " + roomCode_);
    const MatchInfo& mi = isHost_ ? hostInfo_ : sess_->info();
    g.drawString("OVERLOAD24GRAD1", 400, 160, roomCode_, 0xffffff, 1, Align::Center);
    std::string inv = makeInvite(lobbyHost_, lobbyPort_, roomCode_);
    g.drawWrapped("PLAIN", {60, 176, 680, 30}, inv, 0x9fb8ff, 1, Align::Center);
    std::string what = std::string(mi.party ? "Peg Party" : "Duel") + " - " + std::to_string(mi.players) + " players - level " + roomLevelTitle(mi.level);
    g.drawString("PLAIN", 400, 222, fitText(g, "PLAIN", what, 680), 0xffe070, 1, Align::Center);
    const auto& roster = sess_->roster();
    for (int i = 0; i < mi.players; i++) {
      float y = (float)(252 + i * 34);
      g.fill(120, y, 560, 28, 0xffffff, 0.08f);
      bool known = i < (int)roster.size() && roster[i].connected;
      std::string nm = known ? roster[i].name : "(open seat)";
      if (i == 0 && !known && isHost_) nm = playerName();
      g.drawString("PLAIN", 132, y + 19, "Seat " + std::to_string(i + 1), 0xaaaaff);
      g.drawString("PLAIN", 220, y + 19, nm, known || (i == 0 && isHost_) ? 0xffffff : 0x777777);
      if (!masters::all().empty() && i < 4) g.drawString("PLAIN", 440, y + 19, masterNameForPower(mi.master[mi.party ? 0 : i]).substr(0, 18), 0xc0d0ff);
      std::string st = i == 0 ? "host" : !known ? "waiting" : roster[i].ready ? "ready" : "not ready";
      g.drawString("PLAIN", 668, y + 19, st, st == "ready" || st == "host" ? 0x90ff90 : 0xffe070, 1, Align::Right);
    }
    std::string link = linkText(sess_->linkNote(), false);
    if (!link.empty()) g.drawWrapped("PLAIN", {60, (float)(252 + mi.players * 34 + 6), 680, 30}, link, 0xaaaaaa, 1, Align::Center);
    std::string hint = isHost_ ? (sess_->allReady() ? "Everybody is ready - press Start Match." : "Waiting for the other players to join and press Ready...")
                               : (readySent_ ? "Waiting for the host to start..." : guestChecked_ ? "Press Ready when you are." : "Checking the host's game files...");
    g.drawString("PLAIN", 400, 480, hint, 0xffffff, 1, Align::Center);
    drawStatus(g, 40, 556);
  }

 private:
  Checkbox* discordCheck_ = nullptr;
  bool nonDiscordInvite_ = false;
  uint64_t discordInviteUntil_ = 0;
  std::string discordInviteCode_;
  mutable bool discordLongInvite_ = false;
  Prefs prefs_;
  std::filesystem::path prefsFile_;
  std::vector<LevelChoice> levels_;
  std::string roomTitleFor_, roomTitle_;
  bool roomTitleSet_ = false;
  int levelIdx_ = 0, mode_ = 0, master_ = 0, nMasters_ = 10;
  float pickW_ = 0;
  bool create_ = false, room_ = false, isHost_ = false, guestChecked_ = false, readySent_ = false;
  int lastRefresh_ = 0, copiedAt_ = -1000;
  std::vector<RoomRow> rooms_;
  std::string status_, listLine_, jobAddr_, codeSeen_;
  int statusKind_ = 0, listKind_ = 0, heldAt_ = 0, noticeAt_ = 0, seated_ = 0;
  bool listKnown_ = false, heldTimed_ = false;
  std::unique_ptr<Job> job_;
  enum JobKind { None, List, Host, Join, Probe, Register } jobKind_ = None;
  bool held_ = false;
  std::unique_ptr<Session> sess_;
  std::string lobbyHost_, roomCode_;
  int lobbyPort_ = 7770;
  LevelBundle lb_;
  MatchInfo hostInfo_;
  EditBox *lobbyEdit_, *codeEdit_;
  Button *bRefresh_, *bJoin_, *bPaste_, *bCreate_, *bTest_, *bRegister_, *bBack_, *bCopy_, *bReady_, *bStart_, *bLeave_, *bMode_, *bLvlPrev_, *bLvlNext_, *bMasPrev_, *bMasNext_, *bGo_, *bCancel_;
};

}

void showOnline(App& app) { app.setScreen(std::make_unique<OnlineScreen>(app, std::string()), App::Left); }

void addMainMenuButton(Panel& menu) {
  Button* b = menu.patchButton(kMenuButtonId, "Online", "IMAGE_DLG_BROWNBUTTON1", 0x23a, 0x22a);
  b->hoverSound = "MOUSEOVER";
  b->hoverPitch = 15;
  b->clickSound = "BUTTON1";
  if (Widget* r = menu.findWidget(4)) { b->w = r->w; b->h = r->h; }
}

bool handleMenuButton(App& app, int id) {
  if (id != kMenuButtonId) return false;
  showOnline(app);
  return true;
}

void tickMainMenu(App& app) {
  pp::online::steam::ensureInit();
  pp::online::steam::pump();
  std::string joinLink;
  if (pp::online::steam::takeJoinRequest(&joinLink)) g_pendingInvite = joinLink;
  if (app.moving() || app.anyDialog()) return;
  if (g_pendingInvite.empty()) {
    std::string discordInvite = app.takeDiscordInvite();
    if (discordInvite.empty()) return;
    auto screen = std::make_unique<OnlineScreen>(app, discordInvite, true);
    app.setScreen(std::move(screen), App::Left);
    return;
  }
  std::string inv = std::move(g_pendingInvite);
  g_pendingInvite.clear();
  app.setScreen(std::make_unique<OnlineScreen>(app, inv), App::Left);
}

}
