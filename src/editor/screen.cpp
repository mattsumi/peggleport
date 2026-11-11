#include <SDL3/SDL.h>
#include <cstdint>
#include <cstring>
#include <utility>
#include <memory>
#include <vector>
#include <string>
#include <cstdlib>
#include <cctype>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

#include "audio.h"
#include "editor/editor.h"
#include "render.h"
#include "ui/app.h"
#include "ui/hooks.h"

namespace pp::ui {
using namespace pp::ed;
namespace fs = std::filesystem;

namespace {
constexpr float CW = 1440, CH = 810, TOP = 36, LEFTW = 168, RIGHTW = 300, BOTH = 26;
constexpr uint32_t kBg = 0x0a0c1c, kPanel = 0x151a3a, kPanel2 = 0x1d2350, kLine = 0x5a6cb4, kHot = 0x3a5cc8, kText = 0xe8ecff, kDim = 0x8a94c8,
                   kGold = 0xffd860, kRed = 0xff6a5a, kCyan = 0x38e4ff;

struct Ctl {
  Rect r;
  std::function<void(int)> click;
  std::function<void(int)> wheel;
  std::string tip;
};

uint32_t pegColor(int t) {
  switch (t) {
    case 1: return 0x3d8bff;
    case 2: return 0xff8a1c;
    case 3: return 0xc060ff;
    case 4: return 0x3fe03f;
    default: return 0xcccccc;
  }
}
}

class EditorScreen : public Screen {
 public:
  EditorScreen(App& a, const std::string& level);
  ~EditorScreen() override;
  const char* name() const override { return "editor"; }
  int canvasWidth() const override { return (int)CW; }
  int canvasHeight() const override { return (int)CH; }
  void update() override;
  void drawLayer(Gfx& g) override;
  bool input(const Input& in) override;

  Editor ed;

 private:
  float zoom_ = 1, panX_ = 0, panY_ = 0;
  Rect cv_{LEFTW, TOP, CW - LEFTW - RIGHTW, CH - TOP - BOTH};
  bool dr_ = true, layoutDirty_ = true, showGrid_ = true, animate_ = false, spaceDown_ = false, panning_ = false, canvasDrag_ = false;
  Gfx* g_ = nullptr;
  float asc_ = 12, fh_ = 14;
  std::vector<Ctl> ctls_;
  std::string msg_;
  int msgAge_ = 0, ticks_ = 0;
  Mods mods_;
  float lastX_ = 0, lastY_ = 0, panStartX_ = 0, panStartY_ = 0, panOrgX_ = 0, panOrgY_ = 0;
  int tab_ = 0;
  float scroll_[5]{};
  std::string modName_ = "MyLevels";
  bool exitArmed_ = false;
  struct Prompt {
    bool on = false;
    std::string label, text;
    std::function<void(const std::string&)> done;
  } prompt_;
  bool openDlg_ = false;
  bool artDlg_ = false;
  Editor::ArtSlot artSlot_ = Editor::ArtSlot::Background;
  std::vector<Editor::ArtItem> artList_;
  std::string artFilter_;
  float artScroll_ = 0;
  std::string artHover_;
  void openArtDialog(Editor::ArtSlot s);
  void drawArtDialog();
  void chooseArt(const Editor::ArtItem* it);
  void syncModArt();
  std::string artCurrent() const;
  std::string openFilter_;
  std::vector<std::pair<std::string, std::string>> levelList_;
  float openScroll_ = 0;
  int genKind_ = 0;
  float genArg_[4][6] = {{5, 4, 40, 40, 200, 150}, {12, 80, 325, 250, 0, 0}, {24, 20, 100, 2, 325, 250}, {12, 400, 40, 2, 125, 300}};
  int genColor_ = 1;
  Board board_;
  LevelBundle lb_;
  std::vector<NodeP> objNode_;
  int builtRev_ = -1;
  bool boardOk_ = false;
  std::string boardErr_;
  SDL_Texture* target_ = nullptr;
  int targetW_ = 0, targetH_ = 0;
  bool playing_ = false;
  Board play_;
  LevelBundle playLb_;
  bool playOk_ = false, playGuide_ = true;
  int playMouseX_ = 325, playMouseY_ = 200;
  uint32_t playSeed_ = 1;

  void R(float x, float y, float w, float h, uint32_t c, float a = 1) { if (dr_) g_->fill(x, y, w, h, c, a); }
  void F(float x, float y, float w, float h, uint32_t c, float a = 1) { if (dr_) g_->frame(x, y, w, h, c, a); }
  void T(float x, float y, const std::string& s, uint32_t c = kText, Align al = Align::Left) { if (dr_) g_->drawString("PLAIN", x, y + asc_, s, c, 1, al); }
  float TW(const std::string& s) { return g_->textWidth("PLAIN", s); }
  bool hovered(const Rect& r) const { return r.contains(app.mouseX, app.mouseY); }
  void button(Rect r, const std::string& label, std::function<void()> fn, bool active = false, bool enabled = true, uint32_t swatch = 0);
  void say(const std::string& m) { msg_ = m; msgAge_ = 0; }
  void dirtyLayout() { layoutDirty_ = true; }

  V2 toLevel(float cx, float cy) const { return V2((cx - cv_.x - panX_) / zoom_ - ed.wideShift(), (cy - cv_.y - panY_) / zoom_); }
  V2 toCanvas(V2 p) const { return V2(cv_.x + panX_ + (p.x + ed.wideShift()) * zoom_, cv_.y + panY_ + p.y * zoom_); }
  void fitView();
  void zoomAt(float cx, float cy, float factor);
  void centerOn(V2 p);

  void buildUi(Gfx& g, bool draw);
  void drawCanvas();
  void drawBoardTexture();
  void drawOverlay2();
  void drawToolbar();
  void drawPalette();
  void drawRight();
  void drawStatus();
  void drawPrompt();
  void drawOpenDialog();
  void drawPlay();
  void propRow(float x, float& y, float w, const std::string& label, const std::string& value, std::function<void(int)> click, std::function<void(int)> wheel, bool numeric);
  void tabObject(float x, float y0, float w);
  void tabMover(float x, float y0, float w);
  void tabLevel(float x, float y0, float w);
  void tabGen(float x, float y0, float w);
  void tabChecks(float x, float y0, float w);
  void drawEmitter(const Node& n, const Geo& g);
  void outlineGeo(const Geo& g, uint32_t col, float a);
  void circleC(V2 c, float r, uint32_t col, float a, bool fill = false);
  void poly(const std::vector<V2>& pts, bool closed, uint32_t col, float a, bool fill = false);
  void ellipseC(float x0, float y0, float x1, float y1, uint32_t col, float a);
  SDL_Texture* targetTex(int w, int h);
  void renderBoard(Board& b, const LevelBundle& lb, bool guide, int k);
  void rebuildBoard();

  void startPrompt(const std::string& label, const std::string& initial, std::function<void(const std::string&)> done);
  void keyDown(int key, int mod) override;
  bool handleText(const std::string& t);
  void openLevel(const std::string& nameOrPath);
  void newLevel();
  void loadBase();
  void collectLevels();
  void saveMod();
  void indexMod(const fs::path& dir);
  fs::path thumbDir_;
  bool writeThumbnail(const fs::path& modDir);
  void quickSave();
  void startPlay();
  void stopPlay();
  void stepPlay();
  void exitEditor();
  std::string run(const std::string& line);
  fs::path exportPath(const char* ext) const { return app.userDir / "exports" / (ed.meta.name + ext); }
};

EditorScreen::EditorScreen(App& a, const std::string& level) : Screen(a) {
  w = CW;
  h = CH;
  loadBase();
  if (!level.empty()) openLevel(level);
  else newLevel();
  fitView();
}
EditorScreen::~EditorScreen() {
  if (target_) SDL_DestroyTexture(target_);
}

void EditorScreen::loadBase() {
  ed.setVfs(&app.vfs);
  if (auto b = app.vfs.read("levels/_base.dat")) {
    auto lv = parseLevel(b->bytes);
    if (lv) ed.base = std::make_shared<Level>(std::move(*lv));
  }
  if (!ed.base) ed.base = Editor::syntheticBase();
}
void EditorScreen::newLevel() {
  ed.newLevel("NewLevel");
  auto st = listStageLevels(app.vfs);
  if (!st.empty()) ed.meta.background = st.front().name;
  ed.meta.title = "New Level";
  ed.setZoom(zoom_);
  dirtyLayout();
  say("new level: place pegs from the palette, then Save to mod");
}
void EditorScreen::openLevel(const std::string& nameOrPath) {
  std::string err;
  if (ed.loadFromVfs(app.vfs, nameOrPath, &err)) {
    say(ed.status);
    fitView();
  } else {
    say("cannot open: " + err);
  }
  dirtyLayout();
}
void EditorScreen::collectLevels() {
  levelList_.clear();
  std::set<std::string> seen;
  for (auto& s : listStageLevels(app.vfs)) {
    if (seen.insert(normKey(s.name)).second) levelList_.push_back({s.name, s.title.empty() ? s.name : s.name + "  " + s.title});
  }
  for (auto& k : app.vfs.keys()) {
    if (k.rfind("LEVELS\\", 0) != 0 || k.size() < 12 || k.substr(k.size() - 4) != ".DAT") continue;
    std::string n = k.substr(7, k.size() - 11);
    if (n.empty() || n[0] == '_' || n.find('\\') != std::string::npos) continue;
    if (seen.insert(n).second) levelList_.push_back({n, n});
  }
  std::error_code ec;
  fs::path mods = app.userDir / "mods";
  if (fs::is_directory(mods, ec))
    for (auto& m : fs::directory_iterator(mods, ec)) {
      fs::path lv = m.path() / "levels";
      if (!fs::is_directory(lv, ec)) continue;
      for (auto& f : fs::directory_iterator(lv, ec))
        if (f.path().extension() == ".dat") levelList_.push_back({f.path().string(), "mod " + m.path().filename().string() + ": " + f.path().stem().string()});
    }
}

void EditorScreen::fitView() {
  float W = ed.playfieldW();
  zoom_ = std::clamp(std::min((cv_.w - 40) / W, (cv_.h - 40) / kBoardH), 0.25f, 4.0f);
  panX_ = (cv_.w - W * zoom_) * 0.5f;
  panY_ = (cv_.h - kBoardH * zoom_) * 0.5f;
  ed.setZoom(zoom_);
}
void EditorScreen::zoomAt(float cx, float cy, float f) {
  V2 before = toLevel(cx, cy);
  zoom_ = std::clamp(zoom_ * f, 0.25f, 8.0f);
  V2 after = toCanvas(before);
  panX_ += cx - after.x;
  panY_ += cy - after.y;
  ed.setZoom(zoom_);
}
void EditorScreen::centerOn(V2 p) {
  V2 c = toCanvas(p);
  panX_ += cv_.x + cv_.w * 0.5f - c.x;
  panY_ += cv_.y + cv_.h * 0.5f - c.y;
}

void EditorScreen::update() {
  Panel::update();
  ticks_++;
  if (++msgAge_ > 900 && !msg_.empty() && !prompt_.on) msg_.clear();
  if (playing_) { stepPlay(); return; }
  if (builtRev_ != ed.revision) rebuildBoard();
  if (animate_ && boardOk_) {
    board_.step();
  }
}
void EditorScreen::rebuildBoard() {
  boardOk_ = ed.buildBoard(board_, lb_, &objNode_, ed.previewSeed, !ed.previewOranges, &boardErr_);
  builtRev_ = ed.revision;
  if (!boardOk_ && !boardErr_.empty()) say(boardErr_);
}

SDL_Texture* EditorScreen::targetTex(int w, int h) {
  if (target_ && targetW_ == w && targetH_ == h) return target_;
  if (target_) SDL_DestroyTexture(target_);
  target_ = SDL_CreateTexture(app.gfx.renderer(), SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
  if (target_) SDL_SetTextureScaleMode(target_, SDL_SCALEMODE_LINEAR);
  targetW_ = w;
  targetH_ = h;
  return target_;
}
void EditorScreen::renderBoard(Board& b, const LevelBundle& lb, bool guide, int k) {
  SDL_Renderer* r = app.gfx.renderer();
  if (!r || !app.boardRenderer) return;
  int cw = 800 + (b.W - 650);
  SDL_Texture* t = targetTex(cw * k, 600 * k);
  if (!t) return;
  SDL_SetRenderTarget(r, t);
  Hud hud;
  hud.title = lb.title;
  hud.showGuide = guide;
  hud.mode = Hud::ModeQuick;
  app.boardRenderer->draw(b, lb, hud);
  SDL_SetRenderClipRect(r, nullptr);
  SDL_SetRenderTarget(r, nullptr);
}
void EditorScreen::circleC(V2 c, float r, uint32_t col, float a, bool fill) {
  if (!dr_) return;
  V2 p = toCanvas(c);
  float rr = r * zoom_;
  if (fill) {
    int n = std::clamp((int)(rr * 1.2f), 10, 48);
    SDL_Renderer* rd = g_->renderer();
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    SDL_FColor fc{((col >> 16) & 255) / 255.0f, ((col >> 8) & 255) / 255.0f, (col & 255) / 255.0f, a};
    auto P = [&](float x, float y) { return SDL_FPoint{g_->offX() + (g_->originX() + x) * g_->scale(), g_->offY() + (g_->originY() + y) * g_->scale()}; };
    v.push_back({P(p.x, p.y), fc, {0, 0}});
    for (int i = 0; i <= n; i++) {
      float ang = 2 * kPi * i / n;
      v.push_back({P(p.x + rr * std::cos(ang), p.y + rr * std::sin(ang)), fc, {0, 0}});
    }
    for (int i = 1; i <= n; i++) { idx.push_back(0); idx.push_back(i); idx.push_back(i + 1); }
    SDL_SetRenderDrawBlendMode(rd, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(rd, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
    SDL_SetRenderDrawBlendMode(rd, SDL_BLENDMODE_NONE);
  }
  int n = std::clamp((int)(rr * 0.9f), 12, 64);
  float px = p.x + rr, py = p.y;
  for (int i = 1; i <= n; i++) {
    float ang = 2 * kPi * i / n, x = p.x + rr * std::cos(ang), y = p.y + rr * std::sin(ang);
    g_->line(px, py, x, y, col, a);
    px = x;
    py = y;
  }
}
void EditorScreen::drawEmitter(const Node& n, const Geo& g) {
  const uint32_t col = 0xffb040;
  if (g.circular) ellipseC(g.pts[0].x, g.pts[0].y, g.pts[2].x, g.pts[2].y, col, 0.9f);
  else poly(g.pts, true, col, 0.9f);
  V2 c = toCanvas(g.c);
  const float r = 14;
  R(c.x - r - 2, c.y - r - 2, 2 * r + 4, 2 * r + 4, 0x000000, 0.45f);
  std::string img = n.str("emitter_image");
  Tex* t = img.empty() ? nullptr : g_->img(img);
  if (t && t->valid()) {
    float k = std::min(1.0f, std::min(2 * r / t->lw(), 2 * r / t->lh()));
    float w = t->lw() * k, h = t->lh() * k;
    g_->drawStretch(t, c.x - w * 0.5f, c.y - h * 0.5f, w, h, Mode(0.9f));
  } else {
    g_->line(c.x - r + 3, c.y - r + 3, c.x + r - 3, c.y + r - 3, col, 0.9f);
    g_->line(c.x - r + 3, c.y + r - 3, c.x + r - 3, c.y - r + 3, col, 0.9f);
  }
  F(c.x - r - 2, c.y - r - 2, 2 * r + 4, 2 * r + 4, col, 0.9f);
  std::string name = img.empty() ? "emitter" : img.substr(img.find_last_of("/\\") == std::string::npos ? 0 : img.find_last_of("/\\") + 1);
  T(c.x + r + 6, c.y - fh_ * 0.5f, name, col);
}
void EditorScreen::outlineGeo(const Geo& g, uint32_t col, float a) {
  switch (g.kind) {
    case Geo::Circle: circleC(g.c, g.r + 3 / zoom_, col, a); break;
    case Geo::Hole:
      if (g.circular) ellipseC(g.x0 - 3, g.y0 - 3, g.x1 + 3, g.y1 + 3, col, a);
      else poly(g.pts, true, col, a);
      break;
    case Geo::Emitter: poly({V2(g.x0, g.y0), V2(g.x1, g.y0), V2(g.x1, g.y1), V2(g.x0, g.y1)}, true, col, a); break;
    case Geo::Line: poly(g.pts, false, col, a); break;
    default: poly(g.pts, true, col, a); break;
  }
}
void EditorScreen::ellipseC(float x0, float y0, float x1, float y1, uint32_t col, float a) {
  if (!dr_) return;
  V2 c0 = toCanvas(V2(x0, y0)), c1 = toCanvas(V2(x1, y1));
  float cx = (c0.x + c1.x) * 0.5f, cy = (c0.y + c1.y) * 0.5f, rx = (c1.x - c0.x) * 0.5f, ry = (c1.y - c0.y) * 0.5f;
  int n = 40;
  float px = cx + rx, py = cy;
  for (int i = 1; i <= n; i++) {
    float ang = 2 * kPi * i / n, x = cx + rx * std::cos(ang), y = cy + ry * std::sin(ang);
    g_->line(px, py, x, y, col, a);
    px = x;
    py = y;
  }
}
void EditorScreen::poly(const std::vector<V2>& pts, bool closed, uint32_t col, float a, bool fill) {
  if (!dr_ || pts.size() < 2) return;
  std::vector<V2> c;
  for (auto& p : pts) c.push_back(toCanvas(p));
  if (fill && c.size() >= 3) {
    SDL_Renderer* rd = g_->renderer();
    SDL_FColor fc{((col >> 16) & 255) / 255.0f, ((col >> 8) & 255) / 255.0f, (col & 255) / 255.0f, a};
    auto P = [&](V2 p) { return SDL_FPoint{g_->offX() + (g_->originX() + p.x) * g_->scale(), g_->offY() + (g_->originY() + p.y) * g_->scale()}; };
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    for (auto& p : c) v.push_back({P(p), fc, {0, 0}});
    for (size_t i = 1; i + 1 < c.size(); i++) { idx.push_back(0); idx.push_back((int)i); idx.push_back((int)i + 1); }
    SDL_SetRenderDrawBlendMode(rd, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(rd, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
    SDL_SetRenderDrawBlendMode(rd, SDL_BLENDMODE_NONE);
  }
  for (size_t i = 0; i + 1 < c.size(); i++) g_->line(c[i].x, c[i].y, c[i + 1].x, c[i + 1].y, col, a);
  if (closed) g_->line(c.back().x, c.back().y, c[0].x, c[0].y, col, a);
}
static bool convexPoly(const std::vector<V2>& p) {
  size_t n = p.size();
  if (n > 1 && p.front().x == p.back().x && p.front().y == p.back().y) n--;
  if (n < 3) return false;
  int sign = 0;
  for (size_t i = 0; i < n; i++) {
    V2 a = p[i], b = p[(i + 1) % n], c = p[(i + 2) % n];
    float cr = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
    int s = cr > 0.001f ? 1 : cr < -0.001f ? -1 : 0;
    if (s && sign && s != sign) return false;
    if (s) sign = s;
  }
  return true;
}

void EditorScreen::button(Rect r, const std::string& label, std::function<void()> fn, bool active, bool enabled, uint32_t swatch) {
  bool hot = enabled && hovered(r);
  R(r.x, r.y, r.w, r.h, active ? kHot : hot ? kPanel2 + 0x101420 : kPanel2, enabled ? 1.0f : 0.5f);
  F(r.x, r.y, r.w, r.h, active ? kGold : kLine, enabled ? 1.0f : 0.4f);
  float tx = r.x + r.w * 0.5f;
  Align al = Align::Center;
  if (swatch) {
    R(r.x + 5, r.y + 5, r.h - 10, r.h - 10, swatch);
    F(r.x + 5, r.y + 5, r.h - 10, r.h - 10, 0xffffff, 0.6f);
    tx = r.x + r.h + 2;
    al = Align::Left;
  }
  T(tx, r.y + (r.h - fh_) * 0.5f, label, enabled ? (active ? kGold : kText) : kDim, al);
  if (enabled) {
    Ctl c;
    c.r = r;
    c.click = [fn](int b) { if (b == 1) fn(); };
    ctls_.push_back(c);
  }
}

void EditorScreen::buildUi(Gfx& g, bool draw) {
  g_ = &g;
  dr_ = draw;
  asc_ = g.fontAscent("PLAIN");
  fh_ = g.fontHeight("PLAIN");
  ctls_.clear();
  if (draw) g.fill(0, 0, CW, CH, kBg);
  if (draw) {
    drawCanvas();
  }
  drawPalette();
  drawRight();
  drawToolbar();
  drawStatus();
  if (openDlg_) drawOpenDialog();
  if (artDlg_) drawArtDialog();
  if (prompt_.on) drawPrompt();
  layoutDirty_ = false;
}

void EditorScreen::drawLayer(Gfx& g) {
  g_ = &g;
  if (playing_) {
    dr_ = true;
    asc_ = g.fontAscent("PLAIN");
    fh_ = g.fontHeight("PLAIN");
    ctls_.clear();
    drawPlay();
    return;
  }
  if (builtRev_ != ed.revision) rebuildBoard();
  buildUi(g, true);
}

void EditorScreen::drawBoardTexture() {
  if (!thumbDir_.empty()) {
    fs::path d = std::move(thumbDir_);
    thumbDir_.clear();
    if (writeThumbnail(d)) indexMod(d);
    else say(ed.status + "  (no thumbnail: the board did not render)");
  }
  SDL_Renderer* r = g_->renderer();
  if (!r || !boardOk_ || !app.boardRenderer) return;
  const int k = 2;
  renderBoard(board_, lb_, false, k);
  if (!target_) return;
  int W = board_.W;
  SDL_FRect src{75.0f * k, 40.0f * k, (float)W * k, 560.0f * k};
  V2 o = V2(cv_.x + panX_ + ed.wideShift() * 0 + 0, cv_.y + panY_);
  auto S = [&](float x, float y) { return V2(g_->offX() + (g_->originX() + x) * g_->scale(), g_->offY() + (g_->originY() + y) * g_->scale()); };
  V2 a = S(o.x, o.y), b = S(o.x + W * zoom_, o.y + kBoardH * zoom_);
  SDL_FRect dst{a.x, a.y, b.x - a.x, b.y - a.y};
  g_->pushClip(cv_.x, cv_.y, cv_.w, cv_.h);
  SDL_RenderTexture(r, target_, &src, &dst);
  g_->popClip();
}

void EditorScreen::drawCanvas() {
  R(cv_.x, cv_.y, cv_.w, cv_.h, 0x05060f);
  g_->pushClip(cv_.x, cv_.y, cv_.w, cv_.h);
  drawBoardTexture();
  drawOverlay2();
  g_->popClip();
  F(cv_.x, cv_.y, cv_.w, cv_.h, kLine);
}

void EditorScreen::drawOverlay2() {
  float W = ed.playfieldW(), sh = ed.wideShift();
  V2 tl = toCanvas(V2(-sh, 0)), br = toCanvas(V2(-sh + W, kBoardH));
  F(tl.x, tl.y, br.x - tl.x, br.y - tl.y, 0xffffff, 0.5f);
  if (W > 650) {
    V2 a = toCanvas(V2(0, 0)), b = toCanvas(V2(650, kBoardH));
    for (float y = a.y; y < b.y; y += 10) {
      g_->line(a.x, y, a.x, y + 5, kGold, 0.8f);
      g_->line(b.x, y, b.x, y + 5, kGold, 0.8f);
    }
    T(a.x + 4, a.y + 4, "original 650 px board", kGold);
  }
  if (showGrid_ && ed.grid > 0 && ed.grid * zoom_ >= 7) {
    int n = 0;
    for (float x = std::ceil(-sh / ed.grid) * ed.grid; x <= -sh + W && n < 400; x += ed.grid, n++) {
      V2 a = toCanvas(V2(x, 0)), b = toCanvas(V2(x, kBoardH));
      g_->line(a.x, a.y, b.x, b.y, 0xffffff, 0.07f);
    }
    for (float y = 0; y <= kBoardH && n < 800; y += ed.grid, n++) {
      V2 a = toCanvas(V2(-sh, y)), b = toCanvas(V2(-sh + W, y));
      g_->line(a.x, a.y, b.x, b.y, 0xffffff, 0.07f);
    }
  }
  int tick = animate_ ? board_.tick : 0;
  for (auto& n : ed.objects()) {
    Geo g = geometry(*ed.level, *n, tick);
    bool sel = ed.isSelected(n);
    float bx0 = g.x0 + sh, bx1 = g.x1 + sh;
    bool outside = bx1 < 0 || bx0 > W || g.y1 < 0 || g.y0 > kBoardH;
    switch (g.kind) {
      case Geo::Line: poly(g.pts, false, 0xffee66, 0.9f); break;
      case Geo::Poly:
        if (n->type == 3) poly(g.pts, true, 0x66ccff, 0.75f, convexPoly(g.pts));
        else if (outside) poly(g.pts, true, kRed, 0.8f);
        break;
      case Geo::Hole:
        if (g.circular) ellipseC(g.x0, g.y0, g.x1, g.y1, 0x66ff88, 0.9f);
        else poly(g.pts, true, 0x66ff88, 0.9f);
        if (auto ev = n->val("entry"); ev && ev->n) {
          Geo ex = geometry(*ed.level, *ev->n, tick);
          poly({g.c, ex.c}, false, 0x66ff88, 0.5f);
          poly(ex.pts, true, 0xff66dd, 0.9f);
        }
        break;
      case Geo::Emitter: drawEmitter(*n, g); break;
      case Geo::Circle:
        if (outside || !boardOk_) circleC(g.c, g.r, pegColor(pegTypeOf(*n)), 0.8f, true);
        else if (!pegTypeOf(*n)) circleC(g.c, g.r, 0xbbbbbb, 0.0f);
        break;
      default: break;
    }
    if (outside) poly({V2(g.x0, g.y0), V2(g.x1, g.y0), V2(g.x1, g.y1), V2(g.x0, g.y1)}, true, kRed, 0.9f);
    if (sel) {
      uint32_t c = kCyan;
      if (g.kind == Geo::Circle) { circleC(g.c, g.r + 2 / zoom_, c, 1.0f); circleC(g.c, g.r + 3.5f / zoom_, c, 0.5f); }
      else if (g.kind == Geo::Hole && g.circular) ellipseC(g.x0 - 2, g.y0 - 2, g.x1 + 2, g.y1 + 2, c, 1.0f);
      else if (g.kind == Geo::Emitter && g.circular) ellipseC(g.pts[0].x - 2, g.pts[0].y - 2, g.pts[2].x + 2, g.pts[2].y + 2, c, 1.0f);
      else { poly(g.pts, g.kind != Geo::Line, c, 1.0f); }
      if (Node* m = moverOf(*ed.level, *n)) {
        int period = 2, depth = 0;
        for (const Node* c = m; c && depth < 8; c = subOf(*ed.level, *c), depth++) period = std::max(period, (int)c->i("time", 200));
        period = std::min(period, 6000);
        int step = std::max(1, period / 90);
        std::vector<V2> path;
        for (int t = 0; t <= period; t += step) path.push_back(geometry(*ed.level, *n, t).c);
        poly(path, false, 0xff9040, 0.85f);
        circleC(V2(m->f("anchor_x"), m->f("anchor_y")), 3 / zoom_, 0xff9040, 1.0f, true);
      }
      if (NodeP drv = ed.driverOf(n)) {
        Geo dg = geometry(*ed.level, *drv);
        outlineGeo(dg, 0xc080ff, 0.9f);
        V2 a = toCanvas(g.c), b = toCanvas(dg.c);
        g_->line(a.x, a.y, b.x, b.y, 0xc080ff, 0.8f);
      }
      for (auto& f : ed.followersOf(n)) {
        Geo fg = geometry(*ed.level, *f);
        V2 a = toCanvas(g.c), b = toCanvas(fg.c);
        g_->line(a.x, a.y, b.x, b.y, 0xc080ff, 0.45f);
        circleC(fg.c, 3 / zoom_, 0xc080ff, 0.9f, true);
      }
    }
  }
  float x0, y0, x1, y1;
  if (!ed.sel.empty() && ed.selBounds(x0, y0, x1, y1)) {
    poly({V2(x0, y0), V2(x1, y0), V2(x1, y1), V2(x0, y1)}, true, kCyan, 0.35f);
    V2 h;
    if (ed.rotateHandle(h)) {
      V2 a = toCanvas(V2((x0 + x1) * 0.5f, y0)), b = toCanvas(h);
      g_->line(a.x, a.y, b.x, b.y, kCyan, 0.6f);
      circleC(h, 5 / zoom_, kCyan, 0.9f, true);
    }
  }
  if (ed.cornersEditable() && ed.tool == Tool::Select && !animate_) {
    int cs = ed.cornerIndex(), count = ed.cornerCount();
    bool over = cv_.contains(app.mouseX, app.mouseY) && !openDlg_ && !artDlg_ && !prompt_.on;
    int hot = ed.gestureActive() ? cs : over ? ed.cornerAt(ed.hover) : -1;
    for (int i = 0; i < count; i++) {
      V2 c = toCanvas(ed.cornerWorld(i));
      float h = i == cs || i == hot ? 5.0f : 4.0f;
      R(c.x - h, c.y - h, 2 * h, 2 * h, i == cs ? kGold : i == hot ? 0xffffff : kPanel);
      F(c.x - h, c.y - h, 2 * h, 2 * h, kCyan);
    }
    V2 at;
    if (hot < 0 && over && !ed.gestureActive() && ed.edgeAt(ed.hover, &at) >= 0) {
      V2 c = toCanvas(at);
      g_->line(c.x - 5, c.y, c.x + 5, c.y, kGold, 1.0f);
      g_->line(c.x, c.y - 5, c.x, c.y + 5, kGold, 1.0f);
    }
  }
  for (auto& i : ed.issues)
    if (i.node && i.sev != Issue::Info) {
      Geo g = geometry(*ed.level, *i.node, 0);
      if (g.kind == Geo::None) continue;
      V2 p = toCanvas(V2((g.x0 + g.x1) * 0.5f, g.y0 - 8 / zoom_));
      R(p.x - 3, p.y - 3, 6, 6, i.sev == Issue::Error ? kRed : kGold);
    }
  if (ed.boxActive()) {
    V2 a = ed.boxA(), b = ed.boxB();
    poly({a, V2(b.x, a.y), b, V2(a.x, b.y)}, true, kCyan, 0.9f, true);
  }
  if (ed.lineActive()) {
  }
  auto& pp = ed.polyPoints();
  if (!pp.empty()) {
    std::vector<V2> pts = pp;
    pts.push_back(ed.snapPt(ed.hover));
    poly(pts, false, kGold, 1.0f);
    for (auto& p : pp) circleC(p, 3 / zoom_, kGold, 1.0f, true);
  }
  Tool t = ed.tool;
  if (t == Tool::Follow) T(cv_.x + 10, cv_.y + 8, "Click the object that the selection should follow. Esc cancels.", kGold);
  if (cv_.contains(app.mouseX, app.mouseY) && !openDlg_ && !artDlg_ && !prompt_.on) {
    V2 p = ed.snapPt(ed.hover);
    if (t == Tool::PegBlue || t == Tool::PegOrange || t == Tool::PegGreen || t == Tool::PegPurple) circleC(p, 10, pegColor(t == Tool::PegBlue ? 1 : t == Tool::PegOrange ? 2 : t == Tool::PegGreen ? 4 : 3), 0.5f, true);
    else if (t == Tool::PegRound || t == Tool::Bumper) circleC(p, ed.roundRadius, t == Tool::Bumper ? 0xcccccc : 0x3d8bff, 0.5f, true);
    else if (t == Tool::Hole) ellipseC(p.x - 20, p.y - 20, p.x + 20, p.y + 20, 0x66ff88, 0.8f);
    else if (t == Tool::Follow) {
      if (NodeP h = ed.hitObject(ed.hover)) outlineGeo(geometry(*ed.level, *h), 0xc080ff, 1.0f);
    }
    else if (t == Tool::Emitter) poly({V2(p.x - 60, p.y - 30), V2(p.x + 60, p.y - 30), V2(p.x + 60, p.y + 30), V2(p.x - 60, p.y + 30)}, true, 0xffb040, 0.6f);
    else if (t == Tool::Line || t == Tool::Poly || t == Tool::HoleExit) circleC(p, 3 / zoom_, kGold, 1.0f, true);
    if (ed.lineActive()) poly({ed.boxA(), ed.boxB()}, false, kGold, 1.0f);
  }
}

void EditorScreen::drawToolbar() {
  R(0, 0, CW, TOP, kPanel);
  F(0, 0, CW, TOP, kLine);
  float x = 6, y = 5, h = TOP - 10;
  auto add = [&](const std::string& label, float w, std::function<void()> fn, bool active = false, bool enabled = true) {
    w = std::max(w, TW(label) + 18);
    button({x, y, w, h}, label, fn, active, enabled);
    x += w + 4;
  };
  add("New", 44, [&] { newLevel(); });
  add("Open", 48, [&] { collectLevels(); openDlg_ = true; openFilter_.clear(); openScroll_ = 0; dirtyLayout(); });
  add("Save", 48, [&] { quickSave(); });
  add("To mod", 58, [&] { saveMod(); });
  add("Export", 56, [&] { std::string e; if (ed.saveJson(exportPath(".json"), &e)) say("exported " + exportPath(".json").string()); else say(e); });
  add("Import", 56, [&] { std::string e; if (ed.loadJson(exportPath(".json"), &e)) { say(ed.status); fitView(); } else say(e); });
  x += 10;
  add("Undo", 46, [&] { ed.undo(); say(ed.status); }, false, ed.undoDepth() > 0);
  add("Redo", 46, [&] { ed.redo(); say(ed.status); }, false, !ed.redoLabel().empty());
  x += 10;
  add("Grid", 44, [&] { showGrid_ = !showGrid_; }, showGrid_);
  add("Snap", 44, [&] { ed.snap = !ed.snap; }, ed.snap);
  add("Fit", 36, [&] { fitView(); });
  x += 10;
  add("Animate", 62, [&] { animate_ = !animate_; builtRev_ = -1; }, animate_);
  add("Oranges", 62, [&] { ed.previewOranges = !ed.previewOranges; if (ed.previewOranges) ed.previewSeed++; ed.revision++; }, ed.previewOranges);
  add("Check", 50, [&] { ed.validate(); tab_ = 4; say("checked"); });
  add("Test play", 74, [&] { startPlay(); });
  x = CW - 58;
  button({x, y, 52, h}, exitArmed_ ? "Sure?" : "Exit", [&] { exitEditor(); }, exitArmed_);
}

void EditorScreen::drawPalette() {
  R(0, TOP, LEFTW, CH - TOP - BOTH, kPanel);
  F(0, TOP, LEFTW, CH - TOP - BOTH, kLine);
  float x = 7, y = TOP + 6, w = LEFTW - 14, h = 21;
  auto head = [&](const char* s) { T(x, y, s, kGold); y += fh_ + 3; };
  auto tool = [&](const char* label, Tool t, uint32_t sw = 0) {
    button({x, y, w, h}, label, [&, t] { ed.tool = t; ed.cancelGesture(); }, ed.tool == t, true, sw);
    y += h + 3;
  };
  head("Tools");
  tool("Select (V)", Tool::Select);
  tool("Pan (H)", Tool::Pan);
  head("Pegs");
  tool("Blue (1)", Tool::PegBlue, pegColor(1));
  tool("Orange (2)", Tool::PegOrange, pegColor(2));
  tool("Green (3)", Tool::PegGreen, pegColor(4));
  tool("Purple (4)", Tool::PegPurple, pegColor(3));
  tool("Round (5)", Tool::PegRound, 0x5599ee);
  tool("Bumper (6)", Tool::Bumper, 0xbbbbbb);
  {
    char b[32];
    std::snprintf(b, sizeof b, "radius %g", ed.roundRadius);
    button({x, y, 22, h}, "-", [&] { ed.roundRadius = std::max(2.0f, ed.roundRadius - 1); });
    T(x + 26 + (w - 52) * 0.5f, y + 2, b, kText, Align::Center);
    button({x + w - 22, y, 22, h}, "+", [&] { ed.roundRadius = std::min(200.0f, ed.roundRadius + 1); });
    y += h + 5;
  }
  head("Bricks");
  tool("Straight (B)", Tool::BrickStraight);
  tool("Curved (K)", Tool::BrickCurved);
  if (ed.tool == Tool::BrickStraight || ed.tool == Tool::BrickCurved) {
    BrickParams& bp = ed.tool == Tool::BrickStraight ? ed.brickStraight : ed.brick;
    auto field = [&](const char* label, float& v, float step, float lo, float hi) {
      char b[40];
      std::snprintf(b, sizeof b, "%s %g", label, v);
      button({x, y, 20, 18}, "-", [&v, step, lo] { v = std::max(lo, v - step); });
      T(x + 24 + (w - 48) * 0.5f, y + 1, b, kDim, Align::Center);
      button({x + w - 20, y, 20, 18}, "+", [&v, step, hi] { v = std::min(hi, v + step); });
      y += 21;
    };
    field(ed.tool == Tool::BrickStraight ? "length" : "radius", bp.radius, 5, 5, 600);
    if (ed.tool == Tool::BrickCurved) field("sweep", bp.ext, 5, 5, 360);
    field("thick", bp.thick, 2, 2, 100);
    field("rotation", bp.rot, 15, -360, 720);
    if (ed.tool == Tool::BrickCurved) {
      float sub = (float)bp.sub, ty = (float)bp.type;
      field("subdiv", sub, 1, 1, 20);
      field("type", ty, 1, 0, 4);
      bp.sub = (int)sub;
      bp.type = (int)ty;
    }
  }
  head("Shapes");
  tool("Polygon (P)", Tool::Poly);
  tool("Line (L)", Tool::Line);
  tool("Hole (O)", Tool::Hole);
  tool("Hole exit (X)", Tool::HoleExit);
  head("Effects");
  tool("Emitter (N)", Tool::Emitter, 0xffb040);
  head("Motion");
  tool("Follow object (J)", Tool::Follow, 0xc080ff);
  head("Arrange");
  float hw = (w - 4) / 2;
  button({x, y, hw, h}, "Flip H", [&] { ed.mirrorSelection(true); }, false, true);
  button({x + hw + 4, y, hw, h}, "Flip V", [&] { ed.mirrorSelection(false); });
  y += h + 3;
  button({x, y, hw, h}, "Rot +15", [&] { ed.rotateSelection(15); });
  button({x + hw + 4, y, hw, h}, "Rot -15", [&] { ed.rotateSelection(-15); });
  y += h + 3;
  button({x, y, hw, h}, "Duplicate", [&] { ed.duplicateSelection(20, 20); });
  button({x + hw + 4, y, hw, h}, "Delete", [&] { ed.deleteSelection(); });
  y += h + 3;
  button({x, y, hw, h}, "Copy", [&] { ed.copySelection(); });
  button({x + hw + 4, y, hw, h}, "Paste", [&] { ed.paste(ed.hover, false); });
  y += h + 3;
  if (ed.sel.size() >= 2) {
    const char* al[6] = {"Left", "HCenter", "Right", "Top", "VCenter", "Bottom"};
    const char* ak[6] = {"left", "hcenter", "right", "top", "vcenter", "bottom"};
    float bw = (w - 8) / 3;
    for (int i = 0; i < 6; i++) {
      button({x + (i % 3) * (bw + 4), y + (i / 3) * (h + 3), bw, h - 2}, al[i], [&, i] { ed.align(ak[i]); });
    }
    y += 2 * (h + 3);
    button({x, y, hw, h}, "Spread H", [&] { ed.distribute(true); }, false, ed.sel.size() >= 3);
    button({x + hw + 4, y, hw, h}, "Spread V", [&] { ed.distribute(false); }, false, ed.sel.size() >= 3);
  }
}

void EditorScreen::propRow(float x, float& y, float w, const std::string& label, const std::string& value, std::function<void(int)> click, std::function<void(int)> wheel, bool numeric) {
  float h = 20, vx = x + w * 0.46f, vw = w - w * 0.46f;
  T(x + 2, y + 2, label, kDim);
  Rect box{vx, y, vw, h};
  bool hot = hovered(box);
  R(box.x, box.y, box.w, box.h, hot ? 0x242c60 : 0x10142c);
  F(box.x, box.y, box.w, box.h, hot ? kGold : 0x3a4488);
  if (numeric) {
    T(vx + 16, y + 2, value, kText);
    Rect l{vx, y, 14, h}, r2{vx + vw - 14, y, 14, h};
    T(vx + 3, y + 2, "<", kGold);
    T(vx + vw - 11, y + 2, ">", kGold);
    Ctl cl;
    cl.r = l;
    cl.click = [wheel](int b) { if (b == 1) wheel(-1); };
    ctls_.push_back(cl);
    Ctl cr;
    cr.r = r2;
    cr.click = [wheel](int b) { if (b == 1) wheel(1); };
    ctls_.push_back(cr);
    Ctl cm;
    cm.r = {vx + 14, y, vw - 28, h};
    cm.click = click;
    cm.wheel = wheel;
    ctls_.push_back(cm);
  } else {
    T(vx + 5, y + 2, value, kText);
    Ctl c;
    c.r = box;
    c.click = click;
    c.wheel = wheel;
    ctls_.push_back(c);
  }
  y += h + 3;
}

void EditorScreen::tabObject(float x, float y0, float w) {
  float y = y0 - scroll_[0];
  if (ed.sel.empty()) {
    T(x, y, "Nothing selected.", kDim);
    T(x, y + 18, "Click an object, drag a box, or", kDim);
    T(x, y + 34, "pick a tool and click the board.", kDim);
    y += 64;
    char b[96];
    std::snprintf(b, sizeof b, "%zu objects, %d pegs (%d orange)", ed.objects().size(), ed.pegCount(), ed.pegCount(2));
    T(x, y, b, kText);
    return;
  }
  NodeP n = ed.sel.front();
  char hd[96];
  std::snprintf(hd, sizeof hd, "%zu selected: %s%s", ed.sel.size(), typeName(n->type), ed.sel.size() > 1 ? " (+)" : "");
  T(x, y, hd, kGold);
  y += fh_ + 4;
  if (n->type == 3 && ed.cornersEditable()) {
    y += 3;
    T(x, y, "- Corners", kLine + 0x303030);
    y += fh_ + 2;
    const int count = ed.cornerCount(), ci = ed.cornerIndex();
    auto stepCorner = [this](int dir) {
      int c = ed.cornerCount();
      if (c > 0) ed.selectCorner(((ed.cornerIndex() < 0 ? (dir > 0 ? -1 : 0) : ed.cornerIndex()) + dir + c) % c);
      dirtyLayout();
    };
    propRow(x, y, w, "Corner", ci >= 0 ? std::to_string(ci + 1) + " of " + std::to_string(count) : std::to_string(count) + " corners",
            [stepCorner](int b) { stepCorner(b == 3 ? -1 : 1); }, stepCorner, true);
    if (ci >= 0) {
      V2 cw = ed.cornerWorld(ci);
      for (int axis = 0; axis < 2; axis++) {
        char vb[32];
        std::snprintf(vb, sizeof vb, "%.4g", axis ? cw.y : cw.x);
        std::string val = vb;
        auto setAxis = [this, ci, axis](double v) {
          V2 c = ed.cornerWorld(ci);
          ed.setCorner(ci, axis ? V2(c.x, (float)v) : V2((float)v, c.y));
          dirtyLayout();
        };
        propRow(x, y, w, axis ? "Corner Y" : "Corner X", val,
                [this, val, axis, setAxis](int b) {
                  if (b == 1) startPrompt(axis ? "Corner Y" : "Corner X", val, [setAxis](const std::string& t) { setAxis(std::strtod(t.c_str(), nullptr)); });
                },
                [this, axis, ci, setAxis](int dir) { V2 c = ed.cornerWorld(ci); setAxis((axis ? c.y : c.x) + dir * (mods_.shift ? 10.0 : 1.0)); }, true);
      }
    }
    float hw = (w - 4) / 2;
    button({x, y, hw, 20}, "Add corner", [this] { ed.addCorner(); say(ed.status); });
    button({x + hw + 4, y, hw, 20}, "Remove corner", [this] { ed.removeCorner(); say(ed.status); }, false, ci >= 0 && count > 3);
    y += 25;
    T(x, y, "Drag a corner to move it.", kDim);
    T(x, y + 15, "Double-click an edge to add one.", kDim);
    T(x, y + 30, "Delete removes the chosen corner.", kDim);
    y += 48;
  }
  std::string group;
  for (auto& d : propsFor(*n)) {
    if (group != d.group) {
      group = d.group;
      y += 3;
      T(x, y, std::string("- ") + group, kLine + 0x303030);
      y += fh_ + 2;
    }
    std::string val = propText(*ed.level, *n, d);
    std::string key = d.key;
    PropDef def = d;
    std::function<void(int)> click, wheel;
    bool numeric = d.kind == PK::Float || d.kind == PK::Int;
    auto stepVal = [this, n, def, key](int dir) {
      if (def.kind == PK::Enum) {
        int cnt = 1;
        for (const char* p = def.enums; p && *p; p++) cnt += *p == '|';
        int v = (int)getNum(*ed.level, *n, def) - def.enumBase;
        v = ((v + dir) % cnt + cnt) % cnt;
        ed.setProp(key, std::to_string(v + def.enumBase));
      } else if (def.kind == PK::Bool) {
        ed.setProp(key, getNum(*ed.level, *n, def) != 0 ? "0" : "1");
      } else if (def.kind == PK::Float || def.kind == PK::Int) {
        double st = def.step * (mods_.shift ? 10 : 1);
        ed.setProp(key, std::to_string(getNum(*ed.level, *n, def) + dir * st));
      }
      dirtyLayout();
    };
    if (d.kind == PK::Bool || d.kind == PK::Enum) {
      click = [stepVal](int b) { stepVal(b == 3 ? -1 : 1); };
      wheel = stepVal;
    } else {
      click = [this, key, val, def](int b) {
        if (b != 1) return;
        if (key == "image") { openArtDialog(Editor::ArtSlot::Object); return; }
        if (key == "emitter_image") { openArtDialog(Editor::ArtSlot::Emitter); return; }
        startPrompt(def.label, val == "-" ? "" : val, [this, key](const std::string& t) { ed.setProp(key, t); dirtyLayout(); });
      };
      wheel = stepVal;
    }
    propRow(x, y, w, d.label, val, click, wheel, numeric);
  }
  if (pegOf(*n)) {
    const std::string cur = ed.customType(n);
    y += 3;
    T(x, y, "- Mod peg type", kLine + 0x303030);
    y += fh_ + 2;
    propRow(x, y, w, "Custom type", cur.empty() ? "-" : cur, [this, cur](int b) {
      if (b != 1) return;
      startPrompt("Custom type (mod id:type, or empty)", cur, [this](const std::string& t) { ed.setCustomType(t); dirtyLayout(); });
    }, [](int) {}, false);
  }
  y += 4;
  button({x, y, w, 22}, moverOf(*ed.level, *n) ? "Mover: edit..." : "Add mover...", [&] { tab_ = 1; scroll_[1] = 0; });
}

void EditorScreen::tabMover(float x, float y0, float w) {
  float y = y0 - scroll_[1];
  if (ed.sel.empty()) { T(x, y, "Select an object to give it a mover.", kDim); return; }
  NodeP n = ed.sel.front();
  Node* m = moverOf(*ed.level, *n);
  if (!m) {
    T(x, y, "No mover. Pick a motion type:", kGold);
    y += fh_ + 6;
    for (int t = 1; t < kMoverTypes; t++) {
      if (t == 10) continue;
      button({x, y, w, 20}, kMoverTypeNames[t], [this, t] { if (!ed.addMover(t)) say("this object cannot take a mover"); else say(std::string("mover: ") + kMoverTypeNames[t]); dirtyLayout(); });
      y += 23;
    }
    y += 6;
    button({x, y, w, 22}, "Only follow another object (J)", [this] { ed.tool = Tool::Follow; say("click the object that the selection should follow"); });
    return;
  }
  T(x, y, std::string("Mover: ") + kMoverTypeNames[std::min(15, std::abs((int)m->i("move_type")))], kGold);
  y += fh_ + 4;
  for (auto& d : moverProps()) {
    if (!std::strcmp(d.group, "Follow")) continue;
    std::string val = propText(*ed.level, *m, d), key = d.key;
    PropDef def = d;
    bool numeric = d.kind == PK::Float || d.kind == PK::Int;
    NodeP mp = n->val("mover")->n;
    auto stepVal = [this, mp, def, key](int dir) {
      if (def.kind == PK::Enum) {
        int cnt = 16;
        int v = (int)getNum(*ed.level, *mp, def);
        v = ((v + dir) % cnt + cnt) % cnt;
        ed.setMoverProp(key, std::to_string(v));
      } else if (def.kind == PK::Bool) {
        ed.setMoverProp(key, getNum(*ed.level, *mp, def) != 0 ? "0" : "1");
      } else {
        double st = def.step * (mods_.shift ? 10 : 1);
        ed.setMoverProp(key, std::to_string(getNum(*ed.level, *mp, def) + dir * st));
      }
      dirtyLayout();
    };
    std::function<void(int)> click;
    if (d.kind == PK::Bool || d.kind == PK::Enum) click = [stepVal](int b) { stepVal(b == 3 ? -1 : 1); };
    else click = [this, key, val, def](int b) { if (b == 1) startPrompt(def.label, val, [this, key](const std::string& t) { ed.setMoverProp(key, t); dirtyLayout(); }); };
    propRow(x, y, w, d.label, val, click, stepVal, numeric);
  }
  y += 6;
  button({x, y, w, 22}, "Remove mover", [this] { ed.removeMover(); dirtyLayout(); });
  y += 28;
  T(x, y, "- Follows another object", kLine + 0x303030);
  y += fh_ + 2;
  if (m->val("sub")) {
    NodeP drv = ed.driverOf(n);
    propRow(x, y, w, "Follows", drv ? ed.describe(drv) : "a mover nobody uses", [this, drv](int b) {
      if (b != 1 || !drv) return;
      ed.select({drv});
      Geo dg = geometry(*ed.level, *drv);
      centerOn(dg.c);
    }, nullptr, false);
    for (auto& d : moverProps()) {
      if (std::strcmp(d.group, "Follow")) continue;
      std::string val = propText(*ed.level, *m, d), key = d.key;
      PropDef def = d;
      NodeP mp = n->val("mover")->n;
      auto stepVal = [this, mp, def, key](int dir) {
        double st = def.step * (mods_.shift ? 10 : 1);
        ed.setMoverProp(key, std::to_string(getNum(*ed.level, *mp, def) + dir * st));
        dirtyLayout();
      };
      propRow(x, y, w, d.label, val, [this, key, val, def](int b) { if (b == 1) startPrompt(def.label, val, [this, key](const std::string& t) { ed.setMoverProp(key, t); dirtyLayout(); }); }, stepVal, true);
    }
    button({x, y, w, 22}, "Stop following", [this] { ed.stopFollowing(); say(ed.status); dirtyLayout(); });
    y += 27;
  } else {
    T(x, y, "Not following anything.", kDim);
    y += fh_ + 4;
  }
  button({x, y, w, 22}, "Follow an object (J)", [this] { ed.tool = Tool::Follow; say("click the object that the selection should follow"); });
  y += 27;
  if (size_t cnt = ed.followersOf(n).size()) {
    T(x, y, "Followed by " + std::to_string(cnt) + " object" + (cnt == 1 ? "" : "s") + ".", kDim);
    y += fh_ + 4;
  }
  T(x, y, "A follower moves along with the object", kDim);
  T(x, y + 15, "it follows, on top of its own motion.", kDim);
  y += 36;
  T(x, y, "Tip: press Animate in the toolbar to", kDim);
  T(x, y + 15, "watch the motion. The orange line is", kDim);
  T(x, y + 30, "the path of the selected object.", kDim);
}

void EditorScreen::tabLevel(float x, float y0, float w) {
  float y = y0 - scroll_[2];
  auto text = [&](const char* label, const std::string& value, const char* key) {
    std::string k = key;
    propRow(x, y, w, label, value.empty() ? "-" : value, [this, label, value, k](int b) {
      if (b == 1) startPrompt(label, value, [this, k](const std::string& t) { ed.setMeta(k, t == "-" ? "" : t); dirtyLayout(); });
    }, nullptr, false);
  };
  auto num = [&](const char* label, int value, const char* key, int step) {
    std::string k = key;
    propRow(x, y, w, label, std::to_string(value), [this, label, value, k](int b) {
      if (b == 1) startPrompt(label, std::to_string(value), [this, k](const std::string& t) { ed.setMeta(k, t); fitView(); dirtyLayout(); });
    }, [this, k, value, step](int d) { ed.setMeta(k, std::to_string(value + d * step)); dirtyLayout(); }, true);
  };
  text("Name", ed.meta.name, "name");
  text("Title", ed.meta.title, "title");
  propRow(x, y, w, "Art of level", ed.meta.background.empty() ? "-" : ed.meta.background, [this](int b) { if (b == 1) openArtDialog(Editor::ArtSlot::Background); }, nullptr, false);
  text("Master", ed.meta.master, "master");
  num("Board width", ed.meta.boardWidth, "board_width", 1);
  float hw = (w - 4) / 2;
  button({x, y, hw, 20}, "Classic 650", [this] { ed.setMeta("board_width", "650"); fitView(); });
  button({x + hw + 4, y, hw, 20}, "Wide 917", [this] { ed.setMeta("board_width", "917"); fitView(); });
  y += 25;
  num("Balls (0=10)", ed.meta.balls, "balls", 1);
  num("Orange target", ed.meta.orange, "orange", 1);
  y += 4;
  T(x, y, "Physics (- = original)", kGold);
  y += fh_ + 2;
  for (int k = 0; k < PhysicsParams::KeyCount; k++) {
    const char* label = physicsKey(k).name;
    const std::string key = physicsKey(k).alias;
    const std::string value = ed.meta.physics.has(k) ? physicsFormat(ed.meta.physics.v.at(k)) : std::string();
    text(label, value, key.c_str());
  }
  y += 4;
  T(x, y, "Mod folder", kGold);
  y += fh_ + 2;
  propRow(x, y, w, "Mod name", modName_, [this](int b) { if (b == 1) startPrompt("Mod name", modName_, [this](const std::string& t) { if (!t.empty()) modName_ = t; dirtyLayout(); }); }, nullptr, false);
  button({x, y, w, 22}, "Save to mod (shows in Quick Play)", [this] { saveMod(); });
  y += 27;
  T(x, y, (app.userDir / "mods" / modName_).string(), kDim);
  y += fh_ + 6;
  char b[160];
  std::snprintf(b, sizeof b, "file version %d, %zu objects", ed.level->version, ed.objects().size());
  T(x, y, b, kDim);
  y += fh_ + 2;
  T(x, y, ed.savedPath.empty() ? "not saved yet" : "saved: " + fs::path(ed.savedPath).filename().string(), kDim);
}

void EditorScreen::tabGen(float x, float y0, float w) {
  float y = y0 - scroll_[3];
  static const char* kinds[4] = {"Grid", "Circle", "Spiral", "Sine row"};
  static const char* labels[4][6] = {{"columns", "rows", "spacing x", "spacing y", "origin x", "origin y"},
                                     {"count", "radius", "centre x", "centre y", "", ""},
                                     {"count", "inner radius", "outer radius", "turns", "centre x", "centre y"},
                                     {"count", "length", "amplitude", "periods", "origin x", "origin y"}};
  float hw = (w - 4) / 2;
  for (int i = 0; i < 4; i++) button({x + (i % 2) * (hw + 4), y + (i / 2) * 24, hw, 20}, kinds[i], [this, i] { genKind_ = i; dirtyLayout(); }, genKind_ == i);
  y += 52;
  for (int i = 0; i < 6; i++) {
    if (!labels[genKind_][i][0]) continue;
    float* v = &genArg_[genKind_][i];
    char b[32];
    std::snprintf(b, sizeof b, "%g", *v);
    propRow(x, y, w, labels[genKind_][i], b, [this, v, i](int bt) { if (bt == 1) { char t[32]; std::snprintf(t, sizeof t, "%g", *v); startPrompt(labels[genKind_][i], t, [this, v](const std::string& s) { *v = std::strtof(s.c_str(), nullptr); dirtyLayout(); }); } },
            [v, this](int d) { *v += d * (std::fabs(*v) >= 100 ? 10 : 1); dirtyLayout(); }, true);
  }
  y += 4;
  button({x, y, hw, 20}, "Blue", [this] { genColor_ = 1; }, genColor_ == 1, true, pegColor(1));
  button({x + hw + 4, y, hw, 20}, "Orange", [this] { genColor_ = 2; }, genColor_ == 2, true, pegColor(2));
  y += 26;
  T(x, y, "Pegs use the Round radius.", kDim);
  y += fh_ + 6;
  button({x, y, w, 24}, "Create", [this] {
    static const char* names[4] = {"grid", "circle", "spiral", "sine"};
    std::vector<float> a(genArg_[genKind_], genArg_[genKind_] + 6);
    int n = ed.generate(names[genKind_], a, genColor_);
    say("generated " + std::to_string(n) + " pegs (undo with Ctrl+Z)");
    dirtyLayout();
  });
}

void EditorScreen::tabChecks(float x, float y0, float w) {
  float y = y0 - scroll_[4];
  button({x, y, w, 22}, "Run checks", [this] { ed.validate(); say("checked"); dirtyLayout(); });
  y += 28;
  if (ed.issues.empty()) { T(x, y, "Press Run checks (or the Check button).", kDim); return; }
  for (auto& i : ed.issues) {
    uint32_t c = i.sev == Issue::Error ? kRed : i.sev == Issue::Warn ? kGold : kDim;
    const char* tag = i.sev == Issue::Error ? "ERR " : i.sev == Issue::Warn ? "warn " : "info ";
    auto lines = g_->wrap("PLAIN", w - 6, std::string(tag) + i.text);
    float hgt = (float)lines.size() * (fh_ + 1) + 3;
    Rect r{x, y, w, hgt};
    if (hovered(r) && i.node) R(r.x, r.y, r.w, r.h, 0x242c60);
    for (size_t k = 0; k < lines.size(); k++) T(x + 3, y + 1 + k * (fh_ + 1), lines[k], c);
    if (i.node) {
      NodeP node = i.node;
      Ctl cl;
      cl.r = r;
      cl.click = [this, node](int b) {
        if (b != 1) return;
        ed.select({node});
        Geo g = geometry(*ed.level, *node);
        centerOn(V2((g.x0 + g.x1) * 0.5f, (g.y0 + g.y1) * 0.5f));
        tab_ = 4;
      };
      ctls_.push_back(cl);
    }
    y += hgt;
  }
}

void EditorScreen::drawRight() {
  float x0 = CW - RIGHTW;
  R(x0, TOP, RIGHTW, CH - TOP - BOTH, kPanel);
  F(x0, TOP, RIGHTW, CH - TOP - BOTH, kLine);
  static const char* tabs[5] = {"Object", "Mover", "Level", "Gen", "Checks"};
  float tw = (RIGHTW - 8) / 5;
  for (int i = 0; i < 5; i++) button({x0 + 4 + i * tw, TOP + 4, tw - 2, 20}, tabs[i], [this, i] { tab_ = i; }, tab_ == i);
  float cy = TOP + 30, ch = CH - BOTH - cy;
  if (dr_) g_->pushClip(x0 + 2, cy, RIGHTW - 4, ch);
  size_t before = ctls_.size();
  float x = x0 + 8, w = RIGHTW - 16;
  switch (tab_) {
    case 0: tabObject(x, cy + 2, w); break;
    case 1: tabMover(x, cy + 2, w); break;
    case 2: tabLevel(x, cy + 2, w); break;
    case 3: tabGen(x, cy + 2, w); break;
    default: tabChecks(x, cy + 2, w); break;
  }
  Rect vis{x0 + 2, cy, RIGHTW - 4, ch};
  for (size_t i = before; i < ctls_.size(); i++) {
    Rect& r = ctls_[i].r;
    float y1 = std::max(r.y, vis.y), y2 = std::min(r.y + r.h, vis.y + vis.h);
    if (y2 <= y1) { r = {0, 0, 0, 0}; }
    else { r.h = y2 - y1; r.y = y1; }
  }
  if (dr_) g_->popClip();
  Ctl sc;
  sc.r = {x0, cy, RIGHTW, ch};
  sc.wheel = [this](int d) { scroll_[tab_] = std::max(0.0f, scroll_[tab_] - d * 40); dirtyLayout(); };
  ctls_.insert(ctls_.begin(), sc);
}

void EditorScreen::drawStatus() {
  float y = CH - BOTH;
  R(0, y, CW, BOTH, kPanel);
  F(0, y, CW, BOTH, kLine);
  V2 p = ed.hover;
  char b[200];
  std::snprintf(b, sizeof b, "x %.0f  y %.0f   zoom %d%%   grid %g%s   %zu sel   %zu obj   %s", p.x, p.y, (int)(zoom_ * 100), ed.grid, ed.snap ? " snap" : "", ed.sel.size(), ed.objects().size(),
                ed.dirty() ? "modified" : "saved");
  T(8, y + 4, b, kDim);
  std::string m = msg_.empty() ? ed.status : msg_;
  T(CW - 8, y + 4, m, msg_.empty() ? kDim : kGold, Align::Right);
}

void EditorScreen::drawPrompt() {
  float x = cv_.x + 40, y = cv_.y + 12, w = cv_.w - 80, h = 30;
  R(x, y, w, h, 0x000010, 0.95f);
  F(x, y, w, h, kGold);
  std::string t = prompt_.label + ": " + prompt_.text + ((ticks_ / 40) % 2 ? "_" : " ");
  T(x + 8, y + 6, t, kText);
  T(x + w - 8, y + 6, "Enter = ok   Esc = cancel", kDim, Align::Right);
}

void EditorScreen::drawOpenDialog() {
  float x = 300, y = 80, w = 680, h = 560;
  R(0, 0, CW, CH, 0x000000, 0.55f);
  R(x, y, w, h, kPanel);
  F(x, y, w, h, kGold);
  T(x + 12, y + 8, "Open a level   (type to filter, Esc closes)", kGold);
  T(x + 12, y + 30, "filter: " + openFilter_ + "_", kText);
  float ly = y + 56 - openScroll_;
  Rect clip{x + 4, y + 52, w - 8, h - 58};
  if (dr_) g_->pushClip(clip.x, clip.y, clip.w, clip.h);
  float colW = (w - 24) / 2;
  std::vector<std::pair<std::string, std::string>> items;
  for (auto& it : levelList_) {
    std::string lo = it.second, f = openFilter_;
    for (auto& c : lo) c = (char)std::tolower((unsigned char)c);
    for (auto& c : f) c = (char)std::tolower((unsigned char)c);
    if (f.empty() || lo.find(f) != std::string::npos) items.push_back(it);
  }
  int rows = ((int)items.size() + 1) / 2;
  for (int i = 0; i < (int)items.size(); i++) {
    int col = i / std::max(1, rows), row = i % std::max(1, rows);
    Rect r{x + 12 + col * colW, ly + row * 20, colW - 6, 19};
    if (r.y + r.h < clip.y || r.y > clip.y + clip.h) continue;
    if (hovered(r) && clip.contains(app.mouseX, app.mouseY)) R(r.x, r.y, r.w, r.h, kHot);
    T(r.x + 4, r.y + 2, items[i].second, kText);
    Ctl c;
    c.r = {r.x, std::max(r.y, clip.y), r.w, std::min(r.y + r.h, clip.y + clip.h) - std::max(r.y, clip.y)};
    std::string nm = items[i].first;
    c.click = [this, nm](int b) { if (b == 1) { openDlg_ = false; openLevel(nm); } };
    ctls_.push_back(c);
  }
  if (dr_) g_->popClip();
  Ctl sc;
  sc.r = {x, y, w, h};
  sc.wheel = [this](int d) { openScroll_ = std::max(0.0f, openScroll_ - d * 40); };
  ctls_.push_back(sc);
  Ctl all;
  all.r = {0, 0, CW, CH};
  all.click = [this](int) { openDlg_ = false; dirtyLayout(); };
  ctls_.insert(ctls_.begin(), all);
}

std::string EditorScreen::artCurrent() const {
  using S = Editor::ArtSlot;
  if (artSlot_ == S::Background) return ed.meta.background.empty() ? ed.meta.name : ed.meta.background;
  if (ed.sel.empty()) return "";
  return ed.sel.front()->str(artSlot_ == S::Emitter ? "emitter_image" : "image");
}
void EditorScreen::openArtDialog(Editor::ArtSlot s) {
  if (s != Editor::ArtSlot::Background && ed.sel.empty()) { say("select an object first"); return; }
  artSlot_ = s;
  artList_ = ed.artChoices(s, Editor::modDirFor(app.userDir, modName_));
  artFilter_.clear();
  artScroll_ = 0;
  artHover_.clear();
  artDlg_ = true;
  dirtyLayout();
}
void EditorScreen::syncModArt() {
  fs::path dir = Editor::modDirFor(app.userDir, modName_);
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return;
  bool mounted = false;
  for (auto& L : app.vfs.layers())
    if (L.isMod && L.root == dir) mounted = true;
  std::string e2;
  if (!mounted) app.vfs.mountMod(dir, &e2);
  VfsChange ch;
  ch.any = true;
  for (auto& L : app.vfs.layers())
    if (L.isMod && L.root == dir) {
      auto& lm = const_cast<Layer&>(L);
      for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) {
          std::string key = normKey(fs::relative(it->path(), dir, ec).generic_string());
          if (key == "MOD.INI") continue;
          lm.files[key] = it->path();
          ch.keys.insert(key);
        }
    }
  app.gfx.reloadAssets(ch);
  if (app.boardRenderer) app.boardRenderer->reloadAssets(ch);
  builtRev_ = -1;
}
void EditorScreen::chooseArt(const Editor::ArtItem* it) {
  std::string v = it ? it->value : "";
  if (it && !it->file.empty() && !ed.artExists(artSlot_, v)) syncModArt();
  bool ok = ed.setArt(artSlot_, v);
  if (it && ok && !ed.artExists(artSlot_, v)) ed.status += "  (the game cannot find that file)";
  artDlg_ = false;
  say(ed.status);
  dirtyLayout();
}
void EditorScreen::drawArtDialog() {
  using S = Editor::ArtSlot;
  const float x = 230, y = 50, w = 980, h = 710;
  R(0, 0, CW, CH, 0x000000, 0.55f);
  R(x, y, w, h, kPanel);
  F(x, y, w, h, kGold);
  const char* what = artSlot_ == S::Background ? "the picture of the level" : artSlot_ == S::Emitter ? "the picture of the emitter" : "the picture of the object";
  T(x + 12, y + 8, std::string("Choose ") + what + "   (type to filter, Esc closes)", kGold);
  T(x + 12, y + 30, "filter: " + artFilter_ + "_", kText);
  const float listW = 470;
  Rect clip{x + 4, y + 52, listW, h - 60};
  std::string f = artFilter_;
  for (auto& c : f) c = (char)std::tolower((unsigned char)c);
  std::vector<const Editor::ArtItem*> items;
  for (auto& it : artList_) {
    std::string lo = it.value;
    for (auto& c : lo) c = (char)std::tolower((unsigned char)c);
    if (f.empty() || lo.find(f) != std::string::npos) items.push_back(&it);
  }
  const std::string cur = artCurrent();
  std::string curLo = cur;
  for (auto& c : curLo) c = (char)std::tolower((unsigned char)c);
  const float rowH = 19;
  float maxScroll = std::max(0.0f, (items.size() + 1) * rowH - clip.h + 8);
  artScroll_ = std::min(artScroll_, maxScroll);
  float ly = clip.y + 4 - artScroll_;
  if (dr_) g_->pushClip(clip.x, clip.y, clip.w, clip.h);
  std::string hover;
  const Editor::ArtItem* hoverItem = nullptr;
  bool hoverNone = false;
  for (int i = -1; i < (int)items.size(); i++) {
    const Editor::ArtItem* it = i < 0 ? nullptr : items[(size_t)i];
    Rect r{x + 8, ly + (i + 1) * rowH, listW - 12, rowH - 1};
    if (r.y + r.h < clip.y || r.y > clip.y + clip.h) continue;
    bool hot = hovered(r) && clip.contains(app.mouseX, app.mouseY);
    std::string lo = it ? it->value : "";
    for (auto& c : lo) c = (char)std::tolower((unsigned char)c);
    bool isCur = it ? lo == curLo : (artSlot_ == S::Background ? ed.meta.background.empty() : cur.empty());
    if (isCur) R(r.x, r.y, r.w, r.h, 0x203860);
    if (hot) { R(r.x, r.y, r.w, r.h, kHot); hoverItem = it; hoverNone = !it; }
    std::string label = it ? it->value : (artSlot_ == S::Background ? "(the level's own picture)" : "(no picture)");
    T(r.x + 4, r.y + 2, label, it ? kText : kGold);
    if (it) T(r.x + r.w - 4, r.y + 2, it->source, kDim, Align::Right);
    Ctl c;
    c.r = {r.x, std::max(r.y, clip.y), r.w, std::min(r.y + r.h, clip.y + clip.h) - std::max(r.y, clip.y)};
    c.click = [this, it](int b) { if (b == 1) chooseArt(it); };
    ctls_.push_back(c);
  }
  if (dr_) g_->popClip();
  if (hoverItem) artHover_ = hoverItem->value;
  else if (hoverNone) artHover_.clear();
  const Editor::ArtItem* shown = hoverItem;
  if (!shown && !hoverNone)
    for (auto& it : artList_)
      if (normKey(it.value) == normKey(cur)) shown = &it;
  const float px = x + listW + 24, py = y + 56, pw = w - listW - 40, ph = 400;
  R(px, py, pw, ph, 0x0a1020);
  F(px, py, pw, ph, kLine);
  Tex* t = nullptr;
  if (shown) {
    std::string path = ed.artPath(artSlot_, shown->value);
    if (!path.empty()) t = g_->img(path);
    if ((!t || !t->valid()) && !shown->file.empty()) t = g_->imgFile(shown->file);
  }
  if (t && t->valid()) {
    float lw = t->lw(), lh = t->lh();
    float k = std::min((pw - 8) / lw, (ph - 8) / lh);
    float dw = lw * k, dh = lh * k;
    g_->drawStretch(t, px + (pw - dw) * 0.5f, py + (ph - dh) * 0.5f, dw, dh);
  } else {
    T(px + 12, py + 12, shown ? "no preview for this file" : hoverNone ? "" : "move the mouse over a picture to see it", kDim);
  }
  float iy = py + ph + 10;
  if (shown) {
    T(px, iy, shown->value, kText);
    T(px, iy + 16, "from " + shown->source, kDim);
    if (t && t->valid()) T(px, iy + 32, std::to_string((int)t->lw()) + " x " + std::to_string((int)t->lh()) + " pixels", kDim);
  }
  iy += 56;
  if (artSlot_ == S::Background) {
    T(px, iy, "The level file holds no picture. The game looks for a", kDim);
    T(px, iy + 15, "file called levels/<level name> in the game or a mod", kDim);
    T(px, iy + 30, "folder, or uses the one named here (saved as 'background').", kDim);
    T(px, iy + 45, "646 x 543 fits the classic board, 800 x 600 fills the screen.", kDim);
  } else {
    T(px, iy, "Pictures of the game and of your mod folder (levels and", kDim);
    T(px, iy + 15, "images). Put your own .png or .jpg into the mod folder", kDim);
    T(px, iy + 30, "first, then pick it here.", kDim);
  }
  button({px, y + h - 34, 220, 24}, "Type a name instead...", [this] {
    artDlg_ = false;
    Editor::ArtSlot sl = artSlot_;
    startPrompt("Picture name (empty = none)", artCurrent(), [this, sl](const std::string& t) {
      artSlot_ = sl;
      ed.setArt(sl, t == "-" ? "" : t);
      say(ed.status);
      dirtyLayout();
    });
  });
  button({px + 232, y + h - 34, 120, 24}, "Close", [this] { artDlg_ = false; dirtyLayout(); });
  Ctl sc;
  sc.r = {x, y, w, h};
  sc.wheel = [this](int d) { artScroll_ = std::max(0.0f, artScroll_ - d * 40); };
  ctls_.insert(ctls_.begin(), sc);
  Ctl all;
  all.r = {0, 0, CW, CH};
  all.click = [this](int) { artDlg_ = false; dirtyLayout(); };
  ctls_.insert(ctls_.begin(), all);
}

void EditorScreen::startPrompt(const std::string& label, const std::string& initial, std::function<void(const std::string&)> done) {
  prompt_.on = true;
  prompt_.label = label;
  prompt_.text = initial;
  prompt_.done = std::move(done);
  dirtyLayout();
}
void EditorScreen::quickSave() {
  std::string err;
  if (!ed.savedPath.empty() && fs::path(ed.savedPath).extension() == ".dat" && ed.saveDat(ed.savedPath, &err)) { say(ed.status); return; }
  saveMod();
}
void EditorScreen::saveMod() {
  std::string err;
  fs::path dir = Editor::modDirFor(app.userDir, modName_);
  ed.validate();
  if (ed.saveToMod(dir, &err)) {
    int errors = 0;
    for (auto& i : ed.issues) errors += i.sev == Issue::Error;
    say("saved to mod '" + modName_ + "'" + (errors ? " (" + std::to_string(errors) + " check error(s), see Checks)" : "") + "  enable it in Options, Mods to play it");
    thumbDir_ = dir;
    indexMod(dir);
  } else {
    say("save failed: " + err);
  }
}
bool EditorScreen::writeThumbnail(const fs::path& modDir) {
  SDL_Renderer* r = g_->renderer();
  if (!r || !app.boardRenderer) return false;
  if (builtRev_ != ed.revision) rebuildBoard();
  if (!boardOk_) return false;
  renderBoard(board_, lb_, false, 2);
  if (!target_) return false;
  SDL_SetRenderTarget(r, target_);
  SDL_Rect rc{150, 80, board_.W * 2, 1120};
  SDL_Surface* px = SDL_RenderReadPixels(r, &rc);
  SDL_SetRenderTarget(r, nullptr);
  if (!px) return false;
  SDL_Surface* small = SDL_ScaleSurface(px, 200, 177, SDL_SCALEMODE_LINEAR);
  SDL_DestroySurface(px);
  if (!small) return false;
  std::error_code ec;
  fs::path d = modDir / "levels" / "cached_thumbnails";
  fs::create_directories(d, ec);
  SDL_Surface* rgba = SDL_ConvertSurface(small, SDL_PIXELFORMAT_BGRA32);
  SDL_DestroySurface(small);
  if (!rgba) return false;
  const int w = rgba->w, h = rgba->h;
  uint8_t hdr[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, (uint8_t)(w & 255), (uint8_t)(w >> 8), (uint8_t)(h & 255), (uint8_t)(h >> 8), 32, 8 | 0x20};
  std::ofstream f(d / (ed.meta.name + ".tga"), std::ios::binary);
  bool ok = (bool)f;
  if (ok) {
    f.write((const char*)hdr, 18);
    for (int y = 0; y < h; y++) f.write((const char*)rgba->pixels + (size_t)y * rgba->pitch, (std::streamsize)w * 4);
    ok = (bool)f;
  }
  SDL_DestroySurface(rgba);
  return ok;
}
void EditorScreen::indexMod(const fs::path& dir) {
  std::string e2;
  bool mounted = false;
  for (auto& L : app.vfs.layers())
    if (L.isMod && L.root == dir) {
      mounted = true;
      auto& lm = const_cast<Layer&>(L);
      std::error_code ec;
      for (fs::recursive_directory_iterator it(dir / "levels", ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) lm.files[normKey("levels/" + fs::relative(it->path(), dir / "levels", ec).generic_string())] = it->path();
    }
  if (!mounted) app.vfs.mountMod(dir, &e2);
}
void EditorScreen::exitEditor() {
  if (ed.dirty() && !exitArmed_) {
    exitArmed_ = true;
    say("unsaved changes: press Exit again to leave");
    return;
  }
  app.showMainMenu();
}
void EditorScreen::startPlay() {
  ed.validate();
  std::string err;
  playSeed_ = app.rand();
  if (app.audio) app.audio->dropQueued();
  playOk_ = ed.buildBoard(play_, playLb_, nullptr, playSeed_, false, &err);
  if (!playOk_) { say("cannot play: " + err); return; }
  playing_ = true;
  playMouseX_ = 325;
  playMouseY_ = 250;
  say("");
}
void EditorScreen::stopPlay() {
  playing_ = false;
  if (app.audio) app.audio->stopLoops();
  builtRev_ = -1;
  dirtyLayout();
  say("back in the editor");
}
void EditorScreen::stepPlay() {
  if (!playOk_) return;
  bool fast = false;
  for (int k = 0; k < (fast ? 4 : 1); k++) {
    play_.step();
    if (app.audio) app.audio->tick();
  }
  if (app.audio) {
    app.audio->onEvents(play_.events, play_);
    if (app.boardRenderer) app.audio->playFx(app.boardRenderer->takeSounds());
  }
  play_.events.clear();
}
void EditorScreen::drawPlay() {
  g_->fill(0, 0, CW, CH, 0x000000);
  if (!playOk_) return;
  renderBoard(play_, playLb_, playGuide_, 2);
  int cw = 800 + (play_.W - 650);
  float areaH = CH - TOP - BOTH, s = std::min(CW / (float)cw, areaH / 600.0f);
  float ox = (CW - cw * s) * 0.5f, oy = TOP + (areaH - 600 * s) * 0.5f;
  if (target_) {
    auto S = [&](float x, float y) { return V2(g_->offX() + x * g_->scale(), g_->offY() + y * g_->scale()); };
    V2 a = S(ox, oy), b = S(ox + cw * s, oy + 600 * s);
    SDL_FRect dst{a.x, a.y, b.x - a.x, b.y - a.y};
    SDL_RenderTexture(g_->renderer(), target_, nullptr, &dst);
  }
  R(0, 0, CW, TOP, kPanel);
  T(10, 9, "TEST PLAY  -  " + ed.meta.title, kGold);
  T(CW - 10, 9, "click = shoot   R = restart   G = guide   Esc = back to the editor", kDim, Align::Right);
  if (play_.phase == Phase::Over || play_.result.finished) {
    R(CW / 2 - 200, CH / 2 - 30, 400, 60, 0x000010, 0.85f);
    F(CW / 2 - 200, CH / 2 - 30, 400, 60, kGold);
    T(CW / 2, CH / 2 - 20, play_.result.won ? "Level cleared!" : "Out of balls", kGold, Align::Center);
    T(CW / 2, CH / 2 + 2, "R plays again, Esc returns to the editor", kText, Align::Center);
  }
}

bool EditorScreen::handleText(const std::string& t) {
  if (prompt_.on) {
    for (char c : t)
      if ((unsigned char)c >= 32 && prompt_.text.size() < 120) prompt_.text += c;
    return true;
  }
  if (openDlg_) {
    for (char c : t)
      if ((unsigned char)c >= 32) openFilter_ += c;
    openScroll_ = 0;
    return true;
  }
  if (artDlg_) {
    for (char c : t)
      if ((unsigned char)c >= 32 && artFilter_.size() < 60) artFilter_ += c;
    artScroll_ = 0;
    return true;
  }
  if (t == ":") {
    startPrompt("command", "", [this](const std::string& s) { say(run(s)); });
    return true;
  }
  return false;
}

bool EditorScreen::input(const Input& in) {
  auto rebuild = [&] { if (layoutDirty_) buildUi(app.gfx, false); };
  switch (in.type) {
    case Input::Text: handleText(in.text); return true;
    case Input::KeyUp:
      if (in.key == SDLK_SPACE) spaceDown_ = false;
      if (in.key == SDLK_LSHIFT || in.key == SDLK_RSHIFT) mods_.shift = false;
      if (in.key == SDLK_LCTRL || in.key == SDLK_RCTRL || in.key == SDLK_LGUI || in.key == SDLK_RGUI) mods_.ctrl = false;
      if (in.key == SDLK_LALT || in.key == SDLK_RALT) mods_.alt = false;
      return true;
    case Input::KeyDown: keyDown(in.key, in.mod); return true;
    case Input::MouseMove: {
      lastX_ = in.x;
      lastY_ = in.y;
      if (playing_) {
        float areaH = CH - TOP - BOTH;
        int cw = 800 + (play_.W - 650);
        float s = std::min(CW / (float)cw, areaH / 600.0f), ox = (CW - cw * s) * 0.5f, oy = TOP + (areaH - 600 * s) * 0.5f;
        playMouseX_ = (int)std::floor((in.x - ox) / s - 75);
        playMouseY_ = (int)std::floor((in.y - oy) / s - 40);
        if (playOk_) play_.gunMouse(playMouseX_, playMouseY_);
        return true;
      }
      if (panning_) {
        panX_ = panOrgX_ + (in.x - panStartX_);
        panY_ = panOrgY_ + (in.y - panStartY_);
        return true;
      }
      V2 p = toLevel(in.x, in.y);
      if (canvasDrag_ || cv_.contains(in.x, in.y)) ed.mouseMove(p, mods_);
      return true;
    }
    case Input::MouseDown: {
      lastX_ = in.x;
      lastY_ = in.y;
      if (playing_) {
        if (in.button == 1 && playOk_) { play_.gunMouse(playMouseX_, playMouseY_); play_.gunClick(); }
        return true;
      }
      rebuild();
      exitArmed_ = false;
      for (size_t i = ctls_.size(); i-- > 0;) {
        Ctl& c = ctls_[i];
        if (c.r.contains(in.x, in.y) && c.click) {
          c.click(in.button);
          dirtyLayout();
          return true;
        }
        if (c.r.contains(in.x, in.y) && c.wheel && !c.click) return true;
      }
      if (openDlg_ || artDlg_ || prompt_.on) return true;
      if (cv_.contains(in.x, in.y)) {
        if (in.button == 2 || spaceDown_ || ed.tool == Tool::Pan) {
          panning_ = true;
          panStartX_ = in.x;
          panStartY_ = in.y;
          panOrgX_ = panX_;
          panOrgY_ = panY_;
          return true;
        }
        canvasDrag_ = true;
        ed.setZoom(zoom_);
        ed.mouseDown(toLevel(in.x, in.y), in.button, mods_, in.clicks);
        dirtyLayout();
      }
      return true;
    }
    case Input::MouseUp: {
      if (playing_) return true;
      if (panning_) { panning_ = false; return true; }
      if (canvasDrag_) {
        ed.mouseUp(toLevel(in.x, in.y), in.button, mods_);
        canvasDrag_ = false;
        dirtyLayout();
      }
      return true;
    }
    case Input::Wheel: {
      if (playing_) return true;
      rebuild();
      float d = in.wheel > 0 ? 1.0f : in.wheel < 0 ? -1.0f : 0.0f;
      if (d == 0) return true;
      for (size_t i = ctls_.size(); i-- > 0;)
        if (ctls_[i].r.contains(lastX_, lastY_) && ctls_[i].wheel) { ctls_[i].wheel((int)d); return true; }
      if (cv_.contains(lastX_, lastY_) && !openDlg_ && !artDlg_) zoomAt(lastX_, lastY_, std::pow(1.12f, d));
      return true;
    }
    default: return false;
  }
}

void EditorScreen::keyDown(int key, int mod) {
  bool ctrl = (mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0 || mods_.ctrl, shift = (mod & SDL_KMOD_SHIFT) != 0 || mods_.shift;
  if (key == SDLK_LSHIFT || key == SDLK_RSHIFT) { mods_.shift = true; return; }
  if (key == SDLK_LCTRL || key == SDLK_RCTRL || key == SDLK_LGUI || key == SDLK_RGUI) { mods_.ctrl = true; return; }
  if (key == SDLK_LALT || key == SDLK_RALT) { mods_.alt = true; return; }
  if (playing_) {
    if (key == SDLK_ESCAPE) stopPlay();
    else if (key == SDLK_R) { startPlay(); }
    else if (key == SDLK_G) playGuide_ = !playGuide_;
    else if (key == SDLK_SPACE && playOk_) { play_.gunMouse(playMouseX_, playMouseY_); play_.gunClick(); }
    return;
  }
  if (prompt_.on) {
    if (key == SDLK_ESCAPE) { prompt_.on = false; }
    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
      prompt_.on = false;
      auto done = prompt_.done;
      std::string t = prompt_.text;
      if (done) done(t);
    } else if (key == SDLK_BACKSPACE) {
      while (!prompt_.text.empty() && ((unsigned char)prompt_.text.back() & 0xC0) == 0x80) prompt_.text.pop_back();
      if (!prompt_.text.empty()) prompt_.text.pop_back();
    }
    dirtyLayout();
    return;
  }
  if (artDlg_) {
    if (key == SDLK_ESCAPE) artDlg_ = false;
    else if (key == SDLK_BACKSPACE && !artFilter_.empty()) artFilter_.pop_back();
    else if (key == SDLK_RETURN) {
      std::string f = artFilter_;
      for (auto& c : f) c = (char)std::tolower((unsigned char)c);
      for (auto& it : artList_) {
        std::string lo = it.value;
        for (auto& c : lo) c = (char)std::tolower((unsigned char)c);
        if (!f.empty() && lo.find(f) != std::string::npos) { chooseArt(&it); break; }
      }
    }
    dirtyLayout();
    return;
  }
  if (openDlg_) {
    if (key == SDLK_ESCAPE) openDlg_ = false;
    else if (key == SDLK_BACKSPACE && !openFilter_.empty()) openFilter_.pop_back();
    else if (key == SDLK_RETURN) {
      std::string f = openFilter_;
      for (auto& c : f) c = (char)std::tolower((unsigned char)c);
      for (auto& it : levelList_) {
        std::string lo = it.second;
        for (auto& c : lo) c = (char)std::tolower((unsigned char)c);
        if (f.empty() || lo.find(f) != std::string::npos) { openDlg_ = false; openLevel(it.first); break; }
      }
    }
    dirtyLayout();
    return;
  }
  dirtyLayout();
  if (key == SDLK_SPACE) { spaceDown_ = true; return; }
  if (key == SDLK_ESCAPE) {
    if (ed.gestureActive() || !ed.polyPoints().empty()) ed.cancelGesture();
    else if (ed.tool != Tool::Select) ed.tool = Tool::Select;
    else if (ed.cornerIndex() >= 0) ed.selectCorner(-1);
    else ed.clearSelection();
    return;
  }
  if (key == SDLK_RETURN || key == SDLK_KP_ENTER) { ed.keyEnter(); return; }
  if (ctrl) {
    switch (key) {
      case SDLK_Z: if (shift) ed.redo(); else ed.undo(); say(ed.status); return;
      case SDLK_Y: ed.redo(); say(ed.status); return;
      case SDLK_C: ed.copySelection(); say(ed.status); return;
      case SDLK_X: ed.cutSelection(); return;
      case SDLK_V: ed.paste(ed.hover, cv_.contains(lastX_, lastY_)); return;
      case SDLK_A: ed.selectAll(); return;
      case SDLK_D: ed.duplicateSelection(20, 20); return;
      case SDLK_S: if (shift) saveMod(); else quickSave(); return;
      case SDLK_N: newLevel(); return;
      case SDLK_O: collectLevels(); openDlg_ = true; openFilter_.clear(); openScroll_ = 0; return;
      default: return;
    }
  }
  switch (key) {
    case SDLK_DELETE:
    case SDLK_BACKSPACE: {
      bool corner = ed.cornerIndex() >= 0;
      ed.deleteKey();
      if (corner) say(ed.status);
      return;
    }
    case SDLK_V: ed.tool = Tool::Select; return;
    case SDLK_H: ed.tool = Tool::Pan; return;
    case SDLK_1: ed.tool = Tool::PegBlue; return;
    case SDLK_2: ed.tool = Tool::PegOrange; return;
    case SDLK_3: ed.tool = Tool::PegGreen; return;
    case SDLK_4: ed.tool = Tool::PegPurple; return;
    case SDLK_5: ed.tool = Tool::PegRound; return;
    case SDLK_6: ed.tool = Tool::Bumper; return;
    case SDLK_B: ed.tool = Tool::BrickStraight; return;
    case SDLK_K: ed.tool = Tool::BrickCurved; return;
    case SDLK_P: ed.tool = Tool::Poly; ed.cancelGesture(); return;
    case SDLK_L: ed.tool = Tool::Line; return;
    case SDLK_O: ed.tool = Tool::Hole; return;
    case SDLK_X: ed.tool = Tool::HoleExit; return;
    case SDLK_N: ed.tool = Tool::Emitter; return;
    case SDLK_J: ed.tool = Tool::Follow; return;
    case SDLK_G: showGrid_ = !showGrid_; return;
    case SDLK_S: ed.snap = !ed.snap; say(ed.snap ? "snap on" : "snap off"); return;
    case SDLK_F: fitView(); return;
    case SDLK_A: animate_ = !animate_; builtRev_ = -1; return;
    case SDLK_E: ed.previewOranges = !ed.previewOranges; if (ed.previewOranges) ed.previewSeed++; ed.revision++; say(ed.previewOranges ? "random orange preview" : "stored colours"); return;
    case SDLK_M: ed.mirrorSelection(!shift); return;
    case SDLK_R: ed.rotateSelection(shift ? -15.0f : 15.0f); return;
    case SDLK_D: ed.duplicateSelection(20, 20); return;
    case SDLK_T:
    case SDLK_F5: startPlay(); return;
    case SDLK_F6: ed.validate(); tab_ = 4; return;
    case SDLK_EQUALS:
    case SDLK_PLUS:
    case SDLK_KP_PLUS: zoomAt(cv_.x + cv_.w / 2, cv_.y + cv_.h / 2, 1.2f); return;
    case SDLK_MINUS:
    case SDLK_KP_MINUS: zoomAt(cv_.x + cv_.w / 2, cv_.y + cv_.h / 2, 1 / 1.2f); return;
    case SDLK_LEFT: ed.moveSelection(shift ? -ed.grid : -1, 0); return;
    case SDLK_RIGHT: ed.moveSelection(shift ? ed.grid : 1, 0); return;
    case SDLK_UP: ed.moveSelection(0, shift ? -ed.grid : -1); return;
    case SDLK_DOWN: ed.moveSelection(0, shift ? ed.grid : 1); return;
    default: return;
  }
}

std::string EditorScreen::run(const std::string& line) {
  std::istringstream in(line);
  std::string c;
  in >> c;
  std::string rest;
  std::getline(in, rest);
  rest.erase(0, rest.find_first_not_of(' '));
  auto num = [&](float d) { return rest.empty() ? d : std::strtof(rest.c_str(), nullptr); };
  if (c == "new") {
    newLevel();
    if (!rest.empty()) {
      std::istringstream r2(rest);
      std::string nm;
      int bw = 650;
      r2 >> nm >> bw;
      ed.meta.name = nm;
      ed.meta.title = nm;
      ed.setMeta("board_width", std::to_string(bw));
      ed.metaDirty = false;
      fitView();
    }
    return "new level " + ed.meta.name;
  }
  if (c == "zoom") { zoomAt(cv_.x + cv_.w / 2, cv_.y + cv_.h / 2, num(1) / zoom_); return "zoom " + std::to_string((int)(zoom_ * 100)) + "%"; }
  if (c == "fit") { fitView(); return "fit"; }
  if (c == "pan") { float a = 0, b = 0; std::sscanf(rest.c_str(), "%f %f", &a, &b); panX_ += a; panY_ += b; return "panned"; }
  if (c == "center") { float a = 0, b = 0; std::sscanf(rest.c_str(), "%f %f", &a, &b); centerOn(V2(a, b)); return "centered"; }
  if (c == "tab") { static const char* t[5] = {"object", "mover", "level", "gen", "checks"}; for (int i = 0; i < 5; i++) if (rest == t[i]) tab_ = i; if (rest == "checks") ed.validate(); dirtyLayout(); return "tab " + rest; }
  if (c == "open") { openLevel(rest); return msg_; }
  if (c == "savemod") { if (!rest.empty()) modName_ = rest; saveMod(); return msg_; }
  if (c == "save" && rest.empty()) { quickSave(); return msg_; }
  if (c == "modname") { modName_ = rest; return "mod " + rest; }
  if (c == "testplay") { startPlay(); return playing_ ? "test play" : msg_; }
  if (c == "stop") { if (playing_) stopPlay(); return "stopped"; }
  if (c == "play") {
    float x = 325, y = 300;
    std::sscanf(rest.c_str(), "%f %f", &x, &y);
    if (!playing_) startPlay();
    if (playOk_) { play_.gunMouse((int)x, (int)y); play_.gunClick(); }
    return "shot";
  }
  if (c == "ticks") { int n = (int)num(1); for (int i = 0; i < n; i++) app.tick(); return "ticked"; }
  if (c == "anim") { animate_ = rest != "off"; builtRev_ = -1; return animate_ ? "animating" : "static"; }
  if (c == "showgrid") { showGrid_ = rest != "off"; return "grid view"; }
  if (c == "exit") { app.showMainMenu(); return "left the editor"; }
  if (c == "view") {
    char b[160];
    std::snprintf(b, sizeof b, "zoom %.2f pan %.0f,%.0f tab %d playing %d board %d", zoom_, panX_, panY_, tab_, playing_, boardOk_);
    return b;
  }
  ed.setZoom(zoom_);
  bool ok = true;
  std::string m = ed.exec(line, &ok);
  dirtyLayout();
  return m;
}

}

extern "C" {
int pp_editor_available(void) { return 1; }
void* pp_editor_make_screen(void* app) { return new pp::ui::EditorScreen(*static_cast<pp::ui::App*>(app), ""); }
void* pp_editor_make_screen_level(void* app, const char* level) { return new pp::ui::EditorScreen(*static_cast<pp::ui::App*>(app), level ? level : ""); }
}
