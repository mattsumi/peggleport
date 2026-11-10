#pragma once
#include <cstdint>
#include <vector>

#include "ui/fireworks.h"

namespace pp::ui {

class LoadingFairies {
 public:
  struct Fairy {
    float t = 0, dx = 0, dy = 0, x = 0, y = 0, alpha = 0;
  };
  struct Spark {
    float x = 0, y = 0;
    int age = 0;
    float vx = 0, vy = 0;
  };
  struct Sprite {
    int cel = 0, x = 0, y = 0, alpha = 0;
  };
  struct Dot {
    int x = 0, y = 0, w = 0, h = 0, alpha = 0;
    uint32_t rgb = 0;
  };

  static constexpr int kMax = 5;

  explicit LoadingFairies(CrtRand& rng) : rng_(rng) {}

  void restartScene() { glow_ = 0.0f; }
  void update(bool night, int mouseX, int mouseY);
  int click(int x, int y);
  void layout(int imageSize, std::vector<Sprite>& sprites, std::vector<Dot>& dots);
  void layoutSprites(int imageSize, std::vector<Sprite>& sprites);
  void layoutDots(std::vector<Dot>& dots);

  float glow() const { return glow_; }
  const std::vector<Fairy>& fairies() const { return fairies_; }
  const std::vector<Spark>& sparks() const { return sparks_; }

 private:
  void updateSparks();
  void updateGlow();
  void spawn();
  void moveFairy(Fairy& f, int mouseX, int mouseY);
  void leaveTrail(int x, int y);
  void burst(int x, int y);

  CrtRand& rng_;
  float glow_ = 0.0f;
  std::vector<Fairy> fairies_;
  std::vector<Spark> sparks_;
};

}
