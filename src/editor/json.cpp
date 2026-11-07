#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <set>
#include <cstdint>
#include <utility>
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <cctype>

#include "editor/model.h"

namespace pp::ed {

void J::set(const std::string& k, J v) {
  for (auto& kv : o)
    if (kv.first == k) { kv.second = std::move(v); return; }
  o.emplace_back(k, std::move(v));
}
const J* J::get(const std::string& k) const {
  for (auto& kv : o)
    if (kv.first == k) return &kv.second;
  return nullptr;
}
static void esc(const std::string& s, std::string& out) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
        else out += (char)c;
    }
  }
  out += '"';
}
static std::string fmtNum(double v) {
  if (v == 0 && std::signbit(v)) return "-0";
  if (std::floor(v) == v && std::fabs(v) < 1e15) {
    char b[32];
    std::snprintf(b, sizeof b, "%lld", (long long)v);
    return b;
  }
  char b[40];
  for (int prec = 6; prec <= 17; prec++) {
    std::snprintf(b, sizeof b, "%.*g", prec, v);
    if ((float)std::strtod(b, nullptr) == (float)v && (prec >= 9 || (double)(float)v == v || true)) {
      if ((float)std::strtod(b, nullptr) == (float)v) return b;
    }
  }
  std::snprintf(b, sizeof b, "%.9g", v);
  return b;
}
static bool simpleArr(const J& j) {
  if (j.a.size() > 8) return false;
  for (auto& e : j.a)
    if (e.t == J::Obj || e.t == J::Arr) return false;
  return true;
}
static void dumpJ(const J& j, int ind, int depth, std::string& out) {
  auto nl = [&](int d) {
    if (ind <= 0) return;
    out += '\n';
    out.append((size_t)(d * ind), ' ');
  };
  switch (j.t) {
    case J::Null: out += "null"; break;
    case J::Bool: out += j.b ? "true" : "false"; break;
    case J::Num: out += fmtNum(j.n); break;
    case J::Str: esc(j.s, out); break;
    case J::Arr: {
      if (j.a.empty()) { out += "[]"; break; }
      bool flat = simpleArr(j);
      out += '[';
      for (size_t i = 0; i < j.a.size(); i++) {
        if (i) out += flat ? ", " : ",";
        if (!flat) nl(depth + 1);
        dumpJ(j.a[i], ind, depth + 1, out);
      }
      if (!flat) nl(depth);
      out += ']';
      break;
    }
    case J::Obj: {
      if (j.o.empty()) { out += "{}"; break; }
      bool leaf = j.o.size() <= 4;
      for (auto& kv : j.o)
        if (kv.second.t == J::Obj || (kv.second.t == J::Arr && !kv.second.a.empty())) leaf = false;
      out += '{';
      for (size_t i = 0; i < j.o.size(); i++) {
        if (i) out += leaf ? ", " : ",";
        if (!leaf) nl(depth + 1);
        esc(j.o[i].first, out);
        out += ": ";
        dumpJ(j.o[i].second, ind, depth + 1, out);
      }
      if (!leaf) nl(depth);
      out += '}';
      break;
    }
  }
}
std::string J::dump(int indent) const {
  std::string s;
  dumpJ(*this, indent, 0, s);
  return s;
}

namespace {
struct Parser {
  const std::string& s;
  size_t p = 0;
  std::string err;
  explicit Parser(const std::string& t) : s(t) {}
  void ws() { while (p < s.size() && std::isspace((unsigned char)s[p])) p++; }
  bool fail(const std::string& m) { if (err.empty()) err = m + " at offset " + std::to_string(p); return false; }
  bool str(std::string& out) {
    if (s[p] != '"') return fail("expected string");
    p++;
    while (p < s.size() && s[p] != '"') {
      char c = s[p++];
      if (c == '\\' && p < s.size()) {
        char e = s[p++];
        switch (e) {
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case 'r': out += '\r'; break;
          case 'u': {
            unsigned v = (unsigned)std::strtoul(s.substr(p, 4).c_str(), nullptr, 16);
            p += 4;
            if (v < 0x80) out += (char)v;
            else if (v < 0x800) { out += (char)(0xC0 | (v >> 6)); out += (char)(0x80 | (v & 63)); }
            else { out += (char)(0xE0 | (v >> 12)); out += (char)(0x80 | ((v >> 6) & 63)); out += (char)(0x80 | (v & 63)); }
            break;
          }
          default: out += e;
        }
      } else out += c;
    }
    if (p >= s.size()) return fail("unterminated string");
    p++;
    return true;
  }
  bool val(J& j, int depth = 0) {
    ws();
    if (p >= s.size()) return fail("unexpected end");
    if (depth > 64) return fail("too deep");
    char c = s[p];
    if (c == '{') {
      p++;
      j = J::obj();
      ws();
      if (s[p] == '}') { p++; return true; }
      for (;;) {
        ws();
        std::string k;
        if (!str(k)) return false;
        ws();
        if (s[p] != ':') return fail("expected ':'");
        p++;
        J v;
        if (!val(v, depth + 1)) return false;
        j.o.emplace_back(k, std::move(v));
        ws();
        if (s[p] == ',') { p++; continue; }
        if (s[p] == '}') { p++; return true; }
        return fail("expected ',' or '}'");
      }
    }
    if (c == '[') {
      p++;
      j = J::arr();
      ws();
      if (s[p] == ']') { p++; return true; }
      for (;;) {
        J v;
        if (!val(v, depth + 1)) return false;
        j.a.push_back(std::move(v));
        ws();
        if (s[p] == ',') { p++; continue; }
        if (s[p] == ']') { p++; return true; }
        return fail("expected ',' or ']'");
      }
    }
    if (c == '"') { j.t = J::Str; return str(j.s); }
    if (!s.compare(p, 4, "true")) { p += 4; j = J::boolean(true); return true; }
    if (!s.compare(p, 5, "false")) { p += 5; j = J::boolean(false); return true; }
    if (!s.compare(p, 4, "null")) { p += 4; j = J(); return true; }
    char* end = nullptr;
    double v = std::strtod(s.c_str() + p, &end);
    if (end == s.c_str() + p) return fail("unexpected character");
    p = (size_t)(end - s.c_str());
    j = J::num(v);
    return true;
  }
};
}

bool parseJson(const std::string& text, J& out, std::string* err) {
  Parser ps(text);
  if (!ps.val(out)) {
    if (err) *err = ps.err;
    return false;
  }
  ps.ws();
  if (ps.p != text.size()) {
    if (err) *err = "trailing characters at offset " + std::to_string(ps.p);
    return false;
  }
  return true;
}

namespace {
enum Ctx { CObj = 0, CMover = 1, CPeg = 2, CStruct = 3 };

const std::map<std::string, Prim>& primTable(Ctx c) {
  static std::map<std::string, Prim> obj, mover, peg, strc;
  if (obj.empty()) {
    for (const char* k : {"rolly", "bouncy", "image_dx", "image_dy", "image_rot", "max_bounce_vel", "x", "y", "radius", "x1", "y1", "x2", "y2", "u_0xfc", "u_0x100",
                          "poly_rotation", "u_0x120", "poly_scale", "u_0x160", "u_0x164", "brick_left_angle", "brick_right_angle_first", "brick_right_angle",
                          "brick_angle", "brick_thickness", "brick_radius", "brick_rotation", "hx", "hy", "hardware_rate", "rand_rate", "software_rate",
                          "spiral_count", "alpha_fade_at", "alpha_into", "duration", "ua", "ub"})
      obj[k] = Prim::F32;
    for (const char* k : {"u_0x4c", "u_0xc8", "u_0x50", "sub_id", "poly_grow_type", "width", "height", "score", "out_delay", "main", "fast_forward", "max"}) obj[k] = Prim::I32;
    for (const char* k : {"solid_color", "outline_color"}) obj[k] = Prim::U32;
    for (const char* k : {"sound", "flipper_flags", "u_0x138", "brick_type", "brick_subdiv", "u_0x170", "u_0x174", "u_0x178", "u_0x17c", "unk_byte_0x942"}) obj[k] = Prim::U8;
    for (const char* k : {"u_0x140", "u_0xec"}) obj[k] = Prim::U16;
    obj["poly_normal_dir"] = Prim::I8;
    mover = {{"move_type", Prim::I8}, {"anchor_x", Prim::F32}, {"anchor_y", Prim::F32}, {"time", Prim::U16}, {"offset", Prim::I16}, {"radius", Prim::I16},
             {"phase", Prim::F32}, {"move_rotation", Prim::F32}, {"radius2", Prim::I16}, {"pause1", Prim::U16}, {"pause2", Prim::U16}, {"phase1", Prim::I8},
             {"phase2", Prim::I8}, {"post_delay_phase", Prim::F32}, {"max_angle", Prim::F32}, {"u_0x64", Prim::F32}, {"rotation", Prim::F32},
             {"sub_x", Prim::F32}, {"sub_y", Prim::F32}, {"obj_x", Prim::F32}, {"obj_y", Prim::F32}};
    peg = {{"type", Prim::U8}, {"u_0x18", Prim::I32}, {"u_0x20", Prim::I32}, {"u_0x24", Prim::U8}, {"u_0x1c", Prim::U8}};
    strc = {{"tag", Prim::U8}};
  }
  return c == CObj ? obj : c == CMover ? mover : c == CPeg ? peg : strc;
}

int keyRank(const std::string& k) {
  static const char* order[] = {"type", "kind", "_id", "id", "x", "y", "radius", "x1", "y1", "x2", "y2", "width", "height", "vertices", "peg", "mover"};
  for (int i = 0; i < 16; i++)
    if (k == order[i]) return i;
  return 100;
}

struct Writer {
  std::map<const Node*, int> ids;
  int next = 1;

  J num(const Val& v) {
    if (v.p == Prim::F32) {
      float f = v.f();
      if (!std::isfinite(f)) {
        J o = J::obj();
        o.set("bits", J::num((double)v.bits));
        return o;
      }
      return J::num((double)f);
    }
    return J::num((double)v.i());
  }
  J node(const Node& n, Ctx ctx) {
    std::vector<std::pair<std::string, J>> kv;
    if (ctx == CObj) {
      kv.emplace_back("type", J::num(n.type));
      kv.emplace_back("kind", J::str(typeName(n.type)));
    }
    for (auto& [k, v] : n.fields) {
      switch (v.k) {
        case VK::Num: kv.emplace_back(k, num(v)); break;
        case VK::Str: kv.emplace_back(k, J::str(v.s)); break;
        case VK::Verts: {
          J a = J::arr();
          for (size_t i = 0; i + 1 < v.verts.size(); i += 2) {
            J p = J::arr();
            p.a.push_back(J::num(v.verts[i]));
            p.a.push_back(J::num(v.verts[i + 1]));
            a.a.push_back(p);
          }
          kv.emplace_back(k, a);
          break;
        }
        case VK::Ptr: kv.emplace_back(k, ptr(v, k == "mover" || k == "sub" ? CMover : CObj)); break;
        case VK::Ref: {
          J o = J::obj();
          o.set("$ref", J::num(v.n && ids.count(v.n.get()) ? ids[v.n.get()] : v.ref));
          kv.emplace_back(k, o);
          break;
        }
        case VK::Child: kv.emplace_back(k, node(*v.n, k == "peg" ? CPeg : CStruct)); break;
        case VK::Null: break;
      }
    }
    for (auto& [k, b] : n.flags) kv.emplace_back(k, J::boolean(b));
    for (auto& [k, b] : n.spare) kv.emplace_back("_spare_" + k, J::num((double)b));
    std::stable_sort(kv.begin(), kv.end(), [](auto& a, auto& b) { return keyRank(a.first) < keyRank(b.first); });
    J o = J::obj();
    o.o = std::move(kv);
    return o;
  }
  J ptr(const Val& v, Ctx ctx) {
    if (v.k == VK::Null || !v.n) return J();
    if (v.k == VK::Ref) {
      J o = J::obj();
      o.set("$ref", J::num(v.n && ids.count(v.n.get()) ? ids[v.n.get()] : v.ref));
      return o;
    }
    int id = ids.count(v.n.get()) ? ids[v.n.get()] : (ids[v.n.get()] = next++);
    J o = node(*v.n, ctx);
    o.o.insert(o.o.begin() + (ctx == CObj ? 2 : 0), {"_id", J::num(id)});
    return o;
  }
};

void walk(const NodeP& n, const std::function<void(const NodeP&)>& f) {
  f(n);
  for (auto& kv : n->fields) {
    const Val& v = kv.second;
    if ((v.k == VK::Ptr || v.k == VK::Child) && v.n) walk(v.n, f);
  }
}
}

void linkRefs(Level& L) {
  std::map<const Node*, NodeP> sp;
  for (auto& e : L.entries)
    if (e.k == VK::Ptr && e.n) walk(e.n, [&](const NodeP& n) { sp[n.get()] = n; });
  std::function<void(const NodeP&)> fix = [&](const NodeP& n) {
    for (auto& kv : n->fields) {
      Val& v = kv.second;
      if (v.k == VK::Ref && !v.n && v.ref >= 3 && (size_t)(v.ref - 3) < L.byId.size()) {
        auto it = sp.find(L.byId[(size_t)v.ref - 3]);
        if (it != sp.end()) v.n = it->second;
      } else if ((v.k == VK::Ptr || v.k == VK::Child) && v.n) fix(v.n);
    }
  };
  for (auto& e : L.entries) {
    if (e.k == VK::Ref && !e.n && e.ref >= 3 && (size_t)(e.ref - 3) < L.byId.size()) {
      auto it = sp.find(L.byId[(size_t)e.ref - 3]);
      if (it != sp.end()) e.n = it->second;
    } else if (e.k == VK::Ptr && e.n) fix(e.n);
  }
}

J levelToJson(const Level& L, const LevelMeta& m) {
  J root = J::obj();
  root.set("format", J::str("peggleport-level"));
  root.set("version", J::num(L.version));
  root.set("mode", J::num(L.mode));
  J meta = J::obj();
  meta.set("name", J::str(m.name));
  meta.set("title", J::str(m.title));
  meta.set("background", J::str(m.background));
  meta.set("master", J::str(m.master));
  meta.set("board_width", J::num(m.boardWidth));
  meta.set("balls", J::num(m.balls));
  meta.set("orange", J::num(m.orange));
  if (m.physics.any()) {
    J ph = J::obj();
    for (int k = 0; k < PhysicsParams::KeyCount; k++)
      if (m.physics.has(k)) ph.set(physicsKey(k).name, J::num(std::strtod(physicsFormat(m.physics.v.at(k)).c_str(), nullptr)));
    meta.set("physics", ph);
  }
  root.set("meta", meta);
  Writer w;
  for (auto& e : L.entries)
    if (e.k == VK::Ptr && e.n) walk(e.n, [&](const NodeP& n) { if (!w.ids.count(n.get())) w.ids[n.get()] = w.next++; });
  J objs = J::arr();
  for (auto& e : L.entries) objs.a.push_back(w.ptr(e, CObj));
  root.set("objects", objs);
  return root;
}

namespace {
struct Reader {
  std::map<int, NodeP> ids;
  struct Pending { Val* v; int id; };
  std::vector<Pending> pend;
  std::string err;

  bool num(const J& j, Ctx ctx, const std::string& key, Val& v) {
    v.k = VK::Num;
    if (j.t == J::Obj) {
      const J* b = j.get("bits");
      v.p = Prim::F32;
      v.bits = b ? (uint32_t)b->n : 0;
      return true;
    }
    auto& tab = primTable(ctx);
    auto it = tab.find(key);
    Prim p;
    if (it != tab.end()) p = it->second;
    else if (ctx == CStruct) p = Prim::F32;
    else { err = "unknown numeric field '" + key + "'"; return false; }
    v.p = p;
    if (p == Prim::F32) { float f = (float)j.n; std::memcpy(&v.bits, &f, 4); }
    else v.bits = (uint32_t)(int64_t)std::llround(j.n);
    return true;
  }
  NodeP node(const J& j, Ctx ctx) {
    auto n = std::make_shared<Node>();
    if (j.t != J::Obj) { err = "object expected"; return nullptr; }
    for (auto& [k, jv] : j.o) {
      if (k == "kind") continue;
      if (k == "_id") { ids[(int)jv.n] = n; continue; }
      if (k.rfind("_spare_", 0) == 0) { n->spare[k.substr(7)] = (uint32_t)jv.n; continue; }
      if (ctx == CObj && k == "type") { n->type = (int)jv.n; continue; }
      switch (jv.t) {
        case J::Bool: n->flags[k] = jv.b; break;
        case J::Num: { Val v; if (!num(jv, ctx, k, v)) return nullptr; n->fields[k] = v; break; }
        case J::Str: { Val v; v.k = VK::Str; v.s = jv.s; n->fields[k] = v; break; }
        case J::Arr: {
          Val v; v.k = VK::Verts;
          for (auto& p : jv.a) {
            if (p.t != J::Arr || p.a.size() != 2) { err = "vertex pairs expected"; return nullptr; }
            v.verts.push_back((float)p.a[0].n);
            v.verts.push_back((float)p.a[1].n);
          }
          n->fields[k] = v;
          break;
        }
        case J::Obj: {
          if (const J* r = jv.get("$ref")) {
            Val v; v.k = VK::Ref; v.ref = 3;
            Val& slot = n->fields[k];
            slot = v;
            pend.push_back({&slot, (int)r->n});
          } else if (jv.get("bits") && jv.o.size() == 1) {
            Val v; if (!num(jv, ctx, k, v)) return nullptr; n->fields[k] = v;
          } else if (ctx == CObj && k == "peg") {
            Val v; v.k = VK::Child; v.n = node(jv, CPeg); if (!v.n) return nullptr; n->fields[k] = v;
          } else if (k == "mover" || k == "sub") {
            Val v; v.k = VK::Ptr; v.n = node(jv, CMover); if (!v.n) return nullptr; n->fields[k] = v;
          } else if (k == "entry") {
            Val v; v.k = VK::Ptr; v.n = node(jv, CObj); if (!v.n) return nullptr; n->fields[k] = v;
          } else {
            Val v; v.k = VK::Child; v.n = node(jv, CStruct); if (!v.n) return nullptr; n->fields[k] = v;
          }
          break;
        }
        default: break;
      }
    }
    return n;
  }
};
}

bool levelFromJson(const J& j, Level& out, LevelMeta& meta, std::string* err) {
  auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
  if (j.t != J::Obj) return fail("not a JSON object");
  const J* fmt = j.get("format");
  if (!fmt || fmt->s != "peggleport-level") return fail("not a peggleport-level JSON file");
  out = Level();
  if (auto v = j.get("version")) out.version = (int32_t)v->n;
  if (auto v = j.get("mode")) out.mode = (uint8_t)v->n;
  if (auto m = j.get("meta")) {
    auto s = [&](const char* k, std::string& dst) { if (auto v = m->get(k)) dst = v->s; };
    auto n = [&](const char* k, int& dst) { if (auto v = m->get(k)) dst = (int)v->n; };
    s("name", meta.name); s("title", meta.title); s("background", meta.background); s("master", meta.master);
    n("board_width", meta.boardWidth); n("balls", meta.balls); n("orange", meta.orange);
    if (const J* ph = m->get("physics"); ph && ph->t == J::Obj) {
      meta.physics = PhysicsEdit();
      for (const auto& kv : ph->o) {
        int pk = 0;
        if (kv.second.t == J::Num && physicsKeyByName(kv.first, pk) && std::isfinite(kv.second.n)) meta.physics.set(pk, kv.second.n);
      }
    }
  }
  const J* objs = j.get("objects");
  if (!objs || objs->t != J::Arr) return fail("no objects array");
  Reader r;
  out.entries.resize(objs->a.size());
  for (size_t i = 0; i < objs->a.size(); i++) {
    const J& o = objs->a[i];
    Val& e = out.entries[i];
    if (o.t == J::Obj && o.get("$ref")) {
      e.k = VK::Ref;
      e.ref = 3;
      r.pend.push_back({&e, (int)o.get("$ref")->n});
      continue;
    }
    e.k = VK::Ptr;
    e.n = r.node(o, CObj);
    if (!e.n) return fail("object " + std::to_string(i) + ": " + r.err);
    if (!isObjectType(e.n->type) && e.n->type != 1 && e.n->type != 9) return fail("object " + std::to_string(i) + ": unsupported type " + std::to_string(e.n->type));
  }
  for (auto& p : r.pend) {
    auto it = r.ids.find(p.id);
    if (it == r.ids.end()) return fail("dangling $ref " + std::to_string(p.id));
    p.v->n = it->second;
  }
  return true;
}

}
