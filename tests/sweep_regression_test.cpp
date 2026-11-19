#include "content.h"
#include "physball.h"

#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace pp;

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  int checks = 0, failures = 0;
  auto check = [&](bool pass, const std::string& name) {
    ++checks;
    if (!pass) { ++failures; std::cerr << "FAIL " << name << '\n'; }
  };
  auto level = std::make_shared<Level>();
  auto line = std::make_shared<Node>();
  line->type = 2;
  line->flags["visible"] = line->flags["collidable"] = true;
  line->setF("x1", 100);
  line->setF("x2", 100);
  line->setF("y1", 100);
  line->setF("y2", 300);
  auto orange = std::make_shared<Node>();
  orange->type = 5;
  orange->setF("x", 400);
  orange->setF("y", 400);
  orange->setF("radius", 10);
  auto peg = std::make_shared<Node>();
  peg->setF("type", 2);
  orange->fields["peg"].k = VK::Ptr;
  orange->fields["peg"].n = peg;
  for (const auto& node : {line, orange}) {
    Val value;
    value.k = VK::Ptr;
    value.n = node;
    level->entries.push_back(value);
  }
  BoardConfig cfg;
  cfg.greens = cfg.purple = false;
  std::string err;
  for (int side : {-1, 1}) for (float initial : {5.98f, 6.0f, 6.02f}) {
    Board board;
    if (!board.load(nullptr, level, cfg, &err) || !board.fire(4.712389f)) return 2;
    const std::vector<float> distances = {initial, 6.02f, 6.0f, 6.0f, 6.0f, 5.98f, 6.0f, 6.0f, 6.02f, 6.02f, 6.0f, 5.98f};
    for (size_t frame = 0; frame < distances.size(); ++frame) {
      if (board.balls.size() != 1 || board.phase != Phase::Shot) return 2;
      Ball& ball = board.balls[0];
      phys::ballSetPos(ball, 100 + side * distances[frame], 200);
      ball.vel = {0, -board.physics.gravityY};
      ball.stuck = 0;
      board.step();
      bool contact = false;
      for (auto [bi, oi] : board.contactNotes()) contact |= oi == 0;
      const std::string label = "side=" + std::to_string(side) + " initial=" + std::to_string(initial) + " frame=" + std::to_string(frame) + " distance=" + std::to_string(distances[frame]);
      check(contact == (distances[frame] <= 6), "separated/overlap/tangent transition " + label);
      check(!board.balls.empty() && std::isfinite(board.balls[0].pos.x) && std::isfinite(board.balls[0].pos.y), "finite transition " + label);
    }
  }
  struct Replay {
    const char* name;
    float angle;
    std::vector<std::pair<int, int>> contacts;
  };
  std::vector<Replay> replays = {{"TOTEMPOLE", 4.4f, {{621, 47}}}, {"GATEWAY", 5.0f, {}}, {"HOLES", 3.5f, {{878, 28}, {880, 28}, {882, 28}, {884, 28}}}};
  for (const auto& range : std::vector<std::array<int, 3>>{{520, 552, 45}, {554, 562, 37}, {570, 580, 38}, {582, 590, 39}, {592, 600, 40}, {602, 608, 41}, {610, 616, 42}, {618, 618, 43}})
    for (int tick = range[0]; tick <= range[1]; tick += 2) replays[1].contacts.push_back({tick, range[2]});
  Vfs v;
  if (!v.mountGame(argv[1], &err)) { std::cerr << err << '\n'; return 2; }
  int stockContacts = 0;
  for (const auto& replay : replays) {
    LevelBundle lb;
    if (!loadLevelBundle(v, replay.name, lb, &err)) return 2;
    lb.cfg.seed = 4357;
    lb.cfg.greens = lb.cfg.purple = false;
    Board board;
    if (!board.load(lb.base, lb.level, lb.cfg, &err)) return 2;
    board.emit = false;
    if (!board.fire(replay.angle)) return 2;
    Board repeat = board;
    bool deterministic = true, finite = true;
    size_t next = 0;
    while (board.tick < replay.contacts.back().first && board.phase == Phase::Shot) {
      board.step();
      repeat.step();
      deterministic &= board.hash() == repeat.hash();
      for (const auto& ball : board.balls) finite &= std::isfinite(ball.pos.x) && std::isfinite(ball.pos.y) && std::isfinite(ball.vel.x) && std::isfinite(ball.vel.y);
      if (next == replay.contacts.size() || board.tick != replay.contacts[next].first) continue;
      const int object = replay.contacts[next].second;
      bool contact = false;
      for (auto [bi, oi] : board.contactNotes()) contact |= oi == object;
      check(contact, std::string(replay.name) + " seed=4357 angle=" + std::to_string(replay.angle) + " tick=" + std::to_string(board.tick) + " object=" + std::to_string(object));
      if (std::string(replay.name) == "GATEWAY" && (board.tick == 554 || board.tick == 570 || board.tick == 582 || board.tick == 592))
        check(board.objs[object].lit, "GATEWAY tangent lights unlit peg on tick=" + std::to_string(board.tick));
      if (contact) ++stockContacts;
      ++next;
    }
    check(next == replay.contacts.size(), std::string(replay.name) + " reaches every saved contact tick");
    check(deterministic && finite, std::string(replay.name) + " finite deterministic replay");
  }
  check(stockContacts == 52, "all 52 saved stock tangent contacts");
  std::cout << "SWEEP_REGRESSION checks=" << checks << " failures=" << failures << " stock_contacts=" << stockContacts << "/52\n";
  return failures ? 1 : 0;
}
