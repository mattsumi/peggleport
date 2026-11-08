#include "ui/nights_loading.h"

#include <cmath>
#include <utility>

namespace pp::ui {

namespace {

float fl(double v) { return (float)v; }

constexpr double kHalfPi = 1.5707963705062866;
constexpr double kSpread = 4.300000190734863;
constexpr double kDrift = -0.07100000232458115;
constexpr double kGlowStep = 0.0011500000255182385;
constexpr double kMoonStep = 0.0027000000700354576;
constexpr double kProgressStep = 0.0012600000482052565;
constexpr double kDisplayRate = 0.0010000000474974513;
constexpr double kFastRate = 0.009999999776482582;
constexpr double kBrightGain = 1.4500000476837158;
constexpr double kGravity = 0.20000000298023224;
constexpr double kStarStep = 0.029999999329447746;
constexpr double kStarGate = 0.44999998807907104;
constexpr double kMoonGate = 0.800000011920929;
constexpr float kHoldBack = 0.9998999834060669f;
constexpr float kDisplayCap = 0.9900000095367432f;
constexpr float kDayLimit = 0.6000000238418579f;
constexpr float kLogoGate = 0.6200000047683716f;
constexpr int kMusicTick = 1500;

struct Star {
  int x, y;
};

constexpr Star kStars[28] = {
    {313, 55},  {475, 68},  {758, 129}, {724, 30},  {709, 149}, {678, 193}, {636, 127},
    {624, 23},  {616, 26},  {594, 99},  {583, 65},  {515, 47},  {512, 102}, {509, 82},
    {505, 37},  {505, 125}, {504, 30},  {475, 74},  {443, 58},  {438, 38},  {348, 36},
    {314, 92},  {312, 36},  {270, 126}, {249, 66},  {154, 104}, {0, 0},     {0, 0},
};

constexpr int kStarPeriods[3] = {91, 173, 137};

struct StarFlash {
  int tick;
  int star;
};

constexpr StarFlash kStarFlashes[2] = {{1135, 0}, {1220, 1}};

NightsCmd image(const char* name, float x, float y) {
  NightsCmd c;
  c.op = NightsCmd::Image;
  c.img = name;
  c.x = x;
  c.y = y;
  return c;
}

NightsCmd tinted(NightsCmd c, int r, int g, int b, int a) {
  c.colorize = true;
  c.r = r;
  c.g = g;
  c.b = b;
  c.a = a;
  return c;
}

NightsCmd additive(NightsCmd c) {
  c.add = true;
  return c;
}

NightsCmd fill(float x, float y, int w, int h, int r, int g, int b, int a, bool add) {
  NightsCmd c;
  c.op = NightsCmd::Fill;
  c.x = x;
  c.y = y;
  c.w = w;
  c.h = h;
  c.r = r;
  c.g = g;
  c.b = b;
  c.a = a;
  c.add = add;
  return c;
}

NightsCmd stretch(const char* name, int x, int y, int w, int h) {
  NightsCmd c;
  c.op = NightsCmd::Stretch;
  c.img = name;
  c.x = (float)x;
  c.y = (float)y;
  c.w = w;
  c.h = h;
  return c;
}

float envelope(int v) {
  if (v < 5) return fl((double)v / 5.0);
  if (v < 30) return fl(1.0 - (double)fl((double)(v - 5) / 25.0));
  return 0.0f;
}

}

NightsCurve::NightsCurve(std::vector<Node> nodes, float outMin, float outMax, float inMax)
    : nodes_(std::move(nodes)), outMin_(outMin), outMax_(outMax), inMax_(inMax) {
  for (size_t i = 0; i + 1 < nodes_.size(); i++) {
    const Node& a = nodes_[i];
    const Node& b = nodes_[i + 1];
    float span = fl((double)b.x - a.x);
    float slope = fl(fl((double)b.y - a.y) / span);
    float c2 = fl(fl((double)slope - a.z) / span);
    float e = fl(fl((double)b.z - slope) / span);
    float c3 = fl(fl((double)e - c2) / span);
    segs_.push_back({a.y, a.z, c2, c3});
  }
}

float NightsCurve::value(double t) const {
  float range = inMax_;
  float u = fl(fl(t) / (double)range);
  if (1.0 <= (double)u) u = 1.0f;
  float v = nodes_.back().y;
  for (size_t i = 0; i + 1 < nodes_.size(); i++) {
    if (nodes_[i + 1].x > u) {
      float dx = fl((double)u - nodes_[i].x);
      float dx2 = fl((double)u - nodes_[i + 1].x);
      const Seg& s = segs_[i];
      float acc = fl((double)s.c0 + fl((double)s.c1 * dx));
      float sq = fl((double)dx * dx);
      float m = fl((double)sq * dx2);
      float m3 = fl((double)m * s.c3);
      float m2 = fl((double)sq * s.c2);
      acc = fl((double)m2 + acc);
      v = fl((double)m3 + acc);
      break;
    }
  }
  float span = fl((double)outMax_ - outMin_);
  float r = fl(fl((double)v * span) + outMin_);
  if (outMax_ > outMin_) {
    float cand = r <= outMin_ ? outMin_ : r;
    if (cand >= outMax_) return outMax_;
    return r > outMin_ ? r : outMin_;
  }
  return r;
}

NightsLoading::NightsLoading(CrtRand& rng, bool fast)
    : rng_(rng),
      fairies_(rng),
      fast_(fast),
      drop_({{0.0f, 1.0f, 0.0f}, {0.0244444441f, 1.0f, 0.0f}, {0.821111202f, 0.433333337f, -0.058580339f}, {1.0f, 0.5f, 0.903734148f}}, -1.0f, 1.0f, 1.25f),
      grow_({{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.351448715f}}, 0.75f, 1.0f, 1.25f),
      title_({{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}}, 0.0f, 1.0f, 3.0f),
      flicker_({{0.0f, 0.0f, 0.0f},
                {0.0455555581f, 0.0f, 0.0f},
                {0.075555563f, 0.677777767f, 0.0f},
                {0.135555565f, 0.677777767f, 0.0f},
                {0.167777792f, 0.0f, 0.0f},
                {0.331111133f, 0.0f, 0.0f},
                {0.365555584f, 0.822222233f, 0.0f},
                {0.425555587f, 0.822222233f, 0.0f},
                {0.446666688f, 0.0f, 0.0f},
                {0.702222288f, 0.0f, 0.0f},
                {0.732222319f, 0.522222221f, 0.0f},
                {1.0f, 0.522222221f, 0.0f}},
               0.0f, 1.0f, 0.800000011920929f) {
  if (fast_) {
    cnt_ = 400;
    state_ = 3;
    stateLen_ = 100;
  }
}

void NightsLoading::startBall() {
  bounceTick_ = 0;
  ballState_ = 1;
  flash_ = 0;
  ballX_ = 205.0f;
  ballY_ = -20.0f;
  ballVX_ = 4.5f;
  ballVY_ = 0.0f;
}

void NightsLoading::spawnParticles() {
  int step = state_ > 2 && state_ < 5 ? 1 : 2;
  float ang = fl(std::atan2((double)ballVY_, (double)ballVX_));
  float t = fl((double)ang + kHalfPi);
  double cs = std::cos((double)t);
  double sn = std::sin((double)t);
  for (int i = -7; i <= 7; i += step) {
    Particle p;
    p.angle = ang;
    int r1 = rng_.next() % 30;
    float ox = fl(fl(cs * kSpread) * (double)i);
    p.x = fl(fl((double)r1 + (double)fl((double)ox + (double)ballX_)) - 15.0);
    int r2 = rng_.next() % 30;
    float oy = fl(fl(sn * kSpread) * (double)i);
    p.y = fl(fl((double)r2 + (double)fl((double)oy + (double)ballY_)) - 15.0);
    p.vx = fl((double)fl(cs * kDrift) * (double)i);
    p.vy = fl((double)i * (double)fl(sn * kDrift));
    p.alpha = 255;
    p.flip = i > 0;
    particles_.push_back(p);
  }
}

void NightsLoading::updateParticles() {
  size_t i = 0;
  while (i < particles_.size()) {
    Particle& p = particles_[i];
    p.alpha -= 6;
    if (p.alpha <= 0) {
      p = particles_.back();
      particles_.pop_back();
      continue;
    }
    float speed = fl((double)fl((double)p.vx * p.vx) + (double)fl((double)p.vy * p.vy));
    double k = (double)fl((double)fl(0.25 - (double)speed) * 3.0);
    float sn = fl(std::sin((double)p.angle));
    p.y = fl((double)fl((double)fl((double)sn * k) + p.vy) + p.y);
    float cs = fl(std::cos((double)p.angle));
    p.x = fl((double)fl((double)fl((double)cs * k) + p.vx) + p.x);
    i++;
  }
}

void NightsLoading::updateBall(unsigned& snd) {
  if (ballState_ <= 0 || ballState_ > 2) return;
  int was = ballState_;
  ballVY_ = fl((double)ballVY_ + kGravity);
  ballX_ = fl((double)ballVX_ + ballX_);
  ballY_ = fl((double)ballY_ + ballVY_);
  if (ballY_ > 150.0f && was == 1) {
    if (bounceTick_ == 0) bounceTick_ = cnt_;
    ballVY_ = fl((double)ballVY_ * -0.75);
    flash_ = 1;
    ballState_ = 2;
    snd |= kPegHit;
  } else if (ballX_ > 860.0f) {
    ballState_ = 3;
  }
  if (ballX_ < 840.0f) spawnParticles();
}

unsigned NightsLoading::update(const Env& env) {
  unsigned snd = 0;
  cnt_++;
  int old = state_;
  if (stateLen_ != -1 && stateCnt_ >= stateLen_ && state_ >= 0 && state_ <= 10) {
    if (state_ == 0) {
      stateLen_ = 100;
      state_ = 3;
    } else if (state_ == 3) {
      state_ = 4;
      stateLen_ = 200;
    } else if (state_ == 4) {
      state_ = 4;
      stateLen_ = 1000;
    }
  }
  if (old == state_) {
    stateCnt_++;
  } else {
    stateCnt_ = 0;
    if (state_ == 3) {
      glow_ = 0;
      moon_ = 0;
      fairies_.restartScene();
    }
  }

  if (doneCnt_ != 0) doneCnt_++;
  if (cnt_ == 1) snd |= kMoonlight;
  updateBall(snd);
  updateParticles();
  if (flash_ > 0) {
    flash_++;
    if (flash_ > 30) flash_ = 0;
  }

  if (cnt_ > 1) loaderStarted_ = true;
  float load = env.loadFraction;
  if (cnt_ <= 250 && !fast_) load = 0.0f;
  if (env.loadDone) load = 1.0f;
  else if (!(load < 1.0f)) load = kHoldBack;
  float disp = fast_ ? fl((double)cnt_ * kFastRate) : fl((double)(cnt_ - 250) * kDisplayRate);
  if (loaderStarted_ && !env.loadDone && disp > kDisplayCap) disp = kDisplayCap;
  if (disp < load) load = disp;
  if (0.0f > load) load = 0.0f;
  if (progress_ < load) {
    float np = fl((double)progress_ + (fast_ ? kFastRate : kProgressStep));
    progress_ = np;
    if (np > load) progress_ = load;
    beams_ = (int)fl((double)progress_ * 25.0);
    if (!(1.0 > (double)progress_)) {
      doneCnt_ = 1;
      ready_ = true;
    }
  }

  {
    float s = fl((double)glow_ + kGlowStep);
    glow_ = 1.0 > (double)s ? s : 1.0f;
  }
  if (kLogoGate < glow_) logoClock_++;
  if ((double)glow_ > kMoonGate) {
    float s = fl((double)moon_ + kMoonStep);
    moon_ = s <= 1.0f ? s : 1.0f;
  }
  if (state_ > 2 && state_ < 5 && cnt_ > 0x500) {
    glow_ = 1.0f;
    if (ballState_ == 0) startBall();
  }
  {
    float b = fl((double)glow_ * kBrightGain);
    bright_ = b <= 1.0f ? b : 1.0f;
  }
  fairies_.update(state_ > 2 && state_ < 5, env.mouseX, env.mouseY);
  if (bounceTick_ > 0 && cnt_ - bounceTick_ == 0) snd |= kLogoBuzz;
  if (cnt_ == kMusicTick) snd |= kMusic;
  logoShift_ = 0;
  if (state_ > 2 && state_ < 5) {
    if ((double)glow_ > kStarGate) {
      float s = fl((double)starFade_ + kStarStep);
      starFade_ = 1.0 > (double)s ? s : 1.0f;
    }
    if (flash_ > 0 && flash_ < 10) {
      logoShift_ = (int)fl(8.0 - (double)fl((double)flash_ * 1.5));
      logoY_ += logoShift_;
    }
  }
  return snd;
}

void NightsLoading::drawStars(std::vector<NightsCmd>& out) {
  if (!((double)glow_ > kStarGate)) return;
  for (int period : kStarPeriods) {
    int q = cnt_ / period;
    int rem = cnt_ % period;
    int id = (q * 1137) % 28;
    float ratio = fl((double)rem / (double)period);
    float t = fl((double)ratio * 2.0);
    float tri = t <= 1.0f ? t : fl(2.0 - (double)t);
    int a = (int)fl((double)fl((double)tri * 150.0) * starFade_);
    NightsCmd c = additive(tinted(image("IMAGE_LOAD_STAR", (float)(kStars[id].x - 5), (float)(kStars[id].y - 5)), 255, 255, 255, a));
    out.push_back(c);
  }
  for (const StarFlash& f : kStarFlashes) {
    if (!(cnt_ > f.tick && cnt_ < f.tick + 70)) continue;
    float t = fl((double)(cnt_ - f.tick) / 70.0);
    float u = fl((double)t * 2.0);
    float tri = u <= 1.0f ? u : fl(2.0 - (double)u);
    int a = (int)fl(150.0 * (double)tri);
    float k = fl(fl((double)tri * 0.75) + 1.0);
    int w = (int)fl(9.0 * (double)k);
    int h = (int)fl((double)k * 9.0);
    int hw = w / 2, hh = h / 2;
    for (int rep = 0; rep < 2; rep++) {
      NightsCmd c = stretch("IMAGE_LOAD_STAR", kStars[f.star].x - hw, kStars[f.star].y - hh - 1, w, h);
      c = additive(tinted(c, 0, 255, 255, a));
      out.push_back(c);
    }
  }
}

void NightsLoading::drawBeam(std::vector<NightsCmd>& out, int i) {
  int cel = i == 0 ? 4 : i == 1 ? 3 : i == 23 ? 1 : i == 24 ? 0 : 2;
  NightsCmd c;
  c.op = NightsCmd::Cel;
  c.img = "IMAGE_LOAD_LIGHTS";
  c.x = (float)(i * 9 + 0x11f);
  c.y = (float)0x1fa;
  c.sx = cel * 12;
  c.sy = 0;
  c.sw = 12;
  c.sh = 31;
  out.push_back(c);
}

void NightsLoading::drawLogo(std::vector<NightsCmd>& out, float scale, int y) {
  int sw = (int)fl(535.0 * (double)scale);
  int sh = (int)fl((double)scale * 282.0);
  out.push_back(stretch("IMAGE_LOAD_LOGO", (535 - sw) / 2 + logoX_, (282 - sh) / 2 + y, sw, sh));
}

void NightsLoading::drawScene(std::vector<NightsCmd>& out, int textWidth) {
  out.clear();
  if (!(state_ > 2 && state_ < 5)) {
    out.push_back(fill(0, 0, 800, 600, 0, 0, 0, 255, false));
    return;
  }
  float scale = 0.5f;
  int y0 = logoY_ - logoShift_;
  int yc = y0;
  if (logoClock_ > 0) {
    double t = (double)fl((double)logoClock_ / 100.0);
    yc = (int)fl(fl((double)drop_.value(t) * 400.0) + (double)y0);
    scale = grow_.value((double)fl((double)logoClock_ / 100.0));
  }

  out.push_back(fill(0, 0, 800, 600, 0, 0, 0, 255, false));
  out.push_back(image("IMAGE_LOAD_BACK", 10.0f, -30.0f));
  out.back().op = NightsCmd::ImageF;

  int e1 = (int)fl(384.0 - (double)fl((double)glow_ * 512.0));
  int e2 = (int)fl(255.0 - (double)fl((double)glow_ * 255.0));
  int skyA = e1 >= 255 ? 255 : e1 < 0 ? 0 : e1;
  out.push_back(tinted(stretch("IMAGE_LOAD_BACKBRIGHT", 0, 0, 800, 600), e2, e2, e2, skyA));

  drawStars(out);

  int sunY = (int)fl(fl(1000.0 * (double)glow_) + -214.0);
  float moonPow = fl(std::pow((double)moon_, 0.4000000059604645));
  float moonT = fl(1.0 - (double)moonPow);
  int moonOff = (int)fl(fl(220.0 * (double)moonT) - 314.0);
  if (kDayLimit > glow_) {
    int v = (int)fl(255.0 - (double)fl((double)glow_ * 255.0));
    NightsCmd glowCmd = image("IMAGE_SUNGLOW", -36.0f, (float)sunY);
    glowCmd.op = NightsCmd::Rotated;
    glowCmd.w = 394;
    glowCmd.h = 383;
    out.push_back(tinted(glowCmd, v, v, v, 255));
    out.push_back(tinted(image("IMAGE_LOAD_SUNHEAD", 283.0f, (float)(sunY + 0x133)), v, v, v, 255));
  } else if (moonT != 1.0f) {
    out.push_back(image("IMAGE_LOAD_MOON", 256.0f, (float)(moonOff + 0x148)));
  }

  if (logoClock_ > 0 && logoClock_ < 0x76) drawLogo(out, scale, yc);

  yc += logoShift_;

  out.push_back(tinted(image("IMAGE_LOAD_BACKTUTE", 0.0f, 200.0f), e2, e2, e2, skyA));

  float hillY = fl(fl((double)bright_ * 65.0) + 250.0);
  int litA = (int)fl(255.0 - (double)fl((double)glow_ * 254.0));
  {
    NightsCmd c = image("IMAGE_LOAD_HILL2", 0.0f, hillY);
    c.op = NightsCmd::ImageF;
    out.push_back(c);
    NightsCmd d = tinted(image("IMAGE_LOAD_HILL2BRIGHT", 0.0f, hillY), 255, 255, 255, litA);
    d.op = NightsCmd::ImageF;
    out.push_back(d);
  }
  int hillTop = (int)fl(fl((double)bright_ * 110.0) - 119.0);
  out.push_back(image("IMAGE_LOAD_HILL", 14.0f, (float)hillTop));
  out.push_back(tinted(image("IMAGE_LOAD_HILLBRIGHT", 14.0f, (float)hillTop), 255, 255, 255, litA));

  {
    int v = (int)fl(255.0 - (double)fl((double)glow_ * 60.0));
    NightsCmd c = tinted(image("IMAGE_LOAD_OVERLAY", 272.0f, 493.0f), v, v, v, 255);
    c.op = NightsCmd::ImageF;
    out.push_back(c);
  }

  if (logoClock_ >= 0x76) {
    drawLogo(out, scale, yc);
    if (flash_ != 0) {
      int a = (int)fl((double)envelope(flash_) * 128.0);
      out.push_back(additive(tinted(image("IMAGE_LOAD_LOGO", (float)logoX_, (float)yc), 255, 255, 255, a)));
    }
    if (cnt_ > 0x384) {
      float f = title_.value((double)fl((double)(cnt_ - 0x384) / 100.0));
      int a = (int)fl((double)f * 255.0);
      out.push_back(tinted(image("IMAGE_LOAD_NIGHTS", (float)(logoX_ + 0x77), (float)(yc + 0xbd)), 255, 255, 255, a));
    }
  }

  if (bounceTick_ != 0) {
    float f = flicker_.value((double)fl((double)(cnt_ - bounceTick_) / 100.0));
    int a = (int)fl((double)f * 255.0);
    if (a > 0) {
      out.push_back(additive(tinted(image("IMAGE_LOAD_GLOW", (float)(logoX_ + 0x1c), (float)(yc + 0x22)), 255, 255, 255, a)));
    }
  }

  if (1.0f > progress_) {
    for (int i = 0; i < beams_; i++) drawBeam(out, i);
    NightsCmd t;
    t.op = NightsCmd::Text;
    t.img = kLoadingText;
    t.x = (float)textX(textWidth);
    t.y = (float)0x247;
    t.colorize = true;
    t.r = 0x91;
    t.g = 0xec;
    t.b = 0xff;
    t.a = 255;
    out.push_back(t);
  }

  if (cnt_ > 0x578) {
    float v = fl(fl((double)cnt_ - 1400.0) / 50.0);
    if (1.0 <= (double)v) v = 1.0f;
    int a = (int)fl((double)v * 255.0);
    out.push_back(tinted(image("IMAGE_LOAD_TM", (float)(logoX_ + 0x1f2), (float)(logoY_ + 0xde)), 255, 255, 255, a));
  }
}

void NightsLoading::drawParticles(std::vector<NightsCmd>& out) {
  for (const Particle& p : particles_) {
    int b = rng_.next() % 128;
    int g = rng_.next() % 128;
    int r = rng_.next() % 128;
    int h = rng_.next() % 2 + 1;
    int w = rng_.next() % 2 + 1;
    out.push_back(fill((float)(int)fl((double)p.x - 1.0), (float)(int)fl((double)p.y - 1.0), w, h, r, g, b, p.alpha, true));
    if (rng_.next() % 255 < p.alpha) {
      out.push_back(fill((float)(int)fl((double)p.x - 4.0), (float)(int)fl((double)p.y - 4.0), 8, 8, 2, 2, 1, 255, true));
    }
  }
}

void NightsLoading::drawOverlay(std::vector<NightsCmd>& out, int fairySize) {
  out.clear();
  if (state_ == 0) {
    int a = 255;
    int c = 0;
    if (cnt_ < 50) {
      c = stateCnt_;
      a = c * 255 / 50;
    } else if (cnt_ > 150) {
      c = 200 - stateCnt_;
      a = c * 255 / 50;
    }
    NightsCmd p = tinted(image("IMAGE_LOAD_POPCAP", 250.0f, 150.0f), 255, 255, 255, a);
    out.push_back(p);
    return;
  }
  bool live = state_ > 2 && state_ < 5;
  if (live) {
    if (doneCnt_ > 0 && doneCnt_ < 50) {
      int k = doneCnt_ < 25 ? doneCnt_ : 50 - doneCnt_;
      float v = fl(fl((double)k / 25.0) * 100.0);
      int col = (int)v;
      NightsCmd c;
      c.op = NightsCmd::Highlight;
      c.x = (float)kPlayX;
      c.y = (float)kPlayY;
      c.add = true;
      c.colorize = true;
      c.r = c.g = c.b = col;
      c.a = 255;
      out.push_back(c);
    }
    if (ballState_ > 0) {
      NightsCmd c = image("IMAGE_LOAD_BALL", ballX_, ballY_);
      c.op = NightsCmd::Centered;
      out.push_back(c);
    }
  }
  if (state_ > 1) {
    std::vector<LoadingFairies::Sprite> sprites;
    std::vector<LoadingFairies::Dot> dots;
    fairies_.layoutSprites(fairySize, sprites);
    for (const auto& s : sprites) {
      out.push_back(tinted(image(s.cel & 1 ? "IMAGE_LOAD_FLY1" : "IMAGE_LOAD_FLY0", (float)s.x, (float)s.y), 255, 255, 255, s.alpha));
    }
    drawParticles(out);
    fairies_.layoutDots(dots);
    for (const auto& d : dots) {
      out.push_back(fill((float)d.x, (float)d.y, d.w, d.h, (int)((d.rgb >> 16) & 255), (int)((d.rgb >> 8) & 255), (int)(d.rgb & 255), d.alpha, true));
    }
    out.push_back(image("IMAGE_SCREENFRAMELEFT", 0.0f, 0.0f));
    out.push_back(image("IMAGE_SCREENFRAMETOP", 50.0f, 0.0f));
    out.push_back(image("IMAGE_SCREENFRAMEBOTTOM", 50.0f, 566.0f));
    out.push_back(image("IMAGE_SCREENFRAMERIGHT", 750.0f, 0.0f));
  }
  if (state_ == 3) {
    int cut = (int)fl(fl((double)stateCnt_ * 255.0) / (double)stateLen_);
    out.push_back(fill(0, 0, 800, 600, 0, 0, 0, 255 - cut, false));
  }
}

}
