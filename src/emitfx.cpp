#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "render.h"
#include "ui/sparkle.h"

namespace pp {

namespace {

constexpr float kPi = 3.14159274f;

struct Curve {
  bool expr = false;
  float v = 0.0f;
  float step = 0.0f;
  float now = 0.0f;
  ui::SparkCurve c;

  void set(float value) {
    expr = false;
    v = value;
    step = 0.0f;
  }

  void load(const Val* val, float dflt) {
    set(dflt);
    if (!val || !val->n) return;
    const Node& n = *val->n;
    if (n.i("tag") != 0) {
      v = n.f("value", dflt);
      return;
    }
    const std::string text = n.str("expr");
    if (!c.parse(text)) return;
    expr = true;
    size_t at = text.find(',');
    if (at != std::string::npos) at = text.find(',', at + 1);
    if (at != std::string::npos) step = (float)std::strtod(text.c_str() + at + 1, nullptr);
  }

  float at(float t) const { return expr ? c.eval(t) : v; }
  float cur() const { return at(now); }
  void advance() {
    if (expr && step != 0.0f) now = now + step;
  }
};

struct Particle {
  float x = 0, y = 0, vx = 0, vy = 0, ax = 0, ay = 0;
  float rot = 0, scale = 0;
  float spiralR = 0, spiralPhase = 0;
  int age = 0, life = 1, fadeAt = 0, fadeIn = 0;
};

struct Emitter {
  const Node* node = nullptr;
  std::string image;
  int w = 150, h = 150, maxCount = 100;
  bool circular = false, attached = false, additive = true, orient = false, randomStart = true, visible = true, paused = false;
  int layer = 1;
  float randRate = 0, rateMul = 1, fadeAt = 0.5f, fadeInto = 0.1f, duration = 0.75f, spiral = 0, scaleRand = 0, rotRand = 0;
  float xRand = 0, yRand = 0, accX = 0, gravity = 0;
  int pending = 100;
  Curve rate, distrib, scale, scaleVel, alpha, colR, colG, colB, rot, rotVel, velX, velY, posX, posY;
  float acc = 0;
  std::vector<Particle> parts;
};

}

struct EmitFx {
  std::vector<Emitter> em;
  uint32_t seed = 0x2545f491u;

  int rnd() {
    seed = seed * 214013u + 2531011u;
    return (int)((seed >> 16) & 0x7fffu);
  }
  float unit() { return (float)rnd() / 32767.0f; }

  void init(const Board& b) {
    em.clear();
    for (const Emit& e : b.emitters) {
      const Node& n = *e.node;
      Emitter m;
      m.node = &n;
      m.image = n.str("emitter_image");
      m.w = (int)n.i("width", 150);
      m.h = (int)n.i("height", 150);
      m.maxCount = std::max(1, (int)n.i("max", 100));
      m.circular = n.flag("circular");
      m.attached = n.flag("attached");
      m.additive = n.flag("additive");
      m.orient = n.flag("orient_rot_to_vel");
      m.randomStart = n.flag("random_start");
      m.visible = n.flag("visible");
      m.layer = n.flag("background") ? 0 : n.flag("foreground2") ? 3 : n.flag("foreground") ? 2 : 1;
      m.randRate = n.f("rand_rate");
      m.rateMul = n.has("hardware_rate") ? n.f("hardware_rate") : 1.0f;
      m.fadeAt = n.has("alpha_fade_at") ? n.f("alpha_fade_at") : 0.5f;
      m.fadeInto = n.has("alpha_into") ? n.f("alpha_into") : 0.1f;
      m.duration = n.has("duration") ? n.f("duration") : 0.75f;
      m.spiral = n.f("spiral_count");
      m.pending = n.has("fast_forward") ? (int)n.i("fast_forward") : 100;
      m.rate.load(n.val("rate"), 0.0f);
      m.distrib.load(n.val("distrib"), 1.0f);
      m.alpha.load(n.val("alpha"), 1.0f);
      m.scale.set(1.0f);
      m.scaleVel.set(0.0f);
      m.colR.set(1.0f);
      m.colG.set(1.0f);
      m.colB.set(1.0f);
      m.rot.set(0.0f);
      m.rotVel.set(0.0f);
      m.velX.set(0.0f);
      m.velY.set(0.0f);
      m.posX.set(0.0f);
      m.posY.set(0.0f);
      auto sub = [&](const char* key) -> const Node* {
        const Val* v = n.val(key);
        return v ? v->n.get() : nullptr;
      };
      if (const Node* s = sub("scale")) {
        m.scale.load(s->val("value"), 1.0f);
        m.scaleVel.load(s->val("velocity"), 0.0f);
        m.scaleRand = s->f("rand");
      }
      if (const Node* c = sub("color")) {
        m.colR.load(c->val("r"), 1.0f);
        m.colG.load(c->val("g"), 1.0f);
        m.colB.load(c->val("b"), 1.0f);
      }
      if (const Node* r = sub("rotation")) {
        m.rot.load(r->val("value"), 0.0f);
        m.rotVel.load(r->val("velocity"), 0.0f);
        m.rotRand = r->f("rand");
      }
      if (const Node* v = sub("velocity")) {
        m.velX.load(v->val("x"), 0.0f);
        m.velY.load(v->val("y"), 0.0f);
        m.xRand = v->f("x_rand");
        m.yRand = v->f("y_rand");
        m.accX = v->f("acc_x");
        m.gravity = v->f("gravity");
      }
      if (const Node* p = sub("pos")) {
        m.posX.load(p->val("x"), 0.0f);
        m.posY.load(p->val("y"), 0.0f);
      }
      em.push_back(std::move(m));
    }
  }

  bool spawn(Emitter& m, V2 pos) {
    if ((int)m.parts.size() >= m.maxCount) return false;
    Particle p;
    p.life = std::max(1, (int)(m.duration * 100.0f));
    p.fadeAt = (int)(m.fadeAt * 100.0f);
    p.fadeIn = (int)(m.fadeInto * 100.0f);
    p.scale = unit() * m.scaleRand;
    p.rot = unit() * m.rotRand / 180.0f * kPi;
    if (m.spiral != 0.0f) {
      p.spiralR = (float)((m.w + m.h) / 4);
      p.spiralPhase = unit();
    }
    p.vx = unit() * m.xRand;
    p.vy = unit() * m.yRand;
    p.ax = m.accX;
    p.ay = m.gravity;
    float x = 0.0f, y = 0.0f;
    if (!m.randomStart) {
      if (!m.attached) {
        x = pos.x;
        y = pos.y;
      }
    } else if (m.circular) {
      float k = 1.0f;
      if (!(!m.distrib.expr && m.distrib.v == 1.0f)) k = m.distrib.at(unit());
      float s = k * (float)m.w - (float)m.w * 0.5f;
      float a = unit() * kPi * 2.0f;
      x = std::cos(a) * s * (float)m.w / (float)m.h;
      y = std::sin(a) * s * (float)m.h / (float)m.w;
      if (!m.attached) {
        x += pos.x;
        y += pos.y;
      }
    } else {
      float k = 1.0f;
      if (!(!m.distrib.expr && m.distrib.v == 1.0f)) k = m.distrib.at(unit());
      const int hw = m.w / 2, hh = m.h / 2;
      x = (float)(m.w > 0 ? rnd() % m.w - hw : 0) * k + (float)hw;
      y = (float)(m.h > 0 ? rnd() % m.h - hh : 0) * k + (float)hh;
      if (!m.attached) {
        x = (pos.x - (float)hw) + x;
        y = (pos.y - (float)hh) + y;
      } else {
        x = x - (float)hw;
        y = y - (float)hh;
      }
    }
    p.x = x;
    p.y = y;
    m.parts.push_back(p);
    return true;
  }

  static bool update(const Emitter& m, Particle& p) {
    p.age++;
    const float t = (float)p.age / (float)p.life;
    p.rot = m.rotVel.at(t) / 180.0f * kPi + p.rot;
    p.x = (float)(m.velX.at(t) + p.vx) + p.x;
    p.y = (float)(m.velY.at(t) + p.vy) + p.y;
    p.vx = p.ax + p.vx;
    p.vy = p.ay + p.vy;
    const float d = m.scaleVel.at(t);
    if (d != 0.0f) {
      p.scale = d + p.scale;
      if (!(0.0f < p.scale)) {
        p.age = p.life;
        return false;
      }
    }
    return p.age < p.life;
  }

  void advanceParticles(Emitter& m) {
    for (size_t i = 0; i < m.parts.size();) {
      if (update(m, m.parts[i])) {
        i++;
      } else {
        m.parts[i] = m.parts.back();
        m.parts.pop_back();
      }
    }
  }

  void step(Emitter& m, V2 pos) {
    if (!m.paused) {
      const int before = (int)m.acc;
      const float jitter = unit() * m.randRate;
      float inc = jitter + m.rate.cur();
      if (m.circular) inc = (float)((double)inc * 1.33);
      m.rate.advance();
      m.acc = m.rateMul * inc + m.acc;
      const int after = (int)m.acc;
      for (int k = before; k < after; k++)
        if (!spawn(m, pos)) break;
    }
    advanceParticles(m);
  }

  void tick(Emitter& m, V2 pos) {
    if (!m.paused && m.pending > 0) {
      const float steps = m.rateMul * (float)m.pending;
      const int n = steps > 100000.0f ? 100000 : steps > 0.0f ? (int)steps : 0;
      for (int k = 0; k < n; k++) step(m, pos);
      m.pending = 0;
    }
    step(m, pos);
  }
};

void Renderer::emitAdvance(const Board& b, int ticks, bool fresh) {
  if (b.emitters.empty()) {
    emit_.reset();
    return;
  }
  bool rebuild = fresh || !emit_ || emit_->em.size() != b.emitters.size();
  for (size_t i = 0; !rebuild && i < b.emitters.size(); i++)
    if (emit_->em[i].node != b.emitters[i].node) rebuild = true;
  if (rebuild) {
    emit_ = std::make_shared<EmitFx>();
    emit_->init(b);
    ticks = std::max(ticks, 1);
  }
  for (int t = 0; t < ticks; t++)
    for (size_t i = 0; i < emit_->em.size(); i++) emit_->tick(emit_->em[i], b.emitters[i].pos);
}

void Renderer::emitDraw(const Board& b, int layer, size_t at) {
  if (!emit_) return;
  for (size_t i = 0; i < emit_->em.size() && i < b.emitters.size(); i++) {
    const Emitter& m = emit_->em[i];
    const Emit& e = b.emitters[i];
    if (e.before != at || m.layer != layer || !m.visible) continue;
    Tex* t = objImage(m.image);
    if (!t || !t->t) continue;
    const float ox = m.attached ? e.pos.x : 0.0f, oy = m.attached ? e.pos.y : 0.0f;
    const SDL_FRect src{0, 0, (float)t->w, (float)t->h};
    for (const Particle& p : m.parts) {
      const float u = (float)p.age / (float)p.life;
      float alpha = m.alpha.at(u);
      if (p.fadeIn > 0 && p.age < p.fadeIn)
        alpha = alpha * ((float)p.age / (float)p.fadeIn);
      else if (p.fadeAt >= 0 && p.fadeAt <= p.age)
        alpha = alpha * (1.0f - (float)(p.age - p.fadeAt) / (float)std::max(1, p.life - p.fadeAt));
      auto chan = [&](const Curve& c) { return std::clamp((int)(c.at(u) * 255.0f), 0, 255); };
      const uint32_t rgb = ((uint32_t)chan(m.colR) << 16) | ((uint32_t)chan(m.colG) << 8) | (uint32_t)chan(m.colB);
      const int a = std::clamp((int)(std::clamp(alpha, 0.0f, 1.0f) * 255.0f), 0, 255);
      if (a == 0) continue;
      float x = m.posX.at(u) + ox + p.x, y = m.posY.at(u) + oy + p.y;
      if (p.spiralR != 0.0f) {
        const float f = 1.0f - (float)p.age / (float)p.life;
        const float ang = (((float)p.age / (float)p.life) * m.spiral + p.spiralPhase) * kPi * 2.0f;
        x += std::cos(ang) * f * p.spiralR;
        y += std::sin(ang) * f * p.spiralR;
      }
      float rot = m.rot.at(u) / 180.0f * kPi + p.rot;
      const float s = m.scale.at(u) + p.scale;
      if (m.orient) {
        float vx = m.velX.at(u) + p.vx, vy = m.velY.at(u) + p.vy;
        if (vx != 0.0f) rot += std::atan(vy / vx) + (vy < 0.0f ? kPi : 0.0f);
      }
      if (s <= 0.0f) continue;
      sprite(t, src, BX + x, BY + y, t->lw() * s, t->lh() * s, -rot, (Uint8)a, m.additive, rgb);
    }
  }
}

}
