#include "editor/editor.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <cstdint>
#include <utility>
#include <memory>
#include <vector>
#include <string>
#include <set>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <cctype>

namespace fs = std::filesystem;

namespace pp::ed {

static const struct { const char* name; Tool t; } kTools[] = {
    {"select", Tool::Select}, {"pan", Tool::Pan}, {"blue", Tool::PegBlue}, {"orange", Tool::PegOrange}, {"green", Tool::PegGreen},
    {"purple", Tool::PegPurple}, {"round", Tool::PegRound}, {"bumper", Tool::Bumper}, {"brick_straight", Tool::BrickStraight},
    {"brick_curved", Tool::BrickCurved}, {"poly", Tool::Poly}, {"line", Tool::Line}, {"hole", Tool::Hole}, {"hole_exit", Tool::HoleExit}};
const char* toolName(Tool t) {
  for (auto& e : kTools)
    if (e.t == t) return e.name;
  return "select";
}
bool toolFromName(const std::string& s, Tool& t) {
  for (auto& e : kTools)
    if (s == e.name) { t = e.t; return true; }
  return false;
}

Editor::Editor() {
  brick.type = 0; brick.sub = 4; brick.radius = 80; brick.ext = 30; brick.thick = 20; brick.rot = -90;
  brickStraight.type = 5; brickStraight.sub = 2; brickStraight.radius = 60; brickStraight.thick = 20; brickStraight.rot = 90;
  newLevel("NewLevel");
}

static bool slurp(const fs::path& p, std::vector<uint8_t>& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  out.assign(std::istreambuf_iterator<char>(f), {});
  return true;
}
static bool spit(const fs::path& p, const void* data, size_t n) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  std::ofstream f(p, std::ios::binary);
  if (!f) return false;
  f.write((const char*)data, (std::streamsize)n);
  return (bool)f;
}
static bool spit(const fs::path& p, const std::string& s) { return spit(p, s.data(), s.size()); }

void Editor::resetHistory() {
  undo_.clear();
  redo_.clear();
  open_ = false;
  clean_ = 0;
  metaDirty = false;
  sel.clear();
  polyPts_.clear();
  g_ = G();
  issues.clear();
  changed();
}

void Editor::newLevel(const std::string& name, int boardWidth) {
  level = std::make_shared<Level>();
  level->version = 0x42;
  level->mode = 1;
  meta = LevelMeta();
  meta.name = name;
  meta.title = name;
  meta.boardWidth = std::max(650, boardWidth);
  savedPath.clear();
  resetHistory();
  status = "new level " + name;
}

std::shared_ptr<Level> Editor::syntheticBase() {
  auto L = std::make_shared<Level>();
  L->version = 0x42;
  L->mode = 1;
  auto wall = [&](float x, float y, float hw, float hh) {
    NodeP n = newPoly(x, y, {{-hw, -hh}, {-hw, hh}, {hw, hh}, {hw, -hh}});
    putStr(*n, "id", "wall");
    putNum(*n, "bouncy", Prim::F32, 0.75);
    Val v; v.k = VK::Ptr; v.n = n;
    L->entries.push_back(v);
  };
  wall(-20, 240, 19.5f, 999.5f);
  wall(670, 240, 19.5f, 999.5f);
  wall(325, -20, 999.5f, 19.5f);
  NodeP h = newHole(326, 556, 130, 20, false, 0);
  putStr(*h, "id", "freeball");
  attachMover(*L, *h, 2);
  Node* m = moverOf(*L, *h);
  m->setF("anchor_x", 326);
  m->setF("anchor_y", 556);
  putNum(*m, "radius", Prim::I16, 260);
  putNum(*m, "time", Prim::U16, 600);
  putNum(*m, "phase", Prim::F32, 0.25);
  Val v; v.k = VK::Ptr; v.n = h;
  L->entries.push_back(v);
  auto bytes = writeLevel(*L);
  auto back = parseLevel(bytes);
  return std::make_shared<Level>(std::move(*back));
}

std::shared_ptr<Level> Editor::bake() const {
  try {
    auto bytes = writeLevel(*level);
    auto lv = parseLevel(bytes);
    if (!lv) return nullptr;
    return std::make_shared<Level>(std::move(*lv));
  } catch (const std::exception&) {
    return nullptr;
  }
}

void Editor::finishLoad() {
  linkRefs(*level);
  resetHistory();
}

bool Editor::loadFromVfs(const Vfs& vfs, const std::string& nameOrPath, std::string* err) {
  LevelBundle lb;
  std::string e;
  if (!loadLevelBundle(vfs, nameOrPath, lb, &e)) {
    if (err) *err = e;
    return false;
  }
  std::shared_ptr<Level> copy;
  try {
    auto bytes = writeLevel(*lb.level);
    auto lv = parseLevel(bytes, &e);
    if (!lv) { if (err) *err = e; return false; }
    copy = std::make_shared<Level>(std::move(*lv));
  } catch (const std::exception& ex) {
    if (err) *err = ex.what();
    return false;
  }
  level = copy;
  base = lb.base;
  meta = LevelMeta();
  meta.name = lb.name;
  meta.title = lb.title;
  meta.background = lb.background;
  meta.boardWidth = lb.cfg.boardWidth;
  meta.balls = lb.cfg.balls;
  meta.orange = lb.cfg.orange;
  savedPath.clear();
  finishLoad();
  status = "opened " + lb.name + " (" + std::to_string(objects().size()) + " objects)";
  return true;
}

bool Editor::loadDatFile(const fs::path& p, std::string* err) {
  std::vector<uint8_t> bytes;
  if (!slurp(p, bytes)) { if (err) *err = "cannot read " + p.string(); return false; }
  std::string e;
  auto lv = parseLevel(bytes, &e);
  if (!lv) { if (err) *err = e; return false; }
  level = std::make_shared<Level>(std::move(*lv));
  meta.name = p.stem().string();
  meta.title = meta.name;
  savedPath = p.string();
  finishLoad();
  status = "opened " + p.string();
  return true;
}

bool Editor::saveDat(const fs::path& p, std::string* err) {
  try {
    auto bytes = writeLevel(*level);
    if (!spit(p, bytes.data(), bytes.size())) { if (err) *err = "cannot write " + p.string(); return false; }
  } catch (const std::exception& ex) {
    if (err) *err = ex.what();
    return false;
  }
  savedPath = p.string();
  clean_ = (int)undo_.size();
  metaDirty = false;
  status = "saved " + p.string();
  return true;
}

bool Editor::saveJson(const fs::path& p, std::string* err) {
  std::string text = levelToJson(*level, meta).dump(2) + "\n";
  if (!spit(p, text)) { if (err) *err = "cannot write " + p.string(); return false; }
  status = "exported " + p.string();
  return true;
}

bool Editor::loadJson(const fs::path& p, std::string* err) {
  std::vector<uint8_t> bytes;
  if (!slurp(p, bytes)) { if (err) *err = "cannot read " + p.string(); return false; }
  J j;
  std::string e;
  if (!parseJson(std::string(bytes.begin(), bytes.end()), j, &e)) { if (err) *err = p.string() + ": " + e; return false; }
  Level lv;
  LevelMeta m;
  if (!levelFromJson(j, lv, m, &e)) { if (err) *err = e; return false; }
  level = std::make_shared<Level>(std::move(lv));
  meta = m;
  savedPath.clear();
  resetHistory();
  status = "imported " + p.string();
  return true;
}

bool Editor::saveToMod(const fs::path& modDir, std::string* err) {
  static const std::string okChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
  if (meta.name.empty() || meta.name.find_first_not_of(okChars) != std::string::npos) {
    if (err) *err = "level names may only use letters, digits, '_' and '-': '" + meta.name + "'";
    return false;
  }
  fs::path lv = modDir / "levels";
  std::string e;
  if (!saveDat(lv / (meta.name + ".dat"), &e)) { if (err) *err = e; return false; }
  std::ostringstream ini;
  ini << "# written by the PegglePort level editor\n";
  ini << "title = " << meta.title << "\n";
  if (meta.boardWidth > 650) ini << "board_width = " << meta.boardWidth << "\n";
  if (meta.balls > 0) ini << "balls = " << meta.balls << "\n";
  if (meta.orange != 25) ini << "orange = " << meta.orange << "\n";
  if (!meta.background.empty() && meta.background != meta.name) ini << "background = " << meta.background << "\n";
  if (!meta.master.empty()) ini << "master = " << meta.master << "\n";
  spit(lv / (meta.name + ".ini"), ini.str());
  std::vector<std::pair<std::string, std::string>> levels;
  {
    std::vector<uint8_t> bytes;
    if (slurp(lv / "mod_levels.cfg", bytes)) {
      std::istringstream in(std::string(bytes.begin(), bytes.end()));
      std::string line;
      while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t k = line.find_first_not_of(" \t");
        if (k == std::string::npos || line.compare(k, 6, "Level:") != 0) continue;
        std::string rest = line.substr(k + 6);
        rest.erase(0, rest.find_first_not_of(" \t"));
        std::string nm = rest.substr(0, rest.find(','));
        nm.erase(nm.find_last_not_of(" \t") + 1);
        levels.push_back({nm, line.substr(k)});
      }
    }
  }
  std::string entry = "Level: " + meta.name + ", \"" + meta.title + "\", 100000, 3";
  bool found = false;
  for (auto& l : levels)
    if (l.first == meta.name) { l.second = entry; found = true; }
  if (!found) levels.push_back({meta.name, entry});
  std::string out = "// levels of this mod, written by the PegglePort level editor (Quick Play shows each Stage as a page of 5)\n";
  for (size_t i = 0; i < levels.size(); i += 5) {
    std::string stageName = modDir.filename().string() + (i ? " " + std::to_string(i / 5 + 1) : "");
    out += "Stage\n{\nName: \"" + stageName + "\"\nDesc: \"Levels made with the PegglePort level editor\"\n";
    for (size_t j = i; j < std::min(levels.size(), i + 5); j++) out += "\t" + levels[j].second + "\n";
    out += "}\n";
  }
  spit(lv / "mod_levels.cfg", out);
  if (!fs::exists(modDir / "mod.ini")) {
    std::string id = modDir.filename().string();
    for (auto& c : id) c = (char)std::tolower((unsigned char)c);
    spit(modDir / "mod.ini", "id = " + id + "\nname = " + modDir.filename().string() + "\nversion = 1\ndescription = Levels made with the PegglePort level editor\npriority = 10\n");
  }
  status = "saved " + (lv / (meta.name + ".dat")).string();
  return true;
}

void Editor::begin(const std::string& label, const std::vector<NodeP>& touch) {
  if (open_) abort();
  cur_ = Step();
  cur_.label = label;
  cur_.eb = level->entries;
  cur_.touched = touch;
  for (auto& n : touch)
    if (auto v = n->val("mover"); v && v->n && std::find(cur_.touched.begin(), cur_.touched.end(), v->n) == cur_.touched.end()) cur_.touched.push_back(v->n);
  for (auto& n : cur_.touched) cur_.sb.push_back(snapNode(n));
  open_ = true;
}
void Editor::commit() {
  if (!open_) return;
  cur_.ea = level->entries;
  for (auto& n : cur_.touched) cur_.sa.push_back(snapNode(n));
  undo_.push_back(std::move(cur_));
  if (clean_ > (int)undo_.size() - 1 && !redo_.empty()) clean_ = -1;
  redo_.clear();
  cur_ = Step();
  open_ = false;
  changed();
}
void Editor::abort() {
  if (!open_) return;
  level->entries = cur_.eb;
  for (auto& s : cur_.sb) restoreNode(s);
  cur_ = Step();
  open_ = false;
  prune();
  changed();
}
void Editor::prune() {
  std::set<const Node*> alive;
  for (auto& e : level->entries)
    if (e.n) alive.insert(e.n.get());
  sel.erase(std::remove_if(sel.begin(), sel.end(), [&](const NodeP& n) { return !alive.count(n.get()); }), sel.end());
}
bool Editor::undo() {
  if (open_) abort();
  if (undo_.empty()) return false;
  Step s = std::move(undo_.back());
  undo_.pop_back();
  level->entries = s.eb;
  for (auto it = s.sb.rbegin(); it != s.sb.rend(); ++it) restoreNode(*it);
  status = "undo: " + s.label;
  redo_.push_back(std::move(s));
  prune();
  changed();
  return true;
}
bool Editor::redo() {
  if (open_) abort();
  if (redo_.empty()) return false;
  Step s = std::move(redo_.back());
  redo_.pop_back();
  level->entries = s.ea;
  for (auto& x : s.sa) restoreNode(x);
  status = "redo: " + s.label;
  undo_.push_back(std::move(s));
  prune();
  changed();
  return true;
}

std::vector<NodeP> Editor::objects() const {
  std::vector<NodeP> v;
  for (auto& e : level->entries)
    if (e.k == VK::Ptr && e.n && isObjectType(e.n->type)) v.push_back(e.n);
  return v;
}
bool Editor::isSelected(const NodeP& n) const { return std::find(sel.begin(), sel.end(), n) != sel.end(); }
NodeP Editor::hitObject(V2 p) const {
  NodeP best;
  float bestArea = 1e30f;
  float tol = 3.0f / std::max(0.05f, zoom_);
  for (auto& n : objects()) {
    Geo g = geometry(*level, *n);
    if (!hitTest(g, p, tol)) continue;
    float area = std::max(1.0f, (g.x1 - g.x0) * (g.y1 - g.y0));
    if (g.kind == Geo::Line) area = 1;
    if (area <= bestArea) { bestArea = area; best = n; }
  }
  return best;
}
std::vector<NodeP> Editor::inBox(float x0, float y0, float x1, float y1) const {
  if (x0 > x1) std::swap(x0, x1);
  if (y0 > y1) std::swap(y0, y1);
  std::vector<NodeP> v;
  for (auto& n : objects())
    if (hitBox(geometry(*level, *n), x0, y0, x1, y1)) v.push_back(n);
  return v;
}
bool Editor::selBounds(float& x0, float& y0, float& x1, float& y1) const {
  bool any = false;
  for (auto& n : sel) {
    Geo g = geometry(*level, *n);
    if (g.kind == Geo::None) continue;
    if (!any) { x0 = g.x0; y0 = g.y0; x1 = g.x1; y1 = g.y1; any = true; }
    else { x0 = std::min(x0, g.x0); y0 = std::min(y0, g.y0); x1 = std::max(x1, g.x1); y1 = std::max(y1, g.y1); }
  }
  return any;
}
int Editor::pegCount(int type) const {
  int c = 0;
  for (auto& n : objects()) {
    int t = pegTypeOf(*n);
    if (t > 0 && (type < 0 || t == type)) c++;
  }
  return c;
}

void Editor::selectAll() { sel = objects(); }

NodeP Editor::makeFor(Tool t, V2 p) const {
  switch (t) {
    case Tool::PegBlue: return newCircle(p.x, p.y, 10, 1);
    case Tool::PegOrange: return newCircle(p.x, p.y, 10, 2);
    case Tool::PegGreen: return newCircle(p.x, p.y, 10, 4, false);
    case Tool::PegPurple: return newCircle(p.x, p.y, 10, 3, false);
    case Tool::PegRound: return newCircle(p.x, p.y, roundRadius, 1);
    case Tool::Bumper: return newCircle(p.x, p.y, roundRadius, 0);
    case Tool::BrickStraight: return newBrick(p.x, p.y, brickStraight, 1);
    case Tool::BrickCurved: { BrickParams b = brick; if (b.type == 5) b.type = 0; return newBrick(p.x, p.y, b, 1); }
    case Tool::Hole: return newHole(p.x, p.y, 40, 40, true, 0);
    default: return nullptr;
  }
}
void Editor::addNodes(const std::vector<NodeP>& v, const std::string& label) {
  if (v.empty()) return;
  begin(label, {});
  for (auto& n : v) {
    Val e;
    e.k = VK::Ptr;
    e.n = n;
    level->entries.push_back(e);
  }
  commit();
  sel = v;
}
NodeP Editor::place(Tool t, V2 p) {
  NodeP n = makeFor(t, snapPt(p));
  if (!n) return nullptr;
  addNodes({n}, std::string("Place ") + toolName(t));
  return n;
}
void Editor::deleteSelection() {
  if (sel.empty()) return;
  begin("Delete", {});
  std::set<const Node*> del;
  for (auto& n : sel) del.insert(n.get());
  auto& es = level->entries;
  es.erase(std::remove_if(es.begin(), es.end(), [&](const Val& e) { return e.n && del.count(e.n.get()); }), es.end());
  commit();
  sel.clear();
}
void Editor::moveSelection(float dx, float dy) {
  if (sel.empty()) return;
  begin("Move", sel);
  std::set<const Node*> done;
  for (auto& n : sel) translateNode(*n, dx, dy, &done);
  commit();
}
void Editor::rotateSelection(float degrees) {
  if (sel.empty()) return;
  float x0, y0, x1, y1;
  selBounds(x0, y0, x1, y1);
  V2 pivot((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
  begin("Rotate", sel);
  std::set<const Node*> done;
  for (auto& n : sel) rotateNode(*level, *n, pivot, deg2rad(degrees), &done);
  commit();
}
void Editor::mirrorSelection(bool horizontal) {
  std::vector<NodeP> v = sel.empty() ? objects() : sel;
  if (v.empty()) return;
  float axis;
  if (sel.empty()) axis = horizontal ? kBoardW * 0.5f : kBoardH * 0.5f;
  else {
    float x0, y0, x1, y1;
    selBounds(x0, y0, x1, y1);
    axis = horizontal ? (x0 + x1) * 0.5f : (y0 + y1) * 0.5f;
  }
  begin(horizontal ? "Mirror horizontally" : "Mirror vertically", v);
  std::set<const Node*> done;
  for (auto& n : v) mirrorNode(*level, *n, horizontal, axis, &done);
  commit();
}
static std::vector<NodeP> cloneAll(const std::vector<NodeP>& src) {
  std::map<const Node*, NodeP> remap;
  std::vector<NodeP> out;
  for (auto& n : src) out.push_back(cloneNode(n, &remap));
  return out;
}
void Editor::duplicateSelection(float dx, float dy) {
  if (sel.empty()) return;
  auto c = cloneAll(sel);
  std::set<const Node*> done;
  for (auto& n : c) translateNode(*n, dx, dy, &done);
  addNodes(c, "Duplicate");
}
void Editor::copySelection() {
  if (sel.empty()) return;
  clip_ = cloneAll(sel);
  float x0, y0, x1, y1;
  selBounds(x0, y0, x1, y1);
  clipOrigin_ = V2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
  status = "copied " + std::to_string(clip_.size()) + " object(s)";
}
void Editor::cutSelection() {
  if (sel.empty()) return;
  copySelection();
  deleteSelection();
}
void Editor::paste(V2 at, bool useAt) {
  if (clip_.empty()) { status = "nothing to paste"; return; }
  auto c = cloneAll(clip_);
  V2 d = useAt ? snapPt(at) - clipOrigin_ : V2(grid > 1 ? grid : 10, grid > 1 ? grid : 10);
  if (useAt && snap) d = snapPt(clipOrigin_ + d) - clipOrigin_;
  std::set<const Node*> done;
  for (auto& n : c) translateNode(*n, d.x, d.y, &done);
  addNodes(c, "Paste");
  if (!useAt) clipOrigin_ = clipOrigin_ + d;
}
void Editor::align(const std::string& mode) {
  if (sel.size() < 2) return;
  std::vector<Geo> g;
  for (auto& n : sel) g.push_back(geometry(*level, *n));
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (auto& q : g) { x0 = std::min(x0, q.x0); y0 = std::min(y0, q.y0); x1 = std::max(x1, q.x1); y1 = std::max(y1, q.y1); }
  begin("Align " + mode, sel);
  std::set<const Node*> done;
  for (size_t i = 0; i < sel.size(); i++) {
    float dx = 0, dy = 0;
    if (mode == "left") dx = x0 - g[i].x0;
    else if (mode == "right") dx = x1 - g[i].x1;
    else if (mode == "top") dy = y0 - g[i].y0;
    else if (mode == "bottom") dy = y1 - g[i].y1;
    else if (mode == "hcenter") dx = (x0 + x1) * 0.5f - (g[i].x0 + g[i].x1) * 0.5f;
    else if (mode == "vcenter") dy = (y0 + y1) * 0.5f - (g[i].y0 + g[i].y1) * 0.5f;
    translateNode(*sel[i], dx, dy, &done);
  }
  commit();
}
void Editor::distribute(bool horizontal) {
  if (sel.size() < 3) return;
  struct It { NodeP n; float c; };
  std::vector<It> v;
  for (auto& n : sel) {
    Geo g = geometry(*level, *n);
    v.push_back({n, horizontal ? (g.x0 + g.x1) * 0.5f : (g.y0 + g.y1) * 0.5f});
  }
  std::sort(v.begin(), v.end(), [](const It& a, const It& b) { return a.c < b.c; });
  float lo = v.front().c, hi = v.back().c;
  begin(horizontal ? "Distribute horizontally" : "Distribute vertically", sel);
  std::set<const Node*> done;
  for (size_t i = 0; i < v.size(); i++) {
    float want = lo + (hi - lo) * (float)i / (float)(v.size() - 1);
    translateNode(*v[i].n, horizontal ? want - v[i].c : 0, horizontal ? 0 : want - v[i].c, &done);
  }
  commit();
}
bool Editor::setProp(const std::string& key, const std::string& text) {
  if (sel.empty()) return false;
  bool any = false;
  begin("Set " + key, sel);
  for (auto& n : sel)
    for (auto& d : propsFor(*n))
      if (key == d.key) { any |= setPropText(*level, *n, d, text); break; }
  if (any) commit(); else abort();
  return any;
}
bool Editor::setMoverProp(const std::string& key, const std::string& text) {
  std::vector<NodeP> movers;
  for (auto& n : sel)
    if (auto v = n->val("mover"); v && v->n) movers.push_back(v->n);
  if (movers.empty()) return false;
  bool any = false;
  begin("Set mover " + key, movers);
  for (auto& m : movers)
    for (auto& d : moverProps())
      if (key == d.key) { any |= setPropText(*level, *m, d, text); break; }
  if (any) commit(); else abort();
  return any;
}
bool Editor::addMover(int type) {
  if (sel.empty()) return false;
  begin("Add mover", sel);
  bool any = false;
  for (auto& n : sel) any |= attachMover(*level, *n, type);
  if (any) commit(); else abort();
  return any;
}
bool Editor::setHoleExit(V2 p) {
  std::vector<NodeP> holes;
  for (auto& n : sel)
    if (n->type == 8) holes.push_back(n);
  if (holes.empty()) { status = "select a hole first, then click where the ball should come out"; return false; }
  std::vector<NodeP> touch = holes;
  for (auto& h : holes)
    if (auto v = h->val("entry"); v && v->n) touch.push_back(v->n);
  begin("Set hole exit", touch);
  for (auto& h : holes) {
    auto v = h->val("entry");
    if (v && v->n) {
      V2 now = nodePos(*level, *v->n);
      translateNode(*v->n, p.x - now.x, p.y - now.y);
      continue;
    }
    NodeP ex = newPoly(p.x, p.y, {{-19.5f, -19.5f}, {-19.5f, 19.5f}, {19.5f, 19.5f}, {19.5f, -19.5f}});
    ex->flags["collidable"] = false;
    Val e;
    e.k = VK::Ptr;
    e.n = ex;
    h->fields["entry"] = e;
    if (!h->has("out_delay")) putNum(*h, "out_delay", Prim::I32, 20);
  }
  commit();
  status = "hole exit set";
  return true;
}
void Editor::removeMover() {
  if (sel.empty()) return;
  begin("Remove mover", sel);
  for (auto& n : sel) detachMover(*level, *n);
  commit();
}
int Editor::generate(const std::string& kind, const std::vector<float>& a, int pegType) {
  std::vector<V2> pts;
  auto A = [&](size_t i, float d) { return i < a.size() ? a[i] : d; };
  if (kind == "grid") pts = gridPoints((int)A(0, 5), (int)A(1, 4), A(2, 40), A(3, 40), V2(A(4, 200), A(5, 150)));
  else if (kind == "circle") pts = circlePoints((int)A(0, 12), A(1, 80), V2(A(2, 325), A(3, 250)), A(4, -90));
  else if (kind == "spiral") pts = spiralPoints((int)A(0, 24), A(1, 20), A(2, 100), A(3, 2), V2(A(4, 325), A(5, 250)));
  else if (kind == "sine") pts = sinePoints((int)A(0, 12), A(1, 400), A(2, 40), A(3, 2), V2(A(4, 125), A(5, 300)));
  else return 0;
  if (pts.size() > 2000) pts.resize(2000);
  std::vector<NodeP> v;
  for (auto& p : pts) v.push_back(newCircle(std::round(p.x * 100) / 100, std::round(p.y * 100) / 100, roundRadius, pegType, pegType <= 2));
  addNodes(v, "Generate " + kind);
  return (int)v.size();
}
void Editor::setMeta(const std::string& k, const std::string& v) {
  if (k == "name") meta.name = v;
  else if (k == "title") meta.title = v;
  else if (k == "background") meta.background = v;
  else if (k == "master") meta.master = v;
  else if (k == "board_width") meta.boardWidth = std::clamp(std::atoi(v.c_str()), 650, 1400);
  else if (k == "balls") meta.balls = std::clamp(std::atoi(v.c_str()), 0, 99);
  else if (k == "orange") meta.orange = std::clamp(std::atoi(v.c_str()), 0, 500);
  else return;
  metaDirty = true;
  changed();
}

bool Editor::rotateHandle(V2& pos) const {
  float x0, y0, x1, y1;
  if (sel.empty() || !selBounds(x0, y0, x1, y1)) return false;
  pos = V2((x0 + x1) * 0.5f, y0 - 26.0f / std::max(0.05f, zoom_));
  return true;
}
bool Editor::overRotateHandle(V2 p) const {
  V2 h;
  return rotateHandle(h) && (p - h).len2() <= (9.0f / zoom_) * (9.0f / zoom_);
}
void Editor::cancelGesture() {
  if (g_.kind == G::Move || g_.kind == G::Rotate) abort();
  g_ = G();
  polyPts_.clear();
}
void Editor::mouseDown(V2 p, int button, Mods m, int clicks) {
  hover = p;
  if (button == 3) {
    if (g_.kind != G::None || !polyPts_.empty()) cancelGesture();
    else tool = Tool::Select;
    return;
  }
  if (button != 1 || tool == Tool::Pan) return;
  switch (tool) {
    case Tool::Select: {
      if (!sel.empty() && overRotateHandle(p)) {
        float x0, y0, x1, y1;
        selBounds(x0, y0, x1, y1);
        g_ = G();
        g_.kind = G::Rotate;
        g_.pivot = V2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
        g_.ang0 = std::atan2(p.y - g_.pivot.y, p.x - g_.pivot.x);
        begin("Rotate", sel);
        return;
      }
      NodeP hit = hitObject(p);
      if (hit) {
        if (m.shift) {
          auto it = std::find(sel.begin(), sel.end(), hit);
          if (it != sel.end()) { sel.erase(it); return; }
          sel.push_back(hit);
        } else if (!isSelected(hit)) {
          sel = {hit};
        }
        if (m.alt) {
          auto c = cloneAll(sel);
          addNodes(c, "Duplicate");
          hit = c.empty() ? hit : c.front();
        }
        g_ = G();
        g_.kind = G::Move;
        g_.start = p;
        g_.primary = isSelected(hit) ? hit : sel.front();
        g_.primaryStart = nodePos(*level, *g_.primary);
        begin("Move", sel);
      } else {
        if (!m.shift) sel.clear();
        g_ = G();
        g_.kind = G::Box;
        g_.a = g_.b = p;
        g_.shiftBox = m.shift;
      }
      break;
    }
    case Tool::HoleExit: setHoleExit(snapPt(p)); break;
    case Tool::Line:
      g_ = G();
      g_.kind = G::Line;
      g_.a = g_.b = snapPt(p);
      break;
    case Tool::Poly: {
      V2 q = snapPt(p);
      if (polyPts_.size() >= 3 && ((q - polyPts_[0]).len2() < (9 / zoom_) * (9 / zoom_) || clicks >= 2)) finishPoly();
      else polyPts_.push_back(q);
      break;
    }
    default: place(tool, p); break;
  }
}
void Editor::mouseMove(V2 p, Mods m) {
  hover = p;
  switch (g_.kind) {
    case G::Move: {
      V2 d = p - g_.start;
      if (snap) d = snapPt(g_.primaryStart + d) - g_.primaryStart;
      V2 step = d - g_.appliedD;
      if (step.x != 0 || step.y != 0) {
        std::set<const Node*> done;
        for (auto& n : sel) translateNode(*n, step.x, step.y, &done);
        g_.appliedD = d;
        g_.moved = true;
        changed();
      }
      break;
    }
    case G::Rotate: {
      float ang = std::atan2(p.y - g_.pivot.y, p.x - g_.pivot.x) - g_.ang0;
      if (snap || m.shift) {
        float q = deg2rad(15.0f);
        ang = std::floor(ang / q + 0.5f) * q;
      }
      float step = ang - g_.applied;
      if (step != 0) {
        std::set<const Node*> done;
        for (auto& n : sel) rotateNode(*level, *n, g_.pivot, step, &done);
        g_.applied = ang;
        g_.moved = true;
        changed();
      }
      break;
    }
    case G::Box: g_.b = p; break;
    case G::Line: {
      V2 q = snapPt(p);
      if (m.shift) {
        V2 d = q - g_.a;
        float len = std::sqrt(d.len2()), a = std::floor(std::atan2(d.y, d.x) / deg2rad(15) + 0.5f) * deg2rad(15);
        q = g_.a + V2(std::cos(a), std::sin(a)) * len;
      }
      g_.b = q;
      break;
    }
    default: break;
  }
}
void Editor::mouseUp(V2 p, int button, Mods m) {
  hover = p;
  if (button != 1) return;
  switch (g_.kind) {
    case G::Move:
    case G::Rotate:
      if (g_.moved) commit(); else abort();
      break;
    case G::Box: {
      auto in = inBox(g_.a.x, g_.a.y, g_.b.x, g_.b.y);
      if (g_.shiftBox) {
        for (auto& n : in)
          if (!isSelected(n)) sel.push_back(n);
      } else sel = in;
      break;
    }
    case G::Line:
      if ((g_.b - g_.a).len2() > 4) addNodes({newLine(g_.a.x, g_.a.y, g_.b.x, g_.b.y)}, "Place line");
      break;
    default: break;
  }
  g_ = G();
}
void Editor::keyEnter() {
  if (tool == Tool::Poly && polyPts_.size() >= 3) finishPoly();
}
void Editor::finishPoly() {
  if (polyPts_.size() < 3) { polyPts_.clear(); return; }
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (auto& p : polyPts_) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
  V2 o = snapPt(V2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f));
  std::vector<V2> local;
  for (auto& p : polyPts_) local.push_back(p - o);
  double area = 0;
  for (size_t i = 0; i < local.size(); i++) {
    const V2 &a = local[i], &b = local[(i + 1) % local.size()];
    area += (double)a.x * b.y - (double)b.x * a.y;
  }
  if (area > 0) std::reverse(local.begin(), local.end());
  polyPts_.clear();
  addNodes({newPoly(o.x, o.y, local)}, "Place polygon");
}

BoardConfig Editor::boardConfig(uint32_t seed) const {
  BoardConfig c;
  c.boardWidth = std::max(650, meta.boardWidth);
  c.balls = meta.balls;
  c.orange = meta.orange;
  c.seed = seed;
  int m = meta.master.empty() ? 0 : Board::masterByName(meta.master);
  for (int i = 0; i < 4; i++) c.master[i] = m;
  return c;
}
static bool buildable(const Node& n) {
  if (n.type == 3) {
    auto v = n.val("vertices");
    return v && v->verts.size() >= 4;
  }
  return n.type == 2 || n.type == 5 || n.type == 6 || n.type == 8;
}
static std::vector<const Node*> builtNodes(const Level& L) {
  std::vector<const Node*> out;
  auto has = [&](const Node* n) { return std::find(out.begin(), out.end(), n) != out.end(); };
  for (auto& e : L.entries)
    if (auto n = L.resolve(e))
      if (buildable(*n) && !has(n)) out.push_back(n);
  for (size_t i = 0; i < out.size(); i++) {
    if (out[i]->type != 8) continue;
    if (auto v = out[i]->val("entry"))
      if (auto en = L.resolve(*v))
        if (buildable(*en) && !has(en)) out.push_back(en);
  }
  return out;
}
bool Editor::buildBoard(Board& b, LevelBundle& lb, std::vector<NodeP>* objNode, uint32_t seed, bool stored, std::string* err) const {
  auto baked = bake();
  if (!baked) {
    if (err) *err = "the level cannot be written (a required field is missing)";
    return false;
  }
  lb = LevelBundle();
  lb.base = base;
  lb.level = baked;
  lb.name = meta.name;
  lb.title = meta.title;
  lb.background = meta.background.empty() ? meta.name : meta.background;
  lb.source = "editor";
  lb.cfg = boardConfig(seed);
  if (!b.load(base, baked, lb.cfg, err)) return false;
  size_t baseCount = base ? builtNodes(*base).size() : 0;
  auto nodes = builtNodes(*baked);
  if (objNode) objNode->assign(b.objs.size(), nullptr);
  if (b.objs.size() != baseCount + nodes.size()) return true;
  std::map<const Node*, NodeP> toLive;
  for (size_t i = 0; i < baked->entries.size() && i < level->entries.size(); i++)
    if (baked->entries[i].n && level->entries[i].n) toLive[baked->entries[i].n.get()] = level->entries[i].n;
  for (size_t i = 0; i < nodes.size(); i++) {
    auto it = toLive.find(nodes[i]);
    if (it == toLive.end()) continue;
    if (objNode) (*objNode)[baseCount + i] = it->second;
    if (stored) b.objs[baseCount + i].pegType = pegTypeOf(*it->second);
  }
  return true;
}

std::vector<Issue> Editor::validate() {
  std::vector<Issue> out;
  auto add = [&](Issue::Sev s, const std::string& t, NodeP n = nullptr) { out.push_back({s, t, n}); };
  auto objs = objects();
  float sh = wideShift(), W = playfieldW();
  if (objs.empty()) add(Issue::Error, "the level has no objects");

  int pegs = 0, cand = 0, fixedOrange = 0, storedOrange = 0, purple = 0, green = 0;
  for (auto& n : objs) {
    Node* p = pegOf(*n);
    if (!p) continue;
    pegs++;
    int t = (int)p->i("type");
    bool var = p->flag("variable");
    if (t == 2) storedOrange++;
    if (var && (t == 1 || t == 2)) cand++;
    else if (!var && t == 2) fixedOrange++;
    if (t == 3) purple++;
    if (t == 4) green++;
  }
  if (pegs == 0) add(Issue::Error, "no pegs: the level cannot be played");
  else if (cand + fixedOrange == 0) add(Issue::Error, "no peg can become orange (needs pegs with 'variable colour' on)");
  else if (cand + fixedOrange < meta.orange) add(Issue::Warn, std::to_string(cand + fixedOrange) + " pegs can be orange but the target is " + std::to_string(meta.orange) + " (the original rules want 25)");
  if (pegs > 0) add(Issue::Info, std::to_string(pegs) + " pegs: " + std::to_string(storedOrange) + " orange stored, " + std::to_string(purple) + " purple, " + std::to_string(green) + " green, " + std::to_string(cand) + " random-colour candidates");

  bool ownBucket = false;
  for (auto& n : objs)
    if (n->str("id") == "freeball") ownBucket = true;
  bool baseBucket = false;
  if (base)
    for (auto& e : base->entries)
      if (e.n && e.n->str("id") == "freeball") baseBucket = true;
  bool underscore = !meta.name.empty() && meta.name[0] == '_';
  if (ownBucket || (baseBucket && !underscore)) add(Issue::Info, ownBucket ? "bucket: defined by the level" : "bucket and walls come from _base.dat");
  else if (underscore) add(Issue::Error, "no bucket: names starting with '_' do not merge _base.dat and the level has no 'freeball' hole");
  else add(Issue::Warn, "no bucket found: no _base.dat is loaded (open the editor with the game data)");

  std::vector<std::pair<NodeP, Geo>> solid;
  std::vector<std::pair<NodeP, Geo>> circles;
  for (auto& n : objs) {
    Geo g = geometry(*level, *n);
    if (g.kind == Geo::Circle) circles.push_back({n, g});
    else if (n->flag("collidable") && (g.kind == Geo::Poly || g.kind == Geo::Line)) solid.push_back({n, g});
  }
  int outside = 0, partly = 0, wide = 0;
  for (auto& n : objs) {
    Geo g = geometry(*level, *n);
    if (g.kind == Geo::None) continue;
    float bx0 = g.x0 + sh, bx1 = g.x1 + sh;
    bool bg = n->flag("background") || !n->flag("collidable");
    if (bx1 < 0 || bx0 > W || g.y1 < 0 || g.y0 > kBoardH + 40) {
      if (!bg) { outside++; add(Issue::Error, std::string(typeName(n->type)) + " is completely outside the playfield", n); }
    } else if (bx0 < 0 || bx1 > W || g.y0 < 0 || g.y1 > kBoardH) {
      if (!bg && pegOf(*n)) { partly++; add(Issue::Warn, std::string(typeName(n->type)) + " peg sticks out of the playfield", n); }
    }
    if (W > 650 && (g.x0 < 0 || g.x1 > 650) && !bg) wide++;
  }
  if (W > 650 && wide) add(Issue::Info, std::to_string(wide) + " object(s) lie outside the original 650 px region (this level needs the wide board)");
  if (W == 650) {
    int beyond = 0;
    for (auto& n : objs) {
      Geo g = geometry(*level, *n);
      if (pegOf(*n) && g.kind != Geo::None && (g.x0 < 0 || g.x1 > 650)) beyond++;
    }
    (void)beyond;
  }
  int overlapWall = 0, overlapPeg = 0, overlapBucket = 0;
  for (size_t i = 0; i < circles.size(); i++) {
    auto& [n, g] = circles[i];
    if (!pegOf(*n)) continue;
    for (auto& [sn, sg] : solid) {
      if (sn.get() == n.get()) continue;
      if (distToGeo(sg, g.c) < g.r - 0.5f) {
        if (++overlapWall <= 6) add(Issue::Warn, "peg overlaps a " + std::string(typeName(sn->type)), n);
        break;
      }
    }
    if (g.c.y + sh * 0 > kBoardH - 40 && g.c.y < kBoardH + 40) {
      if (++overlapBucket <= 3) add(Issue::Warn, "peg in the bucket zone (y > " + std::to_string((int)(kBoardH - 40)) + ")", n);
    }
    if (g.c.x - g.r + sh < 0 || g.c.x + g.r + sh > W) {
      if (++overlapWall <= 6) add(Issue::Warn, "peg overlaps the side wall", n);
    }
    for (size_t j = i + 1; j < circles.size(); j++) {
      auto& [m2, g2] = circles[j];
      if (!pegOf(*m2)) continue;
      float rr = g.r + g2.r - 1.0f;
      if ((g.c - g2.c).len2() < rr * rr) {
        if (++overlapPeg <= 6) add(Issue::Warn, (g.c - g2.c).len2() < 1 ? "two pegs sit on the same spot" : "pegs overlap", n);
      }
    }
  }
  if (overlapWall > 6) add(Issue::Warn, std::to_string(overlapWall - 6) + " more peg/wall overlaps");
  if (overlapPeg > 6) add(Issue::Warn, std::to_string(overlapPeg - 6) + " more peg/peg overlaps");

  std::map<std::string, std::vector<NodeP>> ids;
  for (auto& n : objs) {
    std::string id = n->str("id");
    if (!id.empty()) ids[id].push_back(n);
  }
  for (auto& [id, v] : ids) {
    if (v.size() > 1) add(Issue::Warn, "duplicate id '" + id + "' on " + std::to_string(v.size()) + " objects", v[0]);
    if (base)
      for (auto& e : base->entries)
        if (e.n && e.n->str("id") == id) { add(Issue::Warn, "id '" + id + "' is used by _base.dat (walls/bucket logic)", v[0]); break; }
  }
  for (auto& n : objs) {
    Node* m = moverOf(*level, *n);
    if (!m) continue;
    int t = std::abs((int)m->i("move_type"));
    if (t == 10 || t > 15) add(Issue::Warn, "mover type " + std::to_string(t) + " does nothing", n);
    if (t != 0 && m->i("time", 200) < 1) add(Issue::Warn, "mover period is below 1 tick", n);
    if ((t >= 1 && t <= 7 && t != 6 && t != 7) && m->i("radius") == 0 && m->i("radius2") == 0) add(Issue::Info, "mover with radius 0 does not move", n);
  }
  if (level->version > 0x42) add(Issue::Info, "file version " + std::to_string(level->version) + ": loads in Peggle Nights only (Deluxe refuses versions above 66)");
  int skipped = 0;
  for (auto& e : level->entries)
    if (e.n && !isObjectType(e.n->type)) skipped++;
  if (skipped) add(Issue::Info, std::to_string(skipped) + " entries (emitters / bare objects) are kept but not shown");
  static const std::string okChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
  if (meta.name.empty() || meta.name.find_first_not_of(okChars) != std::string::npos) add(Issue::Error, "level name '" + meta.name + "' must be letters, digits, '_' or '-'");
  bool hasErr = false, hasWarn = false;
  for (auto& i : out) { hasErr |= i.sev == Issue::Error; hasWarn |= i.sev == Issue::Warn; }
  if (!hasErr && !hasWarn) add(Issue::Info, "no problems found");
  std::stable_sort(out.begin(), out.end(), [](const Issue& a, const Issue& b) { return a.sev > b.sev; });
  issues = out;
  return out;
}

static std::vector<std::string> words(const std::string& line) {
  std::vector<std::string> w;
  std::istringstream in(line);
  std::string t;
  while (in >> t) w.push_back(t);
  return w;
}
static std::vector<float> nums(const std::vector<std::string>& w, size_t from) {
  std::vector<float> v;
  for (size_t i = from; i < w.size(); i++) {
    char* e = nullptr;
    float f = std::strtof(w[i].c_str(), &e);
    if (e != w[i].c_str()) v.push_back(f);
  }
  return v;
}
std::string Editor::exec(const std::string& line, bool* okOut) {
  bool ok = true;
  std::string msg;
  auto w = words(line);
  Mods mods;
  for (auto it = w.begin(); it != w.end();) {
    if (*it == "shift") { mods.shift = true; it = w.erase(it); }
    else if (*it == "alt") { mods.alt = true; it = w.erase(it); }
    else if (*it == "ctrl") { mods.ctrl = true; it = w.erase(it); }
    else ++it;
  }
  if (w.empty()) { if (okOut) *okOut = true; return ""; }
  const std::string& c = w[0];
  auto n = nums(w, 1);
  auto need = [&](size_t k) { if (n.size() < k) { ok = false; msg = "'" + c + "' needs " + std::to_string(k) + " number(s)"; return false; } return true; };
  std::string rest;
  for (size_t i = 2; i < w.size(); i++) rest += (i > 2 ? " " : "") + w[i];
  std::string err;
  if (c == "new") {
    newLevel(w.size() > 1 ? w[1] : "NewLevel", w.size() > 2 ? std::atoi(w[2].c_str()) : 650);
    msg = status;
  } else if (c == "save") {
    if (w.size() > 1) ok = saveDat(w[1], &err); else if (!savedPath.empty()) ok = saveDat(savedPath, &err); else { ok = false; err = "save needs a path (or use savemod)"; }
    msg = ok ? status : err;
  } else if (c == "export") {
    ok = w.size() > 1 && saveJson(w[1], &err);
    msg = ok ? status : (w.size() > 1 ? err : "export needs a path");
  } else if (c == "import") {
    ok = w.size() > 1 && loadJson(w[1], &err);
    msg = ok ? status : (w.size() > 1 ? err : "import needs a path");
  } else if (c == "loaddat") {
    ok = w.size() > 1 && loadDatFile(w[1], &err);
    msg = ok ? status : (w.size() > 1 ? err : "loaddat needs a path");
  } else if (c == "tool") {
    Tool t;
    if (w.size() > 1 && toolFromName(w[1], t)) { tool = t; polyPts_.clear(); msg = "tool " + w[1]; } else { ok = false; msg = "unknown tool"; }
  } else if (c == "place") {
    Tool t;
    if (w.size() > 3 && toolFromName(w[1], t) && t != Tool::Select && t != Tool::Pan && t != Tool::Poly && t != Tool::Line) {
      auto q = nums(w, 2);
      if (t == Tool::PegRound || t == Tool::Bumper) { if (q.size() > 2) roundRadius = q[2]; }
      bool sv = snap; snap = false;
      NodeP node = place(t, V2(q[0], q[1]));
      snap = sv;
      msg = node ? std::string("placed ") + w[1] : "nothing placed";
    } else { ok = false; msg = "usage: place TOOL x y [radius]"; }
  } else if (c == "exit") {
    if (need(2)) { ok = setHoleExit(V2(n[0], n[1])); msg = ok ? "hole exit set" : status; }
  } else if (c == "line") {
    if (need(4)) { addNodes({newLine(n[0], n[1], n[2], n[3])}, "Place line"); msg = "line placed"; }
  } else if (c == "poly") {
    if (n.size() >= 6) {
      polyPts_.clear();
      for (size_t i = 0; i + 1 < n.size(); i += 2) polyPts_.push_back(V2(n[i], n[i + 1]));
      finishPoly();
      msg = "polygon placed";
    } else { ok = false; msg = "poly needs at least 3 points"; }
  } else if (c == "click" || c == "down" || c == "up" || c == "move") {
    if (need(2)) {
      V2 p(n[0], n[1]);
      if (c == "click") { mouseDown(p, 1, mods); mouseUp(p, 1, mods); }
      else if (c == "down") mouseDown(p, 1, mods);
      else if (c == "up") mouseUp(p, 1, mods);
      else mouseMove(p, mods);
      msg = c;
    }
  } else if (c == "rclick") {
    if (need(2)) { mouseDown(V2(n[0], n[1]), 3, mods); msg = "rclick"; }
  } else if (c == "enter") {
    keyEnter();
  } else if (c == "drag") {
    if (need(4)) {
      V2 a(n[0], n[1]), b(n[2], n[3]);
      mouseDown(a, 1, mods);
      for (int i = 1; i <= 5; i++) mouseMove(a + (b - a) * (i / 5.0f), mods);
      mouseUp(b, 1, mods);
      msg = "dragged";
    }
  } else if (c == "select") {
    if (w.size() > 1 && w[1] == "all") selectAll();
    else if (w.size() > 1 && w[1] == "none") sel.clear();
    else if (w.size() > 1 && w[1] == "box") { if (need(4)) sel = inBox(n[0], n[1], n[2], n[3]); }
    else if (n.size() >= 2) { NodeP h = hitObject(V2(n[0], n[1])); sel = h ? std::vector<NodeP>{h} : std::vector<NodeP>{}; }
    else if (w.size() > 1 && w[1] == "orange") { sel.clear(); for (auto& o : objects()) if (pegTypeOf(*o) == 2) sel.push_back(o); }
    else { ok = false; msg = "usage: select all|none|orange|x y|box x0 y0 x1 y1"; }
    if (ok) msg = std::to_string(sel.size()) + " selected";
  } else if (c == "delete") { deleteSelection(); msg = "deleted"; }
  else if (c == "undo") { ok = undo(); msg = ok ? status : "nothing to undo"; }
  else if (c == "redo") { ok = redo(); msg = ok ? status : "nothing to redo"; }
  else if (c == "copy") { copySelection(); msg = status; }
  else if (c == "cut") { cutSelection(); msg = "cut"; }
  else if (c == "paste") { paste(n.size() >= 2 ? V2(n[0], n[1]) : V2(), n.size() >= 2); msg = "pasted"; }
  else if (c == "dup") { duplicateSelection(n.size() > 0 ? n[0] : 20, n.size() > 1 ? n[1] : 20); msg = "duplicated"; }
  else if (c == "nudge") { if (need(2)) { moveSelection(n[0], n[1]); msg = "moved"; } }
  else if (c == "rotate") { if (need(1)) { rotateSelection(n[0]); msg = "rotated"; } }
  else if (c == "mirror") { mirrorSelection(w.size() < 2 || w[1] != "v"); msg = "mirrored"; }
  else if (c == "align") { if (w.size() > 1) { align(w[1]); msg = "aligned"; } else { ok = false; msg = "align left|right|top|bottom|hcenter|vcenter"; } }
  else if (c == "distribute") { distribute(w.size() < 2 || w[1] != "v"); msg = "distributed"; }
  else if (c == "set") { if (w.size() > 2) { ok = setProp(w[1], rest); msg = ok ? "set " + w[1] : "cannot set " + w[1]; } else { ok = false; msg = "usage: set KEY VALUE"; } }
  else if (c == "mset") { if (w.size() > 2) { ok = setMoverProp(w[1], rest); msg = ok ? "mover " + w[1] + " set" : "no mover or unknown key"; } else { ok = false; msg = "usage: mset KEY VALUE"; } }
  else if (c == "mover") {
    if (w.size() > 1 && w[1] == "remove") { removeMover(); msg = "mover removed"; }
    else if (w.size() > 2 && w[1] == "add") { ok = addMover(std::atoi(w[2].c_str())); msg = ok ? "mover added" : "no object can take a mover"; }
    else { ok = false; msg = "usage: mover add TYPE | mover remove"; }
  } else if (c == "gen") {
    if (w.size() > 1) {
      int type = 1;
      std::vector<std::string> ww(w.begin() + 1, w.end());
      if (!ww.empty() && ww.back() == "orange") { type = 2; ww.pop_back(); }
      int made = generate(ww[0], nums(ww, 1), type);
      ok = made > 0;
      msg = ok ? "generated " + std::to_string(made) + " pegs" : "usage: gen grid|circle|spiral|sine numbers...";
    } else { ok = false; msg = "usage: gen grid|circle|spiral|sine numbers..."; }
  } else if (c == "grid") { if (need(1)) { grid = std::max(0.0f, n[0]); msg = "grid " + std::to_string((int)grid); } }
  else if (c == "snap") { snap = w.size() < 2 || w[1] != "off"; msg = snap ? "snap on" : "snap off"; }
  else if (c == "meta") { if (w.size() > 2) { setMeta(w[1], rest); msg = "meta " + w[1]; } else { ok = false; msg = "usage: meta name|title|background|master|board_width|balls|orange VALUE"; } }
  else if (c == "validate") {
    auto v = validate();
    int e = 0, wn = 0;
    for (auto& i : v) { e += i.sev == Issue::Error; wn += i.sev == Issue::Warn; }
    msg = std::to_string(e) + " error(s), " + std::to_string(wn) + " warning(s)";
    for (auto& i : v) msg += std::string("\n  ") + (i.sev == Issue::Error ? "ERROR " : i.sev == Issue::Warn ? "warn  " : "info  ") + i.text;
  } else if (c == "oranges") {
    previewOranges = w.size() < 2 || w[1] != "off";
    if (n.size() > 0) previewSeed = (uint32_t)n[0]; else if (previewOranges) previewSeed++;
    changed();
    msg = previewOranges ? "random orange preview, seed " + std::to_string(previewSeed) : "stored colours";
  } else if (c == "info") {
    msg = meta.name + ": " + std::to_string(objects().size()) + " objects, " + std::to_string(pegCount()) + " pegs (" + std::to_string(pegCount(2)) + " orange), " +
          std::to_string(sel.size()) + " selected, undo " + std::to_string(undo_.size()) + ", " + (dirty() ? "modified" : "saved");
  } else {
    ok = false;
    msg = "unknown editor command '" + c + "'";
  }
  status = msg.substr(0, msg.find('\n'));
  if (okOut) *okOut = ok;
  return msg;
}

}
