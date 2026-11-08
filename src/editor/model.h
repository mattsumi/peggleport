#pragma once
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <utility>

#include "brick.h"
#include "level.h"
#include "sim.h"
#include "vec2.h"

namespace pp::ed {

constexpr float kBoardW = 650.0f, kBoardH = 560.0f;
constexpr float kPi = 3.14159265358979f;
inline float rad2deg(float r) { return r * 180.0f / kPi; }
inline float deg2rad(float d) { return d * kPi / 180.0f; }

void putNum(Node& n, const std::string& key, Prim p, double v);
void putStr(Node& n, const std::string& key, const std::string& v);
void eraseField(Node& n, const std::string& key);
bool isObjectType(int type);
const char* typeName(int type);
Node* moverOf(const Level& L, const Node& n);
Node* pegOf(const Node& n);
int pegTypeOf(const Node& n);

NodeP newCircle(float x, float y, float r, int pegType, bool variable = true);
NodeP newLine(float x1, float y1, float x2, float y2, bool visible = false);
NodeP newPoly(float x, float y, const std::vector<V2>& localOpen);
NodeP newBrick(float x, float y, const BrickParams& bp, int pegType = 1);
NodeP newHole(float x, float y, int w, int h, bool circular, int score = 0);
NodeP newEmitter(float x, float y, const std::string& image);
NodeP newMover(int type, float anchorX, float anchorY);
BrickParams brickParamsOf(const Node& n);
void syncBrickCache(Node& n);
NodeP cloneNode(const NodeP& n, std::map<const Node*, NodeP>* remap = nullptr);

struct Geo {
  enum Kind { None, Circle, Line, Poly, Hole, Emitter } kind = None;
  V2 c;
  float r = 0, rot = 0;
  bool circular = false;
  bool closed = false;
  std::vector<V2> pts;
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
Geo geometry(const Level& L, const Node& n, int tick = 0);
V2 rotGame(V2 p, float rot);
bool hitTest(const Geo& g, V2 p, float tol);
bool hitBox(const Geo& g, float x0, float y0, float x1, float y1);
float distToSegment(V2 p, V2 a, V2 b);
bool pointInPoly(const std::vector<V2>& pts, V2 p);
float distToGeo(const Geo& g, V2 p);

bool polyHasClosing(const Node& n);
std::vector<V2> polyCorners(const Node& n);
bool polyShapeOk(const std::vector<V2>& before, const std::vector<V2>& after);
V2 polyWorldToLocal(const Level& L, const Node& n, V2 world);
bool polySetCorners(Node& n, const std::vector<V2>& corners);
int hitCorner(const Level& L, const Node& n, V2 p, float tol);
int hitEdge(const Level& L, const Node& n, V2 p, float tol, V2* at);
V2 polyCornerWorld(const Level& L, const Node& n, int i);

void translateNode(Node& n, float dx, float dy, std::set<const Node*>* done = nullptr);
void rotateNode(const Level& L, Node& n, V2 pivot, float radians, std::set<const Node*>* done = nullptr);
void mirrorNode(const Level& L, Node& n, bool flipX, float axis, std::set<const Node*>* done = nullptr);
V2 nodePos(const Level& L, const Node& n);

enum class PK { Float, Int, Bool, Str, Enum, Color };
struct PropDef {
  const char* key;
  const char* label;
  PK kind;
  Prim prim;
  bool isFlag;
  double def;
  double lo, hi, step;
  bool required;
  const char* enums;
  int enumBase;
  const char* group;
  bool vf = false;
};
const std::vector<PropDef>& propsFor(const Node& n);
const std::vector<PropDef>& moverProps();
bool hasProp(const Node& n, const PropDef& d);
double getNum(const Level& L, const Node& n, const PropDef& d);
std::string getStr(const Node& n, const PropDef& d);
std::string propText(const Level& L, const Node& n, const PropDef& d);
bool setNum(Level& L, Node& n, const PropDef& d, double v);
bool setStr(Node& n, const PropDef& d, const std::string& s);
bool setPropText(Level& L, Node& n, const PropDef& d, const std::string& text);
bool attachMover(Level& L, Node& obj, int moverType);
void detachMover(Level& L, Node& obj);
void setPegType(Node& n, int type);
extern const char* kMoverTypeNames[];
constexpr int kMoverTypes = 16;

std::vector<V2> gridPoints(int cols, int rows, float dx, float dy, V2 origin);
std::vector<V2> circlePoints(int n, float radius, V2 centre, float startDeg = -90.0f);
std::vector<V2> spiralPoints(int n, float r0, float r1, float turns, V2 centre);
std::vector<V2> sinePoints(int n, float length, float amplitude, float periods, V2 origin);
inline float snapTo(float v, float grid) { return grid > 0.01f ? std::floor(v / grid + 0.5f) * grid : v; }

struct J {
  enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<J> a;
  std::vector<std::pair<std::string, J>> o;
  J() = default;
  static J num(double v) { J j; j.t = Num; j.n = v; return j; }
  static J str(const std::string& v) { J j; j.t = Str; j.s = v; return j; }
  static J boolean(bool v) { J j; j.t = Bool; j.b = v; return j; }
  static J arr() { J j; j.t = Arr; return j; }
  static J obj() { J j; j.t = Obj; return j; }
  void set(const std::string& k, J v);
  const J* get(const std::string& k) const;
  std::string dump(int indent = 0) const;
};
bool parseJson(const std::string& text, J& out, std::string* err);

struct LevelMeta {
  std::string name = "NewLevel", title = "New Level", background, master;
  int boardWidth = 650, balls = 0, orange = 25;
  PhysicsEdit physics;
};
J levelToJson(const Level& L, const LevelMeta& m);
bool levelFromJson(const J& j, Level& out, LevelMeta& meta, std::string* err);
void linkRefs(Level& L);

struct NodeSnap {
  NodeP node;
  Node content;
  std::vector<NodeSnap> kids;
};
NodeSnap snapNode(const NodeP& n);
void restoreNode(const NodeSnap& s);

}
