#pragma once
#include <cstdint>
#include <vector>

namespace pp::ui {

class CrtRand {
 public:
  explicit CrtRand(uint32_t seed = 1) : seed_(seed) {}
  void seed(uint32_t s) { seed_ = s; }
  int next() {
    seed_ = seed_ * 214013u + 2531011u;
    return (int)((seed_ >> 16) & 0x7fffu);
  }

 private:
  uint32_t seed_;
};

class StoryFireworks {
 public:
  struct Particle {
    float x = 0, y = 0, vx = 0, vy = 0;
    int age = 0, life = 0, type = 0;
    uint32_t color = 0;
  };

  StoryFireworks(CrtRand& rng, int screenW, int screenH);
  bool update(int updateCount);
  float scroll() const { return scroll_; }
  const std::vector<Particle>& particles() const { return parts_; }
  bool rocketFlying() const { return countdown_ >= 0; }
  float rocketX() const { return rx_; }
  float rocketY() const { return ry_; }
  float rocketVX() const { return rvx_; }
  float rocketVY() const { return rvy_; }

 private:
  void burst();
  void spawnRound(int count, int type, uint32_t color, int spread, float spreadSub);
  void newRocket();
  void updateParticles();
  float unitAngle();
  void finish(Particle& p, uint32_t color, int type);

  CrtRand& rng_;
  int w_, h_;
  float rx_ = 0, ry_ = 0, rvx_ = 0, rvy_ = 0;
  int countdown_ = -1;
  float scroll_ = 0;
  std::vector<Particle> parts_;
};

}
