#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <string_view>

#include <map>
#include <utility>
#include <string>
#include <vector>

#include "content.h"
#include "effects.h"
#include "font.h"
#include "image.h"
#include "pegtypes.h"
#include "sim.h"
#include "vfs.h"
#include "visopts.h"

namespace pp {

enum class Blend { Normal, Add };
struct Paint {
  uint32_t rgba = 0xffffffffu;
  Blend mode = Blend::Normal;
};

struct Hud {
  std::string title, status, footer;
  std::vector<std::string> players;
  bool showGuide = true;
  bool menuOver = false, replayOver = false;
  bool clearedBefore = false;
  enum Mode { ModeStage = 0, ModeQuick = 1, ModeChallenge = 2, ModeDemo = 3, ModeDuel = 4 } mode = ModeStage;
  int topScore = 0, totalScore = 0;
  std::string topName, challengeText, challengeRight;
  float alpha = 1.0f;
  int stageNo = -1, levelNo = -1;
};

class Renderer {
  friend struct ModOverlayAccess;
 public:
  Renderer(SDL_Renderer* r, const Vfs& v) : r_(r), vfs_(v) {}
  ~Renderer();
  void draw(const Board& b, const LevelBundle& lb, const Hud& hud);
  void fxZenPowerup(float x, float y);
  void fxElectro(float x0, float y0, float x1, float y1, int life = 30);
  void fxTimeBomb(float x, float y);
  void fxSpaceBlast(float x, float y);
  void fxBubble(float x, float y, int rise, int cel, int age);
  void masterBadge();
  void setVisualOptions(const VisualOptions& v) { vis_ = v; }
  const VisualOptions& visualOptions() const { return vis_; }
  int canvasWidth() const { return canvasW_; }
  V2 toBoard(float px, float py) const;
  void advance(const Board& b);
  std::vector<FxSound> takeSounds() { return std::exchange(fx_.sounds, {}); }

  Font* font(const std::string& id) { return fonts_.get(id); }
  void drawText(Font* f, float x, float y, std::string_view s, uint32_t rgba, TextAlign al = TextAlign::Left, float scale = 1, bool colorCodes = true);
  int drawTextWrapped(Font* f, float x, float y, int wrapWidth, std::string_view s, uint32_t rgba, TextAlign al = TextAlign::Left,
                      int lineSpacing = -1, float scale = 1);
  int wrappedHeight(Font* f, int wrapWidth, std::string_view s, int lineSpacing = -1) const;
  void beginCanvas(int canvasW = 800);
  void reloadAssets(const VfsChange& change);

 private:
  struct Tex {
    SDL_Texture* t = nullptr;
    int w = 0, h = 0;
    float scale = 1;
    int rows = 1, cols = 1;
    int frameDelay = 0;
    float lw() const { return w / scale; }
    float lh() const { return h / scale; }
    float cw() const { return lw() / cols; }
    float ch() const { return lh() / rows; }
  };
  Tex* tex(const std::string& path, int rows = 1, int cols = 1, const std::string& mask = "", int alphaColor = -1);
  Tex* img(const std::string& resId);
  Tex* grayImg(const std::string& resId);
  void put(Tex* t, SDL_FRect srcLogical, float x, float y, Paint p = {}, float w = -1, float h = -1, float rotRad = 0, float pivotX = 0, float pivotY = 0);
  void putCel(Tex* t, int cel, float x, float y, Paint p = {});
  void putCelAt(Tex* t, int col, int row, float x, float y, Paint p = {});
  void putCentered(Tex* t, float cx, float cy, Paint p = {}, float scale = 1, float rotRad = 0);
  void putCelCentered(Tex* t, int cel, float cx, float cy, Paint p = {});
  Tex* objImage(const std::string& name);
  Tex* wideBackground(const std::string& path, int targetW, float* topY = nullptr);
  VisualOptions vis_;
  struct BloomRec {
    SDL_Texture* t;
    SDL_FRect src, dst;
    double ang;
    SDL_FPoint c;
    bool rot;
    uint8_t r, g, b, a;
  };
  std::vector<BloomRec> bloom_;
  bool bloomOn_ = false;
  SDL_Texture *bloomA_ = nullptr, *bloomB_ = nullptr;
  int bloomW_ = 0, bloomH_ = 0;
  void bloomPass(int ow, int oh, const SDL_Rect& clip);
  int texQ_ = 0;
  float texS_ = 1;
  void fxPegSparks(const Obj& o, float cx, float cy);
  void sprite(Tex* t, SDL_FRect src, float cx, float cy, float w, float h, float rotRad, Uint8 a = 255, bool add = false, Uint32 tint = 0xffffff, bool flip = false);
  void quad(Tex* t, SDL_FRect src, float x, float y, float w, float h);
  enum class Pass { Shadow, Main, Light };
  void observe(const Board& b);
  void drawObj(const Board& b, const Obj& o, int idx, Pass pass);
  void drawPeg(const Board& b, const Obj& o, int idx, Pass pass);
  void drawBrickPeg(const Board& b, const Obj& o, int idx, Pass pass);
  void drawPegGlow(const Obj& o, float cx, float cy);
  void drawCustomPeg(const Board& b, const Obj& o, const PegTypeDef& d, float cx, float cy);
  void drawBigPulse(int pegType, float cx, float cy, int age, int kind, uint32_t tint = 0xffffffffu);
  void drawBall(const Board& b, const Ball& bl, bool inGun);
  void drawGuide(const Board& b);
  void doDrawGuide(const Board& b, const Board::GuideTrace& tr, float x0, float y0, float ang, float speed, int hitStep, int len, int start,
                   bool far, bool fireball, bool superCol);
  int chCountBlink_ = 0, chPowerBlink_ = 0, chTick_ = 0, chLastCount_[4]{-2, -2, -2, -2};
  struct HatSnap { V2 pos, prev; };
  std::map<int, HatSnap> hatSnap_;
  int aimTicks_ = 0;
  int hintTick_ = -1;
  bool flipX_ = false;
  bool zenOn_ = false;
  int guideAge_ = 1, guideRack_ = 0;
  void drawBoardShadow();
  void drawZoomFrame(const Board& b);
  void brickMesh(const Obj& o, Tex* t, int row, float dx, float dy, uint32_t rgba, Blend mode, float inflate = 0);
  void drawHud(const Board& b, const Hud& hud);
  void drawFrame(int dw);
  void drawPortrait(const Board& b, int dw);
  void drawModPortrait(const Board& b, int dw);
  void drawOrbFront(const Board& b, int dw);
  struct Portrait {
    std::string key;
    Image frames, mask, eye;
    CharInfo info;
    SDL_Texture* tex = nullptr;
    int frame = 0, ticks = 0, lastTick = -1;
    uint64_t sig = ~0ull;
    bool ok = false;
  };
  Portrait portrait_;
  std::vector<CharInfo> chars_;
  bool charsLoaded_ = false;
  FxState fx_;
  void fxSpawn(const Board& b, const Board::FxRec& r);
  void fxTick(const Board& b, int mode = 0, float frac = 0);
  void fxBallTrails(const Board& b);
  void fxCannonBlast(float x, float y, float angle);
  void fxFlowerSparkles(float x, float y);
  void fxSpookyBall(float x, float y);
  void fxFeverTally(const Board& b);
  void fxDrawEffects(const Board& b, int layer);
  void fxDrawTexts(const Board& b, int layer);
  FxText& fxText(const std::string& s, float x, float y, int font = -1);
  FxEffect& fxAdd(int type, float x, float y, int prio, bool front = false);
  void fxDrawOne(const Board& b, const FxEffect& e);
  void fxFeverHole(float x, float y, int level);
  void xformSprite(Tex* t, float x, float y, float sx, float sy, float tx, float ty, float rot, Paint p);
  void fxStyleText(const Board& b, const std::string& name, int pts, float x, float y, bool big);
  void fxUpdateOne(const Board& b, FxEffect& e, std::vector<FxEffect>& out);
  void drawBallColumn(const Board& b);
  void drawOrbBack(const Board& b, const Hud& hud, int dw);
  void drawOrangeMeter(const Board& b, int dw);
  float BX = 75;
  static constexpr float BY = 40;
  int stageCel_ = 0, stageNo_ = 0, levelNo_ = 0;
  std::string stageFor_;
  SDL_Texture* fontTexture(const Image* img);
  void blitGlyphs(const std::vector<GlyphQuad>& q, float x, float y, float scale);
  void text(float x, float y, const std::string& s, float size, Uint32 rgb = 0xffffff, bool centre = false);
  float px(float cx) const { return ox_ + cx * s_; }
  float py(float cy) const { return oy_ + cy * s_; }

  struct ObjViz {
    bool wasPopped = false;
    int pulseStart = -1, pulseKind = 0;
    int pegType = 0;
    int blinkStart = -1, blinkN = 0;
  };
  struct BlinkReq { int type = 0, n = 0, tick = -1; } blinkReq_;
  int shotsFired0_ = 0;
  std::vector<ObjViz> viz_;
  int lastTick_ = -1;
  size_t lastObjs_ = 0;
  SDL_Renderer* r_;
  const Vfs& vfs_;
  std::map<std::string, Tex> cache_;
  FontManager fonts_{vfs_};
  ImageManifest man_{vfs_};
  std::map<const Image*, SDL_Texture*> fontTex_;
  float s_ = 1, ox_ = 0, oy_ = 0;
  int canvasW_ = 800, boardW_ = 650;
};

inline void (*g_modOverlay)(Renderer&, const Board&) = nullptr;

}
