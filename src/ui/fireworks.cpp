#include "ui/fireworks.h"

#include <cmath>

namespace pp::ui {

namespace {

constexpr float kTwoPiF = 6.283f;

float fl(double v) { return (float)v; }

}

StoryFireworks::StoryFireworks(CrtRand& rng, int screenW, int screenH) : rng_(rng), w_(screenW), h_(screenH) {
  ry_ = (float)screenH;
}

float StoryFireworks::unitAngle() {
  float a = fl((double)(rng_.next() % 100) * (double)kTwoPiF);
  return fl((double)a / 100.0);
}

void StoryFireworks::finish(Particle& p, uint32_t color, int type) {
  p.color = color;
  p.type = type;
  p.life = rng_.next() % 30 + 70;
}

void StoryFireworks::spawnRound(int count, int type, uint32_t color, int spread, float spreadSub) {
  for (int i = 0; i < count; i++) {
    Particle p;
    float t = (float)(rng_.next() % spread);
    p.x = (t + rx_) - spreadSub;
    t = (float)(rng_.next() % spread);
    p.y = (t + ry_) - spreadSub;
    float ang = unitAngle();
    float sp;
    if (type == 3) {
      int n = 10 + rng_.next() % 10;
      sp = fl((double)0.1f * (double)n);
    } else {
      int n = rng_.next() % 8 + 2;
      sp = fl((double)n * (double)0.2f);
    }
    float s = fl(std::sin((double)ang));
    p.vx = s * sp;
    float c = fl(std::cos((double)ang));
    p.vy = c * sp;
    finish(p, color, type);
    parts_.push_back(p);
  }
}

void StoryFireworks::burst() {
  uint32_t color = 0xff5511;
  int pick = rng_.next() % 3;
  if (pick == 0) color = 0x55ff22;
  else if (pick == 1) color = 0x3355ff;
  spawnRound(60, 0, color, 10, 5.0f);
  spawnRound(120, 2, color, 10, 5.0f);
  spawnRound(60, 3, color, 8, 4.0f);
  if (rng_.next() % 2 == 0) {
    for (int i = 0; i < 30; i++) {
      Particle p;
      float t = (float)(rng_.next() % 8);
      p.x = (t + rx_) - 4.0f;
      t = (float)(rng_.next() % 8);
      p.y = (t + ry_) - 4.0f;
      float k = fl((double)(rng_.next() % 100) * (double)0.4f);
      k = fl((double)k / 100.0);
      float ang = fl((double)2.2f + (double)k);
      int n = 4 + rng_.next() % 2;
      float sp = fl((double)0.2f * (double)n);
      float s = fl(std::sin((double)ang));
      p.vx = s * sp;
      float c = fl(std::cos((double)ang));
      p.vy = c * sp;
      finish(p, color, 4);
      parts_.push_back(p);
    }
    for (int i = 0; i < 60; i++) {
      Particle p;
      float t = (float)(rng_.next() % 8);
      p.x = (t + rx_) - 4.0f;
      t = (float)(rng_.next() % 8);
      p.y = (t + ry_) - 4.0f;
      float k = fl((double)(rng_.next() % 100) * (double)0.4f);
      k = fl((double)k / 100.0);
      float ang = fl((double)-2.2f - (double)k);
      int n = 4 + rng_.next() % 2;
      float sp = fl((double)0.2f * (double)n);
      float s = fl(std::sin((double)ang));
      p.vx = s * sp;
      float c = fl(std::cos((double)ang));
      p.vy = c * sp;
      finish(p, color, 4);
      parts_.push_back(p);
    }
    for (int i = 0; i < 90; i++) {
      Particle p;
      float t = (float)(rng_.next() % 10);
      p.x = (t + rx_) - 5.0f;
      t = (float)(rng_.next() % 10);
      p.y = (t + ry_) - 5.0f;
      float k = fl((double)(rng_.next() % 100) * (double)2.0f);
      k = fl((double)k / 100.0);
      float ang = fl((double)-1.0f + (double)k);
      rng_.next();
      float sp = fl((double)0.2f * 5.0);
      float s = fl(std::sin((double)ang));
      p.vx = s * sp;
      float c = fl(std::cos((double)ang));
      p.vy = c * sp;
      finish(p, color, 4);
      parts_.push_back(p);
    }
  } else {
    for (int i = 0; i < 190; i++) {
      Particle p;
      float t = (float)(rng_.next() % 6);
      p.x = (t + rx_) - 3.0f;
      t = (float)(rng_.next() % 6);
      p.y = (t + ry_) - 3.0f;
      float k = fl((double)(rng_.next() % 100) * (double)kTwoPiF);
      k = fl((double)k / 100.0);
      float ang = fl(0.0 + (double)k);
      float c = fl(std::cos((double)ang));
      float radius = fl((double)0.95f * (double)fl((double)c + (double)1.0f));
      float s = fl(std::sin((double)ang));
      p.vx = s * radius;
      p.vy = radius * c;
      if (0.0f < p.vy) {
        float m = 1.0f - c;
        p.vx = m * p.vx;
      }
      p.vy = fl((double)p.vy - (double)0.75f);
      finish(p, color, 4);
      parts_.push_back(p);
    }
  }
}

void StoryFireworks::newRocket() {
  bool half = rng_.next() % 2 == 0;
  ry_ = (float)h_;
  float base = half ? 0.5f : 1.0f;
  int r = rng_.next() % 500;
  float q = fl((double)r / 1000.0);
  q = fl((double)q + (double)base);
  q = fl((double)q * (double)w_);
  rx_ = fl((double)q * 0.5);
  int j = 10 - rng_.next() % 20;
  rvx_ = fl((double)0.12f * (double)j);
  rvy_ = -4.0f;
  countdown_ = 40 + rng_.next() % 80;
}

void StoryFireworks::updateParticles() {
  for (int i = 0; i < 3; i++) {
    Particle p;
    float t = (float)(rng_.next() % 3);
    p.x = t + rx_;
    t = (float)(rng_.next() % 3);
    p.y = (t + ry_) - 1.0f;
    p.life = 30;
    p.type = 1;
    float ang = unitAngle();
    float sp = fl((double)(rng_.next() % 10) * (double)0.03f);
    float s = fl(std::sin((double)ang));
    p.vx = s * sp;
    float c = fl(std::cos((double)ang));
    p.vy = c * sp;
    parts_.push_back(p);
  }
  size_t i = 0;
  while (i < parts_.size()) {
    Particle& p = parts_[i];
    if (p.age >= p.life) {
      p = parts_.back();
      parts_.pop_back();
    } else {
      p.age++;
      p.x = p.vx + p.x;
      p.y = p.vy + p.y;
      p.vy = fl((double)0.01f + (double)p.vy);
      i++;
    }
  }
}

bool StoryFireworks::update(int updateCount) {
  bool pop = false;
  if (countdown_ > 0) {
    rx_ = rvx_ + rx_;
    countdown_--;
    ry_ = rvy_ + ry_;
  }
  if (countdown_ <= 0) {
    if ((double)ry_ < 500.0) {
      burst();
      if (updateCount > 20) pop = true;
    }
    newRocket();
  }
  updateParticles();
  scroll_ = fl((double)0.25f + (double)scroll_);
  if (4100.0 < (double)scroll_) scroll_ = 0.0f;
  return pop;
}

}
