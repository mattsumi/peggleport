#pragma once
#include <cstdint>
#include <vector>

#include "ui/fairies.h"
#include "ui/fireworks.h"

namespace pp::ui {

struct NightsCmd {
  enum Op : uint8_t { Image, Stretch, ImageF, Cel, Centered, Fill, Highlight, Text, Rotated };
  Op op = Image;
  const char* img = "";
  bool colorize = false;
  bool add = false;
  int r = 255, g = 255, b = 255, a = 255;
  float x = 0, y = 0;
  int w = 0, h = 0;
  int sx = 0, sy = 0, sw = 0, sh = 0;
  float angle = 0;
};

class NightsCurve {
 public:
  struct Node {
    float x, y, z;
  };
  NightsCurve(std::vector<Node> nodes, float outMin, float outMax, float inMax);
  float value(double t) const;

 private:
  struct Seg {
    float c0, c1, c2, c3;
  };
  std::vector<Node> nodes_;
  std::vector<Seg> segs_;
  float outMin_, outMax_, inMax_;
};

class NightsLoading {
 public:
  enum Sound : unsigned { kMoonlight = 1, kPegHit = 2, kLogoBuzz = 4, kMusic = 8 };

  struct Env {
    int mouseX = 0, mouseY = 0;
    float loadFraction = 0;
    bool loadDone = false;
  };

  struct Particle {
    float x = 0, y = 0;
    int alpha = 0;
    float vx = 0, vy = 0;
    bool flip = false;
    float angle = 0;
  };

  explicit NightsLoading(CrtRand& rng, bool fast = false);

  unsigned update(const Env& env);
  void drawScene(std::vector<NightsCmd>& out, int textWidth);
  void drawOverlay(std::vector<NightsCmd>& out, int fairySize);

  int click(int x, int y) { return fairies_.click(x, y); }

  static int textX(int width) { return (800 - width) / 2 + 4; }
  static constexpr int kPlayX = 288;
  static constexpr int kPlayY = 504;
  static constexpr const char* kLoadingText = "Loading...";

  int tick() const { return cnt_; }
  int state() const { return state_; }
  int stateCount() const { return stateCnt_; }
  int stateLength() const { return stateLen_; }
  float progress() const { return progress_; }
  bool ready() const { return ready_; }
  int pulse() const { return doneCnt_; }
  float glow() const { return glow_; }
  float moon() const { return moon_; }
  float bright() const { return bright_; }
  int beams() const { return beams_; }
  int ballState() const { return ballState_; }
  int flash() const { return flash_; }
  int logoClock() const { return logoClock_; }
  int bounceTick() const { return bounceTick_; }
  int logoY() const { return logoY_; }
  float ballX() const { return ballX_; }
  float ballY() const { return ballY_; }
  float ballVX() const { return ballVX_; }
  float ballVY() const { return ballVY_; }
  float starFade() const { return starFade_; }
  const std::vector<Particle>& particles() const { return particles_; }
  const LoadingFairies& fairies() const { return fairies_; }

 private:
  void startBall();
  void updateBall(unsigned& snd);
  void spawnParticles();
  void updateParticles();
  void drawStars(std::vector<NightsCmd>& out);
  void drawBeam(std::vector<NightsCmd>& out, int i);
  void drawLogo(std::vector<NightsCmd>& out, float scale, int y);
  void drawParticles(std::vector<NightsCmd>& out);

  CrtRand& rng_;
  LoadingFairies fairies_;
  bool fast_;
  NightsCurve drop_, grow_, title_, flicker_;
  std::vector<Particle> particles_;

  int cnt_ = 0;
  int state_ = 0, stateCnt_ = 0, stateLen_ = 200;
  float progress_ = 0;
  int doneCnt_ = 0;
  float glow_ = 0, moon_ = 0, bright_ = 0;
  int ballState_ = 0, flash_ = 0;
  float ballX_ = 0, ballY_ = 0, ballVX_ = 0, ballVY_ = 0;
  int logoClock_ = 0;
  int bounceTick_ = 0;
  int beams_ = 0;
  float starFade_ = 0;
  int logoX_ = 0x74, logoY_ = 0x6a;
  bool ready_ = false;
  bool loaderStarted_ = false;
  int logoShift_ = 0;
};

}
