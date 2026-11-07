#include "level.h"

#include <cstring>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <utility>
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <optional>

namespace pp {

float Val::f() const {
  if (p == Prim::F32) {
    float v;
    std::memcpy(&v, &bits, 4);
    return v;
  }
  return (float)i();
}

int64_t Val::i() const {
  switch (p) {
    case Prim::I32: return (int32_t)bits;
    case Prim::I16: return (int16_t)bits;
    case Prim::I8: return (int8_t)bits;
    case Prim::U32: return bits;
    case Prim::U16: return bits & 0xffff;
    case Prim::U8: return bits & 0xff;
    case Prim::F32: return (int64_t)f();
  }
  return 0;
}

const Val* Node::val(const std::string& k) const {
  auto it = fields.find(k);
  return it == fields.end() ? nullptr : &it->second;
}
float Node::f(const std::string& k, float d) const {
  auto v = val(k);
  return v && v->k == VK::Num ? v->f() : d;
}
int64_t Node::i(const std::string& k, int64_t d) const {
  auto v = val(k);
  return v && v->k == VK::Num ? v->i() : d;
}
bool Node::flag(const std::string& k) const {
  auto it = flags.find(k);
  return it != flags.end() && it->second;
}
std::string Node::str(const std::string& k) const {
  auto v = val(k);
  return v && v->k == VK::Str ? v->s : std::string();
}
void Node::setF(const std::string& k, float x) {
  Val& v = fields[k];
  v.k = VK::Num;
  v.p = Prim::F32;
  std::memcpy(&v.bits, &x, 4);
}

const Node* Level::resolve(const Val& v) const {
  if (v.k == VK::Ptr || v.k == VK::Child) return v.n.get();
  if (v.k == VK::Ref && v.ref >= 3 && (size_t)(v.ref - 3) < byId.size()) return byId[v.ref - 3];
  return nullptr;
}

namespace {

using Have = std::set<std::string>;
enum class T { F32, I32, U32, I16, U16, I8, U8, Str, Obj, Mover, Peg, VF, Verts, Struct };

struct It {
  const char* key;
  T t;
  const char* gate;
  std::vector<It> sub;
};
It G(const char* k, T t) { return {k, t, nullptr, {}}; }
It A(const char* k, T t) { return {k, t, "", {}}; }
It GG(const char* k, T t, const char* g) { return {k, t, g, {}}; }
It S(const char* k, std::vector<It> sub, const char* g = nullptr) { return {k, T::Struct, g, std::move(sub)}; }

int primSize(Prim p) {
  switch (p) {
    case Prim::F32: case Prim::I32: case Prim::U32: return 4;
    case Prim::I16: case Prim::U16: return 2;
    default: return 1;
  }
}

struct IO {
  bool rd = true;
  const uint8_t* in = nullptr;
  size_t n = 0, pos = 0;
  std::vector<uint8_t> out;
  int version = 0;
  std::vector<char> kinds;
  std::vector<Node*> nodes;
  std::map<const Node*, int> wid;

  uint32_t raw(int sz) {
    if (pos + sz > n) throw std::runtime_error("short read at offset " + std::to_string(pos));
    uint32_t v = 0;
    for (int i = 0; i < sz; i++) v |= uint32_t(in[pos + i]) << (8 * i);
    pos += sz;
    return v;
  }
  void put(uint32_t v, int sz) {
    for (int i = 0; i < sz; i++) out.push_back(uint8_t(v >> (8 * i)));
  }
  void num(Prim p, Val& v) {
    if (rd) {
      v.k = VK::Num;
      v.p = p;
      v.bits = raw(primSize(p));
    } else {
      put(v.bits, primSize(p));
    }
  }
  int32_t i32(int32_t w = 0) {
    if (rd) return (int32_t)raw(4);
    put((uint32_t)w, 4);
    return w;
  }
  void str(Val& v) {
    if (rd) {
      uint32_t len = raw(2);
      if (pos + len > n) throw std::runtime_error("short string");
      v.k = VK::Str;
      v.s.assign((const char*)in + pos, len);
      pos += len;
    } else {
      put((uint32_t)v.s.size(), 2);
      out.insert(out.end(), v.s.begin(), v.s.end());
    }
  }
};

void entity(IO& c, Node& d);
void syncMover(IO& c, Node& d);
void value(IO& c, const It& it, Val& v);

Have group(IO& c, Node& d, const char* name, const std::vector<const char*>& spec) {
  Have have;
  int nb = (int)spec.size(), sz = (nb + 7) / 8;
  if (c.rd) {
    uint32_t w = c.raw(sz);
    for (int i = 0; i < nb; i++) {
      bool b = (w >> i) & 1;
      const char* nm = spec[i];
      if (nm[0] == '?') {
        if (b) have.insert(nm + 1);
      } else {
        d.flags[nm] = b;
      }
    }
    uint32_t sp = w >> nb;
    if (sp) d.spare[name] = sp;
  } else {
    uint32_t w = 0;
    for (int i = 0; i < nb; i++) {
      const char* nm = spec[i];
      bool b;
      if (nm[0] == '?') {
        b = d.has(nm + 1);
        if (b) have.insert(nm + 1);
      } else {
        b = d.flag(nm);
      }
      if (b) w |= 1u << i;
    }
    auto it = d.spare.find(name);
    if (it != d.spare.end()) w |= it->second << nb;
    c.put(w, sz);
  }
  return have;
}

void items(IO& c, Node& d, const Have& have, const std::vector<It>& spec) {
  for (auto& it : spec) {
    const char* g = it.gate == nullptr ? it.key : (it.gate[0] ? it.gate : nullptr);
    if (g && !have.count(g)) continue;
    if (!c.rd && !d.has(it.key)) throw std::runtime_error(std::string("missing field ") + it.key);
    value(c, it, d.fields[it.key]);
  }
}

void inlineNode(IO& c, Node& d, char kind) {
  if (kind == 'o') {
    d.type = c.i32(d.type);
    switch (d.type) {
      case 1: case 2: case 3: case 5: case 6: case 8: case 9: break;
      default: throw std::runtime_error("unsupported entry type " + std::to_string(d.type));
    }
  }
  c.kinds.push_back(kind);
  if (c.rd) {
    d.id = (int)c.kinds.size() + 2;
    c.nodes.push_back(&d);
  } else {
    c.wid[&d] = (int)c.kinds.size() + 2;
  }
  if (kind == 'o') entity(c, d);
  else syncMover(c, d);
}

void ptr(IO& c, Val& v, char kind) {
  auto checkRef = [&](int tag) {
    if (tag < 3 || tag >= (int)c.kinds.size() + 3) throw std::runtime_error("unknown back-reference " + std::to_string(tag));
    if (c.kinds[tag - 3] != kind) throw std::runtime_error("back-reference kind mismatch");
  };
  if (c.rd) {
    int tag = c.i32();
    if (tag == 0) {
      v.k = VK::Null;
    } else if (tag == 1) {
      v.k = VK::Ptr;
      v.n = std::make_shared<Node>();
      inlineNode(c, *v.n, kind);
    } else {
      checkRef(tag);
      v.k = VK::Ref;
      v.ref = tag;
    }
    return;
  }
  if ((v.k == VK::Ref || v.k == VK::Ptr) && v.n) {
    auto it = c.wid.find(v.n.get());
    if (it != c.wid.end()) {
      c.i32(it->second);
    } else {
      c.i32(1);
      inlineNode(c, *v.n, kind);
    }
    return;
  }
  if (v.k == VK::Null) {
    c.i32(0);
  } else if (v.k == VK::Ref) {
    c.i32(v.ref);
    checkRef(v.ref);
  } else {
    c.i32(1);
    inlineNode(c, *v.n, kind);
  }
}

const std::vector<const char*> PEG_GROUP = {"unk_flag_0", "variable", "?u_0x18", "crumble", "?u_0x20", "?u_0x24", "unk_flag_6", "?u_0x1c"};
const std::vector<It> PEG_ITEMS = {G("u_0x18", T::I32), G("u_0x20", T::I32), G("u_0x24", T::U8), G("u_0x1c", T::U8)};

void child(IO& c, Val& v) {
  if (c.rd) {
    v.k = VK::Child;
    v.n = std::make_shared<Node>();
  }
}

void value(IO& c, const It& it, Val& v) {
  switch (it.t) {
    case T::F32: c.num(Prim::F32, v); return;
    case T::I32: c.num(Prim::I32, v); return;
    case T::U32: c.num(Prim::U32, v); return;
    case T::I16: c.num(Prim::I16, v); return;
    case T::U16: c.num(Prim::U16, v); return;
    case T::I8: c.num(Prim::I8, v); return;
    case T::U8: c.num(Prim::U8, v); return;
    case T::Str: c.str(v); return;
    case T::Obj: ptr(c, v, 'o'); return;
    case T::Mover: ptr(c, v, 'm'); return;
    case T::Peg: {
      child(c, v);
      Node& d = *v.n;
      c.num(Prim::U8, d.fields["type"]);
      Have h = group(c, d, "peg", PEG_GROUP);
      items(c, d, h, PEG_ITEMS);
      return;
    }
    case T::VF: {
      child(c, v);
      Node& d = *v.n;
      Val& tag = d.fields["tag"];
      c.num(Prim::U8, tag);
      if (tag.bits & 0xff) c.num(Prim::F32, d.fields["value"]);
      else c.str(d.fields["expr"]);
      return;
    }
    case T::Verts: {
      if (c.rd) {
        int32_t n = c.i32();
        if (n < 0 || n > 100000) throw std::runtime_error("bad vertex count");
        v.k = VK::Verts;
        v.verts.resize((size_t)n * 2);
        for (auto& x : v.verts) {
          uint32_t b = c.raw(4);
          std::memcpy(&x, &b, 4);
        }
      } else {
        c.i32((int32_t)(v.verts.size() / 2));
        for (float x : v.verts) {
          uint32_t b;
          std::memcpy(&b, &x, 4);
          c.put(b, 4);
        }
      }
      return;
    }
    case T::Struct: {
      child(c, v);
      items(c, *v.n, {}, it.sub);
      return;
    }
  }
}

const std::vector<const char*> MOVER_A = {"?offset", "?radius", "?phase", "?move_rotation", "?radius2", "?pause1", "?pause2", "?phase1"};
const std::vector<const char*> MOVER_B = {"?phase2", "?post_delay_phase", "?max_angle", "?u_0x64", "?sub_x", "?obj_x", "?rotation"};
const std::vector<It> MOVER_HEAD = {A("move_type", T::I8), A("anchor_x", T::F32), A("anchor_y", T::F32), A("time", T::U16)};
const std::vector<It> MOVER_ITEMS = {
    G("offset", T::I16), G("radius", T::I16), G("phase", T::F32), G("move_rotation", T::F32),
    G("radius2", T::I16), G("pause1", T::U16), G("pause2", T::U16), G("phase1", T::I8),
    G("phase2", T::I8), G("post_delay_phase", T::F32), G("max_angle", T::F32), G("u_0x64", T::F32),
    G("rotation", T::F32), G("sub_x", T::F32), GG("sub_y", T::F32, "sub_x"), GG("sub", T::Mover, "sub_x"),
    G("obj_x", T::F32), GG("obj_y", T::F32, "obj_x")};

void syncMover(IO& c, Node& d) {
  items(c, d, {}, MOVER_HEAD);
  Have h = group(c, d, "mover_a", MOVER_A);
  Have h2 = group(c, d, "mover_b", MOVER_B);
  h.insert(h2.begin(), h2.end());
  items(c, d, h, MOVER_ITEMS);
}

const std::vector<const char*> PHYS_G0 = {"?rolly", "?bouncy", "?peg", "?mover", "?u_0x4c", "collidable", "visible", "can_move"};
const std::vector<const char*> PHYS_G1 = {"?solid_color", "?outline_color", "?image", "?image_dx", "?image_dy", "?image_rot", "background", "base_obj"};
const std::vector<const char*> PHYS_G2 = {"?u_0xc8", "?id", "?u_0x50", "?sound", "ball_stop_reset", "?logic", "foreground", "?max_bounce_vel"};
const std::vector<const char*> PHYS_G3 = {"draw_sort", "foreground2", "?sub_id", "?flipper_flags", "draw_float", "unk_flag_29"};
const std::vector<const char*> PHYS_G3N = {"draw_sort", "foreground2", "?sub_id", "?flipper_flags", "draw_float", "unk_flag_29", "draw_shadow"};
const std::vector<It> PHYS_ITEMS = {
    G("rolly", T::F32), G("bouncy", T::F32), G("u_0x4c", T::I32), G("solid_color", T::U32),
    G("outline_color", T::U32), G("image", T::Str), G("image_dx", T::F32), G("image_dy", T::F32),
    G("image_rot", T::F32), G("u_0xc8", T::I32), G("id", T::Str), G("u_0x50", T::I32),
    G("sound", T::U8), G("logic", T::Str), G("max_bounce_vel", T::F32), G("sub_id", T::I32),
    G("flipper_flags", T::U8), G("peg", T::Peg), G("mover", T::Mover)};

void physobj(IO& c, Node& d) {
  Have h = group(c, d, "physobj_0", PHYS_G0);
  Have h1 = group(c, d, "physobj_1", PHYS_G1);
  Have h2 = group(c, d, "physobj_2", PHYS_G2);
  h.insert(h1.begin(), h1.end());
  h.insert(h2.begin(), h2.end());
  if (c.version > 14) {
    Have h3 = group(c, d, "physobj_3", c.version > 0x4F ? PHYS_G3N : PHYS_G3);
    h.insert(h3.begin(), h3.end());
  }
  items(c, d, h, PHYS_ITEMS);
}

const std::vector<const char*> CIRCLE_A = {"static", "?x", "unk_flag_2", "unk_flag_3", "unk_flag_4", "unk_flag_5", "unk_flag_6", "unk_flag_7"};
const std::vector<const char*> CIRCLE_B = {"unk_flag_8"};
const std::vector<It> CIRCLE_ITEMS = {G("x", T::F32), GG("y", T::F32, "x"), A("radius", T::F32)};

const std::vector<const char*> LINE_G = {"?u_0xfc", "?u_0x100", "unk_flag_2"};
const std::vector<It> LINE_ITEMS = {A("x1", T::F32), A("y1", T::F32), A("x2", T::F32), A("y2", T::F32), G("u_0xfc", T::F32), G("u_0x100", T::F32)};

const std::vector<const char*> POLY_A = {"unk_flag_0", "?poly_normal_dir", "?poly_rotation", "?u_0x120", "?x", "?poly_scale", "image_tile", "image_mirror"};
const std::vector<const char*> POLY_B = {"?u_0x138", "?poly_grow_type", "?u_0x140", "unk_flag_b3"};
const std::vector<It> POLY_ITEMS_A = {G("poly_rotation", T::F32), G("u_0x120", T::F32), G("poly_scale", T::F32), G("poly_normal_dir", T::I8), G("x", T::F32), GG("y", T::F32, "x")};
const std::vector<It> POLY_ITEMS_B = {G("u_0x138", T::U8), G("poly_grow_type", T::I32), G("u_0x140", T::U16)};

const std::vector<const char*> BRICK_A = {"anchor_left", "anchor_right", "?brick_type", "?brick_subdiv", "?brick_angle", "?brick_left_angle", "?brick_right_angle", "?brick_thickness"};
const std::vector<const char*> BRICK_B = {"?u_0x160", "?u_0x164", "tex_flip"};
const std::vector<It> BRICK_ITEMS = {
    G("u_0x160", T::F32), G("u_0x164", T::F32), G("brick_type", T::U8), G("brick_subdiv", T::U8),
    G("brick_left_angle", T::F32), GG("brick_right_angle_first", T::F32, "brick_right_angle"), G("brick_right_angle", T::F32),
    G("brick_angle", T::F32), G("brick_thickness", T::F32), A("brick_radius", T::F32), A("brick_rotation", T::F32),
    A("u_0x170", T::U8), A("u_0x174", T::U8), A("u_0x178", T::U8), A("u_0x17c", T::U8)};

const std::vector<const char*> HOLE_G = {"circular", "?u_0xec", "?x", "?score", "?entry", "?out_delay", "?hx"};
const std::vector<It> HOLE_ITEMS = {
    A("width", T::I32), A("height", T::I32), G("u_0xec", T::U16), G("score", T::I32), G("out_delay", T::I32),
    G("entry", T::Obj), G("x", T::F32), GG("y", T::F32, "x"), G("hx", T::F32), GG("hy", T::F32, "hx")};

const std::vector<const char*> EMITTER_G = {"circular", "attached", "additive", "orient_rot_to_vel", "random_start", "?x", "?ua", "?scale",
                                            "?color", "?alpha", "?velocity", "?polar", "?rotation", "?pos", "unk_flag_14"};
const std::vector<It> EMITTER_ITEMS = {
    A("emitter_image", T::Str), A("width", T::I32), A("height", T::I32),
    GG("fast_forward", T::I32, "main2"), GG("hardware_rate", T::F32, "main2"), GG("trigger_obj", T::Str, "main2"), GG("unk_byte_0x942", T::U8, "main2"),
    S("pos", {A("x", T::VF), A("y", T::VF)}),
    G("x", T::F32), GG("y", T::F32, "x"),
    A("emitter_image_2", T::Str), A("rand_rate", T::F32), A("software_rate", T::F32), A("spiral_count", T::F32), A("max", T::I32),
    A("alpha_fade_at", T::F32), A("alpha_into", T::F32), A("duration", T::F32), A("rate", T::VF), A("distrib", T::VF),
    S("rotation", {A("value", T::VF), A("velocity", T::VF), A("rand", T::F32)}),
    S("scale", {A("value", T::VF), A("velocity", T::VF), A("rand", T::F32)}),
    S("color", {A("r", T::VF), A("g", T::VF), A("b", T::VF)}),
    G("alpha", T::VF),
    S("velocity", {A("x", T::VF), A("y", T::VF), A("x_rand", T::F32), A("y_rand", T::F32), A("acc_x", T::F32), A("gravity", T::F32)}),
    S("polar", {A("vel", T::F32), A("vel_rand", T::F32), A("acc", T::F32), A("rot_vel", T::F32), A("rot_vel_rand", T::F32)}),
    G("ua", T::F32), GG("ub", T::F32, "ua")};

void polyBody(IO& c, Node& d, bool verts) {
  physobj(c, d);
  Have h = group(c, d, "poly_a", POLY_A);
  if (c.version > 0x22) {
    Have hb = group(c, d, "poly_b", POLY_B);
    h.insert(hb.begin(), hb.end());
  }
  items(c, d, h, POLY_ITEMS_A);
  if (verts) value(c, A("vertices", T::Verts), d.fields["vertices"]);
  items(c, d, h, POLY_ITEMS_B);
}

void entity(IO& c, Node& d) {
  switch (d.type) {
    case 1: physobj(c, d); return;
    case 2: {
      physobj(c, d);
      Have h = group(c, d, "line", LINE_G);
      items(c, d, h, LINE_ITEMS);
      return;
    }
    case 3: polyBody(c, d, true); return;
    case 5: {
      physobj(c, d);
      Have h = group(c, d, "circle_0", CIRCLE_A);
      if (c.version > 0x51) group(c, d, "circle_1", CIRCLE_B);
      items(c, d, h, CIRCLE_ITEMS);
      return;
    }
    case 6: {
      polyBody(c, d, false);
      Have h = group(c, d, "brick_a", BRICK_A);
      Have h2 = group(c, d, "brick_b", BRICK_B);
      h.insert(h2.begin(), h2.end());
      items(c, d, h, BRICK_ITEMS);
      return;
    }
    case 8: {
      physobj(c, d);
      Have h = group(c, d, "hole", HOLE_G);
      items(c, d, h, HOLE_ITEMS);
      return;
    }
    case 9: {
      physobj(c, d);
      if (!c.rd && !d.has("main")) {
        Val v;
        v.k = VK::Num;
        v.p = Prim::I32;
        v.bits = 2;
        d.fields["main"] = v;
      }
      c.num(Prim::I32, d.fields["main"]);
      Have h = group(c, d, "emitter", EMITTER_G);
      if (d.fields["main"].i() >= 2) h.insert("main2");
      else h.erase("pos");
      items(c, d, h, EMITTER_ITEMS);
      return;
    }
  }
}

}

std::optional<Level> parseLevel(const std::vector<uint8_t>& data, std::string* err, int maxVersion) {
  try {
    IO c;
    c.rd = true;
    c.in = data.data();
    c.n = data.size();
    Level lv;
    lv.version = c.i32();
    if (lv.version > maxVersion) throw std::runtime_error("version " + std::to_string(lv.version) + " is newer than " + std::to_string(maxVersion));
    c.version = lv.version;
    lv.mode = (uint8_t)c.raw(1);
    int32_t count = c.i32();
    if (count < 0) throw std::runtime_error("negative entry count");
    lv.entries.resize((size_t)count);
    for (auto& e : lv.entries) ptr(c, e, 'o');
    lv.trailing.assign(data.begin() + (long)c.pos, data.end());
    lv.byId = c.nodes;
    return lv;
  } catch (const std::exception& e) {
    if (err) *err = e.what();
    return std::nullopt;
  }
}

std::vector<uint8_t> writeLevel(const Level& lv) {
  IO c;
  c.rd = false;
  c.version = lv.version;
  c.i32(lv.version);
  c.put(lv.mode, 1);
  c.i32((int32_t)lv.entries.size());
  for (auto e : lv.entries) ptr(c, e, 'o');
  c.out.insert(c.out.end(), lv.trailing.begin(), lv.trailing.end());
  return c.out;
}

}
