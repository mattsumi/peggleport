#pragma once
#include <filesystem>
#include <functional>
#include <set>
#include <cstdint>
#include <algorithm>
#include <map>
#include <memory>
#include <vector>
#include <string>

#include "content.h"
#include "editor/model.h"
#include "vfs.h"

namespace pp::ed {

enum class Tool { Select, Pan, PegBlue, PegOrange, PegGreen, PegPurple, PegRound, Bumper, BrickStraight, BrickCurved, Poly, Line, Hole, HoleExit, Emitter };
const char* toolName(Tool t);
bool toolFromName(const std::string& s, Tool& t);

struct Mods {
  bool shift = false, ctrl = false, alt = false;
};

struct Issue {
  enum Sev { Info, Warn, Error } sev = Info;
  std::string text;
  NodeP node;
};

class Editor {
 public:
  Editor();

  std::shared_ptr<Level> level;
  std::shared_ptr<const Level> base;
  LevelMeta meta;
  std::string savedPath;
  std::vector<NodeP> sel;
  std::string status;
  std::vector<Issue> issues;
  int revision = 0;
  std::map<const Node*, std::string> customTypes;
  const Vfs* vfs = nullptr;
  std::string emitterImage;
  void setVfs(const Vfs* v);
  bool imageExists(const std::string& path) const;
  std::string customType(const NodeP& n) const;
  void setCustomType(const std::string& key);

  void newLevel(const std::string& name, int boardWidth = 650);
  bool loadFromVfs(const Vfs& vfs, const std::string& nameOrPath, std::string* err);
  bool loadDatFile(const std::filesystem::path& p, std::string* err);
  bool saveDat(const std::filesystem::path& p, std::string* err);
  bool saveJson(const std::filesystem::path& p, std::string* err);
  bool loadJson(const std::filesystem::path& p, std::string* err);
  bool saveToMod(const std::filesystem::path& modDir, std::string* err);
  static std::filesystem::path modDirFor(const std::filesystem::path& userDir, const std::string& modName) { return userDir / "mods" / modName; }
  std::shared_ptr<Level> bake() const;
  bool metaDirty = false;
  bool dirty() const { return (int)undo_.size() != clean_ || metaDirty; }
  static std::shared_ptr<Level> syntheticBase();

  std::vector<NodeP> objects() const;
  NodeP hitObject(V2 p) const;
  std::vector<NodeP> inBox(float x0, float y0, float x1, float y1) const;
  bool isSelected(const NodeP& n) const;
  bool selBounds(float& x0, float& y0, float& x1, float& y1) const;
  float wideShift() const { return (std::max(650, meta.boardWidth) - 650) * 0.5f; }
  float playfieldW() const { return (float)std::max(650, meta.boardWidth); }
  void setZoom(float z) { zoom_ = z; }
  int pegCount(int type = -1) const;

  void select(const std::vector<NodeP>& v) { sel = v; }
  void selectAll();
  void clearSelection() { sel.clear(); }
  NodeP place(Tool t, V2 p);
  void addNodes(const std::vector<NodeP>& v, const std::string& label);
  void deleteSelection();
  void moveSelection(float dx, float dy);
  void rotateSelection(float degrees);
  void mirrorSelection(bool horizontal);
  void duplicateSelection(float dx, float dy);
  void copySelection();
  void cutSelection();
  void paste(V2 at, bool useAt);
  void align(const std::string& mode);
  void distribute(bool horizontal);
  bool setProp(const std::string& key, const std::string& text);
  bool setMoverProp(const std::string& key, const std::string& text);
  bool addMover(int type);
  bool setHoleExit(V2 p);
  void removeMover();

  bool cornersEditable() const;
  NodeP cornerPoly() const { return cornersEditable() ? sel.front() : nullptr; }
  int cornerCount() const;
  int cornerIndex() const;
  void selectCorner(int i);
  V2 cornerWorld(int i) const;
  int cornerAt(V2 p) const;
  int edgeAt(V2 p, V2* at) const;
  bool setCorner(int i, V2 world);
  bool addCorner();
  bool addCornerAt(V2 world);
  bool removeCorner();
  bool deleteKey();
  int generate(const std::string& kind, const std::vector<float>& a, int pegType);
  void setMeta(const std::string& key, const std::string& value);
  bool undo();
  bool redo();
  std::string undoLabel() const { return undo_.empty() ? "" : undo_.back().label; }
  std::string redoLabel() const { return redo_.empty() ? "" : redo_.back().label; }
  size_t undoDepth() const { return undo_.size(); }

  Tool tool = Tool::Select;
  float grid = 10;
  bool snap = true;
  float roundRadius = 10;
  BrickParams brick, brickStraight;
  int placeMoverType = 0;
  V2 snapPt(V2 p) const { return snap ? V2(snapTo(p.x, grid), snapTo(p.y, grid)) : p; }

  void mouseDown(V2 p, int button, Mods m, int clicks = 1);
  void mouseMove(V2 p, Mods m);
  void mouseUp(V2 p, int button, Mods m);
  void cancelGesture();
  void keyEnter();
  bool gestureActive() const { return g_.kind != G::None; }
  bool boxActive() const { return g_.kind == G::Box; }
  V2 boxA() const { return g_.a; }
  V2 boxB() const { return g_.b; }
  const std::vector<V2>& polyPoints() const { return polyPts_; }
  bool lineActive() const { return g_.kind == G::Line; }
  V2 hover;
  bool rotateHandle(V2& pos) const;
  bool overRotateHandle(V2 p) const;

  std::vector<Issue> validate();
  bool previewOranges = false;
  uint32_t previewSeed = 1;
  bool buildBoard(Board& b, LevelBundle& lb, std::vector<NodeP>* objNode, uint32_t seed, bool storedColours, std::string* err) const;
  BoardConfig boardConfig(uint32_t seed) const;

  std::string exec(const std::string& line, bool* ok = nullptr);

 private:
  struct Step {
    std::string label;
    std::vector<Val> eb, ea;
    std::vector<NodeP> touched;
    std::vector<NodeSnap> sb, sa;
  };
  std::vector<Step> undo_, redo_;
  Step cur_;
  bool open_ = false;
  int clean_ = 0;
  float zoom_ = 1;
  std::vector<NodeP> clip_;
  V2 clipOrigin_;
  void begin(const std::string& label, const std::vector<NodeP>& touch);
  void commit();
  void abort();
  void changed() { revision++; }
  void prune();
  void resetHistory();
  void finishLoad();

  struct G {
    enum Kind { None, Move, Box, Rotate, Line, Stamp, Corner } kind = None;
    V2 a, b, start, pivot;
    float applied = 0, ang0 = 0;
    V2 appliedD;
    bool moved = false;
    NodeP primary;
    V2 primaryStart;
    bool shiftBox = false;
    int corner = -1;
    V2 cornerStart;
    std::vector<V2> cornersAtStart;
  } g_;
  std::vector<V2> polyPts_;
  NodeP cornerPoly_;
  int cornerIdx_ = -1;
  void finishPoly();
  NodeP makeFor(Tool t, V2 p) const;
  bool applyCorners(const NodeP& poly, const std::vector<V2>& corners, const std::string& label);
};

}
