#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pp::ui {

class SparkCurve {
 public:
  bool parse(const std::string& text);
  float eval(float t) const;
  float duration() const { return dur_; }

 private:
  struct Point { float x, y, slope; };
  struct Piece { float a, b, c, d; };
  float spline(float x) const;
  float lo_ = 0.0f, hi_ = 1.0f, dur_ = 1.0f;
  bool free_ = false;
  bool tangents_ = false;
  std::vector<Point> pts_;
  std::vector<Piece> pieces_;
};

class SparkleField {
 public:
  struct Particle {
    float x = 0, y = 0, vx = 0, vy = 0, ay = 0;
    int age = 0;
    int image = 0;
  };
  static constexpr int kImages = 6;

  SparkleField(int rx, int ry, int rw, int rh, float emit, uint32_t seed);
  bool update();
  const std::vector<Particle>& particles() const { return parts_; }
  float scale(const Particle& p) const;
  int alpha(const Particle& p) const;

 private:
  uint32_t next();
  void spawn();
  SparkCurve rate_, size_, fade_;
  float cx_ = 0, cy_ = 0, spawnRate_ = 0, acc_ = 0;
  int w_ = 0, h_ = 0, period_ = 400, counter_ = 0;
  uint32_t seed_ = 0;
  std::vector<Particle> parts_;
};

}
