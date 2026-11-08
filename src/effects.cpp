#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <utility>
#include <vector>
#include <string>
#include <map>

#include "render.h"

namespace pp {

namespace {
constexpr float PI = 3.14159265f;

const char* kFontNames[] = {
    "FONT_OVERLOAD16", "FONT_OVERLOAD16OUTLINE", "FONT_OVERLOAD16OUTLINE2",
};

std::string commaSep(long long v) {
  std::string s = std::to_string(v < 0 ? -v : v), out;
  for (size_t i = 0; i < s.size(); i++) {
    if (i && (s.size() - i) % 3 == 0) out += ',';
    out += s[i];
  }
  return v < 0 ? "-" + out : out;
}
}

const char* fxFontName(int id) {
  switch (id) {
    case 13: return "FONT_OVERLOAD16";
    case 14: return "FONT_OVERLOAD16OUTLINE";
    case 15: return "FONT_OVERLOAD16OUTLINE2";
    case 29: return "FONT_PLAIN";
    case 30: return "FONT_PLAIN2BIG";
    case 31: return "FONT_SMALL";
    case 32: return "FONT_SMALLMONO";
    case 33: return "FONT_SCORE_DISP";
    case 34: return "FONT_SMALL_DISP";
    case 35: return "FONT_BALLSLEFT";
    case 36: return "FONT_BUTTON";
    case 37: return "FONT_STAGE";
    case 38: return "FONT_BONK";
    case 39: return "FONT_OVERLOAD10";
    case 40: return "FONT_OVERLOAD12";
    case 41: return "FONT_OVERLOAD24";
    case 42: return "FONT_OVERLOADMONO10";
    case 43: return "FONT_OVERLOADMONO12";
    case 44: return "FONT_OVERLOAD10OUTLINE";
    case 45: return "FONT_OVERLOAD12OUTLINE";
    case 46: return "FONT_OVERLOAD24OUTLINE";
    case 47: return "FONT_OVERLOAD24GRAD1";
    case 48: return "FONT_OVERLOAD24GRAD2";
    case 49: return "FONT_PLAINOUTLINE";
    case 50: return "FONT_PLAIN2";
    case 51: return "FONT_PLAIN2OUTLINE";
    case 52: return "FONT_PLAIN2BIGOUTLINE";
  }
  (void)kFontNames;
  return "FONT_OVERLOAD12OUTLINE";
}

FxText& Renderer::fxText(const std::string& s, float x, float y, int font) {
  FxText t;
  t.id = ++fx_.nextId;
  size_t p = 0;
  for (;;) {
    size_t n = s.find('\n', p);
    t.lines.push_back(s.substr(p, n == std::string::npos ? std::string::npos : n - p));
    if (n == std::string::npos) break;
    p = n + 1;
  }
  t.colorCodes = s.find('^') != std::string::npos;
  t.font = font < 0 ? 0x2d : font;
  t.x = x;
  t.y = y;
  t.vy = -0.1f;
  t.life = 100;
  t.layer = 1;
  fx_.texts.push_back(std::move(t));
  return fx_.texts.back();
}

FxEffect& Renderer::fxAdd(int type, float x, float y, int prio, bool front) {
  prio = std::clamp(prio, -1, 3);
  auto& v = fx_.eff[prio + 1];
  FxEffect e;
  e.type = type;
  e.x = x;
  e.y = y;
  e.prio = prio;
  e.life = 100;
  if (front) {
    v.insert(v.begin(), e);
    return v.front();
  }
  v.push_back(e);
  return v.back();
}

namespace {
V2 pegCenter(const Obj& o) {
  if (o.shape == Shape::Brick && o.brickInner.size() > 1 && o.brickOuter.size() > 1) {
    V2 a = (o.brickInner[o.brickInner.size() / 2] + o.brickOuter[o.brickOuter.size() / 2]) * 0.5f;
    float c = std::cos(o.rot), s = std::sin(o.rot);
    a = a * o.scale;
    return o.pos + V2{a.x * c + a.y * s, a.y * c - a.x * s};
  }
  return o.pos;
}
}

namespace {
uint32_t hslToRgb(int h, int s, int l) {
  int r, g, b;
  double v = (l < 128) ? (l * (255 + s)) / 255 : (l + s - l * s / 255);
  int y = (int)(2 * l - v);
  int c = (6 * h) / 256;
  int x = (int)(y + (v - y) * ((h - (c * 256 / 6)) * 6) / 255);
  if (x > 255) x = 255;
  int z = (int)(v - (v - y) * ((h - (c * 256 / 6)) * 6) / 255);
  if (z < 0) z = 0;
  switch (c) {
    case 0: r = (int)v; g = x; b = y; break;
    case 1: r = z; g = (int)v; b = y; break;
    case 2: r = y; g = (int)v; b = x; break;
    case 3: r = y; g = z; b = (int)v; break;
    case 4: r = x; g = y; b = (int)v; break;
    case 5: r = (int)v; g = y; b = z; break;
    default: r = (int)v; g = x; b = y; break;
  }
  return ((uint32_t)std::clamp(r, 0, 255) << 16) | ((uint32_t)std::clamp(g, 0, 255) << 8) | (uint32_t)std::clamp(b, 0, 255);
}
uint32_t rgbaOf(uint32_t rgb, int a) { return (rgb << 8) | (uint32_t)std::clamp(a, 0, 255); }
}

namespace {
enum : int {
  T_FEVERBLAST_ROCKET = 4, T_COIN = 8, T_STREAK = 6, T_TRAIL = 0x13, T_STAR = 0x14, T_CATCHERGLOW = 0x1a, T_CATCHERSPARK = 0x1b, T_LETTER = 0x1d,
  T_STYLESPARK = 0x2e, T_CHAIN_BOLT = 0x60, T_CHAIN_ARC = 0x61, T_CHAIN_EMBER = 0x62,
};
int bucketIndex(const Board& b) {
  for (size_t i = 0; i < b.objs.size(); i++)
    if (b.objs[i].shape == Shape::Hole && b.objs[i].id == "freeball") return (int)i;
  return -1;
}
}

void Renderer::fxStyleText(const Board&, const std::string& name, int pts, float x, float y, bool big) {
  FxState& S = fx_;
  if (S.frame - S.styleFrame >= 20 || std::fabs(S.styleX - x) >= 20 || std::fabs(S.styleY - y) >= 20) {
    S.styleX = x;
    S.styleY = y;
    S.styleStack = 0;
  } else {
    S.styleStack++;
    y -= 50.0f * S.styleStack;
    x += (S.styleX >= 325.0f ? -50.0f : 50.0f) * S.styleStack;
  }
  S.styleFrame = S.frame;
  std::string pl = pts >= 10000 ? "+" + commaSep(pts) : "+" + std::to_string(pts);
  float yy = y;
  Font* f0 = font("FONT_OVERLOAD16OUTLINE");
  for (int i = 0; i < 2; i++) {
    FxText& t = fxText(i == 0 ? name : pl, x, yy, i == 0 ? 0x0e : 0x2d);
    t.layer = 2;
    t.life = big ? 250 : 200;
    t.vy = 0;
    t.fadeOut = 150;
    t.color = 0x80ff80;
    t.age = -15 * i;
    t.scales = {{0.5f, 0}, {2.0f, 10}, {1.0f, 30}};
    t.scale = 0.5f;
    if (big) {
      t.rotUntil = 10;
      t.rotSpeed = 0.3f;
    }
    if (i == 0 && f0) yy += 5 + f0->height() / 2;
  }
  if (big) {
    for (int i = 0; i < 35; i++) {
      float r = 1.0f - (float)S.rand(1000) / 1000.0f * ((float)S.rand(1000) / 1000.0f);
      (void)r;
      float u = (float)S.rand(1000) / 1000.0f;
      r = 1.0f - u * u;
      float th = 2.0f * PI * i / 35.0f;
      float cx = x, cy = y - 10;
      FxEffect& e = fxAdd(T_STYLESPARK, cx + std::sin(th) * 1.5f * 60 * r, cy + std::cos(th) * 0.5f * 60 * r, 0);
      e.life = 40;
      e.i38 = (int)cx;
      e.i3c = (int)cy;
      e.b40 = true;
    }
  }
}

void Renderer::fxFeverHole(float X, float Y, int level) {
  FxState& S = fx_;
  static const int nRays[3] = {10, 15, 25}, rayLife[3] = {150, 180, 250}, rayLen[3] = {160, 240, 300}, nBase[3] = {50, 60, 70};
  static const float lvScale[3] = {0.7f, 0.8f, 1.0f};
  const int lifeC = rayLife[level], lenBase = rayLen[level], N = nBase[level];
  const float scl = lvScale[level];
  for (int i = 0; i < nRays[level]; i++) {
    FxEffect& r = fxAdd(0x10, X, Y + 20, 1);
    r.life = lifeC;
    r.age = -S.rand(5);
    r.i30 = lenBase + S.rand(100);
    r.i38 = level;
    r.f28 = (float)(S.rand(21) - 10) * PI / 180.0f;
    r.f2c = 0.01f * r.f28;
    if (std::fabs(r.f2c) > 0.001f) r.f2c *= 0.5f;
  }
  static const uint32_t col1[9] = {0x66ff, 0x4444ff, 0x8888ff, 0xff00, 0xbbff00, 0x44ff, 0xff2200, 0xcc5515, 0x88aa30};
  static const uint32_t col2[9] = {0x66ff, 0x4444ff, 0x8888ff, 0xff00, 0xbbff00, 0x44ff, 0xff2200, 0xbb4415, 0x999930};
  for (int batch = 0; batch < 2; batch++) {
    int count = (int)((float)N * scl);
    for (int k = 0; k < count; k++) {
      int delay = -(S.rand(lifeC - 30) + 30);
      int life = batch == 0 ? (int)((float)(lifeC + delay) / 1.2f) : (lifeC + delay) / 2;
      if (life <= 10) continue;
      if (life > (int)(1.5f * (float)N)) life = (int)(1.5f * (float)N);
      float u = (float)S.rand(1000) / 500.0f - 1.0f;
      float th = (u * 15.0f + 90.0f) * PI / 180.0f;
      float c = std::cos(th), sn = std::sin(th);
      float u2 = (float)S.rand(1000) / 500.0f - 1.0f;
      float speed = 7.0f * scl + u2;
      FxEffect& p = fxAdd(batch == 0 ? 0x11 : 6, 60.0f * c + X, Y, 1);
      p.age = delay;
      p.ay = 1.5f * 0.05f;
      if (batch == 1) p.ay = 1.0f * 0.05f;
      p.f28 = (float)S.rand(360) * PI / 180.0f;
      p.f2c = 0.02f + 0.1f * (float)S.rand(1000) / 1000.0f;
      p.vx = speed * c;
      p.vy = -sn * speed;
      if (p.vx > 0) p.f2c = -p.f2c;
      p.life = life;
      p.i30 = 20 + S.rand(30);
      int sel = 3 * level + S.rand(3);
      uint32_t col = (batch == 0 ? col1 : col2)[std::clamp(sel, 0, 8)];
      p.i34 = (int)col;
      if (batch == 0) {
        p.i38 = 3 - level;
        if (S.rand(4) == 0) p.i34 = 0xffffff;
        else p.i38 = 0;
      } else {
        p.i38 = 3;
      }
    }
  }
}

void Renderer::fxCannonBlast(float x, float y, float angle) {
  FxState& S = fx_;
  y += 4;
  const float base = angle + 1.5707964f;
  auto unit = [&](int n) { return (float)S.rand(n) / (float)n; };
  for (int i = 0; i < 40; i++) {
    float speed = 0.3f + 1.5f * (float)S.rand(1000) / 1000.0f;
    float u = (float)S.rand(1000) / 500.0f - 1.0f;
    float th = base + 0.5f * (speed * 0.2f - 1.0f) * u;
    float r = (float)S.rand(10);
    float c = std::cos(th), s = std::sin(th);
    float ex = s * r + x + (float)S.rand(10) - 5.0f, ey = c * r + y + (float)S.rand(10) - 5.0f;
    FxEffect& p = fxAdd(T_STREAK, ex, ey, 2);
    p.i34 = 0x994411;
    p.i38 = 3;
    p.vx = s * speed;
    p.vy = c * speed;
    p.life = 5 + S.rand(30);
    p.ay = 0.1f * 0.05f;
  }
  for (int i = 0; i < 35; i++) {
    float u = (float)S.rand(1000) / 500.0f - 1.0f;
    float th = base + 0.7f * u;
    float s = std::sin(th), c = std::cos(th);
    float r = (float)(S.rand(20) - 10);
    float speed = 0.3f + 1.5f * (float)S.rand(1000) / 1000.0f;
    float ex = r * s + x + (float)S.rand(10) - 5.0f, ey = r * c + y + (float)S.rand(10) - 5.0f;
    FxEffect& p = fxAdd(0x28, ex, ey, 2);
    p.i38 = 1;
    p.i34 = 0xaaaaaa;
    p.vx = speed * s;
    p.vy = speed * c - 0.2f;
    p.life = (int)((float)(30 + S.rand(10)) - 8.0f * speed);
    p.ay = 0.1f * 0.05f;
  }
  for (int i = 0; i < 35; i++) {
    float u = (float)S.rand(1000) / 500.0f - 1.0f;
    float th = base + 0.4f * u;
    float s = std::sin(th), c = std::cos(th);
    float r = (float)(S.rand(20) - 10);
    float speed = 0.1f + 2.0f * (float)S.rand(1000) / 1000.0f;
    float ex = r * s + x + (float)S.rand(10) - 5.0f, ey = r * c + y + (float)S.rand(10) - 5.0f;
    FxEffect& p = fxAdd(0x29, ex, ey, 3);
    p.i38 = S.rand() & 1;
    p.i3c = S.rand() & 15;
    int k = S.rand(3);
    p.i34 = k == 0 ? 0x404040 : k == 1 ? 0x808080 : 0xf0f0f0;
    p.vx = speed * s;
    p.vy = speed * c - 0.2f;
    p.life = (int)((float)(30 + S.rand(10)) - 4.0f * speed);
    p.ay = 0.1f * 0.05f;
  }
  (void)unit;
}

void Renderer::fxFlowerSparkles(float x, float y) {
  FxState& S = fx_;
  for (int i = 0; i < 50; i++) {
    float th = (float)S.rand(360) * PI / 180.0f, c = std::cos(th), s = std::sin(th);
    float r = (float)S.rand(20), spd = 0.25f + 0.5f * (float)S.rand(1000) / 1000.0f;
    FxEffect& p = fxAdd(0xf, r * c + x, r * s + y, 1);
    p.life = 50 + S.rand(30);
    p.vx = spd * c;
    p.vy = spd * s - 0.15f;
    p.ay = 0.15f * 0.05f;
  }
}

void Renderer::fxSpookyBall(float x, float y) {
  FxState& S = fx_;
  for (int i = 0; i < 4; i++) {
    FxEffect& p = fxAdd(0x22, x, y, 0);
    p.life = 40;
    const float v = 0.3f;
    if (i == 0) p.vy = -v;
    else if (i == 1) p.vy = v;
    else if (i == 2) p.vx = v;
    else p.vx = -v;
    p.i38 = i;
  }
  const float base = (float)S.rand(360) * PI / 180.0f;
  for (int i = 0; i < 3; i++) {
    FxEffect& g = fxAdd(0x23, x, y, 2);
    g.life = 100;
    g.age = -5;
    g.f28 = (float)i * 6.28318548f / 3.0f + base;
    g.i34 = 5;
  }
}

void Renderer::fxZenPowerup(float x, float y) {
  FxState& S = fx_;
  for (int i = 0; i < 120; i++) {
    float th = (float)S.rand(360) * PI / 180.0f, c = std::cos(th), sn = std::sin(th);
    float r = (float)(150 + S.rand(20)), spd = 8.5f + 1.0f * (float)S.rand(1000) / 1000.0f;
    FxEffect& p = fxAdd(0x12, r * c + x, r * sn + y, 2);
    p.vx = spd * -c;
    p.vy = -sn * spd;
    p.ay = 0.0f * 0.05f;
    p.life = 125;
    p.i3c = 1 + S.rand(2);
    p.i34 = 0xffff88;
    p.age = -1;
    p.i30 = 1;
  }
}

void Renderer::fxBubble(float x, float y, int rise, int cel, int age) {
  static const float riseSpeed[6] = {0.67f, 0.83f, 1.0f, 1.07f, 0.76f, 0.93f};
  FxEffect& e = fxAdd(9, x, y, 0);
  e.vy = -riseSpeed[std::clamp(rise, 0, 5)];
  e.i30 = cel;
  e.life = 0;
  e.age = -age;
}

void Renderer::fxChainBolt(const Board& b, int from, int to, int delay, bool fire) {
  FxState& S = fx_;
  const V2 a = b.objs[(size_t)from].pos;
  const V2 end = to >= 0 ? b.objs[(size_t)to].pos : strikeRod_;
  const uint32_t arcColor = fire ? 0xff6622u : 0x8888ffu, boltColor = fire ? 0xff6622u : 0x6666ffu;
  for (int k = 0; k < 10; k++) {
    const float t = std::fabs((float)(S.rand(1000) - 500) / 500.0f);
    const float px = a.x * t + end.x * (1.0f - t), py = a.y * t + end.y * (1.0f - t);
    const float th = (float)S.rand(360) * PI / 180.0f, c = std::cos(th), sn = std::sin(th);
    const float speed = (float)S.rand(1000) / 1000.0f * 1.5f + 0.4f;
    FxEffect& f = fxAdd(T_CHAIN_ARC, px + 20.0f * c, py + 20.0f * sn, 0);
    f.vx = speed * c;
    f.vy = speed * sn - 0.1f;
    f.ay = 0.001f;
    f.life = 130;
    f.age = fire ? 0 : -delay;
    f.i34 = (int)arcColor;
  }
  FxEffect& bolt = fxAdd(T_CHAIN_BOLT, a.x, a.y, 0);
  bolt.life = fire ? 30 : 45;
  bolt.age = 0;
  bolt.i30 = from;
  bolt.i34 = to;
  bolt.i38 = (int)boltColor;
  if (!fire) return;
  for (int k = 0; k < 20; k++) {
    const float th = (float)S.rand(360) * PI / 180.0f;
    const float r = (float)S.rand(10000) * 0.001f;
    const float rise = (float)S.rand(1000) / 1000.0f;
    FxEffect& m = fxAdd(T_CHAIN_EMBER, end.x + r * std::cos(th), end.y + r * std::sin(th), 0);
    m.life = 35;
    m.vy = -rise;
    m.age = -S.rand(35);
  }
}

void Renderer::drawChainBolt(const Board& b, const FxEffect& e) {
  if (e.i30 < 0 || e.i30 >= (int)b.objs.size() || e.i34 >= (int)b.objs.size()) return;
  const V2 a = b.objs[(size_t)e.i30].pos;
  const V2 c = e.i34 >= 0 ? b.objs[(size_t)e.i34].pos : V2{(float)b.rod.x, b.rod.y};
  Tex* small = img("IMAGE_LIGHTNING_FRONT_SMALL");
  Tex* big = img("IMAGE_LIGHTNING_FRONT");
  Tex* glow = img("IMAGE_LIGHTNING_BACK");
  const float dx = a.x - c.x, dy = a.y - c.y;
  const float len = std::sqrt(dx * dx + dy * dy);
  Tex* front = small && small->lh() < len ? big : small;
  if (!front) return;
  const float height = front->lh();
  const int n = (int)std::ceil(len / height);
  if (n <= 0) return;
  const float sdx = dx / (float)n, sdy = dy / (float)n, seg = len / (float)n;
  const float rot = std::atan2(sdx, sdy) + PI;
  const int cel = (e.age / 4) % std::max(1, front->cols);
  const uint32_t color = (uint32_t)e.i38;
  for (int i = 0; i < n; i++) {
    const float cx = BX + a.x - sdx * ((float)i + 0.5f), cy = BY + a.y - sdy * ((float)i + 0.5f);
    if (glow) {
      const float w = glow->lw() * 0.5f, h = glow->lh() * (seg / glow->lh());
      put(glow, {0, 0, glow->lw(), glow->lh()}, cx - w / 2, cy - h / 2, {rgbaOf(color, 255), Blend::Add}, w, h, rot, w / 2, h / 2);
    }
    const float w = front->cw() * (0.5f + seg * 0.5f / height), h = front->ch() * (seg / height);
    put(front, {cel * front->cw(), 0, front->cw(), front->ch()}, cx - w / 2, cy - h / 2, {0xffffffffu, Blend::Normal}, w, h, rot, w / 2, h / 2);
  }
}

void Renderer::drawRodRings(const Board& b, Tex* ring) {
  if (!ring) return;
  const Rod& r = b.rod;
  const int W = (int)ring->lw(), H = (int)ring->lh();
  for (int v : r.rings) {
    const float p = (float)(1.0 - (double)v / 90.0);
    float alpha = 205.0f;
    if ((double)p < 0.4000000059604645) alpha = (float)((double)p / 0.4000000059604645 * 205.0);
    if (0.6000000238418579 < (double)p) alpha = (float)((1.0 - ((double)p - 0.6000000238418579) / 0.3999999761581421) * (double)alpha);
    const double s = 0.4000000059604645 * (double)p + 0.44999998807907104;
    const int w = (int)((double)W * s), h = (int)((double)H * s);
    const int Y = (int)((double)r.y - (double)(h >> 1) + 40.0 - 44.0 * (double)p);
    const int X = (int)((double)r.anchorX - (double)(w >> 1));
    put(ring, {0, 0, ring->lw(), ring->lh()}, BX + (float)X, BY + (float)Y, {rgbaOf(0xffffff, (int)alpha), Blend::Add}, (float)w, (float)h);
  }
}

void Renderer::drawRodSparks(const Board& b) {
  Tex* sp = img("IMAGE_LIGHTNING_BALLSPARK");
  if (!sp) return;
  const Rod& r = b.rod;
  const float cx = BX + (float)r.x, cy = BY + (float)(int)((double)r.y - 2.0);
  const int perCel = (int)std::ceil(40.0 / (double)std::max(1, sp->cols));
  for (const Rod::Spark& k : r.sparks) {
    int idx = k.life / std::max(1, perCel);
    if (k.ang < 0.0f) idx = sp->cols - idx - 1;
    idx = std::clamp(idx, 0, sp->cols - 1);
    const float w = sp->cw() * 0.8f, h = sp->ch() * 0.8f;
    put(sp, {idx * sp->cw(), 0, sp->cw(), sp->ch()}, cx - w / 2, cy - h / 2, {rgbaOf(0xffffff, 200), Blend::Add}, w, h, std::fabs(k.ang), w / 2, h / 2);
  }
}

void Renderer::drawRod(const Board& b) {
  if (!b.chainUsed || !(b.rod.y < 600.0f) || b.bucketIndex() < 0) return;
  const Rod& r = b.rod;
  const char* id = r.state == 4 || r.state == 2 || r.state == 1 ? "IMAGE_LIGHTNING_ROD_ZAP"
                   : r.state >= 5 && r.state <= 9               ? "IMAGE_LIGHTNING_ROD"
                                                                : "IMAGE_LIGHTNING_ROD_ON";
  drawRodRings(b, img("IMAGE_LIGHTNING_ROD_RING_BACK"));
  if (Tex* t = img(id)) {
    Paint p{0xffffffffu, Blend::Normal};
    if (r.state == 9) p = {rgbaOf(0xffffff, 255 - (int)((double)r.timer * 255.0 / 100.0)), Blend::Add};
    const int X = r.x - ((int)t->lw() >> 1), Y = (int)((double)r.y - (double)((int)t->lh() >> 1));
    put(t, {0, 0, t->lw(), t->lh()}, BX + (float)X, BY + (float)Y, p);
  }
  drawRodSparks(b);
  drawRodRings(b, img("IMAGE_LIGHTNING_ROD_RING_FRONT"));
}

void Renderer::fxTimeBomb(float x, float y) {
  FxEffect& e = fxAdd(1, x, y, 2);
  e.i30 = 80;
}

void Renderer::fxSpaceBlast(float x, float y) {
  FxState& S = fx_;
  auto add = [&](int type, int age, int life) -> FxEffect& {
    FxEffect& e = fxAdd(type, x, y, 0);
    e.age = age;
    e.life = life;
    return e;
  };
  add(0xc, 0, 30);
  add(0xd, -5, 40);
  add(0xb, -5, 40);
  for (int i = 0; i < 100; i++) {
    float th = (float)S.rand(360) * PI / 180.0f, r = (float)S.rand(60);
    float spd = 0.75f + 2.5f * (float)S.rand(1000) / 1000.0f;
    FxEffect& p = fxAdd(0xe, x + r * std::cos(th), y + r * std::sin(th), 0);
    p.age = 0;
    p.life = 60;
    p.vx = spd * std::cos(th);
    p.vy = spd * std::sin(th) - 0.25f;
    p.ay = 0.15f * 0.05f;
  }
}

void Renderer::fxPegSparks(const Obj& o, float cx, float cy) {
  static const uint32_t rgb[5] = {0xa8c8ff, 0xffb060, 0xff90ff, 0x90ff90, 0xffff90};
  uint32_t col = rgb[std::clamp(o.pegType == 1 ? 0 : o.pegType == 2 ? 1 : o.pegType == 3 ? 2 : o.pegType == 4 ? 3 : 4, 0, 4)];
  if (const PegTypeDef* cd = o.custom ? pegTypeDef(o.custom) : nullptr; cd && cd->hasColor) col = cd->color;
  FxState& S = fx_;
  int n = 10 + S.rand(6);
  for (int i = 0; i < n; i++) {
    FxEffect& e = fxAdd(0x50, cx, cy, 0);
    float ang = (float)S.rand(6283) / 1000.0f, sp = 1.0f + (float)S.rand(300) / 100.0f;
    e.vx = std::cos(ang) * sp;
    e.vy = std::sin(ang) * sp - 0.6f;
    e.ay = 0.07f;
    e.life = 22 + S.rand(20);
    e.age = -1;
    e.i34 = (int)col;
  }
}

void Renderer::fxSpawn(const Board& b, const Board::FxRec& r) {
  const Event& e = r.e;
  FxState& S = fx_;
  switch (e.type) {
    case Event::ChainStrike: {
      float ry;
      std::memcpy(&ry, &e.c, sizeof ry);
      strikeRod_ = {(float)e.b, ry};
      break;
    }
    case Event::ChainBolt:
      if (e.a >= 0 && e.a < (int)b.objs.size() && e.b < (int)b.objs.size()) fxChainBolt(b, e.a, e.b, e.c & 0xff, ((e.c >> 8) & 1) != 0);
      break;
    case Event::PegLit: {
      if (e.a < 0 || e.a >= (int)b.objs.size()) break;
      bool fever = (e.b & 0x40000000) != 0;
      int pts = e.b & 0x3fffffff;
      V2 c = pegCenter(b.objs[e.a]);
      S.lastHit = c;
      if (vis_.sparks) fxPegSparks(b.objs[e.a], c.x, c.y);
      if (e.c >= 2) fxFlowerSparkles(c.x, c.y);
      FxText& t = fxText(std::to_string(pts), c.x, c.y, fever ? 0x0e : 0x2d);
      t.alignY = -1;
      t.life = 0x32;
      t.fadeOut = 10;
      t.vy = 0;
      if (fever) {
        t.scales = {{0.2f, 0}, {1.6f, 10}, {1.3f, 12}};
        t.scale = 0.2f;
      }
      break;
    }
    case Event::StyleShot: {
      static const bool big[24] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1};
      int id = e.a;
      float x = S.lastHit.x, y = S.lastHit.y;
      if (!b.balls.empty()) { x = b.balls[0].pos.x; y = b.balls[0].pos.y - 10; }
      static const bool bucketStyle[24] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 0};
      if (id >= 0 && id < 24 && bucketStyle[id]) {
        int bi = bucketIndex(b);
        if (bi >= 0) { x = b.objs[bi].pos.x; y = b.objs[bi].pos.y - 60; }
      }
      std::string nm = r.note;
      if (!nm.empty() && nm.back() != '!') nm += "!";
      fxStyleText(b, nm, e.b, x, y, id >= 0 && id < 24 && big[id]);
      break;
    }
    case Event::FreeBall: {
      int bi = bucketIndex(b);
      if (bi < 0) break;
      const Obj& h = b.objs[bi];
      FxEffect& g = fxAdd(T_CATCHERGLOW, h.pos.x, h.pos.y, 0);
      g.age = -10;
      g.life = 90;
      g.i3c = bi;
      for (int i = 0; i < 20; i++) {
        FxEffect& sp = fxAdd(T_CATCHERSPARK, h.pos.x + (float)S.rand(60) - 30, h.pos.y - 20 + (float)S.rand(40), 0);
        sp.age = -10;
        sp.life = 70 + S.rand(50);
        sp.vx = 0.01f * (sp.x - h.pos.x);
        sp.vy = -(float)(S.rand(50) + 50) * 0.01f;
        sp.i38 = 1 + S.rand(3);
        sp.i3c = S.rand(100);
        sp.i30 = bi;
      }
      if (e.a == 1) {
        FxText& t = fxText("Free Ball!", h.pos.x, h.pos.y, 0x0e);
        t.layer = -1;
        t.vy = -1.0f;
        t.fadeOut = 10;
        t.scales = {{0.2f, 0}, {1.0f, 10}};
        t.scale = 0.2f;
        t.life = 100;
        t.refObj = bi;
        t.refX = h.pos.x;
        t.refY = h.pos.y;
      }
      break;
    }
    case Event::FeverSlot: {
      if (e.b == 1) {
        int n = std::max(1, b.ballsLeft[b.cur] - std::max(0, b.feverBallsPaid - 1));
        int shown = std::min(n, 15), vis = std::min(n, 25);
        Tex* ib = img("IMAGE_INT_BALL");
        float spacing = ib ? ib->lh() : 20.0f, bottom = 410.0f + 3.0f * (float)shown;
        float tx = 30.0f + (ib ? ib->lw() / 2 : 6.0f) - 75.0f, ty = bottom - (float)(vis - 1) * spacing - 40.0f;
        if (ty >= -40.0f) {
          FxText& t = fxText("+" + commaSep(e.a), tx, ty, -1);
          t.vy = -1.0f;
          t.life = 100;
          t.fadeOut = 10;
        }
        if (S.countText) {
          S.countCur = b.shotScore;
          for (auto& t : S.texts)
            if (t.id == S.countText) t.lines = {commaSep(S.countCur)};
        }
        break;
      }
      int oi = e.b - 2;
      if (oi < 0 || oi >= (int)b.objs.size()) break;
      const Obj& h = b.objs[(size_t)oi];
      int sc = (b.cfg.duel && b.players > 1) ? h.score * 2 : h.score;
      fxFeverHole(h.pos.x, h.pos.y, sc <= 10000 ? 0 : sc <= 50000 ? 1 : 2);
      S.rainbowWait = true;
      break;
    }
    case Event::LevelDone: {
      if (e.a == 1 && !(b.cfg.duel && b.players > 1)) {
        FxEffect& r = fxAdd(7, 325.0f, 320.0f, 0);
        r.age = -130;
        r.life = 0;
      }
      break;
    }
    case Event::ShotEnd: {
      if (S.rainbowAge < 0 && !S.rainbowWait) break;
      S.rainbowWait = false;
      for (auto& v : S.eff)
        for (auto& ef : v) {
          if (ef.type == 0x18) ef.life = ef.age + 100;
          else if (ef.type == 0x26) ef.life = ef.age + 80;
          else if (ef.type == 0x15) ef.life = ef.age + 60;
        }
      for (auto& t : S.texts)
        if (t.id == S.countText) t.life = t.age + 60;
      S.rainbowAge = -1;
      S.countText = 0;
      break;
    }
    case Event::CoinSpin: {
      FxEffect& c = fxAdd(T_COIN, 325.0f, 280.0f, 0);
      c.life = 250;
      c.i30 = e.b;
      c.i34 = e.a;
      break;
    }
    case Event::Shot: {
      if (b.cur == 0) shotsFired0_++;
      float ang;
      std::memcpy(&ang, &e.a, sizeof ang);
      V2 m = b.muzzle(ang);
      fxCannonBlast(m.x, m.y, ang);
      break;
    }
    case Event::HoleIn: {
      if (e.b < 0 || e.b >= (int)b.balls.size() || e.a < 0 || e.a >= (int)b.objs.size()) break;
      const Ball& bl = b.balls[(size_t)e.b];
      FxEffect& g = fxAdd(0x20, bl.pos.x, bl.pos.y, 0);
      g.vx = bl.vel.x;
      g.vy = bl.vel.y;
      g.life = 10;
      g.i30 = bl.fire ? 1 : 0;
      g.i34 = bl.hat ? 1 : 0;
      break;
    }
    case Event::Jimmy: {
      if (b.zoom > 0) break;
      for (auto& v : S.eff)
        for (auto& ef : v)
          if (ef.type == 0x1f) return;
      Tex* jt = img("IMAGE_JIMMY");
      float jx = e.a ? (float)b.W - 40.0f : 40.0f - (jt ? jt->lw() : 118.0f), jy = 560.0f - 3.0f;
      int seed = e.b < 0 ? S.rand(0x7fffffff) : e.b;
      for (int k = 0; k < 2; k++) {
        FxEffect& j = fxAdd(0x1f, jx, jy, k == 0 ? -1 : 2);
        j.life = 150;
        j.i38 = e.a ? 1 : 0;
        j.i34 = seed % 0xff;
        j.age = -30;
        j.b40 = true;
      }
      break;
    }
    case Event::Hint: {
      bool alive = false;
      for (auto& t : S.texts) alive |= t.id == S.hintText;
      if (alive) break;
      V2 p = b.pivot();
      FxText& t = fxText("You can only fire ^00FF00^one ball^oldclr^ at a time!^oldclr^", p.x, 110.0f + p.y, 0x0e);
      S.hintText = t.id;
      t.color = 0xffffff;
      t.life = 200;
      t.fadeIn = 10;
      t.fadeOut = 30;
      t.layer = 1;
      t.vy = 0;
      break;
    }
    case Event::Zen: {
      if (e.a != 1 && e.a != 2) break;
      if (e.a == 2) {
        V2 m = b.muzzle(b.gunAngle);
        fxZenPowerup(m.x, m.y);
      }
      V2 p = b.pivot();
      FxText& t = fxText(e.a == 1 ? "Divining Best Shot..." : "Zen Achieved!", p.x, 110.0f + p.y, 0x0e);
      t.life = 120;
      t.fadeIn = 10;
      t.fadeOut = 10;
      t.layer = 3;
      t.vy = 0;
      if (e.a == 2 && e.b > 0) {
        FxText& u = fxText(e.b == 1 ? "No Improvement!" : e.b == 2 ? std::to_string(e.c) + "% More Zen!" : std::string("MAXIMUM ZEN ACHIEVED!!"), p.x, 140.0f + p.y, 0x0e);
        u.life = 120;
        u.fadeIn = 10;
        u.fadeOut = 10;
        u.layer = 3;
        u.vy = 0;
      }
      break;
    }
    case Event::Power: {
      static const char* names[] = {"", "SuperGuide!", "Flippers!", "Multiball!", "Pyramid!", "SpaceBlast!", "SpookyBall!", "ZenShot!",
                                    "ShotExtender!", "FlowerPower!", "Nudge!", "TimeBomb!", "Fireball!", "", ""};
      chPowerBlink_ = 40 * ((e.a == 3 || e.a == 6) ? 4 : 2) - 1;
      if (e.a == 4) {
        for (size_t i = 0; i < b.objs.size(); i++)
          if (b.objs[i].image == "bumperc") {
            FxEffect& r = fxAdd(0x17, S.lastHit.x, S.lastHit.y, 2);
            r.life = 120;
            r.i30 = (int)i;
            break;
          }
      }
      if (e.a == 5) fxSpaceBlast(S.lastHit.x, S.lastHit.y);
      if (e.a == 6) {
        fxSpookyBall(S.lastHit.x, S.lastHit.y);
        if (e.c == 0) {
          FxEffect& g = fxAdd(0x24, S.lastHit.x, S.lastHit.y, 2);
          g.life = 10000;
        }
      }
      if (e.a > 0 && e.a < 13 && names[e.a][0]) {
        FxText& t = fxText(names[e.a], S.lastHit.x, S.lastHit.y, -1);
        t.color = 0x88ff88;
        t.age = -35;
        t.vy = -0.5f;
        t.fadeOut = 10;
        t.life = 150;
        t.layer = 1;
      }
      if (const ModMaster* mm = e.a >= masters::kFirstPower ? masters::byPower(e.a) : nullptr) {
        FxText& t = fxText(mm->banner, S.lastHit.x, S.lastHit.y, -1);
        t.color = mm->color;
        t.age = -35;
        t.vy = -0.5f;
        t.fadeOut = 10;
        t.life = 150;
        t.layer = 1;
      }
      break;
    }
    case Event::Fever: {
      float X = S.lastHit.x, Y = S.lastHit.y;
      for (int i = 0; i < 80; i++) {
        float th = (float)S.rand(3600) * PI / 1800.0f;
        float rr = (float)S.rand(10);
        float spd = 1.5f + 2.5f * (float)S.rand(1000) / 1000.0f;
        FxEffect& p = fxAdd(T_STREAK, rr * std::sin(th) + X, Y + rr * std::cos(th), 0);
        p.i34 = (int)hslToRgb(S.rand(255), 255, 0x40);
        p.i38 = 3;
        p.vx = std::sin(th) * spd;
        p.vy = std::cos(th) * spd - 1.0f;
        p.life = 45;
        p.ay = 0.05f;
      }
      for (int i = 0; i < 150; i++) {
        float th = (float)S.rand(3600) * PI / 1800.0f;
        float rr = (float)S.rand(20);
        float spd = 0.1f + 4.0f * (float)S.rand(1000) / 1000.0f;
        FxEffect& p = fxAdd(T_STAR, rr * std::cos(th) + X, rr * std::sin(th) + Y, 0);
        p.age = 0;
        p.vx = spd * std::cos(th);
        p.vy = spd * std::sin(th) - 1.0f;
        p.i38 = S.rand(255);
        p.i3c = 3 + S.rand(45);
        p.life = 30 + p.i3c;
        p.ay = 0.05f;
      }
      for (int i = 0; i < 8; i++) {
        float th = (float)(i * 45) * PI / 180.0f;
        float dx = std::cos(th) * 4, dy = std::sin(th) * 4;
        FxEffect& p = fxAdd(T_FEVERBLAST_ROCKET, X + dx, Y + dy, 2);
        p.life = 1000;
        p.vx = (0.75f + (float)S.rand(10) / 20.0f) * dx;
        p.vy = (0.75f + (float)S.rand(10) / 20.0f) * dy;
        p.i38 = i;
        p.ay = 0.05f;
        p.age = 0;
      }
      float cx = (float)b.W / 2.0f;
      bool ultra = e.a != 0;
      static const char* rows[3] = {"ultra", "extreme", "fever"};
      static const int xoff[3][7] = {{0, 0, -2, 2, 10, 0, 0}, {-2, -4, 2, -4, -7, -4, 3}, {4, -2, 0, 6, 1, 0, 0}};
      static const int rowOff[3] = {0, -22, -3}, step[3] = {-3, 0, 3};
      int delay = 0;
      float y = 50;
      for (int row = ultra ? 0 : 1; row < 3; row++) {
        int n = (int)std::strlen(rows[row]);
        float xx = cx - (float)(n * 78 / 2) + 39 + rowOff[row];
        for (int k = 0; k < n; k++) {
          FxEffect& l = fxAdd(T_LETTER, xx + xoff[row][k], y, 3);
          l.i38 = (int)(std::strchr("ultraexmfv", rows[row][k]) - "ultraexmfv");
          l.life = 300 - delay;
          l.b40 = true;
          l.age = -(delay + 10);
          if (row == 0) {
            l.i3c = 1;
            delay += 20;
          } else {
            l.i30 = row == 1 ? -500 : 500;
            l.f28 = row == 1 ? 20.0f : 10.0f;
          }
          if (row == 1 && k == 0) l.i3c = 2;
          if (row == 2 && k == n - 1) l.i3c = 3;
          xx += 78 + step[row];
        }
        delay += 15;
        y += 88;
      }
      break;
    }
    case Event::TimeBomb:
      fxTimeBomb((float)e.a, (float)e.b);
      break;
    case Event::Toggle:
      S.sound(e.a ? "FLIP" : "BUBBLES", 0, 1, 30);
      break;
    case Event::Bubble:
      fxBubble((float)e.a, (float)e.b, e.c & 15, (e.c >> 4) & 15, e.c >> 8);
      break;
    default:
      break;
  }
}

void Renderer::masterBadge() {
  for (auto& v : fx_.eff)
    for (auto& e : v)
      if (e.type == 7 && e.age < 0) { e.type = 0x21; e.y = 275.0f; }
}

void Renderer::advance(const Board& b) {
  bool fresh = b.fxTotal < fx_.seen || b.frames < fx_.lastFrame || fx_.lastFrame < 0;
  if (fresh) {
    fx_.clear();
    fx_.seen = 0;
    fx_.lastFrame = b.frames;
    fx_.lastTick = b.tick;
  }
  uint32_t base = b.fxTotal - (uint32_t)b.fxlog.size();
  for (uint32_t i = std::max(fx_.seen, base); i < b.fxTotal; i++) fxSpawn(b, b.fxlog[i - base]);
  fx_.seen = b.fxTotal;
  {
    std::map<int, HatSnap> now;
    for (const Ball& bl : b.balls)
      if (bl.alive && bl.hat && bl.hatAge > 0) now[bl.uid] = {bl.hatPos, bl.hatPrev};
    if (!fresh)
      for (auto& [uid, h] : hatSnap_)
        if (!now.count(uid)) {
          Tex* t = img("IMAGE_MAGICHAT");
          FxEffect& f = fxAdd(0x1e, h.pos.x, h.pos.y + 21.0f - (t ? t->lh() / 2 : 20.0f) - 2.0f, 2);
          f.vx = h.pos.x - h.prev.x;
          f.vy = h.pos.y - h.prev.y;
          f.ay = 0.05f;
          f.life = 0;
        }
    hatSnap_ = std::move(now);
  }
  int frames = std::min(b.frames - fx_.lastFrame, 60), ticks = std::min(b.tick - fx_.lastTick, 60);
  fx_.lastFrame = b.frames;
  fx_.lastTick = b.tick;
  fx_.frame = b.frames;
  emitAdvance(b, ticks, fresh);
  for (int t = 0; t < ticks; t++) {
    if (chCountBlink_) chCountBlink_--;
    if (chPowerBlink_) chPowerBlink_--;
    chTick_++;
  }
  if (fresh) {
    for (int& c : chLastCount_) c = -2;
    chCountBlink_ = chPowerBlink_ = 0;
  }
  {
    int pl = std::clamp(b.cur, 0, 3), cnt = std::clamp(b.turns[pl], 0, 99);
    if (chLastCount_[pl] != -2 && chLastCount_[pl] != cnt) chCountBlink_ = 239;
    chLastCount_[pl] = cnt;
  }
  if (fresh) shotsFired0_ = 0;
  if (b.phase == Phase::Aim) {
    int prevAim = aimTicks_;
    aimTicks_ += ticks;
    auto crossed = [&](int t) { return aimTicks_ >= t && prevAim < t; };
    if (crossed(75) && b.orangeLeft <= 6) hintTick_ = b.tick;
    if ((crossed(50) && shotsFired0_ == 0) || (crossed(140) && shotsFired0_ >= 1)) {
      const int maxL = b.cfg.maxLevel;
      const bool onMax = b.cfg.curLevel >= 0 && b.cfg.curLevel == maxL;
      int type = 2, n = 2;
      bool gate = true;
      if (!onMax || shotsFired0_ != 0) {
        if (maxL == 0 || maxL == 1) n = 5;
      } else if (maxL == 0 || maxL == 1) {
        n = 5;
      } else if (maxL == 2 || (maxL > 0 && maxL < 26 && maxL % 5 == 0)) {
        type = 4;
        n = 5;
        gate = false;
      } else if (maxL == 3) {
        type = 3;
        n = 5;
        gate = false;
      }
      if (gate && b.orangeLeft > 10 && ((maxL != 0 && maxL != 1) || shotsFired0_ > 0)) type = 0;
      if (type) blinkReq_ = {type, n, b.tick};
    }
  } else {
    aimTicks_ = 0;
  }
  if (b.phase != Phase::Aim) {
    guideAge_ = 1;
    guideRack_ = 0;
  } else {
    for (int t = 0; t < ticks; t++) {
      if (guideAge_ == 1) {
        if (++guideRack_ >= 25) guideAge_ = 2;
      } else if (guideAge_ > 1 && ++guideAge_ == 30) guideAge_ = 0;
    }
  }
  for (int f = 0; f < frames; f++) {
    if (fx_.shake > 0) fx_.shake--;
    for (auto& t : fx_.texts) {
      t.age++;
      if (t.age < 0) continue;
      if (t.life && t.life < t.age) { t.id = -1; continue; }
      if (t.blink) t.blink--;
      if (!t.scales.empty()) {
        float prev = 1.0f;
        int pt = 0;
        size_t k = 0;
        for (; k < t.scales.size(); k++) {
          if (t.age < t.scales[k].second) break;
          prev = t.scales[k].first;
          pt = t.scales[k].second;
        }
        if (k < t.scales.size()) {
          int dt = t.scales[k].second - pt;
          if (dt > 0) {
            float u = (float)(t.age - pt) / dt;
            t.scale = (1.0f - u) * prev + t.scales[k].first * u;
          }
        } else {
          t.scale = prev;
        }
      }
      if (t.refObj >= 0 && t.refObj < (int)b.objs.size()) {
        t.x += b.objs[t.refObj].pos.x - t.refX;
        t.y += b.objs[t.refObj].pos.y - t.refY;
        t.refX = b.objs[t.refObj].pos.x;
        t.refY = b.objs[t.refObj].pos.y;
      }
      if (t.age < t.rotUntil || t.rot != 0) {
        t.rot += t.rotSpeed;
        if (t.rotSpeed > 0 && t.rot > 6.2831853f) t.rot = t.age < t.rotUntil ? t.rot - 6.2831853f : 0.0f;
      }
      t.x += t.vx;
      t.y += t.vy;
    }
    fx_.texts.erase(std::remove_if(fx_.texts.begin(), fx_.texts.end(), [](const FxText& t) { return t.id < 0; }), fx_.texts.end());
  }
  const bool slow = b.slowOn;
  if (slow)
    for (int f = 0; f < frames; f++) fxTick(b, 2, (float)b.slowSpeed / 1000.0f);
  for (int t = 0; t < ticks; t++) fxTick(b, slow ? 1 : 0);
}

void Renderer::fxBallTrails(const Board& b) {
  for (const Ball& bl : b.balls)
    if (bl.alive && bl.fire && bl.holeOut <= 0) {
      FxEffect& f = fxAdd(5, bl.pos.x, bl.pos.y, 0);
      f.life = 60;
      f.i30 = fx_.rand(360);
    }
  if (!b.fever && !zenOn_) {
    for (const Ball& bl : b.balls) {
      if (!bl.alive || bl.fire || bl.holeOut > 0 || bl.slideA == 0 || bl.slideB <= 1 || !bl.hitFlag142) continue;
      int n = std::clamp(3 - (bl.slideB >> 2), 1, 3);
      if (b.tick % n != 0) continue;
      float spd = std::sqrt(bl.vel.x * bl.vel.x + bl.vel.y * bl.vel.y);
      float r1 = (float)fx_.rand(1000) / 1000.0f * (0.275f * 2.0f);
      float u2 = (float)fx_.rand(1000) / 500.0f, u3 = (float)fx_.rand(1000) / 500.0f;
      float vx = (spd * bl.lastN.x * r1 + ((1.0f - u2) * 0.3f - bl.vel.x)) * 0.25f;
      float vy = (spd * bl.lastN.y * r1 + ((1.0f - u3) * 0.3f - bl.vel.y)) * 0.25f;
      float l2 = vx * vx + vy * vy;
      if (3.0f < l2) {
        vx = 3.0f * vx / l2;
        vy = 3.0f * vy / l2;
      }
      FxEffect& sp = fxAdd(T_STREAK, bl.pos.x + 2.0f * vx - 4.5f * bl.lastN.x, bl.pos.y + 2.0f * vy - 4.5f * bl.lastN.y, 0);
      sp.life = 25;
      sp.ay = 0.25f * 0.05f;
      sp.vx = vx;
      sp.vy = vy;
    }
  }
  if (!b.fever) return;
  for (const Ball& bl : b.balls) {
    if (bl.fire) continue;
    float spd = std::sqrt(bl.vel.x * bl.vel.x + bl.vel.y * bl.vel.y);
    if (!(spd > 0.0f)) continue;
    float& acc = fx_.trailAcc[bl.uid];
    acc += spd;
    float dx = bl.vel.x / spd, dy = bl.vel.y / spd;
    float px = bl.pos.x - acc * dx, py = bl.pos.y - acc * dy;
    int guard = 0;
    while (4.0f <= acc && guard++ < 64) {
      acc -= 4.0f;
      FxEffect& t = fxAdd(T_TRAIL, px, py, py >= 150.0f ? 0 : 2);
      px += dx * 4.0f;
      py += dy * 4.0f;
      t.life = 150;
      t.i38 = 2 | 1;
      t.i34 = (int)hslToRgb(b.tick % 255, 255, 0x80);
      t.age = -1;
      const float dotX = px - 4.0f * dx, dotY = py - 4.0f * dy;
      const int dotLife = 150, dotPrio = dotY >= 150.0f ? 0 : 2;
      int n1 = (int)(5.0f * (float)fx_.rand(1000) / 1000.0f + 1.0f), n2 = (int)(10.0f * (float)fx_.rand(1000) / 1000.0f);
      for (int k = 0; k < n1 + n2; k++) {
        FxEffect& sp = fxAdd(0x12, dotX, dotY, dotPrio);
        sp.life = 25 + dotLife;
        sp.i38 = 1;
        sp.i34 = fx_.rand(0xffffff);
        sp.x += (float)(fx_.rand(9) - 4);
        sp.y += (float)(fx_.rand(9) - 4);
        sp.f28 = (float)fx_.rand(360) * PI / 180.0f;
        sp.age = -1;
        sp.i30 = 1;
        sp.i3c = 0;
        if (k >= n1) {
          float spd = (float)fx_.rand(1000) / 1000.0f, ang = (float)fx_.rand(360) * PI / 180.0f;
          sp.vx = std::cos(ang) * spd;
          sp.vy = std::sin(ang) * spd;
          sp.life = 80 + fx_.rand(80);
        }
      }
    }
  }
}

void Renderer::fxFeverTally(const Board& b) {
  FxState& S = fx_;
  if (S.rainbowWait) {
    const FxEffect* first = nullptr;
    for (int L = 0; L < 5 && !first; L++)
      for (auto& e : S.eff[L])
        if (e.type == 0x11) { first = &e; break; }
    if (!first || first->age >= first->life - 30) {
      S.rainbowWait = false;
      S.rainbowAge = 0;
      fxAdd(0x18, 325.0f, 500.0f, 0).life = 100000;
      S.sound("RAINBOW");
    }
  }
  if (S.rainbowAge < 0) return;
  S.rainbowAge++;
  if (S.rainbowAge == 70) {
    FxEffect& f = fxAdd(0x15, 400.0f, 300.0f, 0);
    f.life = 100000;
    f.i38 = 3;
    FxText& t = fxText("", 325.0f, 205.0f, 0x26);
    t.vy = 0;
    t.life = 100000;
    t.scales = {{5.0f, 0}, {1.0f, 30}};
    t.scale = 5.0f;
    t.color = hslToRgb(0xaa, 255, 0x80);
    S.countText = t.id;
    S.countCur = 0;
  }
  if (S.rainbowAge > 120 && S.countText) {
    int target = b.shotScore, d = target - S.countCur;
    if (d > 0) {
      S.countCur += d < 101 ? 10 : d < 1001 ? 100 : d < 10001 ? 200 : d < 100001 ? 1000 : 5000;
      if (S.countCur > target) S.countCur = target;
      S.sound("SCORECOUNTER", 0, 1, 10);
      int h = S.countCur / 1000 + 0xaa;
      if (h > 0xff) h = 0;
      for (auto& t : S.texts)
        if (t.id == S.countText) {
          t.lines = {commaSep(S.countCur)};
          t.color = hslToRgb(h, 255, 0x80);
        }
    }
  }
}

void Renderer::fxTick(const Board& b, int mode, float frac) {
  if (mode != 2) { fxBallTrails(b); fxFeverTally(b); }
  for (int L = 0; L < 5; L++) {
    auto& v = fx_.eff[L];
    std::vector<FxEffect> spawned;
    for (auto& e : v) {
      if (mode == 1 && e.b40) continue;
      if (mode == 2 && !e.b40) {
        if (e.age >= 0) {
          e.x += frac * e.vx;
          e.y += frac * e.vy;
          if (e.ay > 0) {
            e.vy = frac * e.ay + e.vy;
            if (e.vy > 10.0f) e.vy = 10.0f;
          }
        }
        continue;
      }
      e.age++;
      if (e.age < 0) continue;
      if (e.life != 0 && e.life < e.age) { e.type = -1; continue; }
      fxUpdateOne(b, e, spawned);
      if (mode == 1) continue;
      e.x += e.vx;
      e.y += e.vy;
      if (e.ay > 0) {
        e.vy += e.ay;
        if (e.vy > 10.0f) e.vy = 10.0f;
      }
    }
    v.erase(std::remove_if(v.begin(), v.end(), [](const FxEffect& e) { return e.type < 0; }), v.end());
    for (auto& s : spawned) {
      auto& dst = fx_.eff[std::clamp(s.prio, -1, 3) + 1];
      dst.push_back(s);
    }
  }
}

void Renderer::xformSprite(Tex* t, float x, float y, float sx, float sy, float tx, float ty, float rot, Paint p) {
  if (!t) return;
  float w = t->lw() * sx, h = t->lh() * sy;
  put(t, {0, 0, t->lw(), t->lh()}, x + tx - w / 2, y + ty - h / 2, p, w, h, rot, w / 2 - tx, h / 2 - ty);
}

void Renderer::fxUpdateOne(const Board& b, FxEffect& e, std::vector<FxEffect>& out) {
  FxState& S = fx_;
  switch (e.type) {
    case T_FEVERBLAST_ROCKET: {
      int h = e.age;
      int sz = std::max(1, 60 / (10 + h));
      FxEffect p;
      p.type = T_TRAIL;
      p.prio = e.y >= 150 ? 0 : 2;
      p.x = e.x;
      p.y = e.y;
      p.life = 25;
      p.i38 = 7;
      p.vx = 0.1f * (1.0f - (float)S.rand(200) / 100.0f) + 0.25f * e.vx;
      p.vy = 0.1f * (1.0f - (float)S.rand(200) / 100.0f) + 0.25f * e.vy;
      p.i30 = 4 + sz;
      p.i34 = (int)hslToRgb(e.age % 255, 255, 0x80);
      p.age = -1;
      out.push_back(p);
      if (!b.balls.empty() && e.age >= 20) {
        const Ball& bl = b.balls[(size_t)e.i38 % b.balls.size()];
        float dx = bl.pos.x - e.x, dy = bl.pos.y - e.y;
        float d = std::sqrt(dx * dx + dy * dy);
        if (d > 0.6f) {
          e.vx *= 0.95f;
          e.vy *= 0.95f;
          e.vx += 1.7f * dx / d;
          e.vy += 1.7f * dy / d;
          float v2 = e.vx * e.vx + e.vy * e.vy;
          if (v2 < 6.0f && v2 > 0.0f) {
            e.vx = 6.0f * e.vx / v2;
            e.vy = 6.0f * e.vy / v2;
          }
        }
      }
      break;
    }
    case T_STAR: {
      e.i34 = (int)hslToRgb(e.i38 % 255, 255, 0x80);
      if (e.age == e.i3c) {
        for (int i = 0; i < 5; i++) {
          FxEffect p;
          p.type = T_TRAIL;
          p.prio = e.y >= 150 ? 0 : 2;
          p.x = (float)S.rand(2) + e.x;
          p.y = (float)S.rand(2) + e.y;
          p.life = 25;
          p.i38 = 2 | 4;
          float u = (float)S.rand(200) / 100.0f, v = (float)S.rand(200) / 100.0f;
          p.vx = e.vx + 3.6f * (1.0f - u);
          p.vy = e.vy + 3.6f * (1.0f - v);
          p.i30 = 4;
          p.ay = e.ay;
          p.i34 = e.i34;
          p.age = -1;
          out.push_back(p);
        }
      }
      break;
    }
    case T_STYLESPARK: {
      e.x -= e.vx;
      e.y -= e.vy;
      float a = 0.295f;
      if (e.age > 18) a /= (float)(e.age - 18);
      float dx = e.x - (float)e.i38, dy = e.y - (float)e.i3c;
      e.vx = e.x;
      e.vy = e.y;
      float c = std::cos(a), s = std::sin(a);
      e.x = (float)e.i38 + dx * c + dy * s;
      e.y = (float)e.i3c - s * dx + c * dy;
      e.vx -= e.x;
      e.vy -= e.y;
      break;
    }
    case T_LETTER: {
      if (S.frame % 10 == 0) e.y -= 1.0f;
      if (e.i3c == 1) {
        if (e.age == 15) {
          S.shake = 10;
          S.sound("FEVERHIT");
        } else if (e.age == 0) {
          S.sound("WHISTLE");
        }
      } else if (e.i3c > 1 && e.age == 20) {
        S.sound("WOOSH");
      }
      bool behind = false;
      if (e.age >= 50)
        for (const Ball& bl : b.balls)
          if (bl.pos.x >= e.x - 40 && bl.pos.x < e.x + 40 && bl.pos.y >= e.y - 40 && bl.pos.y < e.y + 40) { behind = true; break; }
      if (e.life - e.age == 20 && e.i3c > 1) S.sound("WOOSH");
      if (!behind) {
        if (e.i34 > 0 && e.i34 < 255) {
          e.i34 += 5;
          if (e.i34 > 255) e.i34 = 0;
        }
      } else {
        if (e.i34 == 0) e.i34 = 255;
        e.i34 -= 5;
        e.i34 = e.i34 >= 100 && e.i34 < 256 ? e.i34 : (e.i34 < 100 ? 100 : 255);
      }
      break;
    }
    case T_COIN: {
      int old = e.i30;
      if (old < 0) break;
      e.i30 = (old + 9) % 360;
      if (87 < e.i30 && e.i30 < 93) e.i30 = 93;
      else if (267 < e.i30 && e.i30 < 273) e.i30 = 273;
      if (e.life - e.age < 150) {
        if (e.i34 == 1) {
          if (e.i30 < old) e.i30 = -e.age;
        } else if (old < 181 && 179 < e.i30) {
          e.i30 = -e.age;
        }
      }
      break;
    }
    case 0x24: {
      bool any = false;
      for (const Ball& bl : b.balls) {
        if (!bl.spooky && !bl.spookyUsed) continue;
        any = true;
        for (int k = 0; k < 4; k++) {
          float py_ = bl.pos.y - 6.0f + (float)S.rand(12), px_ = bl.pos.x - 6.0f + (float)S.rand(12);
          FxEffect p;
          p.type = 0x25;
          p.prio = 0;
          p.x = px_;
          p.y = py_;
          p.vx = (float)(S.rand(5) - 2) / 10.0f;
          p.vy = (float)(S.rand(5) - 2) / 10.0f;
          p.life = 50;
          p.i38 = S.rand(2);
          p.age = 0;
          out.push_back(p);
        }
      }
      if (!any && e.life == 10000) e.life = e.age + 1;
      break;
    }
    case 9:
      if (e.y < -20.0f) e.life = e.age;
      break;
    case 0x12: {
      e.i38++;
      if (e.i3c == 0) break;
      if (b.balls.empty()) {
        e.vx *= 0.95f;
        e.vy *= 0.95f;
        break;
      }
      const Ball& bl = b.balls[(size_t)S.rand((int)b.balls.size())];
      float dy = e.y - bl.pos.y, dx = e.x - bl.pos.x;
      if (dx == 0.0f && dy == 0.0f) break;
      float d2 = std::max(4.0f, dx * dx + dy * dy);
      if (d2 < 1000.0f) {
        e.vx += 2.0f * dx / d2;
        e.vy += 2.0f * dy / d2;
        float sn = std::sin(0.04f), c = std::cos(0.04f), ovx = e.vx;
        e.vx = c * e.vx + sn * e.vy;
        e.vy = e.vy * c - sn * ovx;
        break;
      }
      e.vx *= 0.95f;
      e.vy *= 0.95f;
      break;
    }
    case 0x1f: {
      if (e.age == 1) S.sound("POWERUP_MULTIBALL", 0, 1, 100);
      if (e.age < e.life - 20) {
        Tex* jt = img("IMAGE_JIMMY");
        int w = 100 + (jt ? (int)jt->lw() : 118), h = 50 + (jt ? (int)jt->lh() : 129);
        int x0 = e.i38 ? b.W - w : 0, y0 = 560 - h;
        for (auto& bl : b.balls) {
          int bx = (int)bl.pos.x, by = (int)bl.pos.y;
          if (x0 <= bx && bx < x0 + w && y0 <= by && by < y0 + h) {
            e.age = std::max(e.life - e.age, e.life - 20);
            break;
          }
        }
      }
      break;
    }
    case T_CATCHERSPARK: e.vy *= 0.99f; break;
    case 0xf:
      e.vx *= 0.97f;
      e.vy *= 0.97f;
      break;
    case 0xe:
      e.vx -= (float)(50 - S.rand(100)) / 100.0f;
      e.vx *= 0.95f;
      e.vy *= 0.95f;
      break;
    case 0x1e:
      if (e.y > 630.0f) e.life = e.age;
      break;
    case 0x11:
      if (e.x < 0 || e.x > 650) e.vx = -e.vx;
      if (e.y > 630) e.life = e.age;
      break;
    default: break;
  }
}



void Renderer::fxDrawOne(const Board& b, const FxEffect& e) {
  const float X = BX + e.x, Y = BY + e.y;
  switch (e.type) {
    case 0x50: {
      Tex* t = img("IMAGE_SPACESTAR");
      if (!t) break;
      float f = std::clamp(1.0f - (float)e.age / (float)std::max(1, e.life), 0.0f, 1.0f);
      Paint p{rgbaOf((uint32_t)e.i34, (int)(255.0f * std::min(1.0f, f * 1.6f))), Blend::Add};
      for (int k = 0; k < 4; k++) {
        float s2 = (9.0f * f + 3.0f) * (1.0f - 0.2f * (float)k);
        put(t, {0, 0, t->lw(), t->lh()}, X - e.vx * (float)k * 1.3f - s2 / 2, Y - e.vy * (float)k * 1.3f - s2 / 2, k == 0 ? p : Paint{rgbaOf((uint32_t)e.i34, (int)(110.0f * f)), Blend::Add}, s2, s2);
      }
      break;
    }
    case 0x20: {
      float f = 1.0f - (float)e.age / (float)std::max(1, e.life), sc = f * 0.9f + 0.1f;
      if (e.i30) {
        if (Tex* t = img("IMAGE_FIREBALL")) putCentered(t, X, Y, {}, sc, 0.05f * (float)b.frames);
      } else if (Tex* t = img("IMAGE_BALL")) {
        putCentered(t, X, Y, {}, sc);
      }
      break;
    }
    case 0x25: {
      int a = 10;
      if (e.age < 10) a = e.age;
      else if (e.age > e.life - 40) a = 10 * (e.life - e.age) / 40;
      if (a < 1) break;
      if (e.i38 == 0) {
        if (Tex* t = img("IMAGE_SPOOKYTRAIL")) putCentered(t, X, Y, {rgbaOf(0x60ff20, a), Blend::Normal});
      } else if (Tex* t = img("IMAGE_SPACESTAR")) {
        putCentered(t, X, Y, {rgbaOf(0x808020, 4 * a), Blend::Add});
      }
      break;
    }
    case 1: {
      Tex* t = img("IMAGE_CIRCLE");
      if (!t) break;
      int a = 0x96 - e.age * 0x96 / 100;
      if (a <= 0) break;
      float r = (float)e.i30;
      put(t, {0, 0, t->lw(), t->lh()}, X - r, Y - r, {rgbaOf(0xff0000, a), Blend::Normal}, 2.0f * r, 2.0f * r);
      break;
    }
    case 0x1e: {
      Tex* t = img("IMAGE_MAGICHAT");
      if (!t) break;
      float tilt = std::clamp(e.vx / 3.0f, -1.0f, 1.0f) * 0.7853982f;
      putCentered(t, X, Y, {}, 1.0f, -tilt);
      break;
    }
    case T_CHAIN_BOLT: drawChainBolt(b, e); break;
    case T_CHAIN_ARC: {
      Tex* t = img("IMAGE_SPACESTAR");
      if (!t) break;
      const int k = (int)((double)(e.life - e.age) * 0.05);
      if (k <= 0) break;
      put(t, {0, 0, t->lw(), t->lh()}, (float)(int)(X - (float)k), (float)(int)(Y - (float)k), {rgbaOf((uint32_t)e.i34, 255), Blend::Add}, (float)(2 * k), (float)(2 * k));
      break;
    }
    case T_CHAIN_EMBER: {
      Tex* t = img("IMAGE_EMBER");
      if (!t) break;
      const int delay = std::max(1, t->frameDelay);
      if (e.age >= t->cols * delay) break;
      const int cel = std::min(t->cols - 1, e.age / delay);
      put(t, {cel * t->cw(), 0, t->cw(), t->ch()}, X - t->cw() / 2, Y - t->ch() / 2, {0x80606080u, Blend::Add});
      break;
    }
    case 9: {
      Tex* t = img("IMAGE_BUBBLES");
      if (!t) break;
      int cel = std::clamp(e.i30, 0, t->rows * t->cols - 1);
      float w = t->cw(), h = t->ch();
      put(t, {(cel % t->cols) * t->cw(), (cel / t->cols) * t->ch(), w, h}, X - (float)(int)(w / 2), Y - (float)(int)(h / 2), {0xffffffffu, Blend::Add});
      break;
    }
    case 0x12: {
      if (e.i3c == 0) {
        Tex* st = img("IMAGE_STREAK");
        if (!st) break;
        int tri = (e.i38 + e.i34) % 30;
        if (tri > 15) tri = 30 - tri;
        float spd = std::sqrt(e.vx * e.vx + e.vy * e.vy), ang = std::atan2(e.vx, e.vy) + PI;
        float w = st->lw() * 0.35f, h = st->lh() * (0.05f + 0.3f * spd);
        put(st, {0, 0, st->lw(), st->lh()}, X - w / 2, Y - h / 2, {rgbaOf(hslToRgb((std::max(0, e.age) * 2) % 255, 255, 0x80), 75 * tri / 15), Blend::Add}, w, h, -ang, w / 2, h / 2);
        break;
      }
      int a = std::min(255, (int)(30.0f * std::sqrt(e.vx * e.vx + e.vy * e.vy) + 16.0f));
      SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_ADD);
      SDL_SetRenderDrawColor(r_, (Uint8)((e.i34 >> 16) & 255), (Uint8)((e.i34 >> 8) & 255), (Uint8)(e.i34 & 255), (Uint8)a);
      SDL_FRect rc{px(X - (float)e.i30), py(Y - (float)e.i30), 2.0f * (float)e.i30 * s_, 2.0f * (float)e.i30 * s_};
      SDL_RenderFillRect(r_, &rc);
      SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
      break;
    }
    case 0x17: {
      Tex* t = img("IMAGE_PYRAMIDRAY");
      if (!t || e.i30 < 0 || e.i30 >= (int)b.objs.size()) break;
      auto ease = [](int age, int dur, float a, float c) { return age < dur ? (1.0f - (float)age / (float)dur) * a + ((float)age / (float)dur) * c : c; };
      const V2 tp = b.objs[(size_t)e.i30].pos;
      float dx = tp.x - e.x, dy = tp.y - e.y;
      float len = std::sqrt(dx * dx + dy * dy) * ease(e.age, 5, 0.2f, 1.0f);
      float al = 1.0f;
      if (e.age < 20) al = (float)e.age / 20.0f;
      else if (e.age > e.life - 80) al = (float)(e.life - e.age) / 80.0f;
      int a = std::clamp((int)(al * 255.0f), 0, 255);
      float sy = len / t->lh(), sx = ease(e.age, 20, 0.5f, 1.0f);
      float rot = std::atan2(-dy, dx) - 90.0f * PI / 180.0f;
      xformSprite(t, X, Y, sx, sy, 0, -0.5f * t->lh() * sy, rot, {rgbaOf(0x88ff88, a), Blend::Add});
      break;
    }
    case 0x1f: {
      Tex* jt = img("IMAGE_JIMMY");
      if (!jt) break;
      float f = e.age < 20 ? (float)e.age / 20.0f : (e.life - 20 < e.age ? (float)(e.life - e.age) / 20.0f : 1.0f);
      float H = jt->lh();
      int sgn = e.i38 ? -1 : 1, dxi = (int)((float)sgn * 0.5f * H);
      float jx = e.x + (float)dxi * f, jy = e.y - f * H;
      float cw = (float)b.W;
      SDL_Rect oldClip{};
      bool had = SDL_GetRenderClipRect(r_, &oldClip);
      auto clipTo = [&](float cx0, float cy0, float cwid, float chei) {
        SDL_Rect c{(int)std::floor(px(BX + cx0)), (int)std::floor(py(BY + cy0)), (int)std::ceil(cwid * s_), (int)std::ceil(chei * s_)};
        if (had) {
          int x1 = std::max(c.x, oldClip.x), y1 = std::max(c.y, oldClip.y), x2 = std::min(c.x + c.w, oldClip.x + oldClip.w), y2 = std::min(c.y + c.h, oldClip.y + oldClip.h);
          c = {x1, y1, std::max(0, x2 - x1), std::max(0, y2 - y1)};
        }
        SDL_SetRenderClipRect(r_, &c);
      };
      if (e.prio <= 0) clipTo(0, 527, cw, 33);
      else clipTo(jx <= 300.0f ? 3.0f : -3.0f, 0, cw, 560 - 33);
      flipX_ = e.i38 != 0;
      put(jt, {0, 0, jt->lw(), jt->lh()}, BX + (float)(int)jx, BY + (float)(int)jy);
      flipX_ = false;
      if (e.prio > 0 && e.age > 20 && e.age <= e.life - 20) {
        Tex* sp = img("IMAGE_JIMMYSPEECH");
        Font* fnt = font(fxFontName(14));
        int al = e.age > e.life - 21 ? (e.life - 20 - e.age) * 255 : 255;
        if (sp && fnt && al > 0) {
          float bx = e.i38 ? jx - 150.0f : jx + 110.0f, by = jy - 50.0f;
          flipX_ = e.i38 != 0;
          put(sp, {0, 0, sp->lw(), sp->lh()}, BX + (float)(int)bx, BY + (float)(int)by, {rgbaOf(0xffffff, al), Blend::Normal});
          flipX_ = false;
          static const char* words[12] = {"Extreme!", "Tubular!", "Radical!", "Awesome!", "Insane!", "Freak Out!", "Whoa...", "Wicked!", "Mad Skills!", "Excellent!", "Rock On!", "Toasty!"};
          const char* w = words[e.i34 % 12];
          int tw = fnt->width(w), padY = ((int)sp->lh() - fnt->height()) / 2;
          float ty = (float)padY + by + (float)fnt->ascent(), tx = (float)(((int)sp->lw() - tw) / 2) + bx;
          drawText(fnt, BX + (float)(int)tx, BY + (float)(int)ty, w, rgbaOf(0xffffff, al), TextAlign::Left, 1.0f, false);
        }
      }
      if (had) SDL_SetRenderClipRect(r_, &oldClip);
      else SDL_SetRenderClipRect(r_, nullptr);
      break;
    }
    case 5: {
      Tex* t = img("IMAGE_FIREBALLFIRE");
      if (!t || e.age < 0) break;
      int cel = e.age / 2;
      if (cel >= t->rows) break;
      float cw = t->cw(), ch = t->ch();
      put(t, {0, cel * ch, cw, ch}, X - cw / 2, Y - ch / 2, {rgbaOf(0x141414, 255), Blend::Add}, cw, ch, -(float)(e.i30 + e.age) * PI / 180.0f, cw / 2, ch / 2);
      break;
    }
    case 6: {
      Tex* t = img("IMAGE_STREAK");
      if (!t) break;
      int fade = (e.i38 & 2) ? 20 : 10;
      int rem = e.life - e.age, a = 255;
      if (rem < fade) a = rem * 255 / fade;
      uint32_t rgb = (e.i38 & 1) ? (uint32_t)e.i34 : 0xff9944u;
      float ang = std::atan2(e.vx, e.vy) + PI;
      float len = std::sqrt(e.vx * e.vx + e.vy * e.vy);
      float sx = 0.65f, sy = (e.i38 & 2) ? 0.3f * len + 0.15f : 0.5f * len + 0.3f;
      float w = t->lw() * sx, h = t->lh() * sy;
      put(t, {0, 0, t->lw(), t->lh()}, X - w / 2, Y - h / 2, {rgbaOf(rgb, a), Blend::Add}, w, h, -ang, w / 2, h / 2);
      break;
    }
    case 0x10: {
      Tex* t = img("IMAGE_FEVERRAY");
      if (!t) break;
      float f = 1.0f;
      if (e.age < 20) f = (float)e.age / 20.0f;
      else if (e.age > e.life - 80) f = (float)(e.life - e.age) / 80.0f;
      static const uint32_t base[3] = {0x111122, 0x052210, 0x332210};
      uint32_t c = base[std::clamp(e.i38, 0, 2)];
      int r = (int)((float)((c >> 16) & 255) * f), g = (int)((float)((c >> 8) & 255) * f), bl = (int)((float)(c & 255) * f);
      float sy = (float)e.i30 / t->lh() + 0.004f * (float)e.age;
      xformSprite(t, X, Y, 1.0f, sy, 0, -t->lh() * 0.5f * sy, e.f2c * (float)e.age + e.f28, {((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)bl << 8) | 0xffu, Blend::Add});
      break;
    }
    case 0x11: {
      static const char* im[4] = {"IMAGE_FEVERGLOWSTAR", "IMAGE_FEVERSTAR_RED", "IMAGE_FEVERSTAR_GREEN", "IMAGE_FEVERSTAR_BLUE"};
      Tex* t = img(im[std::clamp(e.i38, 0, 3)]);
      if (!t) break;
      float s = (float)e.i30 / t->lw();
      float rot = e.f2c != 0 ? (float)e.age * e.f2c + e.f28 : 0.0f;
      int fadeLen = 50, rem = e.life - e.age;
      Paint p;
      if (e.i38 == 0) {
        float f = rem < fadeLen ? (float)rem / (float)fadeLen : 1.0f;
        uint32_t c = (uint32_t)e.i34;
        p = {((uint32_t)((float)((c >> 16) & 255) * f) << 24) | ((uint32_t)((float)((c >> 8) & 255) * f) << 16) | ((uint32_t)((float)(c & 255) * f) << 8) | 0xffu, Blend::Add};
      } else {
        p = {0xffffff00u | (uint32_t)(rem < fadeLen ? rem * 255 / fadeLen : 255), Blend::Normal};
      }
      xformSprite(t, X, Y, s, s, 0, 0, rot, p);
      break;
    }
    case T_STAR: {
      Tex* t = img("IMAGE_SPACESTAR");
      if (!t) break;
      int sz = e.age < e.i3c ? 2 : (int)(0.35f * (float)(e.life - e.age));
      if (sz <= 0) break;
      put(t, {0, 0, t->lw(), t->lh()}, X - sz, Y - sz, {rgbaOf((uint32_t)e.i34, 255), Blend::Add}, (float)sz * 2, (float)sz * 2);
      break;
    }
    case T_TRAIL: {
      uint32_t rgb = (uint32_t)e.i34;
      if (e.i38 & 1) rgb = hslToRgb((int)std::lround(2.0f * (float)e.age) % 255, 255, 0x80);
      if (e.i38 & 4) {
        Tex* t = img("IMAGE_SPACESTAR");
        if (!t) break;
        float s2 = (float)e.i30 * 2;
        put(t, {0, 0, t->lw(), t->lh()}, X - e.i30, Y - e.i30, {rgbaOf(rgb, 0x60), Blend::Add}, s2, s2);
      } else {
        Tex* t = img("IMAGE_FEVERTRAIL");
        if (!t) break;
        float sc = 1.1f - 0.004f * (float)e.age;
        if (sc <= 0) break;
        putCentered(t, X, Y, {rgbaOf(rgb, 255), Blend::Add}, sc);
      }
      break;
    }
    case T_STYLESPARK: {
      Tex* t = img("IMAGE_SPACESTAR");
      if (!t) break;
      int m = std::min(e.age, e.life - e.age);
      int a = 80 + 4 * m;
      if (e.age > 20) a = 160;
      int n = std::min(15, m);
      for (int k = 0; k < n; k += 3) {
        float s2 = 6.0f * (1.0f - (float)k / 15.0f) + 2;
        float px = X + (e.vx) * (float)k * 0.5f, py = Y + (e.vy) * (float)k * 0.5f;
        put(t, {0, 0, t->lw(), t->lh()}, px - s2 / 2, py - s2 / 2, {rgbaOf(0x64ff9b, std::clamp(a, 0, 255)), Blend::Add}, s2, s2);
      }
      break;
    }
    case T_CATCHERGLOW: {
      if (e.i3c < 0 || e.i3c >= (int)b.objs.size()) break;
      const Obj& h = b.objs[e.i3c];
      Tex* t = img("IMAGE_CATCHERGLOW");
      if (!t) break;
      int a = 255;
      if (e.age < 20) a = e.age * 255 / 20;
      else if (e.life - 30 < e.age) a = (e.life - e.age) * 255 / 30;
      a = std::clamp(a * 9 / 10, 0, 255);
      putCentered(t, BX + h.pos.x, BY + h.pos.y - 57, {rgbaOf(0xffff88, a), Blend::Add});
      break;
    }
    case T_CATCHERSPARK: {
      if (e.i30 < 0 || e.i30 >= (int)b.objs.size()) break;
      Tex* t = img("IMAGE_SPACESTAR");
      if (!t) break;
      int a = std::clamp(255 - (e.i38 - 1) * 100 - 0, 0, 255);
      float fade = 1.0f - std::fabs((float)e.age / (float)e.life * 2.0f - 1.0f);
      float s2 = (float)e.i38 * 2;
      put(t, {0, 0, t->lw(), t->lh()}, X - s2 / 2, Y - s2 / 2, {rgbaOf(0xffff88, (int)(a * fade)), Blend::Add}, s2, s2);
      break;
    }
    case T_COIN: {
      int rem = e.life - e.age;
      int a = 255;
      if (e.age < 10) a = e.age * 255 / 10;
      else if (rem < 20) a = rem * 255 / 20;
      a = std::clamp(a, 0, 255);
      int face = 0;
      float sx = 1.0f;
      if (e.i30 >= 0) {
        sx = std::fabs(std::cos((float)(e.i30 % 180) * PI / 180.0f));
        face = ((e.i30 + 90) / 180) & 1;
      } else {
        face = e.i34 == 0 ? 1 : 0;
      }
      Tex* t = img(face == 0 ? "IMAGE_COINHEADS" : "IMAGE_COINTAILS");
      if (t) {
        xformSprite(t, X - 20, Y + 15, sx, 1.0f, 0, 0, 0, {rgbaOf(0x000000, 60 * a / 255), Blend::Normal});
        xformSprite(t, X, Y, sx, 1.0f, 0, 0, 0, {rgbaOf(0xffffff, a), Blend::Normal});
      }
      if (e.i30 < 0 && t) {
        int since = e.age + e.i30;
        if (since >= 0 && since < 90 && (since / 15) % 2 == 0)
          if (Tex* hl = img("IMAGE_COINHILIGHT")) putCel(hl, e.i34 == 1 ? 0 : 1, X - t->lw() / 2.0f + 33.0f, Y - t->lh() / 2.0f + 113.0f, {rgbaOf(0xffffff, a), Blend::Normal});
      }
      if (rem < 150) {
        int ta = (rem - 140) * 255 / 10;
        if (ta >= 0) drawText(font("FONT_OVERLOAD24GRAD2"), X, Y - 100, "Total Miss!", rgbaOf(0xffff00, std::min(ta, 255)), TextAlign::Center);
      }
      break;
    }
    case T_LETTER: {
      float sc = 1.0f;
      if (e.i3c == 1 && e.age < 15) sc += 3.0f * (float)(15 - e.age) / 15.0f;
      float px = e.x, py = e.y;
      if (e.i30 != 0) {
        if (e.f28 > (float)e.age) px += ((e.f28 - (float)e.age) * (float)e.i30) / e.f28;
        else if (e.age > e.life - 10) px += (float)((e.age - e.life + 10) * e.i30) / 10.0f;
      }
      if (e.i3c == 1 && e.life - 10 < e.age) py -= (float)(200 * (e.age - e.life + 10)) / 10.0f;
      int alpha = e.i34 ? e.i34 : 255;
      static const int glowRow[10] = {0, 1, 4, 0, 0, 0, 5, 6, 3, 2};
      if (Tex* g = img("IMAGE_XFEVERGLOW")) {
        float w = g->lw() * sc, h = g->ch() * sc;
        int row = std::clamp(glowRow[std::clamp(e.i38, 0, 9)], 0, g->rows - 1);
        put(g, {0, row * g->ch(), g->lw(), g->ch()}, BX + px - w / 2, BY + py - h / 2, {rgbaOf(0xffffff, alpha), Blend::Add}, w, h);
      }
      if (Tex* t = img("IMAGE_XFEVER")) {
        float w = t->lw() * sc, h = t->ch() * sc;
        int row = std::clamp(e.i38, 0, t->rows - 1);
        put(t, {0, row * t->ch(), t->lw(), t->ch()}, BX + px - w / 2, BY + py - h / 2, {rgbaOf(0xffffff, alpha), Blend::Normal}, w, h);
      }
      break;
    }
    case 0x18: {
      Tex* t = img("IMAGE_FEVERRAINBOW");
      if (!t || !t->t) break;
      const int n = 126;
      int segs = std::clamp((int)(1.5f * (float)e.age), 0, n - 1), s0 = 0;
      float alpha = 1.0f;
      if (e.life > 0 && e.life - 40 < e.age) {
        s0 = std::max(0, (e.age - e.life + 40) * (n - 1) / 40);
        alpha = std::clamp((float)(e.life - e.age) / 40.0f, 0.0f, 1.0f);
        segs = n - 1;
      }
      if (segs <= s0) break;
      float u = ((float)((e.age / 2) % 50) + 0.5f) / 50.0f, v0 = 0.5f / (float)t->h, v1 = 1.0f - v0;
      SDL_FColor col{1, 1, 1, alpha};
      std::vector<SDL_Vertex> vs;
      std::vector<int> idx;
      float th = 0.025f * (float)s0;
      for (int i = s0; i <= segs; i++, th += 0.025f) {
        float c = std::cos(th), sn = std::sin(th);
        vs.push_back({{px(X + c * 420.0f), py(Y - sn * 400.0f)}, col, {u, v0}});
        vs.push_back({{px(X + c * 320.0f), py(Y - sn * 300.0f)}, col, {u, v1}});
      }
      for (int i = 0; i < segs - s0; i++) { int q = i * 2; idx.insert(idx.end(), {q, q + 1, q + 2, q + 1, q + 3, q + 2}); }
      SDL_SetTextureBlendMode(t->t, SDL_BLENDMODE_BLEND);
      SDL_RenderGeometry(r_, t->t, vs.data(), (int)vs.size(), idx.data(), (int)idx.size());
      if (segs < 122 && !(e.life > 0 && e.life - 40 < e.age)) {
        float hth = 0.025f * (float)segs, hc = std::cos(hth), hs = std::sin(hth);
        V2 outer{X + hc * 420.0f, Y - hs * 400.0f}, inner{X + hc * 320.0f, Y - hs * 300.0f};
        float rot = PI / 2.1f - std::atan2(inner.y - outer.y, inner.x - outer.x);
        auto mix = [&](float wi) { return V2{inner.x * wi + outer.x * (1.0f - wi), inner.y * wi + outer.y * (1.0f - wi)}; };
        if (Tex* ss = img("IMAGE_STAR_STREAK"))
          for (float wi : {0.5f, 0.2f, 0.8f}) {
            V2 c = mix(wi);
            putCentered(ss, c.x, c.y, {0xffffff96u, Blend::Add}, 1.0f, rot);
          }
        const char* starIds[3] = {"IMAGE_FEVERSTAR_RED", "IMAGE_FEVERSTAR_GREEN", "IMAGE_FEVERSTAR_BLUE"};
        const float starW[3] = {0.2f, 0.5f, 0.8f};
        for (int k = 0; k < 3; k++)
          if (Tex* st = img(starIds[k])) {
            V2 c = mix(starW[k]);
            put(st, {0, 0, st->lw(), st->lh()}, c.x - 30.0f, c.y - 30.0f, {0xffffffffu, Blend::Normal}, 60.0f, 60.0f);
          }
      }
      break;
    }
    case 0x15: {
      Tex* a = img("IMAGE_FEVERSCORE");
      Tex* tp = img("IMAGE_FEVERSCORETOP");
      if (!a) break;
      float ang = 0;
      if (e.age < 20) ang = (1.0f - (float)e.age / 20.0f) * -1.5707964f;
      else if (e.life - 20 < e.age) ang = (1.0f - (float)(e.life - e.age) / 20.0f) * 1.5707964f;
      float bx = BX + 325.0f - (float)(int)(a->lw() / 2), by = BY + 105.0f;
      put(a, {0, 0, a->lw(), a->lh()}, bx, by, {0xffffffffu, Blend::Add}, a->lw(), a->lh(), ang, 225.0f, 420.0f);
      if (tp) put(tp, {0, 0, tp->lw(), tp->lh()}, bx, by, {0xffffffffu, Blend::Normal}, tp->lw(), tp->lh(), ang, 225.0f, 420.0f);
      break;
    }
    case 0xb: {
      Tex* t = img("IMAGE_SPACEHALO");
      if (!t) break;
      int a = 255;
      if (e.age > 10) a = std::max(0, ((10 - e.age) * 255) / 40 + 255);
      putCentered(t, X, Y, {rgbaOf(0xffffff, a), Blend::Add}, 0.5f + (float)e.age / 15.0f);
      break;
    }
    case 0xc: {
      Tex* t = img("IMAGE_SPACEBURST");
      if (!t) break;
      int g = 255, bl = 0;
      if (e.age > 0) g = 255 - 10 * std::min(e.age, 10);
      if (e.age > 10) { bl = 10 * (e.age - 10); g += 3 * (e.age - 10); }
      g = std::clamp(g, 0, 255);
      bl = std::clamp(bl, 0, 255);
      float f = e.life - 10 < e.age ? 0.8f * (float)(e.life - e.age) / 10.0f : 1.0f;
      float full = 200.0f / t->lw(), sc = e.age < 10 ? 1.0f + (float)e.age * (full - 1.0f) / 10.0f : 0.01f * (float)(e.age - 10) + full;
      putCentered(t, X, Y, {rgbaOf(((uint32_t)255 << 16) | ((uint32_t)g << 8) | (uint32_t)bl, std::clamp((int)(255.0f * f), 0, 255)), Blend::Normal}, sc);
      break;
    }
    case 0xd: {
      Tex* t = img("IMAGE_SPACEBUBBLE");
      if (!t) break;
      float f = e.life - 30 < e.age ? (float)(e.life - e.age) / 30.0f : 1.0f;
      float sc = e.age < 40 ? 0.2f + (float)e.age * (1.7f - 0.2f) / 40.0f : 1.7f;
      putCentered(t, X, Y, {rgbaOf(0x4400ff, std::clamp((int)(255.0f * f), 0, 255)), Blend::Add}, sc);
      break;
    }
    case 7: {
      Tex* t = img("IMAGE_BLUERIBBON");
      if (!t || e.age < 0) break;
      float sc = 1.0f;
      int a = 255;
      if (e.age < 20) {
        a = e.age * 255 / 20;
        sc = (float)e.age * 1.5f / 20.0f + 0.1f;
      } else if (e.age < 30) {
        sc = (1.5f + 0.1f) - ((1.5f + 0.1f) - 1.0f) * (float)(e.age - 20) / 10.0f;
      }
      if (a < 1) break;
      putCentered(t, X, Y, {rgbaOf(0xffffff, a), Blend::Normal}, sc);
      break;
    }
    case 0x21: {
      Tex* t = img("IMAGE_MASTERBADGE");
      if (!t || e.age < 0) break;
      float sc = 1.0f;
      int a = e.age < 5 ? e.age * 255 / 5 : 255;
      if (e.age < 20) sc = (float)e.age * 1.5f / 20.0f + 0.1f;
      else if (e.age < 30) sc = 1.6f - 0.6f * (float)(e.age - 20) / 10.0f;
      if (a < 1) break;
      putCentered(t, X, Y, {rgbaOf(0xffffff, a), Blend::Normal}, sc);
      break;
    }
    case 0xf: {
      int a = 255, rem = e.life - e.age;
      if (rem < 20) a = rem * 255 / 20;
      if (a <= 0) break;
      SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_ADD);
      SDL_SetRenderDrawColor(r_, 0x40, 0x30, 0x10, (Uint8)a);
      SDL_FRect rc{px(X - 1), py(Y - 1), 2 * s_, 2 * s_};
      SDL_RenderFillRect(r_, &rc);
      SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
      break;
    }
    case 0x22: {
      Tex* t = img(e.i38 < 2 ? "IMAGE_SPOOKYPOOF1" : "IMAGE_SPOOKYPOOF2");
      if (!t) break;
      int a = 255, rem = e.life - e.age;
      if (rem < 40) a = rem * 255 / 40;
      if (a <= 0) break;
      float sc = 0.2f + (float)e.age / 10.0f, w = t->cw() * sc, h = t->ch() * sc;
      int cel = e.i38 & 1;
      put(t, {(cel % t->cols) * t->cw(), (cel / t->cols) * t->ch(), t->cw(), t->ch()}, X - w / 2, Y - h / 2, {rgbaOf(0x44ff44, a), Blend::Normal}, w, h);
      break;
    }
    case 0x23: {
      if (e.age < e.i34) break;
      Tex* t = img("IMAGE_SPOOKYGHOST");
      if (!t) break;
      auto pos = [&](int n, float& px_, float& py_) {
        float th = (float)n * 0.03f + e.f28, r = (float)n * 3.0f;
        px_ = e.x + std::cos(th) * r;
        py_ = e.y + std::sin(th) * r;
      };
      float x1, y1, x0, y0;
      pos(e.age, x1, y1);
      pos(e.age - 1, x0, y0);
      float rot = std::atan2(y0 - y1, x1 - x0) - 1.5707964f;
      int a = 255;
      if (e.age < e.i34 + 10) a = (e.age - e.i34) * 255 / 10;
      else if (e.age > e.life - 70) a = (e.life - e.age) * 255 / 70;
      if (a < 1) break;
      putCentered(t, BX + x1, BY + y1, {rgbaOf(0xaaffaa, a), Blend::Normal}, 1.0f, rot);
      break;
    }
    case 0xe: {
      Tex* t = img("IMAGE_SPACESTAR");
      if (!t) break;
      int r = 255, g = 0x80;
      if (e.life - 30 < e.age) g = std::max(0, 0x80 - 10 * (e.age - (e.life - 30)));
      if (e.life - 20 < e.age) r = std::max(0, 255 - 10 * (e.age - (e.life - 20)));
      putCentered(t, X, Y, {rgbaOf(((uint32_t)r << 16) | ((uint32_t)g << 8) | 255u, 255), Blend::Add}, 0.4f * (e.life - 20 < e.age ? (float)(e.life - e.age) / 20.0f : 1.0f));
      break;
    }
    case 0x28: {
      Tex* t = img("IMAGE_SMOKE");
      if (!t) break;
      int cel = e.age / 3;
      if (cel >= t->rows * t->cols) break;
      float sc = e.i38 == 0 ? 1.0f : 0.45f;
      int a = e.i38 == 0 ? 0x30 : (e.life > 0 ? 64 * (e.life - e.age) / e.life : 0);
      uint32_t rgb = e.i38 == 0 ? 0x303030u : (uint32_t)e.i34;
      float w = t->cw() * sc, h = t->ch() * sc;
      put(t, {(cel % t->cols) * t->cw(), (cel / t->cols) * t->ch(), t->cw(), t->ch()}, X - w / 2, Y - h / 2, {rgbaOf(rgb, std::clamp(a, 0, 255)), Blend::Normal}, w, h);
      break;
    }
    case 0x29: {
      Tex* t = img("IMAGE_FLECK");
      if (!t) break;
      int cel = ((e.i3c + e.age) / 12) % 4;
      if (e.i38) cel = 3 - cel;
      int rem = e.life - e.age, a = 64;
      if (rem < 10) a = 64 * rem / 10;
      float w = t->cw() * 0.33f, h = t->ch() * 0.33f;
      put(t, {(cel % t->cols) * t->cw(), (cel / t->cols) * t->ch(), t->cw(), t->ch()}, X - w / 2, Y - h / 2, {rgbaOf((uint32_t)e.i34, std::clamp(a, 0, 255)), Blend::Normal}, w, h);
      break;
    }
    default: break;
  }
}

void Renderer::fxDrawEffects(const Board& b, int layer) {
  for (auto& e : fx_.eff[std::clamp(layer, -1, 3) + 1])
    if (e.age >= 0) fxDrawOne(b, e);
}

void Renderer::fxDrawTexts(const Board&, int layer) {
  for (auto& t : fx_.texts) {
    if (t.layer != layer || t.age < 0) continue;
    if (t.blink != 0 && ((t.blink / 30) & 1) == 0) continue;
    int alpha = 255;
    if (t.fadeIn > 0 && t.age < t.fadeIn) {
      alpha = t.age * 255 / t.fadeIn;
    } else if (t.fadeOut > 0 && t.life - t.fadeOut < t.age) {
      alpha = ((t.life - t.fadeOut) - t.age) * 255 / t.fadeOut + 255;
      if (alpha < 1) continue;
      alpha = std::min(alpha, 255);
    }
    Font* f = font(fxFontName(t.font));
    if (!f) continue;
    int n = (int)t.lines.size();
    int lineH = f->lineSpacing();
    int h = f->height() + (n - 1) * lineH;
    float s = t.scale;
    uint32_t rgb = t.color & 0xffffff;
    uint32_t col = (rgb << 8) | (uint32_t)alpha;
    uint32_t sh = ((t.shadow & 0xffffff) << 8) | (uint32_t)alpha;
    float top = t.y;
    if (t.alignY == 0) top = t.y - h / 2.0f;
    else if (t.alignY == 1 && (s != 1.0f || t.rot != 0)) top = t.y - h;
    float cy = top + h / 2.0f;
    for (int i = 0; i < n; i++) {
      const std::string& ln = t.lines[i];
      int w = t.colorCodes ? f->widthColored(ln) : f->width(ln);
      float lx = t.x;
      if (t.alignX == 0) lx = t.x - w / 2.0f * s;
      else if (t.alignX == 1) lx = t.x - w * s;
      float yy = cy + ((top + i * lineH + f->ascent()) - cy) * s;
      float X = BX + lx, Y = BY + yy;
      if (t.rot != 0) {
        std::vector<GlyphQuad> q;
        int w2 = 0;
        if (t.colorCodes) f->layoutColored(ln, col, q, &w2);
        else f->layout(ln, col, q, &w2);
        float pcx = BX + t.x, pcy = BY + cy;
        float c = std::cos(t.rot), sn = std::sin(t.rot);
        for (auto& g : q) {
          SDL_Texture* tx = fontTexture(g.img);
          if (!tx) continue;
          float gw = (g.dw ? g.dw : g.sw) * s, gh = (g.dh ? g.dh : g.sh) * s;
          float gx = X + g.dx * s + gw / 2 - pcx, gy = Y + g.dy * s + gh / 2 - pcy;
          float rx = gx * c + gy * sn, ry = -gx * sn + gy * c;
          SDL_FRect src{(float)g.sx, (float)g.sy, (float)g.sw, (float)g.sh};
          SDL_FRect dst{px(pcx + rx - gw / 2), py(pcy + ry - gh / 2), gw * s_, gh * s_};
          SDL_SetTextureColorMod(tx, (g.rgba >> 24) & 255, (g.rgba >> 16) & 255, (g.rgba >> 8) & 255);
          SDL_SetTextureAlphaMod(tx, g.rgba & 255);
          SDL_SetTextureBlendMode(tx, g.mode == 1 ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
          SDL_RenderTextureRotated(r_, tx, &src, &dst, -t.rot * 57.29578f, nullptr, SDL_FLIP_NONE);
        }
        continue;
      }
      if (t.shadow) drawText(f, X + 1, Y + 1, ln, sh, TextAlign::Left, s, t.colorCodes);
      drawText(f, X, Y, ln, col, TextAlign::Left, s, t.colorCodes);
    }
  }
}

}
