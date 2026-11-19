#include "editor/editor.h"
#include "ui/cfg.h"

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace pp;
using namespace pp::ed;
namespace fs = std::filesystem;

static int failures;
static bool check(bool ok, const std::string& label) {
  std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
  if (!ok) ++failures;
  return ok;
}
static std::string get(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) throw std::runtime_error("cannot read " + p.string());
  return {std::istreambuf_iterator<char>(in), {}};
}
static void put(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary);
  out.exceptions(std::ios::failbit | std::ios::badbit);
  out << text;
}
static std::string geometry(float x) {
  Editor fixture;
  fixture.place(Tool::PegBlue, {x, 180});
  const auto b = writeLevel(*fixture.level);
  return {b.begin(), b.end()};
}
static bool sameBase(const LevelBundle& b, const std::string& bytes) {
  if (!b.base) return false;
  auto actual = writeLevel(*b.base);
  return std::string(actual.begin(), actual.end()) == bytes;
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const auto root = fs::path(argv[1]) / "editor_export_fixture";
  fs::remove_all(root);
  const auto host = root / "host", donor = root / "donor", other = root / "other", output = root / "export";
  const auto stock = geometry(100), linked = geometry(310), foreign = geometry(520);
  const std::string base = "levels/_pack_base.dat", id = "Board", key = "levels/Board", idx = "levels/mod_levels.cfg";
  const std::string index = "Stage\r\n{\r\n  Name: \"Campaign\"\r\n  Level: Neighbor, First\r\n}\r\nStage\r\n{\r\n  Name: \"Bonus\"\r\n  Desc: Final stage\r\n  Level: Board, \"Old title\", 54321, 8\r\n  Dialog: 1, Epilogue\r\n}\r\n";
  std::string expected = index;
  expected.replace(expected.find("Old title"), 9, "Edited title");
  put(host / "levels/_base.dat", stock);
  put(other / base, foreign);
  put(other / "levels/OnlyOther.tga", "foreign sentinel");
  put(donor / base, linked);
  put(donor / (key + ".dat"), geometry(260));
  put(donor / (key + ".ini"), "base = _pack_base\nboard_width = 917\n");
  put(donor / idx, index);
  put(donor / "mod.ini", "id = donor\npriority = 2\n");
  Vfs v;
  Editor e;
  std::string err;
  if (!v.mountMod(host, &err) || !v.mountMod(donor, &err)) return 2;
  e.setVfs(&v);
  if (!e.loadFromVfs(v, id, &err)) return 2;
  Board board;
  LevelBundle preview;
  if (!check(e.buildBoard(board, preview, nullptr, 1, false, &err) && sameBase(preview, linked), "original editor test-play uses x=310, not x=100")) return 2;
  e.setMeta("title", "Edited title");
  e.place(Tool::PegOrange, {710, 200});
  if (!check(e.saveToMod(donor, &err), "actual in-place mod save: " + err)) return 1;
  auto reopen = [&](const fs::path& pack, bool withDonor, bool withForeign, const std::string& name, LevelBundle& b) {
    Vfs fresh;
    if (!fresh.mountMod(host, &err) || (withDonor && !fresh.mountMod(donor, &err)) || (withForeign && !fresh.mountMod(other, &err)) || !fresh.mountMod(pack, &err)) return false;
    return loadLevelBundle(fresh, name, b, &err);
  };
  LevelBundle after;
  check(reopen(donor, false, false, id, after) && sameBase(after, linked) && after.cfg.authoredWidth == 917 && after.title == "Edited title" && after.level->entries.size() == 2, "in-place reload retains x=310, width, title and edited geometry");
  check(get(donor / idx) == expected, "only selected title changes; both named stages, scores, dialogs and CRLF survive");
  e.setMeta("name", "Copy");
  e.setArt(Editor::ArtSlot::Background, "OnlyOther");
  check(v.mountMod(other, &err), "same-named foreign base and an art sentinel are mounted below the actual donor");
  if (!check(e.saveToMod(output, &err), "actual fresh mod save: " + err)) return 1;
  {
    auto stages = ui::cfgParse(get(output / idx));
    std::map<std::string, std::vector<std::string>> fields;
    if (stages.size() == 1) for (const auto& row : stages.front().kids) fields[row.key] = ui::cfgValues(row.label);
    check(fields["Name"] == std::vector<std::string>{"Bonus"} && fields["Desc"] == std::vector<std::string>{"Final stage"} && !fields["Level"].empty() && fields["Level"][0] == "Copy", "new pack keeps the selected stage name and description, not neighboring boards");
  }
  check(reopen(output, true, false, "Copy", after) && sameBase(after, linked), "new export reload with donor retains custom geometry");
  check(!reopen(output, false, false, "Copy", after) && err.find("_pack_base") != err.npos, "absent linked pack fails by name, never uses stock");
  check(!reopen(output, false, true, "Copy", after) && err.find("_pack_base") != err.npos, "other edition's same-named x=520 base cannot substitute");
  auto snapshot = [&] {
    std::map<fs::path, std::string> tree;
    for (const auto& f : fs::recursive_directory_iterator(output)) if (f.is_regular_file()) tree.emplace(f.path().lexically_relative(output), get(f.path()));
    return tree;
  };
  const auto exchange = root / "exchange";
  fs::create_directory(exchange);
  for (const bool json : {false, true}) {
    const auto file = exchange / (json ? "Board.json" : "Board.dat");
    Editor opened;
    opened.setVfs(&v);
    const bool saved = json ? e.saveJson(file, &err) : e.saveDat(file, &err);
    const bool loaded = saved && (json ? opened.loadJson(file, &err) : opened.loadDatFile(file, &err));
    LevelBundle b;
    check(loaded && opened.buildBoard(board, b, nullptr, 1, false, &err) && sameBase(b, linked), json ? "JSON restores the linked preview" : "loose DAT restores the linked preview");
    opened.newLevel("Fresh");
    check(opened.buildBoard(board, b, nullptr, 1, false, &err) && sameBase(b, stock), "New does not inherit the previous donor geometry");
  }
  const auto path = e.savedPath;
  e.place(Tool::PegOrange, {590, 300});
  const auto depth = e.undoDepth();
  for (bool missing : {false, true}) {
    if (!missing) put(output / base, foreign);
    else { fs::remove(output / base); v.unmountMod(donor); v.unmountMod(other); }
    const auto files = snapshot();
    err.clear();
    const bool saved = e.saveToMod(output, &err);
    check(!saved && err.find("_pack_base") != err.npos && e.dirty() && e.savedPath == path && e.undoDepth() == depth && snapshot() == files, missing ? "missing source: clear failure, exact prior files and edit state" : "conflicting destination: clear failure, exact prior files and edit state");
  }
  check(!fs::exists(output / "levels/_base.dat") && !fs::exists(output / base) && !fs::exists(output / "levels/OnlyOther.tga") && get(host / "levels/_base.dat") == stock && get(other / base) == foreign, "linked export copies no base or foreign assets and leaves both editions alone");
  return failures ? 1 : 0;
}
