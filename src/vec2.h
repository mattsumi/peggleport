#pragma once
namespace pp {

struct V2 {
  float x = 0, y = 0;
  V2() = default;
  V2(float a, float b) : x(a), y(b) {}
  V2 operator+(V2 o) const { return {x + o.x, y + o.y}; }
  V2 operator-(V2 o) const { return {x - o.x, y - o.y}; }
  V2 operator*(float k) const { return {x * k, y * k}; }
  V2 operator-() const { return {-x, -y}; }
  float dot(V2 o) const { return x * o.x + y * o.y; }
  float len2() const { return x * x + y * y; }
};

}
