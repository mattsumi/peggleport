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
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>
#include <thread>
#include <system_error>

#include "log.h"
#include "online/content_hash.h"
#include "online/direct.h"
#include "online/discovery.h"
#include "online/endpoint.h"
#include "online/invite.h"
#include "online/online_ui.h"
#include "online/steam.h"
#include "gamefind.h"
#include "ui/secondary_art.h"

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
  pp::online::LobbyEndpoint endpoint;
  pp::online::LobbySource source = pp::online::LobbySource::Global;
  int players = 2, joined = 1;
  bool party = false, haveLevel = true;
};

constexpr float kColSource = 52, kColCode = 112, kColGame = 184, kColSeats = 262, kColLevel = 306, kColRoom = 414, kRowEnd = 532;
constexpr float kRowRight = 536;
constexpr float kDlgX = 130, kDlgY = 100, kDlgW = 540, kDlgH = 340, kDlgPad = 24;
constexpr float kPickL = 250, kPickR = kDlgX + kDlgW - kDlgPad, kPickMid = (kPickL + kPickR) / 2, kPickGap = 8;
constexpr float kCreateWideDown = 52, kCreateFooterGap = 12;
constexpr int kArrowHalf = 6;
constexpr int kHoldTicks = 1000, kNoticeTicks = 800;
constexpr float kAddrWidth = 300;
constexpr float kJoinX = 283, kJoinY = 150, kJoinW = 500, kJoinH = 380;
constexpr float kHostX = 313, kHostY = 44, kHostW = 440, kHostH = 486;
constexpr float kListX = 187, kListY = 160, kListW = 693, kListH = 360, kListTop = kListY + 88, kListRow = 30;
constexpr int kListRows = 8;

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
  pp::online::LobbyEndpoint endpoint;
  std::unique_ptr<Session> session;
  ~Job() { stop = true; if (th.joinable()) th.join(); }
};

enum Ids { B_Refresh = 1, B_Join, B_Paste, B_Create, B_Back, B_Copy, B_Ready, B_Start, B_Leave, B_Go, B_Cancel, B_Mode, B_LvlPrev, B_LvlNext, B_MasPrev, B_MasNext, B_Test, B_Register, C_Discord, B_Destination, E_Code = 100 };

#if defined(__APPLE__)
constexpr bool kUrlRegistration = false;
#else
constexpr bool kUrlRegistration = true;
#endif

class OnlineScreen : public Screen {
 public:
  OnlineScreen(App& a, const std::string& invite, bool fromDiscord = false, Flow flow = Flow::Browse, bool party = false) : Screen(a) {
    flow_ = a.consoleCanvasWidth() >= 960 ? (invite.empty() ? flow : Flow::Join) : Flow::Browse;
    party_ = party;
    prefsFile_ = a.userDir.parent_path() / "online.ini";
    if (!a.cfg.noSave) prefs_.load(prefsFile_);
    configuredLobby_ = prefs_.lobby.empty() ? pp::online::publicLobby : prefs_.lobby;
    if (const char* overrideLobby = std::getenv("PP_LOBBY"); overrideLobby && pp::online::parseEndpoint(overrideLobby, nullptr)) configuredLobby_ = overrideLobby;
    if (!pp::online::parseEndpoint(configuredLobby_, nullptr)) configuredLobby_ = pp::online::publicLobby;
    levels_ = levelChoices(a);
    for (size_t i = 0; i < levels_.size(); i++)
      if (levels_[i].file == prefs_.level) { levelIdx_ = (int)i; break; }
    mode_ = prefs_.mode;
    if (flow_ == Flow::Host) mode_ = party ? std::max(1, mode_) : 0;
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
    codeEdit_ = edit(E_Code, 556, 176, 204 - joinW, 5, 10, 204 - joinW - 20);
    codeEdit_->onEnter = [this] { onButton(B_Join); };
    focusW = codeEdit_;
    codeEdit_->focused = true;

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
    bDestination_ = btn(B_Destination, "Destination: Global", kPickL, 322, kPickR - kPickL);
    auto discord = std::make_unique<Checkbox>();
    discord->id = C_Discord;
    discord->image = a.gfx.img("IMAGE_DLG_BUTTON1");
    discord->label = "Discord activity";
    discord->font = "PLAIN";
    discord->checked = a.discordEnabled();
    discord->x = 40; discord->y = 20; discord->w = 230;
    discord->h = discord->image ? discord->image->lh() / 4 : 34;
    discordCheck_ = add(std::move(discord));
    if (Tex* art = flow_ == Flow::Browse ? nullptr : secondaryArt(a.gfx, "action_button")) {
      for (Button* b : {bJoin_, bPaste_, bTest_, bRegister_, bMode_, bLvlPrev_, bLvlNext_, bMasPrev_, bMasNext_, bGo_, bDestination_}) {
        b->image = art;
        b->h = art->lh() / 3;
        b->labelStyle = 9;
        b->labelFullGradient = true;
        b->textOffY = 3;
      }
      bGo_->label = "Host Match";
    }
    for (const auto& w : widgets) {
      baseX_[w->id] = w->x;
      baseY_[w->id] = w->y;
    }
    layout();
    if (!invite.empty()) {
      if (fromDiscord) applyDiscordInvite(invite);
      else applyInvite(invite, false);
    }
    else startList();
    if (flow_ == Flow::Host) onButton(B_Create);
  }

  ~OnlineScreen() override {
    if (job_) {
      job_->stop = true;
      job_->session = std::move(sess_);
      pp::online::retireOnlineWork(std::shared_ptr<Job>(std::move(job_)));
    }
    browser_.cancel();
    savePrefs();
    if (sess_) sess_->leave();
  }

  const char* name() const override { return "online"; }
  int canvasWidth() const override { return app.consoleCanvasWidth(); }

  void placeWidgets(float extra) {
    for (const auto& w : widgets) {
      const int id = w->id;
      const bool right = id == E_Code || (id >= B_Refresh && id <= B_Create) || id == B_Test || id == B_Register || id == B_Leave;
      const bool create = (id >= B_Go && id <= B_MasNext) || id == B_Destination;
      const bool center = id == B_Ready || id == B_Start || create;
      w->x = baseX_[id] + (right ? extra : center ? extra / 2 : 0);
      if (create) w->y = baseY_[id] + (extra > 0 ? kCreateWideDown + (id == B_Go || id == B_Cancel ? kCreateFooterGap : 0) : 0);
    }
    for (auto* b : {bBack_, bCopy_, bReady_, bStart_, bLeave_}) b->y = extra > 0 ? 557.0f : 520.0f;
    if (flow_ == Flow::Browse || extra <= 0) return;
    const float crop = (extra - 267) / 2;
    auto put = [&](Widget* w, float x, float y, float width = 0) {
      w->x = x + crop;
      w->y = y;
      if (width > 0) w->w = width;
    };
    put(codeEdit_, kJoinX + 60, kJoinY + 70, 200);
    put(bJoin_, kJoinX + 280, kJoinY + 64, 160);
    put(bPaste_, kJoinX + 60, kJoinY + 122, 380);
    put(bTest_, kJoinX + 60, kJoinY + 176, 380);
    put(bRegister_, kJoinX + 60, kJoinY + 230, 380);
    put(discordCheck_, kJoinX + 60, kJoinY + 288, 380);
    put(bLvlPrev_, kHostX + 40, kHostY + 93);
    put(bLvlNext_, kHostX + kHostW - 40 - bLvlNext_->w, kHostY + 93);
    put(bMasPrev_, kHostX + 40, kHostY + 258);
    put(bMasNext_, kHostX + kHostW - 40 - bMasNext_->w, kHostY + 258);
    put(bMode_, kHostX + 60, kHostY + 324, kHostW - 120);
    put(bDestination_, kHostX + 60, kHostY + (party_ ? 378 : 324), kHostW - 120);
    put(bGo_, kHostX + 60, kHostY + 428, kHostW - 120);
  }

  void savePrefs() {
    prefs_.discord = app.discordEnabled();

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
    invitedAddress_ = selectedAddress_ = pp::online::endpointAddress(iv.host, iv.port);
    selectedCode_ = iv.code;
    setCode(iv.code);
    if (flow_ != Flow::Browse && !room_ && !create_) {
      flow_ = Flow::Join;
      layout();
    }
    hold("Invite for room " + iv.code + " on " + addrShown(iv.host, iv.port) + ". Press Join to connect.", 2, true);
    startList();
  }

  void startList() {
    if (job_ || room_ || create_) return;
    if (listStarted_ && app.updateCount() - lastRefresh_ < 200) return;
    pp::online::BrowseOptions options;
    options.configured = configuredLobby_;
    options.invited = invitedAddress_;
    if (!browser_.start(options)) return;
    listStarted_ = true;
    lastRefresh_ = app.updateCount();
    browse_ = {};
    finishList(browser_.snapshot());
  }

  void startProbe() {
    if (job_ || room_ || create_) return;
    std::string host;
    int port;
    splitHostPort(configuredLobby_, &host, &port);
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

  void finishList(pp::online::BrowseSnapshot snapshot) {
    pp::online::LobbyEndpoint selected;
    if (pp::online::parseEndpoint(selectedAddress_, &selected)) selectedAddress_ = pp::online::roomJoinTargets(snapshot, selectedCode_, &selected).front().address();
    browse_ = std::move(snapshot);
    rooms_.clear();
    listKnown_ = true;
    const bool show = !held_ || (heldTimed_ && app.updateCount() - heldAt_ >= kHoldTicks);
    int localRooms = 0, globalRooms = 0;
    bool globalDone = false, globalOk = false;
    for (const auto& listing : browse_.listings) {
      if (listing.source == pp::online::LobbySource::Global) { globalDone = listing.done; globalOk = listing.ok; }
      if (!listing.ok) continue;
      for (const auto& l : listing.lines) {
        std::istringstream is(l);
        std::string tag;
        RoomRow r;
        int party = 0;
        is >> tag >> r.code >> r.players >> r.joined >> party >> r.level >> r.hash;
        std::getline(is >> std::ws, r.name);
        if (tag != "ROOM" || pp::online::listedRoomCode(l).empty()) continue;
        r.endpoint = listing.endpoint;
        r.source = listing.source;
        r.party = party != 0;
        if ((flow_ == Flow::Quick || flow_ == Flow::Custom) && r.party != party_) continue;
        r.players = std::clamp(r.players, 2, 4);
        r.joined = std::clamp(r.joined, 0, r.players);
        r.haveLevel = levelInstalled(app, r.level);
        for (auto& c : r.name) if (c == '_') c = ' ';
        r.levelShown = fitText(app.gfx, "PLAIN", r.haveLevel ? levelTitle(app, r.level) : cleanText(r.level, 64), kColRoom - kColLevel - 8);
        r.nameShown = r.haveLevel ? fitText(app.gfx, "PLAIN", cleanText(r.name, 40), kRowEnd - kColRoom) : std::string("level not installed");
        rooms_.push_back(r);
        if (r.source == pp::online::LobbySource::Local) localRooms++;
        if (r.source == pp::online::LobbySource::Global) globalRooms++;
      }
    }
    std::stable_sort(rooms_.begin(), rooms_.end(), [](const RoomRow& a, const RoomRow& b) { return (int)a.source < (int)b.source; });
    rowOffset_ = std::clamp(rowOffset_, 0, std::max(0, (int)rooms_.size() - visibleRows()));
    sourceLine_ = "LAN/local: " + std::to_string(localRooms) + (browse_.discovering ? " (searching)" : "") + "   Global: " +
                  (!globalDone ? std::string("checking...") : !globalOk ? std::string("unavailable") : std::to_string(globalRooms));
    listLine_ = rooms_.empty() ? (browse_.busy() ? "Searching LAN/local and global rooms. You can create a room or return to the menu." : "No open rooms. Create one and send your friends the invite.") :
                std::to_string(rooms_.size()) + " open room(s). Click to select, double-click to join.";
    if (flow_ != Flow::Browse)
      listLine_ = rooms_.empty() ? (browse_.busy() ? "Searching for games..." : "No games were found.")
                                 : std::to_string(rooms_.size()) + (rooms_.size() == 1 ? " game" : " games") + " found. Pick one and press Join Game.";
    if (globalDone && !globalOk) listLine_ += " Global lobby unavailable; LAN/local rooms still work.";
    listKind_ = 0;
    if (show) say(listLine_, listKind_);
  }

  void prepareHostTargets() {
    const auto selected = hostTargets_.empty() ? configuredLobby_ : pp::online::roomJoinTargets(browse_, "", &hostTargets_[hostTarget_]).front().address();
    hostTargets_.clear();
    pp::online::LobbyEndpoint endpoint;
    if (pp::online::parseEndpoint(configuredLobby_, &endpoint)) hostTargets_.push_back(pp::online::roomJoinTargets(browse_, "", &endpoint).front());
    for (const auto& listing : browse_.listings) {
      if (listing.source != pp::online::LobbySource::Local || !listing.ok) continue;
      bool known = false;
      for (const auto& target : hostTargets_) if (target.address() == listing.endpoint.address()) known = true;
      if (!known) hostTargets_.push_back(listing.endpoint);
    }
    hostTarget_ = 0;
    for (size_t i = 0; i < hostTargets_.size(); i++) if (hostTargets_[i].address() == selected) hostTarget_ = i;
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
    pp::online::LobbyEndpoint destination;
    pp::online::parseEndpoint(hostTargets_.empty() ? configuredLobby_ : hostTargets_[hostTarget_].address(), &destination);
    splitHostPort(pp::online::roomJoinTargets(browser_.snapshot(), "", &destination).front().address(), &host, &port);
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
    pp::online::LobbyEndpoint selected;
    const bool explicitTarget = selectedCode_ == code && pp::online::parseEndpoint(selectedAddress_, &selected);
    const auto targets = pp::online::roomJoinTargets(browser_.snapshot(), code, explicitTarget ? &selected : nullptr);
    if (explicitTarget) selected = targets.front();
    nonDiscordInvite_ = false;
    isHost_ = false;
    sess_ = std::make_unique<Session>();
    sess_->setName(playerName());
    roomCode_ = code;
    job_ = std::make_unique<Job>();
    jobKind_ = Join;
    jobAddr_ = explicitTarget ? addrShown(selected.secure ? selected.address() : selected.host, selected.port) : "LAN/local and global lobbies";
    Job* j = job_.get();
    Session* sp = sess_.get();
    j->th = std::thread([j, sp, targets, explicitTarget, selected, code] {
      j->endpoint = selected;
      if (!explicitTarget && !pp::online::findRoomEndpoint(Session::listRooms, targets, code, &j->endpoint, &j->err, &j->stop)) { j->done = true; return; }
      const auto& endpoint = j->endpoint;
      j->ok = sp->joinViaLobby(endpoint.secure ? endpoint.address() : endpoint.host, endpoint.port, code, &j->err, &j->stop);
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
    if (flow_ == Flow::Host) {
      create_ = true;
      layout();
    }
  }

  void layout() {
    bool browse = !room_ && !create_;
    const bool console = flow_ != Flow::Browse, join = flow_ == Flow::Join && browse;
    codeEdit_->visible = browse && (!console || join);
    for (Button* b : {bRefresh_, bJoin_, bPaste_, bCreate_, bTest_}) b->visible = browse && !console;
    bJoin_->visible = bPaste_->visible = bTest_->visible = codeEdit_->visible;
    bRegister_->visible = codeEdit_->visible && kUrlRegistration;
    discordCheck_->visible = !console || join;
    bBack_->visible = browse && !console;
    bCopy_->visible = bReady_->visible = bStart_->visible = bLeave_->visible = room_ && !console;
    for (Button* b : {bMode_, bLvlPrev_, bLvlNext_, bMasPrev_, bMasNext_, bGo_, bCancel_, bDestination_}) b->visible = create_;
    if (console) {
      bMode_->visible = create_ && party_;
      bCancel_->visible = false;
    }
    if (room_ && !console) {
      bReady_->visible = !isHost_;
      bStart_->visible = isHost_;
    }
    if (create_) bMode_->label = console ? (mode_ == 1 ? "3 Players" : "4 Players") : mode_ == 0 ? "Duel (2 players)" : mode_ == 1 ? "Peg Party (3 players)" : "Peg Party (4 players)";
    if (create_) bDestination_->label = hostTarget_ ? "Destination: LAN " + std::to_string(hostTarget_) : configuredLobby_ == pp::online::publicLobby ? "Destination: Global" : "Destination: Custom";
    if (codeEdit_->visible) focusW = codeEdit_, codeEdit_->focused = true;
    else if (focusW == codeEdit_) focusW = nullptr, codeEdit_->focused = false;
  }

  void onButton(int id) override {
    if (id == B_Back) { app.showMainMenu(); return; }
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
      case B_Create: finishList(browser_.snapshot()); prepareHostTargets(); create_ = true; release(); layout(); break;
      case B_Destination: if (!hostTargets_.empty()) hostTarget_ = (hostTarget_ + 1) % hostTargets_.size(); layout(); break;
      case B_Cancel:
        if (flow_ != Flow::Browse) { app.showMainMenu(); return; }
        create_ = false;
        layout();
        break;
      case B_Go: create_ = false; layout(); startHost(); break;
      case B_Mode: mode_ = flow_ == Flow::Host ? 3 - mode_ : (mode_ + 1) % 3; layout(); break;
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
      case B_Leave:
        leaveRoom("You left the room.");
        if (flow_ != Flow::Browse) app.showMainMenu();
        break;
    }
  }

  void keyDown(int key, int mod) override {
    if (flow_ != Flow::Browse) {
      if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (room_) {
          if (Button* b = isHost_ ? bStart_ : bReady_; b->enabled) onButton(b->id);
        } else if (create_) onButton(B_Go);
        else if (flow_ == Flow::Custom || flow_ == Flow::Join) onButton(B_Join);
        return;
      }
      if (key == SDLK_F5 && flow_ == Flow::Custom && !room_) {
        if (bRefresh_->enabled) onButton(B_Refresh);
        return;
      }
      if (key == SDLK_C && room_) { onButton(B_Copy); return; }
      if ((key == SDLK_UP || key == SDLK_DOWN) && flow_ == Flow::Custom && !room_ && !job_ && !rooms_.empty()) {
        const int i = selectedRow();
        const int next = i < 0 ? 0 : std::clamp(i + (key == SDLK_UP ? -1 : 1), 0, (int)rooms_.size() - 1);
        selectRoom(next);
        rowOffset_ = std::clamp(rowOffset_, std::max(0, next - kListRows + 1), next);
        return;
      }
    }
    if (key == SDLK_ESCAPE) {
      if (room_) onButton(B_Leave);
      else if (create_) onButton(B_Cancel);
      else app.showMainMenu();
    } else if (key == SDLK_TAB && codeEdit_->visible) {
      focusW = codeEdit_;
      codeEdit_->focused = true;
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
    std::string joinLink;
    if (nonDiscordInvite_ || discordInviteUntil_) pp::online::steam::discardJoinRequest();
    else if (!nonDiscordInvite_ && !discordInviteUntil_ && !room_ && !create_ && !job_ && !app.anyDialog() && !app.moving() && pp::online::steam::takeJoinRequest(&joinLink)) applyInvite(joinLink, false);
    std::string up;
    for (char c : codeEdit_->value) up.push_back((char)std::toupper((unsigned char)c));
    if (up != codeEdit_->value) codeEdit_->value = up;
    if (codeEdit_->value != codeSeen_) {
      codeSeen_ = codeEdit_->value;
      selectedAddress_.clear();
      selectedCode_.clear();
      if (!job_ && !room_ && !create_) release();
    }
    auto snapshot = browser_.snapshot();
    if (room_ && !job_) {
      pp::online::LobbyEndpoint endpoint;
      if (pp::online::parseEndpoint(pp::online::endpointAddress(lobbyHost_, lobbyPort_), &endpoint)) {
        const auto target = pp::online::roomJoinTargets(snapshot, roomCode_, &endpoint).front();
        lobbyHost_ = target.secure ? target.address() : target.host;
        lobbyPort_ = target.port;
      }
    }
    if (!room_ && !job_ && snapshot.revision != browse_.revision) {
      finishList(std::move(snapshot));
      if (create_) { prepareHostTargets(); layout(); }
    }
    if (job_ && job_->done) {
      std::unique_ptr<Job> j = std::move(job_);
      if (j->th.joinable()) j->th.join();
      if (jobKind_ == Probe || jobKind_ == Register) finishReport(*j);
      else if (!j->ok) {
        sess_.reset();
        hold(trouble(j->err, jobKind_ == Host ? Where::Open : Where::Join), 1);
        startList();
        if (flow_ == Flow::Host) {
          create_ = true;
          layout();
        }
      } else {
        if (jobKind_ == Join) {
          lobbyHost_ = j->endpoint.secure ? j->endpoint.address() : j->endpoint.host;
          lobbyPort_ = j->endpoint.port;
        }
        enterRoom();
      }
    }
    if (!job_ && !room_ && !create_ && !sess_ && app.updateCount() - lastRefresh_ > 600 && lastRefresh_ >= 0) startList();
    if (flow_ == Flow::Quick && !job_ && !room_ && listKnown_) {
      int pick = -1;
      for (size_t i = 0; i < rooms_.size() && pick < 0; i++)
        if (rooms_[i].haveLevel && rooms_[i].joined < rooms_[i].players && !tried_.count(rooms_[i].code)) pick = (int)i;
      if (pick >= 0) {
        selectRoom(pick);
        tried_.insert(rooms_[pick].code);
        startJoin(rooms_[pick].code);
      } else if (!browse_.busy()) {
        flow_ = Flow::Custom;
        if (!held_) hold("No games were found. Refresh the list, or go back and host one.", 0, true);
        layout();
      }
    }
    if (sess_ && room_) tickRoom();
    bBack_->enabled = true;
    bRefresh_->enabled = !job_ && !browse_.busy() && app.updateCount() - lastRefresh_ >= 200;
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
    if (in.type == Input::MouseDown && in.button == 1 && in.y >= 535)
      for (const auto& p : prompts_)
        if (in.x >= p.x && in.x < p.x + p.w) {
          app.playSample("BUTTON1");
          keyDown(p.key, 0);
          return true;
        }
    if (!room_ && !create_ && focusW == codeEdit_ &&
        ((in.type == Input::Text && !in.text.empty()) ||
         (in.type == Input::KeyDown && (in.key == SDLK_BACKSPACE || in.key == SDLK_DELETE ||
          (in.key == SDLK_V && (in.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI))))))) {
      nonDiscordInvite_ = true;
      discordInviteUntil_ = 0;
      discordInviteCode_.clear();
    }
    const float extra = static_cast<float>(app.gfx.canvasW() - 800), crop = (extra - 267) / 2;
    const bool overList = console() ? in.x >= kListX + crop && in.x <= kListX + kListW + crop && in.y >= kListY && in.y <= kListY + kListH
                                    : in.x >= 40 && in.x <= 540 + extra && in.y >= 130 && in.y <= 460;
    if (in.type == Input::Wheel && !room_ && !create_ && overList) {
      rowOffset_ = std::clamp(rowOffset_ - (int)std::lround(in.wheel * 3), 0, std::max(0, (int)rooms_.size() - visibleRows()));
      return true;
    }
    bool used = Panel::input(in);
    if (!used && in.type == Input::MouseDown && in.button == 1 && !room_ && !create_ && !job_) {
      int i = rowAt(in.x, in.y);
      if (i >= 0) {
        selectRoom(i);
        if (in.clicks >= 2) onButton(B_Join);
        used = true;
      }
    }
    return used;
  }

  bool console() const { return flow_ != Flow::Browse && app.gfx.canvasW() > 800; }
  int visibleRows() const { return console() ? kListRows : 10; }

  void selectRoom(int i) {
    nonDiscordInvite_ = true;
    discordInviteUntil_ = 0;
    discordInviteCode_.clear();
    setCode(rooms_[i].code);
    selectedCode_ = rooms_[i].code;
    selectedAddress_ = rooms_[i].endpoint.address();
    release();
  }

  int selectedRow() const {
    for (size_t i = 0; i < rooms_.size(); i++)
      if (codeEdit_->value == rooms_[i].code && selectedAddress_ == rooms_[i].endpoint.address()) return (int)i;
    return -1;
  }

  int rowAt(float x, float y) const {
    const float extra = app.gfx.canvasW() - 800.0f;
    if (console()) {
      const float left = kListX + 22 + (extra - 267) / 2;
      if (flow_ != Flow::Custom || x < left || x > left + kListW - 44 || y < kListTop || y >= kListTop + kListRows * kListRow) return -1;
      const int i = rowOffset_ + (int)((y - kListTop) / kListRow);
      return i >= 0 && i < (int)rooms_.size() ? i : -1;
    }
    if (x < 40 || x > (extra > 0 ? kRowRight + extra : 530) || y < 166 || y >= 426) return -1;
    int i = rowOffset_ + (int)((y - 166) / 26);
    return i >= 0 && i < (int)rooms_.size() ? i : -1;
  }

  void panel(Gfx& g, float x, float y, float w, float h, const std::string& title) {
    if (g.canvasW() > 800) secondaryPanel(g, x, y, w, h, title.empty() ? 0.0f : 26.0f);
    else {
      g.fill(x, y, w, h, 0x000000, 0.5f);
      g.frame(x, y, w, h, 0xffd605, 0.8f);
    }
    if (!title.empty()) g.drawString("OVERLOAD16OUTLINE", x + w / 2, y + (g.canvasW() > 800 ? 24 : 20), title, 0xffd605, 1, Align::Center);
  }

  void drawLayer(Gfx& g) override {
    const float width = static_cast<float>(g.canvasW()), extra = width - 800;
    placeWidgets(extra);
    prompts_.clear();
    if (flow_ != Flow::Browse && extra > 0) {
      drawConsole(g, width);
      return;
    }
    if (room_ && extra > 0 && drawStageRoom(g, width)) return;
    if (extra > 0) {
      g.fill(0, 0, width, 600, room_ ? 0x360b20 : 0x211850);
      const char* landscape = "console/menu/home.png";
      Tex* scene = room_ ? secondaryArt(g, "lobby_stage") : g.vfs().exists(landscape) ? g.img(landscape) : nullptr;
      if (scene) {
        const Rect bounds = secondaryCover(*scene, width);
        g.drawStretch(scene, bounds.x, bounds.y, bounds.w, bounds.h);
        if (!room_) {
          constexpr const char* foreground = "console/menu/home_front.png";
          if (g.vfs().exists(foreground)) g.drawStretch(g.img(foreground), bounds.x, bounds.y, bounds.w, bounds.h);
        }
      }
      constexpr const char* header = "console/menu/header.png";
      if (!room_ && g.vfs().exists(header)) g.drawStretch(g.img(header), width / 2 - 162, 0, 324, 76);
      else secondaryPanel(g, width / 2 - 174, 16, 348, 47, 41);
    } else {
      g.fill(0, 0, 800, 600, 0x0a1230);
      g.fill(0, 0, 800, 70, 0x16224d, 1);
      g.fill(0, 70, 800, 2, 0xffd605, 0.7f);
    }
    const char* title = extra > 0 && room_ ? ((isHost_ ? hostInfo_ : sess_->info()).party ? "Peg Party Lobby" : "Duel Lobby") : "Play Online";
    g.drawString("OVERLOAD24GRAD1", width / 2, extra > 0 && room_ ? 51.0f : 46.0f, title, extra > 0 && room_ ? 0xffd27e : 0xffffff, 1, Align::Center);
    if (room_) { drawRoom(g); }
    else {
      if (extra > 0) panel(g, 544 + extra, 72, 228, 383, "");
      const char* listFont = extra > 0 ? "PLAINOUTLINE" : "PLAIN";
      g.drawString(listFont, 40, 101, fitText(g, listFont, sourceLine_, 500 + extra), 0xffffff, 1);
      panel(g, 40, 120, 500 + extra, 340, "LAN / Local and Global Rooms");
      g.drawString("PLAIN", kColSource, 160, "Source", 0xaaaaff, 1);
      g.drawString("PLAIN", kColCode, 160, "Code", 0xaaaaff, 1);
      g.drawString("PLAIN", kColGame, 160, "Game", 0xaaaaff, 1);
      g.drawString("PLAIN", kColSeats, 160, "Seats", 0xaaaaff, 1);
      g.drawString("PLAIN", kColLevel, 160, "Level", 0xaaaaff, 1);
      g.drawString("PLAIN", kColRoom, 160, "Room", 0xaaaaff, 1);
      for (int visible = 0; visible < 10 && rowOffset_ + visible < (int)rooms_.size(); visible++) {
        const RoomRow& r = rooms_[rowOffset_ + visible];
        float y = (float)(166 + visible * 26);
        bool sel = codeEdit_->value == r.code && selectedAddress_ == r.endpoint.address();
        if (sel) g.fill(44, y, kRowRight - 44 + extra, 24, 0xffd605, 0.25f);
        uint32_t col = r.haveLevel && r.joined < r.players ? 0xffffff : 0x888888;
        char seats[16];
        std::snprintf(seats, sizeof seats, "%d/%d", r.joined, r.players);
        const char* source = r.source == pp::online::LobbySource::Local ? "LAN" : r.source == pp::online::LobbySource::Global ? "Global" : r.source == pp::online::LobbySource::Custom ? "Custom" : "Invite";
        g.drawString("PLAIN", kColSource, y + 17, source, col);
        g.drawString("PLAIN", kColCode, y + 17, r.code, col);
        g.drawString("PLAIN", kColGame, y + 17, r.party ? "Peg Party" : "Duel", col);
        g.drawString("PLAIN", kColSeats, y + 17, seats, col);
        g.drawString("PLAIN", kColLevel, y + 17, r.levelShown, col);
        g.drawString("PLAIN", kColRoom, y + 17, extra > 0 && r.haveLevel ? fitText(g, "PLAIN", cleanText(r.name, 40), kRowEnd - kColRoom + extra) : r.nameShown, col);
      }
      if (rooms_.size() > 10) g.drawString("PLAIN", 52, 449, std::to_string(rowOffset_ + 1) + "-" + std::to_string(std::min(rowOffset_ + 10, (int)rooms_.size())) + " of " + std::to_string(rooms_.size()) + " rooms - mouse wheel to scroll", 0xaaaaff);
      g.drawString("PLAIN", 556 + extra, 166, "Join with a room code", 0xffffff, 1);
      if (job_) g.drawString("PLAIN", 556 + extra, 450, "working...", 0xffe070);
      if (extra > 0) panel(g, 40, 466, width - 80, 71, "");
      drawStatus(g, extra > 0 ? 52.0f : 40.0f, 484);
    }
    if (create_) drawCreate(g);
    if (extra > 0) {
      constexpr const char* footer = "console/menu/footer.png";
      if (g.vfs().exists(footer)) g.drawStretch(g.img(footer), 0, 538, width, 62);
      else secondaryFooter(g, width);
    }
  }

  Tex* menuTex(Gfx& g, const char* name) {
    const auto path = std::string("console/menu/") + name + ".png";
    return g.vfs().exists(path) ? g.img(path) : nullptr;
  }

  float prompt(Gfx& g, float x, int glyph, const char* key, const char* label, int code) {
    const float next = drawConsolePrompt(g, x, glyph, key, label, app.pad);
    prompts_.push_back({x, next - x, code});
    return next;
  }

  void drawConsole(Gfx& g, float width) {
    const float crop = (width - 1067) / 2;
    if (room_) {
      if (!drawStageRoom(g, width)) {
        drawWaitScene(g, width);
        drawRoom(g);
        drawConsoleRail(g, width);
      }
      float x = 66;
      if (isHost_ ? bStart_->enabled : bReady_->enabled) x = prompt(g, x, 0, "ENTER", isHost_ ? "Start Game" : "Ready", SDLK_RETURN);
      x = prompt(g, x, 1, "ESC", "Quit", SDLK_ESCAPE);
      prompt(g, x, 3, "C", app.updateCount() - copiedAt_ < 150 ? "Copied!" : "Copy Invite", SDLK_C);
      return;
    }
    drawWaitScene(g, width);
    const bool hosting = job_ && jobKind_ == Host, joining = job_ && jobKind_ == Join;
    const bool searching = !job_ && (flow_ == Flow::Quick || (flow_ == Flow::Custom && rooms_.empty() && browse_.busy()));
    const bool busy = hosting || joining || searching;
    const bool plate = !hosting && !joining && (flow_ == Flow::Custom || flow_ == Flow::Join);
    if (plate) drawConsoleTitle(g, 445 + crop, width, flow_ == Flow::Custom ? "CUSTOM MATCH" : "JOIN WITH CODE");
    if (busy) drawBar(g, 367 + crop, plate ? 278.0f : 228.0f,
                      hosting ? "HOSTING GAME" : joining ? "JOINING GAME" : flow_ == Flow::Quick ? "SEARCHING FOR GAME" : "SEARCHING FOR GAMES");
    else if (flow_ == Flow::Custom) drawRoomList(g, crop);
    else if (create_) drawHostPanel(g, crop);
    else if (flow_ == Flow::Join) drawJoinPanel(g, crop);
    drawConsoleRail(g, width);
    float x = 66;
    if (!busy) x = prompt(g, x, 0, "ENTER", create_ ? "Host Match" : "Join Game", SDLK_RETURN);
    x = prompt(g, x, 1, "ESC", busy ? "Cancel" : "Back", SDLK_ESCAPE);
    if (flow_ == Flow::Custom && !busy) prompt(g, x, 3, "F5", "Refresh List", SDLK_F5);
  }

  void drawWaitScene(Gfx& g, float width) {
    const float crop = (width - 1067) / 2;
    Tex* back = menuTex(g, "wait");
    if (!back) back = menuTex(g, "home");
    if (!back) {
      g.fill(0, 0, width, 600, 0x211850);
      return;
    }
    const Rect r = secondaryCover(*back, width);
    g.drawStretch(back, r.x, r.y, r.w, r.h);
    if (Tex* glow = menuTex(g, "sun_glow")) {
      const float s = 528.0f / glow->lw();
      g.drawRot(glow, 636 + crop, 121, app.updateCount() * 0.0008f, s, s);
    }
    if (Tex* sun = menuTex(g, "sun")) g.drawStretch(sun, 578 + crop, 63, 116, 116);
    if (Tex* front = menuTex(g, "wait_front")) g.drawStretch(front, r.x, r.y, r.w, r.h);
  }

  void drawBar(Gfx& g, float x, float y, const char* caption) {
    constexpr float k = 356.0f / 400;
    if (Tex* plate = menuTex(g, "title_plate")) g.drawStretch(plate, x, y, 356, 89);
    else secondaryPanel(g, x, y + 18, 356, 53);
    const Rect trough{x + 25 * k, y + 24 * k, 349 * k, 54 * k};
    if (Tex* lights = menuTex(g, "loader_lights")) {
      constexpr float pieces[5][2] = {{0, 18}, {19, 17}, {37, 17}, {55, 17}, {76, 14}};
      const float end = trough.x + trough.w * ((app.updateCount() % 300) / 300.0f);
      const float top = trough.y + (trough.h - 49 * k) / 2;
      const auto piece = [&](int i, float px) {
        g.drawStretchSrc(lights, pieces[i][0], 0, pieces[i][1], lights->lh(), px, top, pieces[i][1] * k, 49 * k);
        return px + pieces[i][1] * k;
      };
      g.pushClip(trough.x, trough.y, trough.w, trough.h);
      float px = piece(0, trough.x);
      for (int i = 1; px + (17 + 14) * k <= end; i = i % 3 + 1) px = piece(i, px);
      piece(4, px);
      g.popClip();
    }
    consoleText(g, "OVERLOAD24OUTLINE", x + 178, y + 107, caption, 0xffffff);
    if (held_ || statusKind_ != 0)
      g.drawWrapped("PLAINOUTLINE", {x + 178 - 300, y + 132, 600, 60}, status_, statusColor(), 1, Align::Center);
  }

  void consoleText(Gfx& g, const char* font, float cx, float cy, const std::string& text, uint32_t color) {
    g.drawString(font, cx, cy + g.fontAscent(font) - g.fontHeight(font) * 0.5f, text, color, 1, Align::Center);
  }

  uint32_t statusColor() const { return statusKind_ == 1 ? 0xff9090 : statusKind_ == 2 ? 0x9cffb7 : 0xc5d9ff; }

  void consolePanel(Gfx& g, float x, float y, float w, float h, const char* title) {
    if (Tex* box = secondaryArt(g, "story_panel")) g.drawBox(box, x, y, w, h);
    else secondaryPanel(g, x, y, w, h, 40);
    consoleText(g, "OVERLOAD24GRAD1", x + w / 2, y + 27, title, 0xffffff);
  }

  void drawRoomList(Gfx& g, float crop) {
    const float x = kListX + crop, left = x + 22, right = x + kListW - 22;
    consolePanel(g, x, kListY, kListW, kListH, party_ ? "PEG PARTY GAMES" : "DUEL GAMES");
    g.fill(left, kListY + 56, right - left, kListTop - kListY - 56 + kListRows * kListRow + 4, 0x07062a, 0.85f);
    const float cols[] = {left + 16, left + 300, left + 470, left + 560};
    const char* heads[] = {"ROOM", "LEVEL", "PLAYERS", "WHERE"};
    for (int c = 0; c < 4; c++) g.drawString("OVERLOAD16OUTLINE", cols[c], kListY + 80, heads[c], 0xb6efff);
    const int selected = selectedRow();
    for (int v = 0; v < kListRows && rowOffset_ + v < (int)rooms_.size(); v++) {
      const RoomRow& r = rooms_[rowOffset_ + v];
      const float y = kListTop + v * kListRow;
      const bool sel = rowOffset_ + v == selected, open = r.haveLevel && r.joined < r.players;
      if (sel) g.fill(left + 4, y + 1, right - left - 8, kListRow - 2, 0xd0600e, 0.92f);
      const uint32_t col = sel ? 0xffffff : open ? 0xffe48a : 0x8f8fb0;
      const char* source = r.source == pp::online::LobbySource::Local ? "LAN" : r.source == pp::online::LobbySource::Global ? "Global" : r.source == pp::online::LobbySource::Custom ? "Custom" : "Invite";
      const float base = y + 21;
      g.drawString("PLAINOUTLINE", cols[0], base, fitText(g, "PLAINOUTLINE", r.haveLevel ? cleanText(r.name, 40) : "level not installed", cols[1] - cols[0] - 12), col);
      g.drawString("PLAINOUTLINE", cols[1], base, fitText(g, "PLAINOUTLINE", r.haveLevel ? levelTitle(app, r.level) : cleanText(r.level, 64), cols[2] - cols[1] - 12), col);
      g.drawString("PLAINOUTLINE", cols[2] + 34, base, std::to_string(r.joined) + "/" + std::to_string(r.players), col, 1, Align::Center);
      g.drawString("PLAINOUTLINE", cols[3], base, source, col);
    }
    if (rooms_.empty()) consoleText(g, "OVERLOAD16OUTLINE", x + kListW / 2, kListTop + 100, "NO GAMES WERE FOUND", 0xffe48a);
    else if ((int)rooms_.size() > kListRows)
      g.drawString("PLAINOUTLINE", right - 8, kListY + 80, std::to_string(rowOffset_ + 1) + "-" + std::to_string(std::min(rowOffset_ + kListRows, (int)rooms_.size())) + " of " + std::to_string(rooms_.size()), 0xb6efff, 1, Align::Right);
    g.drawString("PLAINOUTLINE", x + kListW / 2, kListY + kListH - 18, fitText(g, "PLAINOUTLINE", status_, kListW - 60), statusColor(), 1, Align::Center);
  }

  void drawJoinPanel(Gfx& g, float crop) {
    const float x = kJoinX + crop;
    consolePanel(g, x, kJoinY, kJoinW, kJoinH, "ENTER A ROOM CODE");
    if (!held_ && statusKind_ == 0 && !job_) return;
    g.pushClip(x + 30, kJoinY + 318, kJoinW - 60, 52);
    g.drawWrapped("PLAINOUTLINE", {x + 30, kJoinY + 320, kJoinW - 60, 52}, status_, statusColor(), 1, Align::Center);
    g.popClip();
  }

  void drawHostPanel(Gfx& g, float crop) {
    const float x = kHostX + crop, cx = x + kHostW / 2;
    consolePanel(g, x, kHostY, kHostW, kHostH, party_ ? "PEG PARTY OPTIONS" : "DUEL OPTIONS");
    g.fill(cx - 100, kHostY + 62, 200, 112, 0x07062a);
    const float pickW = kHostW - 2 * 110;
    if (!levels_.empty()) {
      const LevelChoice& lc = levels_[levelIdx_];
      drawLevelPreview(g, lc.file, cx - 100, kHostY + 62, 200, 112);
      g.drawString("PLAINOUTLINE", cx, kHostY + 198, fitText(g, "PLAINOUTLINE", lc.title, kHostW - 80), 0xffe070, 1, Align::Center);
      g.drawString("PLAINOUTLINE", cx, kHostY + 218, fitText(g, "PLAINOUTLINE", lc.group, kHostW - 80), 0xc5d9ff, 1, Align::Center);
    }
    consoleText(g, "OVERLOAD16OUTLINE", cx, kHostY + 246, "MASTER", 0xb6efff);
    g.drawString("PLAINOUTLINE", cx, kHostY + 289, fitText(g, "PLAINOUTLINE", masterNameAt(app.game == Game::Nights, master_), pickW), 0xffe070, 1, Align::Center);
    const int nb = app.game == Game::Nights ? 11 : 10;
    if (const auto& mods = masters::all(); master_ >= nb && master_ - nb < (int)mods.size())
      g.drawString("PLAINOUTLINE", cx, kHostY + 309, fitText(g, "PLAINOUTLINE", "mod " + mods[(size_t)(master_ - nb)].modId, pickW), 0x88ff88, 1, Align::Center);
    if (held_ || statusKind_ != 0)
      g.drawString("PLAINOUTLINE", cx, kHostY + 420, fitText(g, "PLAINOUTLINE", status_, kHostW - 60), statusColor(), 1, Align::Center);
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

  void drawLevelPreview(Gfx& g, const std::string& level, float x, float y, float w, float h) {
    const std::string path = "levels/cached_thumbnails/" + level;
    if (Tex* thumb = g.img(path); thumb && thumb->valid()) {
      const float s = std::min(w / thumb->lw(), h / thumb->lh());
      const float tw = thumb->lw() * s, th = thumb->lh() * s;
      const float tx = x + (w - tw) / 2, ty = y + (h - th) / 2;
      g.drawStretch(thumb, tx, ty, tw, th);
      g.frame(tx - 1, ty - 1, tw + 2, th + 2, 0xdeb076);
    }
  }

  void drawCreate(Gfx& g) {
    const float labelX = kDlgX + kDlgPad;
    const float width = static_cast<float>(g.canvasW());
    const bool wide = width > 800;
    g.fill(0, 72, width, 528, 0x000000, 0.6f);
    g.pushOrigin((width - 800) / 2, 0);
    const float top = wide ? 72.0f : kDlgY;
    const float height = kDlgY + kDlgH - top + (wide ? kCreateWideDown + kCreateFooterGap : 0);
    g.fill(kDlgX, top, kDlgW, height, 0x0a1230, 1);
    panel(g, kDlgX, top, kDlgW, height, "Create a Room");
    if (wide && !levels_.empty()) drawLevelPreview(g, levels_[levelIdx_].file, kPickMid - 83, 106, 166, 96);
    g.pushOrigin(0, wide ? kCreateWideDown : 0);
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
    g.drawString("PLAIN", labelX, 344, "Lobby", 0xffffff, 1);
    g.drawWrapped("PLAIN", {labelX, 365, kDlgW - 2 * kDlgPad, 22}, "Friends join with a room code or invite. Start when everybody is ready.", 0xcccccc);
    g.popOrigin();
    g.popOrigin();
  }

  bool drawStageRoom(Gfx& g, float width) {
    const MatchInfo& mi = isHost_ ? hostInfo_ : sess_->info();
    auto art = [&](const char* name) -> Tex* {
      const auto path = std::string("console/lobby/") + name + ".png";
      return g.vfs().exists(path) ? g.img(path) : nullptr;
    };
    Tex* stage = art("stage");
    if (!stage) return false;
    Tex* marquee = art("marque");
    Tex* title = art(mi.party ? "party_title" : "duel_title");
    Tex* sidebar = art("sidebar");
    Tex* seats = art(mi.party ? "party_seats" : "duel_seats");
    Tex* badge = art("host_badge");
    Tex* rail = art("rail");
    if (!marquee || !title || !sidebar || !seats || !badge || !rail) return false;

    const Rect stageBounds = secondaryCover(*stage, width);
    g.drawStretch(stage, stageBounds.x, stageBounds.y, stageBounds.w, stageBounds.h);
    g.pushOrigin((width - 1067) / 2, 0);
    g.drawStretch(marquee, 179, -10, 720, 194);
    g.drawStretch(title, 373, -4, 333, 125);

    constexpr float sidebarX = 672, sidebarY = 0, sidebarScale = 0.81f;
    constexpr Rect opening{sidebarX + 32 * sidebarScale, sidebarY + 182 * sidebarScale, 244 * sidebarScale, 177 * sidebarScale};
    g.fill(opening.x, opening.y, opening.w, opening.h, 0x12102b);
    if (Tex* thumb = g.img("levels/cached_thumbnails/" + mi.level)) {
      const float scale = std::max(opening.w / thumb->lw(), opening.h / thumb->lh());
      const float w = thumb->lw() * scale, h = thumb->lh() * scale;
      g.pushClip(opening.x, opening.y, opening.w, opening.h);
      g.drawStretch(thumb, opening.x + (opening.w - w) / 2, opening.y + (opening.h - h) / 2, w, h);
      g.popClip();
    }
    g.drawStretch(sidebar, sidebarX, sidebarY, sidebar->lw() * sidebarScale, sidebar->lh() * sidebarScale);
    constexpr float sideCenter = sidebarX + 154 * sidebarScale;
    const std::string level = roomLevelTitle(mi.level);
    g.drawString("OVERLOAD16OUTLINE", sideCenter, 292, fitText(g, "OVERLOAD16OUTLINE", level, 181), 0xffffff, 1, Align::Center);
    g.drawString("PLAIN", sideCenter, 330, "ROOM CODE", 0xc5d9ff, 1, Align::Center);
    g.drawString("OVERLOAD24GRAD1", sideCenter, 363, roomCode_, 0xffffff, 1, Align::Center);

    const auto& roster = sess_->roster();
    constexpr float left = 187, seatW = 502;
    const float sourceH = seats->lh() / 2, seatH = mi.party ? 100.0f : sourceH * seatW / seats->lw();
    for (int i = 0; i < mi.players; ++i) {
      const float y = 137 + static_cast<float>(i) * (mi.party ? 92.0f : 149.0f);
      const bool connected = i < (int)roster.size() && roster[i].connected;
      const bool ready = connected && (roster[i].ready || (i == sess_->localPlayer() && readySent_));
      g.drawStretchSrc(seats, 0, i == 0 ? sourceH : 0, seats->lw(), sourceH, left, y, seatW, seatH);
      const std::string name = connected ? roster[i].name : i == 0 && isHost_ ? playerName() : "(open seat)";
      const float nameX = left + (mi.party ? 160 : 53), nameY = y + (mi.party ? 44 : 51);
      g.drawString("OVERLOAD16OUTLINE", nameX, nameY, fitText(g, "OVERLOAD16OUTLINE", name, left + 407 - nameX),
                   connected || i == 0 ? 0xffffff : 0xc0c8e3);
      const char* state = i == 0 ? "HOST" : !connected ? "OPEN" : ready ? "READY" : "NOT READY";
      g.drawString("PLAINOUTLINE", left + 93, y + (mi.party ? 47 : 108), state,
                   ready || i == 0 ? 0x9cffb7 : 0xffdfae, 1, Align::Center);
      if (i == 0) g.drawStretch(badge, left + 421, y + 10, 49, 49);
      g.drawString("PLAINOUTLINE", left + 388, y + (mi.party ? 82.0f : 132.0f), "Seat " + std::to_string(i + 1), 0xffffff, 1, Align::Center);
      if (!masters::all().empty()) {
        const auto master = masterNameForPower(mi.master[mi.party ? 0 : i]);
        const float masterX = left + (mi.party ? 320.0f : 221.0f);
        g.drawString("PLAINOUTLINE", masterX, y + (mi.party ? 82.0f : 131.0f), fitText(g, "PLAINOUTLINE", master, mi.party ? 59.0f : 90.0f), 0xc5d9ff, 1, Align::Center);
      }
    }

    g.drawString("PLAIN", sideCenter, 426, std::to_string(mi.players) + " seats", 0xc5d9ff, 1, Align::Center);
    const std::string link = linkText(sess_->linkNote(), true);
    if (!link.empty()) {
      g.pushClip(sideCenter - 78, 434, 156, 89);
      g.drawWrapped("PLAIN", {sideCenter - 78, 434, 156, 89}, link, 0xffffff, 1, Align::Center);
      g.popClip();
    }

    constexpr float messageX = left + 20, messageW = 449, messageCenter = messageX + messageW / 2;
    const char* hint = isHost_ ? (sess_->allReady() ? (flow_ != Flow::Browse ? "Everybody is ready - press Start Game." : "Everybody is ready - press Start Match.") : "Waiting for players to join and press Ready...")
                              : (readySent_ ? "Waiting for the host to start..." : guestChecked_ ? "Press Ready when you are." : "Checking the host's game files...");
    if (flow_ != Flow::Browse) {
      g.drawString("PLAINOUTLINE", messageCenter, 527, fitText(g, "PLAINOUTLINE", status_.empty() ? hint : status_, messageW - 18),
                   status_.empty() ? 0xffffff : statusColor(), 1, Align::Center);
    } else {
      const std::string invite = makeInvite(lobbyHost_, lobbyPort_, roomCode_);
      g.drawString("PLAINOUTLINE", messageCenter, 518, fitText(g, "PLAINOUTLINE", status_.empty() ? invite : status_, messageW - 18),
                   statusKind_ == 1 ? 0xff9090 : 0xc5d9ff, 1, Align::Center);
      g.drawString("PLAINOUTLINE", messageCenter, 535, hint, 0xffffff, 1, Align::Center);
    }
    g.popOrigin();
    g.drawStretch(rail, stageBounds.x, 537, stageBounds.w, 63);
    return true;
  }

  void drawRoom(Gfx& g) {
    const MatchInfo& mi = isHost_ ? hostInfo_ : sess_->info();
    const float width = static_cast<float>(g.canvasW());
    const bool wide = width > 800;
    const float rosterLeft = wide ? 84.0f : 40.0f, rosterRight = wide ? width - 318 : 760;
    const float center = (rosterLeft + rosterRight) / 2, bodyW = rosterRight - rosterLeft - 32;
    panel(g, rosterLeft, 100, rosterRight - rosterLeft, wide ? 117.0f : 400.0f, isHost_ ? "Your Room" : "Room " + roomCode_);
    g.drawString("OVERLOAD24GRAD1", center, wide ? 151.0f : 160.0f, roomCode_, 0xffffff, 1, Align::Center);
    std::string inv = makeInvite(lobbyHost_, lobbyPort_, roomCode_);
    g.drawWrapped("PLAIN", {wide ? rosterLeft + 16 : 60.0f, wide ? 157.0f : 176.0f, wide ? bodyW : 680.0f, 30}, inv, 0x9fb8ff, 1, Align::Center);
    std::string what = std::string(mi.party ? "Peg Party" : "Duel") + " - " + std::to_string(mi.players) + " players - level " + roomLevelTitle(mi.level);
    g.drawString("PLAIN", center, wide ? 203.0f : 222.0f, fitText(g, "PLAIN", what, wide ? bodyW : 680.0f), 0xffe070, 1, Align::Center);
    if (wide) {
      const float left = rosterRight + 24;
      panel(g, left, 100, 228, 232, "Selected Level");
      drawLevelPreview(g, mi.level, left + 17, 132, 194, 156);
      g.drawString("PLAIN", left + 114, 308, fitText(g, "PLAIN", roomLevelTitle(mi.level), 198), 0xffe070, 1, Align::Center);
    }

    const auto& roster = sess_->roster();
    const float rowY = wide ? 228.0f : 252.0f, rowStep = wide ? 49.0f : 34.0f;
    for (int i = 0; i < mi.players; i++) {
      const float y = rowY + i * rowStep, textY = y + (wide ? 26 : 19);
      if (wide) secondaryPanel(g, rosterLeft, y, rosterRight - rosterLeft, 39, 33);
      else g.fill(120, y, rosterRight - 200, 28, 0xffffff, 0.08f);
      bool known = i < (int)roster.size() && roster[i].connected;
      std::string nm = known ? roster[i].name : "(open seat)";
      if (i == 0 && !known && isHost_) nm = playerName();
      const float nameX = wide ? rosterLeft + 82 : 220.0f, masterX = wide ? rosterRight - 228 : 440.0f;
      g.drawString("PLAIN", wide ? rosterLeft + 16 : 132.0f, textY, "Seat " + std::to_string(i + 1), 0xaaaaff);
      g.drawString("PLAIN", nameX, textY, wide ? fitText(g, "PLAIN", nm, masterX - nameX - 12) : nm,
                   known || (i == 0 && isHost_) ? 0xffffff : wide ? 0xb3bce0 : 0x777777);
      if (!masters::all().empty() && i < 4) {
        const std::string name = masterNameForPower(mi.master[mi.party ? 0 : i]).substr(0, 18);
        g.drawString("PLAIN", masterX, textY, wide ? fitText(g, "PLAIN", name, 132) : name, 0xc0d0ff);
      }
      std::string st = i == 0 ? "host" : !known ? "waiting" : roster[i].ready ? "ready" : "not ready";
      g.drawString("PLAIN", rosterRight - (wide ? 16 : 92), textY, st, st == "ready" || st == "host" ? 0x90ff90 : 0xffe070, 1, Align::Right);
    }
    std::string link = linkText(sess_->linkNote(), false);
    if (wide) panel(g, rosterLeft, 438, rosterRight - rosterLeft, 57, "");
    if (!link.empty()) g.drawWrapped("PLAIN", {wide ? rosterLeft + 16 : 60.0f, wide ? 445.0f : rowY + mi.players * rowStep + 6, wide ? bodyW : 680.0f, 30}, link, 0xaaaaaa, 1, Align::Center);
    std::string hint = isHost_ ? (sess_->allReady() ? "Everybody is ready - press Start Match." : "Waiting for the other players to join and press Ready...")
                               : (readySent_ ? "Waiting for the host to start..." : guestChecked_ ? "Press Ready when you are." : "Checking the host's game files...");
    g.drawString("PLAIN", center, 480, hint, 0xffffff, 1, Align::Center);
    drawStatus(g, 40, wide ? 506.0f : 556.0f);
  }

 private:
  Checkbox* discordCheck_ = nullptr;
  Flow flow_ = Flow::Browse;
  bool party_ = false;
  std::set<std::string> tried_;
  struct Prompt {
    float x, w;
    int key;
  };
  std::vector<Prompt> prompts_;
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
  int rowOffset_ = 0;
  bool listStarted_ = false;
  pp::online::RoomBrowser browser_{Session::listRooms};
  pp::online::BrowseSnapshot browse_;
  std::string configuredLobby_, invitedAddress_, selectedAddress_, selectedCode_, sourceLine_;
  std::vector<pp::online::LobbyEndpoint> hostTargets_;
  size_t hostTarget_ = 0;
  std::vector<RoomRow> rooms_;
  std::string status_, listLine_, jobAddr_, codeSeen_;
  int statusKind_ = 0, listKind_ = 0, heldAt_ = 0, noticeAt_ = 0, seated_ = 0;
  bool listKnown_ = false, heldTimed_ = false;
  std::unique_ptr<Job> job_;
  enum JobKind { None, Host, Join, Probe, Register } jobKind_ = None;
  bool held_ = false;
  std::unique_ptr<Session> sess_;
  std::string lobbyHost_, roomCode_;
  int lobbyPort_ = 7770;
  LevelBundle lb_;
  MatchInfo hostInfo_;
  EditBox* codeEdit_;
  std::array<float, E_Code + 1> baseX_{}, baseY_{};
  Button *bRefresh_, *bJoin_, *bPaste_, *bCreate_, *bTest_, *bRegister_, *bBack_, *bCopy_, *bReady_, *bStart_, *bLeave_, *bMode_, *bLvlPrev_, *bLvlNext_, *bMasPrev_, *bMasNext_, *bGo_, *bCancel_, *bDestination_;
};

}

void showOnline(App& app) {
  if (app.consoleCanvasWidth() >= 960) app.showMainMenu();
  else app.setScreen(std::make_unique<OnlineScreen>(app, std::string()), App::Left);
}

void showOnline(App& app, Flow flow, bool party) {
  app.setScreen(std::make_unique<OnlineScreen>(app, std::string(), false, flow, party), App::Left);
}

void addMainMenuButton(Panel& menu) {
  Button* b = menu.patchButton(kMenuButtonId, "Online", "IMAGE_DLG_BROWNBUTTON1", 0x1a1, 0x1fe);
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
  if (app.moving() || app.anyDialog()) return;
  if (!g_pendingInvite.empty()) {
    std::string invite = std::move(g_pendingInvite);
    g_pendingInvite.clear();
    app.setScreen(std::make_unique<OnlineScreen>(app, invite), App::Left);
    return;
  }
  std::string discordInvite = app.takeDiscordInvite();
  if (!discordInvite.empty()) {
    app.setScreen(std::make_unique<OnlineScreen>(app, discordInvite, true), App::Left);
    return;
  }
  std::string invite;
  if (pp::online::steam::takeJoinRequest(&invite)) app.setScreen(std::make_unique<OnlineScreen>(app, invite), App::Left);
}

}
