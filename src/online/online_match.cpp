#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <utility>
#include <memory>
#include <string>
#include <cctype>

#include "audio.h"
#include "log.h"
#include "online/content_hash.h"
#include "online/online_ui.h"
#include "render.h"

namespace pp::ui::online {

namespace {


class OnlineGameScreen : public Screen {
 public:
  OnlineGameScreen(App& a, MatchLaunch&& L) : Screen(a), net_(std::move(L.session)), lb_(std::move(L.lb)), info_(L.info), lobbyHost_(L.lobbyHost) {
    local_ = net_->localPlayer();
    bRematch_ = patchButton(1, "Rematch", "IMAGE_DLG_BUTTON3", 0, 0);
    bLeave_ = patchButton(2, "Leave", "IMAGE_DLG_BUTTON3", 0, 0);
    bKeep_ = patchButton(3, "Keep Playing", "IMAGE_DLG_BUTTON3", 0, 0);
    for (Button* b : {bRematch_, bLeave_, bKeep_}) { b->w = 150; b->visible = false; }
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
    match_ = Match();
    std::string err;
    ready_ = match_.init(lb_.base, lb_.level, cfg, info_.players, info_.party, &err);
    if (!ready_) {
      loadError_ = err;
      pp::log::error("net", "cannot start the online match on level " + lb_.name + ": " + err);
    } else {
      pp::log::info("net", "online match started on level " + lb_.name + " (" + std::to_string(info_.players) + " player(s), seed " + std::to_string(seed) + ")");
    }
    state_ = Playing;
    wantFire_ = false;
    rematchAsked_ = false;
    wheelNudge_ = 0;
  }

  std::string rawName(int p) const {
    const auto& r = net_->roster();
    return p < (int)r.size() && !r[p].name.empty() ? r[p].name : "Player " + std::to_string(p + 1);
  }
  std::string nameOf(int p) const { return p == local_ ? rawName(p) + " (you)" : rawName(p); }
  int scoreOf(int p) const {
    const Board& b = match_.boards[match_.boardOf(p)];
    return b.score[match_.playerSlot(p)];
  }

  void update() override {
    Panel::update();
    if (!ready_) return;
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
      if (state_ == Over) peerGone_ = true;
      else { state_ = Lost; message_ = net_->error; if (!message_.empty()) message_[0] = (char)std::toupper((unsigned char)message_[0]); }
    }
    if (net_->desynced() && state_ == Playing) { state_ = Desynced; message_ = net_->desyncMsg; }
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
    bRematch_->visible = state_ == Over && !peerGone_;
    bRematch_->enabled = !rematchAsked_;
    bRematch_->label = rematchAsked_ ? "Waiting..." : "Rematch";
    bLeave_->visible = state_ == Over || state_ == Lost || state_ == Desynced || menu_;
    bKeep_->visible = menu_ && state_ == Playing;
    float cx = canvasWidth() / 2.0f;
    bRematch_->x = cx - 160; bLeave_->x = cx + 10; bRematch_->y = bLeave_->y = 340;
    if (menu_ && state_ == Playing) { bLeave_->x = cx - 160; bKeep_->x = cx + 10; bLeave_->y = bKeep_->y = 340; }
    if (state_ == Lost || state_ == Desynced || peerGone_) bLeave_->x = cx - 75;
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

  void drawLayer(Gfx& g) override {
    float cw = (float)canvasWidth();
    if (!ready_) {
      g.fill(0, 0, cw, 600, 0x101030);
      g.drawString("PLAIN", cw / 2, 300, "Could not start the match: " + loadError_, 0xff8080, 1, Align::Center);
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
    char st[200];
    std::snprintf(st, sizeof st, "%s | you are %s | %s | checked by host: %d | rollbacks: %d", info_.party ? "Peg Party" : "Online Duel", rawName(local_).c_str(),
                  net_->linkNote().c_str(), net_->hashesCompared, match_.rollbacks);
    g.fill(0, 584, cw, 16, 0x000000, 0.55f);
    g.drawString("PLAIN", 6, 596, st, 0xdddddd, 1, Align::Left);
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
    g.fill(0, 0, cw, 600, 0x000000, 0.55f);
    float pw = 480, ph = 250, px = (cw - pw) / 2, py = 170;
    g.fill(px, py, pw, ph, 0x101830, 0.92f);
    g.frame(px, py, pw, ph, 0xffd605, 0.85f);
    std::string title, body;
    uint32_t tcol = 0xffd605;
    if (state_ == Lost) {
      title = "Disconnected";
      body = message_.empty() ? "The connection was lost." : message_;
      tcol = 0xff8080;
    } else if (state_ == Desynced) {
      title = "Desync detected";
      body = message_ + "\nThe two games no longer agree, so the match cannot continue. Please report this with the level and both game versions.";
      tcol = 0xff8080;
    } else if (menu_) {
      title = "Leave the match?";
      body = "Leaving ends the game for everybody in the room.";
    } else {
      int best = -1, bestScore = -1;
      bool tie = false;
      for (int p = 0; p < info_.players; p++) {
        int s = scoreOf(p);
        if (s > bestScore) { bestScore = s; best = p; tie = false; }
        else if (s == bestScore) tie = true;
      }
      title = tie ? "It's a Tie!" : best == local_ ? "You Win!" : nameOf(best) + " Wins!";
      for (int p = 0; p < info_.players; p++) body += nameOf(p) + ":  " + std::to_string(scoreOf(p)) + "\n";
      if (peerGone_) { std::string e = net_->error; if (!e.empty()) e[0] = (char)std::toupper((unsigned char)e[0]); body += "\n" + e; }
      else if (rematchAsked_) body += "\nWaiting for the others to accept a rematch (" + std::to_string(net_->rematchVotes()) + "/" + std::to_string(info_.players) + ")";
    }
    g.drawString("OVERLOAD24GRAD1", cw / 2, py + 42, title, tcol, 1, Align::Center);
    g.drawWrapped("PLAIN2", {px + 24, py + 62, pw - 48, 120}, body, 0xffffff, 1, Align::Center);
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
  std::string loadError_, message_;
  Button *bRematch_, *bLeave_, *bKeep_;
};

}

void startMatchScreen(App& app, MatchLaunch&& launch) { app.setScreen(std::make_unique<OnlineGameScreen>(app, std::move(launch)), App::Left); }

static const char* kMasters[] = {"Bjorn", "Jimmy", "Kat Tut", "Splork", "Claude", "Renfield", "Tula", "Warren", "Lord Cinderbottom", "Master Hu", "Marina"};
static const char* kKeys[] = {"bjorn", "jimmy", "tut", "splork", "claude", "renfield", "tula", "warren", "cinder", "hu", "marina"};
const char* masterDisplayName(int i) { return kMasters[std::clamp(i, 0, 10)]; }
const char* masterKey(int i) { return kKeys[std::clamp(i, 0, 10)]; }

}
