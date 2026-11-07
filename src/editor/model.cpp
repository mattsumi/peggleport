#include "editor/model.h"

#include <algorithm>
#include <cstring>
#include <set>
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace pp::ed {

void putNum(Node& n, const std::string& key, Prim p, double v) {
  Val x;
  x.k = VK::Num;
  x.p = p;
  if (p == Prim::F32) {
    float f = (float)v;
    std::memcpy(&x.bits, &f, 4);
  } else {
    x.bits = (uint32_t)(int64_t)std::llround(v);
  }
  n.fields[key] = x;
}
void putStr(Node& n, const std::string& key, const std::string& v) {
  Val x;
  x.k = VK::Str;
  x.s = v;
  n.fields[key] = x;
}
void eraseField(Node& n, const std::string& key) { n.fields.erase(key); }
bool isObjectType(int t) { return t == 2 || t == 3 || t == 5 || t == 6 || t == 8; }
const char* typeName(int t) {
  switch (t) {
    case 1: return "physobj";
    case 2: return "line";
    case 3: return "polygon";
    case 5: return "circle";
    case 6: return "brick";
    case 8: return "hole";
    case 9: return "emitter";
  }
  return "object";
}
Node* moverOf(const Level& L, const Node& n) {
  auto v = n.val("mover");
  if (!v) return nullptr;
  if (v->n) return v->n.get();
  return const_cast<Node*>(L.resolve(*v));
}
Node* pegOf(const Node& n) {
  auto v = n.val("peg");
  return v && v->k == VK::Child ? v->n.get() : nullptr;
}
int pegTypeOf(const Node& n) {
  Node* p = pegOf(n);
  return p ? (int)p->i("type") : 0;
}
void setPegType(Node& n, int type) {
  if (type <= 0) {
    n.fields.erase("peg");
    return;
  }
  Node* p = pegOf(n);
  if (!p) {
    Val v;
    v.k = VK::Child;
    v.n = std::make_shared<Node>();
    v.n->flags["variable"] = true;
    n.fields["peg"] = v;
    p = v.n.get();
  }
  putNum(*p, "type", Prim::U8, type);
}

static void commonFlags(Node& n) {
  n.flags["collidable"] = true;
  n.flags["visible"] = true;
  n.flags["can_move"] = true;
}
NodeP newCircle(float x, float y, float r, int pegType, bool variable) {
  auto n = std::make_shared<Node>();
  n->type = 5;
  commonFlags(*n);
  n->flags["static"] = true;
  n->flags["unk_flag_4"] = true;
  putNum(*n, "x", Prim::F32, x);
  putNum(*n, "y", Prim::F32, y);
  putNum(*n, "radius", Prim::F32, r);
  if (pegType > 0) {
    setPegType(*n, pegType);
    pegOf(*n)->flags["variable"] = variable;
  }
  return n;
}
NodeP newLine(float x1, float y1, float x2, float y2, bool visible) {
  auto n = std::make_shared<Node>();
  n->type = 2;
  commonFlags(*n);
  n->flags["visible"] = visible;
  putNum(*n, "x1", Prim::F32, x1);
  putNum(*n, "y1", Prim::F32, y1);
  putNum(*n, "x2", Prim::F32, x2);
  putNum(*n, "y2", Prim::F32, y2);
  return n;
}
NodeP newPoly(float x, float y, const std::vector<V2>& local) {
  auto n = std::make_shared<Node>();
  n->type = 3;
  commonFlags(*n);
  n->flags["visible"] = false;
  n->flags["unk_flag_0"] = true;
  n->flags["unk_flag_b3"] = true;
  putNum(*n, "x", Prim::F32, x);
  putNum(*n, "y", Prim::F32, y);
  Val v;
  v.k = VK::Verts;
  for (auto& p : local) {
    v.verts.push_back(p.x);
    v.verts.push_back(p.y);
  }
  if (!local.empty()) {
    v.verts.push_back(local[0].x);
    v.verts.push_back(local[0].y);
  }
  n->fields["vertices"] = v;
  return n;
}
BrickParams brickParamsOf(const Node& n) {
  BrickParams p;
  p.type = (int)n.i("brick_type", 0);
  p.sub = n.has("brick_subdiv") ? (int)n.i("brick_subdiv") : 2;
  p.radius = n.f("brick_radius", 35.0f);
  p.ext = n.has("brick_angle") ? n.f("brick_angle") : 30.0f;
  p.ang150 = n.f("brick_left_angle", 0.0f);
  p.ang154 = n.f("brick_right_angle", 0.0f);
  p.thick = n.has("brick_thickness") ? n.f("brick_thickness") : 20.0f;
  p.rot = n.f("brick_rotation", -90.0f);
  p.off = {n.f("u_0x160", 0.0f), n.f("u_0x164", 0.0f)};
  return p;
}
void syncBrickCache(Node& n) {
  BrickShape s = brickInitFromParams(brickParamsOf(n));
  putNum(n, "u_0x170", Prim::U8, s.innerEnd);
  putNum(n, "u_0x174", Prim::U8, s.outerStart);
  putNum(n, "u_0x178", Prim::U8, s.innerStart);
  putNum(n, "u_0x17c", Prim::U8, s.outerEnd);
}
NodeP newBrick(float x, float y, const BrickParams& bp, int pegType) {
  auto n = std::make_shared<Node>();
  n->type = 6;
  commonFlags(*n);
  n->flags["unk_flag_b3"] = true;
  putNum(*n, "x", Prim::F32, x);
  putNum(*n, "y", Prim::F32, y);
  putNum(*n, "brick_type", Prim::U8, bp.type);
  putNum(*n, "brick_subdiv", Prim::U8, bp.sub);
  if (bp.type != 5) putNum(*n, "brick_angle", Prim::F32, bp.ext);
  putNum(*n, "brick_thickness", Prim::F32, bp.thick);
  putNum(*n, "brick_radius", Prim::F32, bp.radius);
  putNum(*n, "brick_rotation", Prim::F32, bp.rot);
  if (bp.ang150 != 0 || bp.ang154 != 0) {
    putNum(*n, "brick_left_angle", Prim::F32, bp.ang150);
    putNum(*n, "brick_right_angle", Prim::F32, bp.ang154);
    putNum(*n, "brick_right_angle_first", Prim::F32, bp.ang154);
  }
  syncBrickCache(*n);
  if (pegType > 0) setPegType(*n, pegType);
  return n;
}
NodeP newHole(float x, float y, int w, int h, bool circular, int score) {
  auto n = std::make_shared<Node>();
  n->type = 8;
  commonFlags(*n);
  n->flags["circular"] = circular;
  putNum(*n, "x", Prim::F32, x);
  putNum(*n, "y", Prim::F32, y);
  putNum(*n, "width", Prim::I32, w);
  putNum(*n, "height", Prim::I32, h);
  if (score) putNum(*n, "score", Prim::I32, score);
  return n;
}
NodeP newMover(int type, float ax, float ay) {
  auto n = std::make_shared<Node>();
  n->type = 0;
  putNum(*n, "move_type", Prim::I8, type);
  putNum(*n, "anchor_x", Prim::F32, ax);
  putNum(*n, "anchor_y", Prim::F32, ay);
  putNum(*n, "time", Prim::U16, 200);
  if (type != 8 && type != 9) putNum(*n, "radius", Prim::I16, 50);
  return n;
}
NodeP cloneNode(const NodeP& n, std::map<const Node*, NodeP>* remap) {
  auto c = std::make_shared<Node>(*n);
  if (remap) (*remap)[n.get()] = c;
  for (auto& kv : c->fields) {
    Val& v = kv.second;
    if ((v.k == VK::Ptr || v.k == VK::Child) && v.n) {
      v.n = cloneNode(v.n, remap);
    } else if (v.k == VK::Ref && v.n && remap) {
      auto it = remap->find(v.n.get());
      if (it != remap->end()) {
        v.n = it->second;
      } else if (kv.first == "mover" || kv.first == "sub") {
        v.k = VK::Ptr;
        v.n = cloneNode(v.n, remap);
      }
    }
  }
  return c;
}

V2 rotGame(V2 p, float rot) {
  double s = std::sin((double)rot), c = std::cos((double)rot);
  return {(float)(c * p.x + s * p.y), (float)(c * p.y - s * p.x)};
}
static V2 rot2(V2 d, float a) {
  float c = std::cos(a), s = std::sin(a);
  return {c * d.x - s * d.y, s * d.x + c * d.y};
}
static bool buildMover(const Level& L, const Node& n, Mover& m) {
  const Node* mn = moverOf(L, n);
  if (!mn) return false;
  m = Mover();
  m.type = (int)mn->i("move_type");
  m.ax = mn->f("anchor_x");
  m.ay = mn->f("anchor_y");
  m.time = (int)mn->i("time", 200);
  m.offset = (int)mn->i("offset");
  m.rx = (float)mn->i("radius");
  m.ry = (float)mn->i("radius2");
  m.phase = mn->f("phase");
  m.moveRot = mn->f("move_rotation");
  m.maxAngle = mn->f("max_angle");
  m.rotation = mn->f("rotation");
  m.pause1 = (int)mn->i("pause1");
  m.pause2 = (int)mn->i("pause2");
  m.phase1 = (int)mn->i("phase1");
  m.phase2 = (int)mn->i("phase2");
  m.postDelay = mn->f("post_delay_phase");
  m.single = true;
  return true;
}
static void bounds(Geo& g) {
  if (g.pts.empty()) return;
  g.x0 = g.x1 = g.pts[0].x;
  g.y0 = g.y1 = g.pts[0].y;
  for (auto& p : g.pts) {
    g.x0 = std::min(g.x0, p.x); g.x1 = std::max(g.x1, p.x);
    g.y0 = std::min(g.y0, p.y); g.y1 = std::max(g.y1, p.y);
  }
}
Geo geometry(const Level& L, const Node& n, int tick) {
  Geo g;
  Mover mv;
  bool hm = buildMover(L, n, mv);
  V2 pos{n.f("x"), n.f("y")};
  float baseRot = n.f("poly_rotation"), rot = baseRot;
  if (hm && n.type != 2) {
    float x = pos.x, y = pos.y, r = baseRot;
    mv.calc(tick, x, y, r);
    pos = {x, y};
    if (mv.rotating()) rot = r;
  }
  switch (n.type) {
    case 5:
      g.kind = Geo::Circle;
      g.c = pos;
      g.r = n.f("radius");
      g.x0 = pos.x - g.r; g.x1 = pos.x + g.r; g.y0 = pos.y - g.r; g.y1 = pos.y + g.r;
      break;
    case 2: {
      g.kind = Geo::Line;
      g.pts = {{n.f("x1"), n.f("y1")}, {n.f("x2"), n.f("y2")}};
      g.c = g.pts[0];
      bounds(g);
      break;
    }
    case 3:
    case 6: {
      g.kind = Geo::Poly;
      g.closed = true;
      g.c = pos;
      g.rot = rot;
      std::vector<V2> local;
      if (n.type == 3) {
        if (auto v = n.val("vertices"))
          for (size_t i = 0; i + 1 < v->verts.size(); i += 2) local.push_back({v->verts[i], v->verts[i + 1]});
      } else {
        local = brickInitFromParams(brickParamsOf(n)).pts;
      }
      for (auto& p : local) g.pts.push_back(pos + rotGame(p, rot));
      bounds(g);
      break;
    }
    case 8: {
      g.kind = Geo::Hole;
      g.c = pos;
      g.circular = n.flag("circular");
      float hw = (float)n.i("width") * 0.5f, hh = (float)n.i("height") * 0.5f;
      g.pts = {{pos.x - hw, pos.y - hh}, {pos.x + hw, pos.y - hh}, {pos.x + hw, pos.y + hh}, {pos.x - hw, pos.y + hh}};
      bounds(g);
      break;
    }
    default: break;
  }
  return g;
}
float distToSegment(V2 p, V2 a, V2 b) {
  V2 ab = b - a;
  float l2 = ab.len2();
  float t = l2 > 1e-9f ? std::clamp((p - a).dot(ab) / l2, 0.0f, 1.0f) : 0.0f;
  V2 q = a + ab * t;
  return std::sqrt((p - q).len2());
}
bool pointInPoly(const std::vector<V2>& pts, V2 p) {
  bool in = false;
  for (size_t i = 0, j = pts.size() ? pts.size() - 1 : 0; i < pts.size(); j = i++) {
    const V2 &a = pts[i], &b = pts[j];
    if (((a.y > p.y) != (b.y > p.y)) && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) in = !in;
  }
  return in;
}
float distToGeo(const Geo& g, V2 p) {
  switch (g.kind) {
    case Geo::Circle: return std::max(0.0f, std::sqrt((p - g.c).len2()) - g.r);
    case Geo::Line: return distToSegment(p, g.pts[0], g.pts[1]);
    case Geo::Hole:
      if (g.circular) {
        float rx = (g.x1 - g.x0) * 0.5f, ry = (g.y1 - g.y0) * 0.5f;
        if (rx < 0.01f || ry < 0.01f) return 1e9f;
        float dx = (p.x - g.c.x) / rx, dy = (p.y - g.c.y) / ry;
        float d = std::sqrt(dx * dx + dy * dy);
        return d <= 1 ? 0 : (d - 1) * std::min(rx, ry);
      }
      if (p.x >= g.x0 && p.x <= g.x1 && p.y >= g.y0 && p.y <= g.y1) return 0;
      return std::max(std::max(g.x0 - p.x, p.x - g.x1), std::max(g.y0 - p.y, p.y - g.y1));
    case Geo::Poly: {
      if (pointInPoly(g.pts, p)) return 0;
      float best = 1e9f;
      for (size_t i = 0; i + 1 < g.pts.size(); i++) best = std::min(best, distToSegment(p, g.pts[i], g.pts[i + 1]));
      return best;
    }
    default: return 1e9f;
  }
}
bool hitTest(const Geo& g, V2 p, float tol) { return g.kind != Geo::None && distToGeo(g, p) <= tol; }
bool hitBox(const Geo& g, float x0, float y0, float x1, float y1) {
  if (g.kind == Geo::None) return false;
  return !(g.x1 < x0 || g.x0 > x1 || g.y1 < y0 || g.y0 > y1);
}

static void addF(Node& n, const char* k, float d) {
  if (n.has(k)) n.setF(k, n.f(k) + d);
}
V2 nodePos(const Level& L, const Node& n) {
  if (n.type == 2) return {n.f("x1"), n.f("y1")};
  return geometry(L, n).c;
}
void translateNode(Node& n, float dx, float dy, std::set<const Node*>* done) {
  if (n.type == 2) {
    addF(n, "x1", dx); addF(n, "x2", dx); addF(n, "y1", dy); addF(n, "y2", dy);
    return;
  }
  Node* mv = nullptr;
  if (auto v = n.val("mover"); v && v->n) mv = v->n.get();
  if (mv && (!done || done->insert(mv).second)) {
    addF(*mv, "anchor_x", dx); addF(*mv, "anchor_y", dy);
    addF(*mv, "obj_x", dx); addF(*mv, "obj_y", dy);
  }
  addF(n, "x", dx);
  addF(n, "y", dy);
}
static void setPos(Node& n, V2 p) {
  if (n.has("x")) { n.setF("x", p.x); n.setF("y", p.y); }
}
static float setDistance(const std::vector<V2>& A, const std::vector<V2>& B) {
  float worst = 0;
  for (auto& a : A) {
    float best = 1e9f;
    for (auto& b : B) best = std::min(best, (a - b).len2());
    worst = std::max(worst, best);
  }
  return std::sqrt(worst);
}
static V2 centroid(const std::vector<V2>& v) {
  size_t n = v.size();
  if (n > 1 && v.front().x == v.back().x && v.front().y == v.back().y) n--;
  V2 c;
  for (size_t i = 0; i < n; i++) c = c + v[i];
  return n ? c * (1.0f / (float)n) : c;
}
void rotateNode(const Level& L, Node& n, V2 pivot, float a, std::set<const Node*>* done) {
  Node* mv = nullptr;
  if (auto v = n.val("mover"); v && v->n) mv = v->n.get();
  auto rotPt = [&](float& x, float& y) {
    V2 q = pivot + rot2(V2(x, y) - pivot, a);
    x = q.x;
    y = q.y;
  };
  if (n.type == 2) {
    float x1 = n.f("x1"), y1 = n.f("y1"), x2 = n.f("x2"), y2 = n.f("y2");
    rotPt(x1, y1);
    rotPt(x2, y2);
    n.setF("x1", x1); n.setF("y1", y1); n.setF("x2", x2); n.setF("y2", y2);
    return;
  }
  if (mv) {
    if (done && !done->insert(mv).second) return;
    float x = mv->f("anchor_x"), y = mv->f("anchor_y");
    rotPt(x, y);
    mv->setF("anchor_x", x); mv->setF("anchor_y", y);
    if (mv->has("obj_x")) { float ox = mv->f("obj_x"), oy = mv->f("obj_y"); rotPt(ox, oy); mv->setF("obj_x", ox); mv->setF("obj_y", oy); }
    return;
  }
  float x = n.f("x"), y = n.f("y");
  if (n.type == 6) {
    std::vector<V2> before = geometry(L, n).pts;
    for (auto& p : before) p = pivot + rot2(p - pivot, a);
    putNum(n, "brick_rotation", Prim::F32, n.f("brick_rotation", -90.0f) + rad2deg(a));
    setPos(n, V2(x, y));
    std::vector<V2> after = geometry(L, n).pts;
    V2 d = centroid(before) - centroid(after);
    setPos(n, V2(x, y) + d);
    syncBrickCache(n);
    return;
  }
  rotPt(x, y);
  setPos(n, V2(x, y));
  if (n.type == 3) {
    if (auto it = n.fields.find("vertices"); it != n.fields.end()) {
      auto& vs = it->second.verts;
      for (size_t i = 0; i + 1 < vs.size(); i += 2) {
        V2 q = rot2(V2(vs[i], vs[i + 1]), a);
        vs[i] = q.x;
        vs[i + 1] = q.y;
      }
    }
  }
}
void mirrorNode(const Level& L, Node& n, bool flipX, float axis, std::set<const Node*>* done) {
  auto refl = [&](float v) { return 2 * axis - v; };
  Node* mv = nullptr;
  if (auto v = n.val("mover"); v && v->n) mv = v->n.get();
  if (n.type == 2) {
    if (flipX) { n.setF("x1", refl(n.f("x1"))); n.setF("x2", refl(n.f("x2"))); }
    else { n.setF("y1", refl(n.f("y1"))); n.setF("y2", refl(n.f("y2"))); }
    return;
  }
  if (mv) {
    if (done && !done->insert(mv).second) return;
    const char* k = flipX ? "anchor_x" : "anchor_y";
    mv->setF(k, refl(mv->f(k)));
    if (mv->has(flipX ? "obj_x" : "obj_y")) mv->setF(flipX ? "obj_x" : "obj_y", refl(mv->f(flipX ? "obj_x" : "obj_y")));
    return;
  }
  const char* kx = flipX ? "x" : "y";
  if (n.type == 6) {
    std::vector<V2> want = geometry(L, n).pts;
    for (auto& p : want) { if (flipX) p.x = refl(p.x); else p.y = refl(p.y); }
    float r0 = n.f("brick_rotation", -90.0f), ext = n.has("brick_angle") ? n.f("brick_angle") : 30.0f;
    float l = n.f("brick_left_angle"), r = n.f("brick_right_angle");
    float cand[3];
    if (flipX) { cand[0] = 180 - r0; cand[1] = 180 - r0 - ext; cand[2] = 180 - r0 + ext; }
    else { cand[0] = -r0; cand[1] = -r0 - ext; cand[2] = -r0 + ext; }
    float best = 1e9f, bestRot = cand[0];
    V2 bestD;
    float swapL = r, swapR = l;
    for (int swap = 0; swap < 2; swap++)
      for (float c : cand) {
        Node t = n;
        putNum(t, "brick_rotation", Prim::F32, c);
        if (swap && (n.has("brick_left_angle") || n.has("brick_right_angle"))) {
          putNum(t, "brick_left_angle", Prim::F32, swapL);
          putNum(t, "brick_right_angle", Prim::F32, swapR);
        } else if (swap) continue;
        std::vector<V2> got = geometry(L, t).pts;
        V2 d = centroid(want) - centroid(got);
        for (auto& p : got) p = p + d;
        float e = setDistance(want, got) + setDistance(got, want);
        if (e < best - 1e-4f) { best = e; bestRot = c; bestD = d; if (swap) { best -= 0; } }
        if (e < best + 1e-4f && e < 0.05f && swap) {   }
      }
    for (int swap = 0; swap < 2; swap++) {
      Node t = n;
      putNum(t, "brick_rotation", Prim::F32, bestRot);
      if (swap) {
        if (!(n.has("brick_left_angle") || n.has("brick_right_angle"))) continue;
        putNum(t, "brick_left_angle", Prim::F32, swapL);
        putNum(t, "brick_right_angle", Prim::F32, swapR);
        putNum(t, "brick_right_angle_first", Prim::F32, swapR);
      }
      std::vector<V2> got = geometry(L, t).pts;
      V2 d = centroid(want) - centroid(got);
      for (auto& p : got) p = p + d;
      float e = setDistance(want, got) + setDistance(got, want);
      if (std::fabs(e - best) < 2e-3f) {
        n = t;
        setPos(n, V2(n.f("x"), n.f("y")) + d);
        syncBrickCache(n);
        return;
      }
    }
    return;
  }
  n.setF(kx, refl(n.f(kx)));
  if (n.type == 3) {
    if (auto it = n.fields.find("vertices"); it != n.fields.end()) {
      auto& vs = it->second.verts;
      std::vector<V2> pts;
      for (size_t i = 0; i + 1 < vs.size(); i += 2) pts.push_back(flipX ? V2(-vs[i], vs[i + 1]) : V2(vs[i], -vs[i + 1]));
      std::reverse(pts.begin(), pts.end());
      for (size_t i = 0; i < pts.size(); i++) { vs[2 * i] = pts[i].x; vs[2 * i + 1] = pts[i].y; }
    }
  }
}

const char* kMoverTypeNames[] = {"None", "Vertical Cycle", "Horizontal Cycle", "Circle", "Horizontal Infinity", "Vertical Infinity", "Horizontal Arc",
                                 "Vertical Arc", "Rotate", "Rotate Back And Forth", "(unused 10)", "Vertical Wrap", "Horizontal Wrap",
                                 "Rotate Around Circle", "Retrace Circle", "Weird Shape"};

#define PF(key, label, def, lo, hi, step, req, grp) PropDef{key, label, PK::Float, Prim::F32, false, def, lo, hi, step, req, nullptr, 0, grp}
#define PI(key, label, prim, def, lo, hi, step, req, grp) PropDef{key, label, PK::Int, prim, false, def, lo, hi, step, req, nullptr, 0, grp}
#define PB(key, label, def, grp) PropDef{key, label, PK::Bool, Prim::U8, true, def, 0, 1, 1, false, nullptr, 0, grp}
#define PS(key, label, grp) PropDef{key, label, PK::Str, Prim::U8, false, 0, 0, 0, 0, false, nullptr, 0, grp}
#define PE(key, label, prim, def, names, base, grp) PropDef{key, label, PK::Enum, prim, false, def, base, base + 99, 1, false, names, base, grp}
#define PC(key, label, grp) PropDef{key, label, PK::Color, Prim::U32, false, 0, 0, 16777215, 1, false, nullptr, 0, grp}

static void addCommon(std::vector<PropDef>& v) {
  v.push_back(PB("collidable", "Collidable", true, "Object"));
  v.push_back(PB("visible", "Visible", true, "Object"));
  v.push_back(PB("can_move", "Moving", true, "Object"));
  v.push_back(PF("bouncy", "Bounce", 0, 0, 10, 0.05, false, "Object"));
  v.push_back(PF("rolly", "Rolling friction", 0, 0, 10, 0.01, false, "Object"));
  v.push_back(PF("max_bounce_vel", "Max bounce vel", 0, 0, 100, 0.5, false, "Object"));
  v.push_back(PS("id", "Id", "Object"));
  v.push_back(PS("logic", "Logic", "Object"));
  v.push_back(PI("sound", "Sound", Prim::U8, 0, 0, 255, 1, false, "Object"));
  v.push_back(PI("sub_id", "Sub id", Prim::I32, 0, -999, 999, 1, false, "Object"));
  v.push_back(PE("flipper_flags", "Flipper", Prim::U8, 0, "Default|Off|On", 0, "Object"));
  v.push_back(PB("foreground", "Foreground", false, "Draw"));
  v.push_back(PB("foreground2", "Foreground 2", false, "Draw"));
  v.push_back(PB("background", "Background", false, "Draw"));
  v.push_back(PB("draw_sort", "Draw sort", false, "Draw"));
  v.push_back(PB("draw_float", "Draw float", false, "Draw"));
  v.push_back(PB("ball_stop_reset", "Ball stop reset", false, "Object"));
  v.push_back(PS("image", "Image", "Draw"));
  v.push_back(PF("image_dx", "Image dx", 0, -999, 999, 1, false, "Draw"));
  v.push_back(PF("image_dy", "Image dy", 0, -999, 999, 1, false, "Draw"));
  v.push_back(PF("image_rot", "Image rot", 0, -7, 7, 0.05, false, "Draw"));
  v.push_back(PC("solid_color", "Fill colour", "Draw"));
  v.push_back(PC("outline_color", "Outline colour", "Draw"));
}
static void addPeg(std::vector<PropDef>& v) {
  v.push_back(PB("is_peg", "Is a peg", false, "Peg"));
  v.push_back(PE("peg.type", "Peg colour", Prim::U8, 1, "Blue|Orange|Purple|Green", 1, "Peg"));
  v.push_back(PB("peg.variable", "Variable colour", true, "Peg"));
  v.push_back(PB("peg.crumble", "Crumble", false, "Peg"));
}
const std::vector<PropDef>& propsFor(const Node& n) {
  static std::map<int, std::vector<PropDef>> cache;
  auto it = cache.find(n.type);
  if (it != cache.end()) return it->second;
  std::vector<PropDef> v;
  switch (n.type) {
    case 5:
      v.push_back(PF("pos.x", "X", 0, -2000, 3000, 1, false, "Shape"));
      v.push_back(PF("pos.y", "Y", 0, -2000, 3000, 1, false, "Shape"));
      v.push_back(PF("radius", "Radius", 10, 1, 500, 1, true, "Shape"));
      addPeg(v);
      break;
    case 2:
      v.push_back(PF("x1", "X1", 0, -2000, 3000, 1, true, "Shape"));
      v.push_back(PF("y1", "Y1", 0, -2000, 3000, 1, true, "Shape"));
      v.push_back(PF("x2", "X2", 0, -2000, 3000, 1, true, "Shape"));
      v.push_back(PF("y2", "Y2", 0, -2000, 3000, 1, true, "Shape"));
      break;
    case 3:
      v.push_back(PF("pos.x", "X", 0, -2000, 3000, 1, false, "Shape"));
      v.push_back(PF("pos.y", "Y", 0, -2000, 3000, 1, false, "Shape"));
      v.push_back(PE("poly_normal_dir", "Collides", Prim::I8, 1, "Keep In|Both|Keep Out", -1, "Shape"));
      v.push_back(PE("poly_grow_type", "Grow type", Prim::I32, 0, "None|Reveal L>R|Reveal R>L|Move L>R|Move R>L|Middle out", 0, "Shape"));
      v.push_back(PB("image_tile", "Tile image", false, "Draw"));
      v.push_back(PB("image_mirror", "Mirror image", false, "Draw"));
      break;
    case 6:
      v.push_back(PF("pos.x", "X", 0, -2000, 3000, 1, false, "Shape"));
      v.push_back(PF("pos.y", "Y", 0, -2000, 3000, 1, false, "Shape"));
      v.push_back(PE("brick_type", "Brick type", Prim::U8, 0, "Normal Curved|Outer Curved|Outer Curved 90|Inner Curved|Inner Curved 90|Straight", 0, "Shape"));
      v.push_back(PF("brick_radius", "Radius / length", 35, 1, 2000, 1, true, "Shape"));
      v.push_back(PF("brick_angle", "Sweep (deg)", 30, -360, 360, 1, false, "Shape"));
      v.push_back(PF("brick_thickness", "Thickness", 20, 1, 500, 1, false, "Shape"));
      v.push_back(PF("brick_rotation", "Rotation (deg)", -90, -3600, 3600, 5, true, "Shape"));
      v.push_back(PI("brick_subdiv", "Subdivisions", Prim::U8, 2, 0, 40, 1, false, "Shape"));
      v.push_back(PF("brick_left_angle", "Left end angle", 0, -90, 90, 1, false, "Shape"));
      v.push_back(PF("brick_right_angle", "Right end angle", 0, -90, 90, 1, false, "Shape"));
      v.push_back(PB("anchor_left", "Left anchor", false, "Shape"));
      v.push_back(PB("anchor_right", "Right anchor", false, "Shape"));
      v.push_back(PB("tex_flip", "Flip texture", false, "Draw"));
      v.push_back(PB("image_tile", "Tile image", false, "Draw"));
      addPeg(v);
      break;
    case 8:
      v.push_back(PF("pos.x", "X", 0, -2000, 3000, 1, false, "Shape"));
      v.push_back(PF("pos.y", "Y", 0, -2000, 3000, 1, false, "Shape"));
      v.push_back(PI("width", "Width", Prim::I32, 40, 1, 2000, 1, true, "Shape"));
      v.push_back(PI("height", "Height", Prim::I32, 40, 1, 2000, 1, true, "Shape"));
      v.push_back(PB("circular", "Circular", false, "Shape"));
      v.push_back(PI("score", "Score", Prim::I32, 0, 0, 10000000, 1000, false, "Hole"));
      v.push_back(PI("out_delay", "Out delay", Prim::I32, 0, 0, 1000, 1, false, "Hole"));
      break;
    default: break;
  }
  addCommon(v);
  return cache[n.type] = v;
}
const std::vector<PropDef>& moverProps() {
  static std::vector<PropDef> v;
  if (!v.empty()) return v;
  v.push_back(PE("move_type_abs", "Mover type", Prim::I8, 0,
                 "None|Vertical Cycle|Horizontal Cycle|Circle|Horizontal Infinity|Vertical Infinity|Horizontal Arc|Vertical Arc|Rotate|Rotate Back And Forth|(10)|Vertical Wrap|Horizontal Wrap|Rotate Around Circle|Retrace Circle|Weird Shape",
                 0, "Mover"));
  v.push_back(PB("move_reverse", "Reverse", false, "Mover"));
  v.push_back(PF("anchor_x", "Anchor X", 0, -2000, 3000, 1, true, "Mover"));
  v.push_back(PF("anchor_y", "Anchor Y", 0, -2000, 3000, 1, true, "Mover"));
  v.push_back(PI("radius", "Radius X", Prim::I16, 0, -2000, 2000, 1, false, "Mover"));
  v.push_back(PI("radius2", "Radius Y", Prim::I16, 0, -2000, 2000, 1, false, "Mover"));
  v.push_back(PI("time", "Period (ticks)", Prim::U16, 200, 1, 65535, 10, true, "Mover"));
  v.push_back(PI("offset", "Time offset", Prim::I16, 0, -32768, 32767, 5, false, "Mover"));
  v.push_back(PF("phase", "Phase (0..1)", 0, -1, 1, 0.05, false, "Mover"));
  v.push_back(PI("pause1", "Pause 1", Prim::U16, 0, 0, 65535, 5, false, "Mover"));
  v.push_back(PI("phase1", "Pause 1 at %", Prim::I8, 0, -128, 127, 5, false, "Mover"));
  v.push_back(PI("pause2", "Pause 2", Prim::U16, 0, 0, 65535, 5, false, "Mover"));
  v.push_back(PI("phase2", "Pause 2 at %", Prim::I8, 0, -128, 127, 5, false, "Mover"));
  v.push_back(PF("post_delay_phase", "Post delay phase", 0, -10, 10, 0.05, false, "Mover"));
  v.push_back(PF("max_angle", "Max angle (rad)", 0, -7, 7, 0.05, false, "Mover"));
  v.push_back(PF("move_rotation", "Path rot (rad)", 0, -7, 7, 0.05, false, "Mover"));
  v.push_back(PF("rotation", "Rotation (rad)", 0, -7, 7, 0.05, false, "Mover"));
  return v;
}

static Node* addr(Node& n, const char*& key) {
  if (std::strncmp(key, "peg.", 4) == 0) {
    key += 4;
    return pegOf(n);
  }
  return &n;
}
bool hasProp(const Node& n, const PropDef& d) {
  if (!std::strcmp(d.key, "is_peg")) return pegOf(n) != nullptr;
  const char* k = d.key;
  Node* t = addr(const_cast<Node&>(n), k);
  if (!t) return false;
  if (d.isFlag) return t->flags.count(k) != 0;
  return t->has(k);
}
double getNum(const Level& L, const Node& n, const PropDef& d) {
  const char* k = d.key;
  if (!std::strcmp(k, "is_peg")) return pegOf(n) ? 1 : 0;
  if (!std::strcmp(k, "pos.x")) return nodePos(L, n).x;
  if (!std::strcmp(k, "pos.y")) return nodePos(L, n).y;
  if (!std::strcmp(k, "move_type_abs")) return std::abs((int)n.i("move_type"));
  if (!std::strcmp(k, "move_reverse")) return n.i("move_type") < 0 ? 1 : 0;
  Node* t = addr(const_cast<Node&>(n), k);
  if (!t) return d.def;
  if (d.isFlag) {
    auto it = t->flags.find(k);
    return it == t->flags.end() ? (d.def ? 1.0 : 0.0) : (it->second ? 1.0 : 0.0);
  }
  if (!t->has(k)) return d.def;
  return d.prim == Prim::F32 ? (double)t->f(k) : (double)t->i(k);
}
std::string getStr(const Node& n, const PropDef& d) { return n.str(d.key); }
std::string propText(const Level& L, const Node& n, const PropDef& d) {
  char b[64];
  switch (d.kind) {
    case PK::Bool: return getNum(L, n, d) != 0 ? "on" : "off";
    case PK::Str: {
      std::string s = getStr(n, d);
      return s.empty() ? "-" : s;
    }
    case PK::Enum: {
      int idx = (int)getNum(L, n, d) - d.enumBase;
      std::string names = d.enums ? d.enums : "";
      size_t pos = 0;
      for (int i = 0;; i++) {
        size_t e = names.find('|', pos);
        std::string tok = names.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        if (i == idx) return tok;
        if (e == std::string::npos) break;
        pos = e + 1;
      }
      std::snprintf(b, sizeof b, "%d", (int)getNum(L, n, d));
      return b;
    }
    case PK::Color: {
      std::snprintf(b, sizeof b, "#%06X", (unsigned)getNum(L, n, d));
      return b;
    }
    case PK::Int: std::snprintf(b, sizeof b, "%d", (int)getNum(L, n, d)); return b;
    default: {
      std::snprintf(b, sizeof b, "%.4g", getNum(L, n, d));
      return b;
    }
  }
}
bool setNum(Level& L, Node& n, const PropDef& d, double v) {
  const char* k = d.key;
  if (d.kind != PK::Bool && d.kind != PK::Str) v = std::clamp(v, d.lo, d.kind == PK::Enum ? d.lo + 99 : d.hi);
  if (!std::strcmp(k, "is_peg")) {
    if (v != 0 && !pegOf(n)) setPegType(n, 1);
    else if (v == 0) n.fields.erase("peg");
    return true;
  }
  if (!std::strcmp(k, "pos.x") || !std::strcmp(k, "pos.y")) {
    V2 p = nodePos(L, n);
    if (!std::strcmp(k, "pos.x")) translateNode(n, (float)v - p.x, 0);
    else translateNode(n, 0, (float)v - p.y);
    return true;
  }
  if (!std::strcmp(k, "move_type_abs")) {
    bool neg = n.i("move_type") < 0;
    putNum(n, "move_type", Prim::I8, neg ? -v : v);
    return true;
  }
  if (!std::strcmp(k, "move_reverse")) {
    int t = std::abs((int)n.i("move_type"));
    putNum(n, "move_type", Prim::I8, v != 0 ? -t : t);
    return true;
  }
  Node* t = addr(n, k);
  if (!t) return false;
  if (d.isFlag) {
    t->flags[k] = v != 0;
    return true;
  }
  if (!d.required && v == d.def && !t->has(k)) return true;
  if (!d.required && v == d.def && d.prim != Prim::U8 && std::strcmp(d.key, "brick_type")) {
    t->fields.erase(k);
    return true;
  }
  putNum(*t, k, d.prim, v);
  if (!std::strcmp(k, "brick_right_angle")) putNum(*t, "brick_right_angle_first", Prim::F32, v);
  if (!std::strcmp(k, "brick_left_angle") && !t->has("brick_right_angle")) {
    putNum(*t, "brick_right_angle", Prim::F32, t->f("brick_right_angle"));
    putNum(*t, "brick_right_angle_first", Prim::F32, t->f("brick_right_angle"));
  }
  if (!std::strcmp(k, "brick_right_angle") && !t->has("brick_left_angle")) putNum(*t, "brick_left_angle", Prim::F32, 0);
  if (n.type == 6 && !std::strncmp(k, "brick_", 6)) syncBrickCache(n);
  return true;
}
bool setStr(Node& n, const PropDef& d, const std::string& s) {
  if (s.empty()) n.fields.erase(d.key);
  else putStr(n, d.key, s);
  return true;
}
bool setPropText(Level& L, Node& n, const PropDef& d, const std::string& text) {
  switch (d.kind) {
    case PK::Str: return setStr(n, d, text == "-" ? "" : text);
    case PK::Bool: return setNum(L, n, d, (text == "1" || text == "on" || text == "true" || text == "yes") ? 1 : 0);
    case PK::Color: {
      std::string t = text;
      if (!t.empty() && t[0] == '#') t.erase(0, 1);
      return setNum(L, n, d, (double)std::strtoul(t.c_str(), nullptr, 16));
    }
    case PK::Enum: {
      char* end = nullptr;
      double v = std::strtod(text.c_str(), &end);
      if (end && *end == 0 && end != text.c_str()) return setNum(L, n, d, v);
      std::string names = d.enums ? d.enums : "";
      size_t pos = 0;
      for (int i = 0;; i++) {
        size_t e = names.find('|', pos);
        std::string tok = names.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        if (tok == text) return setNum(L, n, d, d.enumBase + i);
        if (e == std::string::npos) break;
        pos = e + 1;
      }
      return false;
    }
    default: {
      char* end = nullptr;
      double v = std::strtod(text.c_str(), &end);
      if (end == text.c_str()) return false;
      return setNum(L, n, d, v);
    }
  }
}
bool attachMover(Level& L, Node& obj, int type) {
  if (!isObjectType(obj.type) || obj.type == 2) return false;
  if (obj.val("mover")) return false;
  V2 rest = nodePos(L, obj);
  NodeP m = newMover(type, 0, 0);
  Val tmp;
  tmp.k = VK::Ptr;
  tmp.n = m;
  Node probe = obj;
  probe.fields["mover"] = tmp;
  probe.fields.erase("x");
  probe.fields.erase("y");
  V2 off = geometry(L, probe).c;
  m->setF("anchor_x", rest.x - off.x);
  m->setF("anchor_y", rest.y - off.y);
  putNum(*m, "obj_x", Prim::F32, rest.x);
  putNum(*m, "obj_y", Prim::F32, rest.y);
  obj.fields["mover"] = tmp;
  obj.fields.erase("x");
  obj.fields.erase("y");
  obj.fields.erase("poly_rotation");
  return true;
}
void detachMover(Level& L, Node& obj) {
  if (!obj.val("mover")) return;
  V2 rest = nodePos(L, obj);
  obj.fields.erase("mover");
  putNum(obj, "x", Prim::F32, rest.x);
  putNum(obj, "y", Prim::F32, rest.y);
}

std::vector<V2> gridPoints(int cols, int rows, float dx, float dy, V2 o) {
  std::vector<V2> v;
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++) v.push_back({o.x + c * dx, o.y + r * dy});
  return v;
}
std::vector<V2> circlePoints(int n, float radius, V2 c, float startDeg) {
  std::vector<V2> v;
  for (int i = 0; i < n; i++) {
    float a = deg2rad(startDeg) + 2 * kPi * (float)i / (float)std::max(1, n);
    v.push_back({c.x + radius * std::cos(a), c.y + radius * std::sin(a)});
  }
  return v;
}
std::vector<V2> spiralPoints(int n, float r0, float r1, float turns, V2 c) {
  std::vector<V2> v;
  for (int i = 0; i < n; i++) {
    float t = n > 1 ? (float)i / (float)(n - 1) : 0.0f;
    float a = -kPi / 2 + 2 * kPi * turns * t, r = r0 + (r1 - r0) * t;
    v.push_back({c.x + r * std::cos(a), c.y + r * std::sin(a)});
  }
  return v;
}
std::vector<V2> sinePoints(int n, float length, float amp, float periods, V2 o) {
  std::vector<V2> v;
  for (int i = 0; i < n; i++) {
    float t = n > 1 ? (float)i / (float)(n - 1) : 0.0f;
    v.push_back({o.x + length * t, o.y + amp * std::sin(2 * kPi * periods * t)});
  }
  return v;
}

NodeSnap snapNode(const NodeP& n) {
  NodeSnap s;
  s.node = n;
  s.content = *n;
  for (auto& kv : s.content.fields) {
    const Val& v = kv.second;
    if ((v.k == VK::Ptr || v.k == VK::Child) && v.n) s.kids.push_back(snapNode(v.n));
  }
  return s;
}
void restoreNode(const NodeSnap& s) {
  *s.node = s.content;
  for (auto& k : s.kids) restoreNode(k);
}

}
