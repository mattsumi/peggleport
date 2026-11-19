#include "sim.h"

#include <cmath>
#include <iostream>
#include <memory>

using namespace pp;

int main() {
  int checks = 0, failures = 0;
  auto check = [&](bool pass, const char* name) {
    ++checks;
    if (!pass) { ++failures; std::cerr << "FAIL " << name << '\n'; }
  };
  for (int shape : {2, 3, 5, 6}) for (float x : {-70.0f, 300.0f, 720.0f}) for (bool collidable : {false, true}) for (bool visible : {false, true}) {
    auto level = std::make_shared<Level>();
    auto node = std::make_shared<Node>();
    node->type = shape;
    node->flags["visible"] = visible;
    node->flags["collidable"] = collidable;
    node->setF("x", x);
    node->setF("y", 250);
    node->setF("radius", 10);
    node->setF("x1", x - 20);
    node->setF("x2", x + 20);
    node->setF("y1", 250);
    node->setF("y2", 250);
    node->fields["vertices"].k = VK::Verts;
    node->fields["vertices"].verts = {-20, -10, -20, 10, 20, 10, 20, -10, -20, -10};
    Val entry;
    entry.k = VK::Ptr;
    entry.n = node;
    level->entries.push_back(entry);
    Board original;
    BoardConfig classic;
    std::string err;
    if (!original.load(nullptr, level, classic, &err) || original.objs.size() != 1) return 2;
    const Obj& source = original.objs[0];
    for (int authored : {650, 917, 1400}) for (int display : {917, 1400}) {
      if (display < authored) continue;
      BoardConfig cfg;
      cfg.boardWidth = display;
      cfg.authoredWidth = authored;
      Board board;
      if (!board.load(nullptr, level, cfg, &err) || board.objs.size() != 1) return 2;
      const auto& obj = board.objs[0];
      const bool retained = authored > 650 || x == 300;
      check(obj.collidable == (collidable && retained), "authored extension keeps collision while stock extension remains culled");
      check(obj.visible == (visible && retained), "authored extension preserves visible and hidden decoration flags");
      const float shift = (display - 650) * 0.5f;
      check(std::fabs(obj.x0 - source.x0 - shift) < 0.001f && std::fabs(obj.x1 - source.x1 - shift) < 0.001f && obj.y0 == source.y0 && obj.y1 == source.y1, "authored geometry translates without stretching");
      check(node->flag("visible") == visible && node->flag("collidable") == collidable && node->f("x") == x, "board widening never mutates the authored node");
    }
  }
  std::cout << "AUTHORED_WIDTH checks=" << checks << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
