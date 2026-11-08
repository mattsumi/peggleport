#include "ui/fairies.h"

#include <cmath>

namespace pp::ui {

namespace {

constexpr double kPi = 3.1415927410125732;
constexpr double kPhaseStep = 0.00004;
constexpr double kDecay = 0.9900000095367432;
constexpr double kFadeStep = 0.009999999776482582;
constexpr double kGlowStep = 0.0011500000255182385;
constexpr double kGlowGain = 1.4500000476837158;
constexpr double kReachX = 280.00000417232513;
constexpr double kReachX2 = 70.00000104308128;
constexpr double kReachY = 240.0000035762787;
constexpr double kReachY2 = 53.174999356269836;
constexpr double kGravity = 0.00019999999494757503;
constexpr double kTwoMilli = 0.0020000000949949026;
constexpr double kMilli = 0.0010000000474974513;

float fl(double v) { return (float)v; }

}

void LoadingFairies::updateSparks() {
  size_t i = 0;
  while (i < sparks_.size()) {
    Spark& p = sparks_[i];
    p.age -= 1;
    if (p.age <= 0) {
      p = sparks_.back();
      sparks_.pop_back();
      continue;
    }
    p.y = fl((double)p.vy + (double)p.y);
    p.x = fl((double)p.vx + (double)p.x);
    p.vy = fl((double)p.vy + kGravity);
    i++;
  }
}

void LoadingFairies::updateGlow() {
  float v = fl((double)glow_ + kGlowStep);
  glow_ = 1.0 < (double)v ? 1.0f : v;
}

void LoadingFairies::spawn() {
  Fairy f;
  int r = rng_.next() % 1000;
  f.t = fl((double)r * kMilli);
  fairies_.push_back(f);
}

void LoadingFairies::leaveTrail(int x, int y) {
  Spark s;
  s.x = (float)x;
  s.y = (float)y;
  s.age = 255;
  sparks_.push_back(s);
}

void LoadingFairies::burst(int x, int y) {
  for (int i = 0; i < 30; i++) {
    Spark s;
    s.x = (float)x;
    s.y = (float)y;
    s.age = 255;
    int r1 = rng_.next() % 1000;
    float theta = fl((double)fl((double)r1 * kTwoMilli) * kPi);
    int r2 = rng_.next() % 1000;
    float speed = fl((double)r2 * kMilli);
    float sn = fl(std::sin((double)theta));
    s.vx = fl((double)sn * (double)speed);
    float cs = fl(std::cos((double)theta));
    s.vy = fl((double)cs * (double)speed);
    sparks_.push_back(s);
  }
}

void LoadingFairies::moveFairy(Fairy& f, int mouseX, int mouseY) {
  f.t = fl((double)f.t + kPhaseStep);
  if (1.0 < (double)f.t) f.t = fl((double)f.t - 1.0);
  float a = fl(fl((double)f.t * kPi) * 2.0);
  float a4 = fl((double)a * 4.0);
  float c4 = fl(std::cos((double)a4));
  float a3 = fl((double)a * 3.0);
  float c3 = fl(std::cos((double)a3));
  float s3 = fl(std::sin((double)a3));
  float s4 = fl(std::sin((double)a4));
  f.x = fl(fl(fl((double)c4 * kReachX) + 400.0) + fl((double)c3 * kReachX2));
  f.y = fl(fl(300.0 - fl((double)s3 * kReachY)) - fl((double)s4 * kReachY2));
  f.dx = fl((double)f.dx * kDecay);
  f.dy = fl((double)f.dy * kDecay);
  float next = fl((double)f.alpha + kFadeStep);
  f.alpha = 1.0 <= (double)next ? 1.0f : next;
  float ddx = fl((double)fl((double)f.dx + (double)f.x) - (double)mouseX);
  float ddy = fl((double)fl((double)f.dy + (double)f.y) - (double)mouseY);
  float d2 = fl(fl((double)ddx * (double)ddx) + fl((double)ddy * (double)ddy));
  if (d2 < 10000.0f && 1.0f < d2) {
    f.dx = fl((double)fl((double)fl((double)ddx * 100.0) / (double)d2) + (double)f.dx);
    f.dy = fl((double)fl((double)fl((double)ddy * 100.0) / (double)d2) + (double)f.dy);
  }
  if (rng_.next() % 6 == 0) {
    int ty = (int)fl((double)f.dy + (double)f.y);
    int tx = (int)fl((double)f.dx + (double)f.x);
    leaveTrail(tx, ty);
  }
}

void LoadingFairies::update(bool night, int mouseX, int mouseY) {
  updateSparks();
  updateGlow();
  bool open = night && fl((double)glow_ * kGlowGain) >= 1.0f;
  if (open && (int)fairies_.size() < kMax && rng_.next() % 100 == 0) spawn();
  for (size_t i = 0; i < fairies_.size(); i++) moveFairy(fairies_[i], mouseX, mouseY);
}

int LoadingFairies::click(int x, int y) {
  int popped = 0;
  size_t i = 0;
  while (i < fairies_.size()) {
    const Fairy& f = fairies_[i];
    float ddx = fl((double)fl((double)f.dx + (double)f.x) - (double)x);
    float ddy = fl((double)fl((double)f.dy + (double)f.y) - (double)y);
    float d2 = fl(fl((double)ddx * (double)ddx) + fl((double)ddy * (double)ddy));
    if (!((double)d2 < 600.0)) {
      i++;
      continue;
    }
    popped++;
    int by = (int)fl((double)f.dy + (double)f.y);
    int bx = (int)fl((double)f.dx + (double)f.x);
    burst(bx, by);
    fairies_.erase(fairies_.begin() + (long)i);
  }
  return popped;
}

void LoadingFairies::layout(int imageSize, std::vector<Sprite>& sprites, std::vector<Dot>& dots) {
  float half = (float)(imageSize / 2);
  for (const Fairy& f : fairies_) {
    Sprite s;
    s.cel = (int)(rng_.next() & 1);
    s.alpha = (int)fl((double)f.alpha * 255.0);
    float px = fl((double)f.dx + (double)f.x);
    float py = fl((double)f.dy + (double)f.y);
    s.y = (int)fl((double)py - (double)half);
    s.x = (int)fl((double)px - (double)half);
    sprites.push_back(s);
  }
  for (const Spark& p : sparks_) {
    Dot d;
    int b = (int)(rng_.next() % 32);
    int g = (int)(rng_.next() % 32) + 32;
    int r = (int)(rng_.next() % 32);
    d.rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    d.alpha = p.age;
    d.h = (int)(rng_.next() % 2) + 1;
    d.w = (int)(rng_.next() % 2) + 1;
    d.y = (int)fl((double)p.y - 1.0);
    d.x = (int)fl((double)p.x - 1.0);
    dots.push_back(d);
  }
}

}
