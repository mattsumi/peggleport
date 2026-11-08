#include "ui/sparkle.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace pp::ui {

namespace {

const char* const kRateCurve = "b+0,1,0,1,####  K[5|U   ^gl4>   v#C<v";
const char* const kSizeCurve = "b+0,1,0.01,0.4,####    `i###     AM###";
const char* const kFadeCurve = "b+0,1,0.01,0.4,f###    Y~###     G####";

int digit(unsigned char c) {
  int v = c >= 0x5c ? (int)c - 1 : (int)c;
  return v - 0x23;
}

float unit(unsigned char c) { return (float)digit(c) / 90.0f; }

float degrees(const std::string& s, size_t at) {
  int v = digit((unsigned char)s[at]) * 90;
  v = (v + digit((unsigned char)s[at + 1])) * 90;
  v += digit((unsigned char)s[at + 2]);
  float a = (float)v * 360.0f;
  return a / 729000.0f;
}

bool number(const std::string& s, size_t& pos, float& out) {
  size_t end = s.find(',', pos);
  if (end == std::string::npos) return false;
  out = (float)std::strtod(s.substr(pos, end - pos).c_str(), nullptr);
  pos = end + 1;
  return true;
}

}

bool SparkCurve::parse(const std::string& text) {
  pts_.clear();
  pieces_.clear();
  lo_ = 0.0f;
  hi_ = 1.0f;
  dur_ = 1.0f;
  free_ = false;
  tangents_ = false;
  int version = 0;
  size_t pos = 1;
  if (!text.empty() && text[0] >= 'a' && text[0] <= 'b') version = text[0] - 'a';
  if (version >= 1) {
    if (text.size() < 2) return false;
    int flags = digit((unsigned char)text[1]);
    free_ = (flags & 1) != 0;
    tangents_ = (flags & 8) != 0;
    pos = 2;
  }
  float skip = 0.0f;
  if (!number(text, pos, lo_) || !number(text, pos, hi_) || !number(text, pos, skip)) return false;
  if (version >= 1 && !number(text, pos, dur_)) return false;
  const std::string data = text.substr(pos);
  float x = 0.0f;
  size_t i = 0;
  while (i < data.size()) {
    Point p{x, unit((unsigned char)data[i]), 0.0f};
    i++;
    if (tangents_) {
      if (i + 3 > data.size()) return false;
      p.slope = degrees(data, i);
      i += 3;
    }
    pts_.push_back(p);
    while (i < data.size()) {
      unsigned char c = (unsigned char)data[i++];
      if (c == ' ') {
        x = x + 0.1f;
        continue;
      }
      float step = unit(c) * 0.1f;
      x = std::min(1.0f, step + x);
      break;
    }
  }
  const float pi = 3.14159274f;
  for (Point& p : pts_) {
    float rad = (p.slope * pi) / 180.0f;
    p.slope = (float)std::tan((double)rad);
  }
  for (size_t k = 0; k + 1 < pts_.size(); k++) {
    const Point& p0 = pts_[k];
    const Point& p1 = pts_[k + 1];
    float h = p1.x - p0.x;
    float slope = (p1.y - p0.y) / h;
    float c = (slope - p0.slope) / h;
    float c2 = (p1.slope - slope) / h;
    float d = (c2 - c) / h;
    pieces_.push_back({p0.y, p0.slope, c, d});
  }
  return pts_.size() >= 2;
}

float SparkCurve::spline(float x) const {
  for (size_t i = 0; i < pieces_.size(); i++) {
    if (!(pts_[i + 1].x > x)) continue;
    const Piece& q = pieces_[i];
    float t = x - pts_[i].x;
    float s = x - pts_[i + 1].x;
    float bt = q.b * t;
    float r = bt + q.a;
    float t2 = t * t;
    float ts = t2 * s;
    float ct = q.c * t2;
    float r2 = ct + r;
    float dt = ts * q.d;
    return dt + r2;
  }
  return pts_.back().y;
}

float SparkCurve::eval(float t) const {
  if (pieces_.empty() || dur_ == 0.0f) return 0.0f;
  float u = t / dur_;
  if (!(u < 1.0f)) u = 1.0f;
  float span = hi_ - lo_;
  float v = span * spline(u);
  v = v + lo_;
  if (!free_) {
    float a = std::min(lo_, hi_), b = std::max(lo_, hi_);
    v = std::min(std::max(v, a), b);
  }
  return v;
}

SparkleField::SparkleField(int rx, int ry, int rw, int rh, float emit, uint32_t seed) : seed_(seed) {
  rate_.parse(kRateCurve);
  size_.parse(kSizeCurve);
  fade_.parse(kFadeCurve);
  cx_ = (float)(rx + rw / 2);
  cy_ = (float)(ry + rh / 2);
  w_ = rw;
  h_ = rh;
  spawnRate_ = (float)((double)emit * 0.00008);
}

uint32_t SparkleField::next() {
  seed_ = seed_ * 214013u + 2531011u;
  return (seed_ >> 16) & 0x7fffu;
}

void SparkleField::spawn() {
  Particle p;
  uint32_t rx = next(), ry = next();
  p.x = ((float)(w_ > 0 ? rx % (uint32_t)w_ : 0u) + cx_) - (float)w_ * 0.5f;
  p.y = ((float)(h_ > 0 ? ry % (uint32_t)h_ : 0u) + cy_) - (float)h_ * 0.5f;
  p.image = (int)(next() % (uint32_t)kImages);
  next();
  parts_.push_back(p);
}

bool SparkleField::update() {
  int before = (int)acc_;
  float most = (float)(w_ * h_) * spawnRate_;
  float t = (float)counter_ / (float)period_;
  float r = rate_.eval(t);
  float add = r * most;
  acc_ = add + acc_;
  int target = (int)acc_;
  for (int i = before; i < target; i++) spawn();
  const float life = size_.duration();
  for (int j = (int)parts_.size() - 1; j >= 0; j--) {
    Particle& p = parts_[(size_t)j];
    p.age++;
    if ((float)p.age / 100.0f > life) {
      parts_[(size_t)j] = parts_.back();
      parts_.pop_back();
    } else {
      p.vy = p.ay + p.vy;
      p.x = p.vx + p.x;
      p.y = p.y + p.vy;
    }
  }
  counter_++;
  if (rate_.duration() > t) return true;
  return !parts_.empty();
}

float SparkleField::scale(const Particle& p) const { return size_.eval((float)p.age / 100.0f); }

int SparkleField::alpha(const Particle& p) const {
  float a = fade_.eval((float)p.age / 100.0f);
  a = a * 255.0f;
  int v = (int)a;
  return std::min(std::max(v, 0), 255);
}

}
