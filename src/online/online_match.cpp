#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <utility>
#include <memory>
#include <string>
#include <vector>

#include "audio.h"
#include "log.h"
#include "online/content_hash.h"
#include "online/online_ui.h"
#include "render.h"

namespace pp::ui::online {

namespace {

constexpr const char* kCardFont = "PLAIN2";
constexpr float kCardW = 480, kCardPad = 24, kCardBodyTop = 62;

class OnlineGameScreen : public Screen {
 public:
  OnlineGameScreen(App& a, MatchLaunch&& L) : Screen(a), net_(std::move(L.session)), lb_(std::move(L.lb)), info_(L.info), lobbyHost_(L.lobbyHost) {
    local_ = net_->localPlayer();
    bRematch_ = patchButton(1, "Rematch", "IMAGE_DLG_BUTTON3", 0, 0);
    bLeave_ = patchButton(2, "Leave", "IMAGE_DLG_BUTTON3", 0, 0);
    bKeep_ = patchButton(3, "Keep Playing", "IMAGE_DLG_BUTTON3", 0, 0);
    for (Button* b : {bRematch_, bLeave_, bKeep_}) { b->w = std::max(150.0f, b->image ? 2.0f * std::floor(b->image->lw() / 3.0f) : 0.0f); b->visible = false; }
    startMatch(info_.seed);
    app.playMusic("level");
  }

  const char* name() const override { return "onlinematch"; }
  bool isGame() const override { return true; }
  int canvasWidth() const override { return ready_ ? 800 + (match_.boards[0].W - 650) : 800; }

  void startMatch(uint32_t seed) {
    BoardConfig cfg = lb_.cfg;
    cfg.seed = seed;
    cfg.boardWidth = info_.boardWidth;
    for (int i = 0; i < 4; i++) cfg.master[i] = info_.master[i];
    if (app.audio) app.audio->dropQueued();
    match_ = Match();
    std::string err;
    ready_ = match_.init(lb_.base, lb_.level, cfg, info_.players, info_.party, &err);
    if (!ready_) {
      pp::log::error("net", "cannot start the online match on the level " + levelLogName(app, lb_.name) + ": " + err);
    } else {
      pp::log::info("net", "online match started on the level " + levelLogName(app, lb_.name) + ": " + std::to_string(info_.players) + " player(s), seed " + std::to_string(seed));
    }
    state_ = Playing;
    wantFire_ = false;
    rematchAsked_ = false;
    wheelNudge_ = 0;
    statusKey_.clear();
    if (ready_) noteStatus();
  }

  void noteStatus() {
    const std::string note = net_->linkNote();
    const std::string checked = std::to_string(net_->hashesCompared), rollbacks = std::to_string(match_.rollbacks);
    const std::string key = (note.rfind("Direct UDP link", 0) == 0 ? std::string("Direct UDP link") : note) + "|" + checked + "|" + rollbacks;
    if (key == statusKey_) return;
    statusKey_ = key;
    std::string line = std::string(info_.party ? "Peg Party" : "Online Duel") + " | you are " + rawName(local_);
    if (!note.empty()) line += " | " + linkText(note, false);
    pp::log::info("net", line + " | checked by host: " + checked + " | rollbacks: " + rollbacks);
  }

  std::string rawName(int p) const {
    const auto& r = net_->roster();
    return p < (int)r.size() && !r[p].name.empty() ? r[p].name : "Player " + std::to_string(p + 1);
  }
  std::string nameOf(int p) const { return p == local_ ? rawName(p) + " (you)" : rawName(p); }
  std::string shownName(int p) const { return drawableText(nameOf(p)); }
  int scoreOf(int p) const {
    const Board& b = match_.boards[match_.boardOf(p)];
    return b.score[match_.playerSlot(p)];
  }

  struct Card {
    bool shown = false;
    std::string title, body;
    uint32_t color = 0xffd605;
  };

  Card card() const {
    Card c;
    if (!ready_) {
      c.title = "No match";
      c.body = "The match could not start on this level. Press F10 for the details.";
      c.color = 0xff8080;
    } else if (state_ == Lost) {
      c.title = "Disconnected";
      c.body = message_;
      c.color = 0xff8080;
    } else if (state_ == Desynced) {
      c.title = "Desync detected";
      c.body = "The two games no longer agree, so the match cannot continue. Please report this with the level, both game versions and the log (F10).";
      c.color = 0xff8080;
    } else if (state_ == Playing && menu_) {
      c.title = "Leave the match?";
      c.body = "Leaving ends the game for everybody in the room.";
    } else if (state_ == Over) {
      int best = -1, bestScore = -1;
      bool tie = false;
      for (int p = 0; p < info_.players; p++) {
        int s = scoreOf(p);
        if (s > bestScore) { bestScore = s; best = p; tie = false; }
        else if (s == bestScore) tie = true;
      }
      c.title = tie ? "It's a Tie!" : best == local_ ? "You Win!" : shownName(best) + " Wins!";
      for (int p = 0; p < info_.players; p++) c.body += shownName(p) + ":  " + std::to_string(scoreOf(p)) + "\n";
      if (peerGone_) c.body += "\n" + message_;
      else if (rematchAsked_) c.body += "\nWaiting for the others to accept a rematch (" + std::to_string(net_->rematchVotes()) + "/" + std::to_string(info_.players) + ")";
    } else {
      return c;
    }
    c.shown = true;
    return c;
  }

  void arrange() {
    card_ = card();
    const bool over = ready_ && state_ == Over, asking = ready_ && state_ == Playing && menu_;
    bRematch_->visible = over && !peerGone_;
    bRematch_->enabled = !rematchAsked_;
    bRematch_->label = rematchAsked_ ? "Waiting..." : "Rematch";
    bLeave_->visible = card_.shown;
    bKeep_->visible = asking;
    if (!card_.shown) return;
    Gfx& g = app.gfx;
    const float cx = (float)canvasWidth() / 2.0f;
    const float bodyH = g.fontLineSpacing(kCardFont) * (float)g.wrap(kCardFont, kCardW - 2 * kCardPad, card_.body).size();
    const float buttonTop = std::max(170.0f, kCardBodyTop + bodyH + 12);
    cardH_ = std::max(250.0f, buttonTop + bLeave_->h + 24);
    cardY_ = cardH_ > 250 ? std::max(8.0f, (600 - cardH_) / 2) : 170.0f;
    bRematch_->x = cx - 10 - bRematch_->w;
    bLeave_->x = cx + 10;
    if (asking) { bLeave_->x = cx - 10 - bLeave_->w; bKeep_->x = cx + 10; }
    else if (!bRematch_->visible) bLeave_->x = cx - bLeave_->w / 2;
    bRematch_->y = bLeave_->y = bKeep_->y = cardY_ + buttonTop;
  }

  void fail() {
    if (!message_.empty()) return;
    bool known = true;
    message_ = troubleText(net_->error, Where::Match, net_->isHost(), std::string(), std::string(), &known);
    if (!known) pp::log::warn("net", "no plain message for this answer: " + cleanText(net_->error, 240));
  }

  void update() override {
    Panel::update();
    if (!ready_) { arrange(); return; }
    net_->poll();
    for (auto& s : net_->takeShots())
      if (s.player != local_) match_.submit(s);
    uint32_t seed;
    if (net_->takeRematch(&seed) && !peerGone_) {
      info_.seed = seed;
      startMatch(seed);
      return;
    }
    if (net_->failed() && state_ != Lost) {
      fail();
      if (state_ == Over) peerGone_ = true;
      else state_ = Lost;
    }
    if (net_->desynced() && state_ == Playing) state_ = Desynced;
    if (state_ != Playing && app.audio) app.audio->stopLoops();
    if (state_ == Playing || state_ == Over) {
      Board& vb = match_.boards[match_.boardOf(local_)];
      if (state_ == Playing) {
        float aim = Board::clampAngle(vb.aimAt(V2{mx_, my_}) + wheelNudge_);
        if (match_.canFire(local_)) {
          vb.gunTarget = aim;
          if (wantFire_) {
            ShotCmd c{0, local_, match_.boardOf(local_), aim, vb.tick};
            match_.submit(c);
            net_->sendShot(c);
          }
        }
        wantFire_ = false;
        match_.step();
        if (app.audio) app.audio->tick();
        for (auto& r : match_.drainResolved()) net_->reportHash(r.board, r.shotIndex, r.hash);
        if (app.audio) {
          app.audio->onEvents(vb.events, vb);
          if (app.boardRenderer) app.audio->playFx(app.boardRenderer->takeSounds());
        }
        for (auto& bd : match_.boards) bd.events.clear();
        if (match_.over()) state_ = Over;
      }
    }
    noteStatus();
    arrange();
  }

  void onButton(int id) override {
    if (id == 1 && !rematchAsked_) { rematchAsked_ = true; net_->requestRematch(); }
    else if (id == 2) { net_->leave(); showOnline(app); }
    else if (id == 3) menu_ = false;
  }

  bool input(const Input& in) override {
    switch (in.type) {
      case Input::MouseMove:
        mx_ = std::floor(in.x) - 75;
        my_ = std::floor(in.y) - 40;
        wheelNudge_ = 0;
        break;
      case Input::Wheel: wheelNudge_ += in.wheel * 0.1f * 3.14159265f / 180.0f; break;
      case Input::KeyDown:
        if (in.key == 27   && state_ == Playing) menu_ = !menu_;
        else if ((in.key == ' ' || in.key == 13) && state_ == Playing && !menu_) wantFire_ = true;
        break;
      default: break;
    }
    bool used = Panel::input(in);
    if (in.type == Input::MouseDown && in.button == 1 && !used && state_ == Playing && !menu_) {
      mx_ = std::floor(in.x) - 75;
      my_ = std::floor(in.y) - 40;
      wantFire_ = true;
    }
    return used;
  }

  void lostFocus() override {}

  void drawCard(Gfx& g, float cw) {
    const float px = (cw - kCardW) / 2;
    g.fill(0, 0, cw, 600, 0x000000, 0.55f);
    g.fill(px, cardY_, kCardW, cardH_, 0x101830, 0.92f);
    g.frame(px, cardY_, kCardW, cardH_, 0xffd605, 0.85f);
    g.drawString("OVERLOAD24GRAD1", cw / 2, cardY_ + 42, card_.title, card_.color, 1, Align::Center);
    g.drawWrapped(kCardFont, {px + kCardPad, cardY_ + kCardBodyTop, kCardW - 2 * kCardPad, cardH_ - kCardBodyTop}, card_.body, 0xffffff, 1, Align::Center);
  }

  void drawLayer(Gfx& g) override {
    float cw = (float)canvasWidth();
    if (!ready_) {
      g.fill(0, 0, cw, 600, 0x101030);
      drawCard(g, cw);
      return;
    }
    if (app.boardRenderer) {
      const Board& vb = match_.boards[match_.boardOf(local_)];
      Hud hud;
      hud.title = lb_.title;
      hud.showGuide = true;
      if (!info_.party && info_.players == 2) {
        hud.mode = Hud::ModeDuel;
        hud.challengeText = nameOf(vb.cur);
        hud.topName = nameOf(1 - vb.cur);
        hud.topScore = vb.score[1 - vb.cur];
      } else {
        hud.mode = Hud::ModeQuick;
        int best = 0, who = -1;
        for (int p = 0; p < info_.players; p++)
          if (p != local_ && (who < 0 || scoreOf(p) > best)) { best = scoreOf(p); who = p; }
        if (who >= 0) { hud.topScore = best; hud.topName = nameOf(who); }
      }
      app.boardRenderer->draw(vb, lb_, hud);
    }
    if (info_.party) {
      float y = 104;
      for (int p = 0; p < info_.players; p++) {
        char row[96];
        std::snprintf(row, sizeof row, "%s: %d", nameOf(p).c_str(), scoreOf(p));
        g.drawString("PLAIN", 6, y, row, p == local_ ? 0xffff88 : 0xffffff, 0.9f, Align::Left);
        y += 14;
      }
    }
    if (state_ == Playing && !menu_) {
      bool turn = match_.canFire(local_);
      if (!info_.party && !turn && !match_.boards[0].result.finished) {
        std::string t = "Waiting for " + nameOf(match_.boards[0].cur) + "...";
        g.fill(0, 548, cw, 22, 0x000000, 0.45f);
        g.drawString("OVERLOAD16OUTLINE", cw / 2, 565, t, 0xffe070, 0.95f, Align::Center);
      }
      return;
    }
    if (card_.shown) drawCard(g, cw);
  }

 private:
  enum State { Playing, Over, Lost, Desynced } state_ = Playing;
  std::unique_ptr<Session> net_;
  LevelBundle lb_;
  MatchInfo info_;
  std::string lobbyHost_;
  Match match_;
  int local_ = 0;
  bool ready_ = false, wantFire_ = false, menu_ = false, rematchAsked_ = false, peerGone_ = false;
  float mx_ = 325, my_ = 300, wheelNudge_ = 0;
  std::string message_, statusKey_;
  Card card_;
  float cardY_ = 170, cardH_ = 250;
  Button *bRematch_, *bLeave_, *bKeep_;
};

}

void startMatchScreen(App& app, MatchLaunch&& launch) { app.setScreen(std::make_unique<OnlineGameScreen>(app, std::move(launch)), App::Left); }

static const char* kMasters[] = {"Bjorn", "Jimmy", "Kat Tut", "Splork", "Claude", "Renfield", "Tula", "Warren", "Lord Cinderbottom", "Master Hu", "Marina"};
static const char* kKeys[] = {"bjorn", "jimmy", "tut", "splork", "claude", "renfield", "tula", "warren", "cinder", "hu", "marina"};
const char* masterDisplayName(int i) { return kMasters[std::clamp(i, 0, 10)]; }
const char* masterKey(int i) { return kKeys[std::clamp(i, 0, 10)]; }

int masterTotal(bool nights) { return (nights ? 11 : 10) + (int)masters::all().size(); }

std::string masterNameAt(bool nights, int i) {
  const int nb = nights ? 11 : 10;
  const auto& mods = masters::all();
  if (i >= nb && i - nb < (int)mods.size()) return mods[(size_t)(i - nb)].name;
  return masterDisplayName(i);
}

std::string masterKeyAt(bool nights, int i) {
  const int nb = nights ? 11 : 10;
  const auto& mods = masters::all();
  if (i >= nb && i - nb < (int)mods.size()) return mods[(size_t)(i - nb)].key();
  return masterKey(i);
}

std::string masterNameForPower(int power) {
  if (const ModMaster* m = masters::byPower(power)) return m->name;
  for (int i = 0; i < 11; i++)
    if (Board::masterByName(kKeys[i], false) == power) return kMasters[i];
  return std::string();
}

}
