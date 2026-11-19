#include "editor/editor.h"
#include "ui/cfg.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace pp;
using namespace pp::ed;
namespace fs = std::filesystem;

static int failures;
static void check(bool ok, const std::string& message) {
  std::cout << (ok ? "PASS " : "FAIL ") << message << '\n';
  failures += !ok;
}
static void put(const fs::path& p, const std::string& bytes) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f.exceptions(std::ios::badbit | std::ios::failbit);
  f << bytes;
}
static std::string get(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) throw std::runtime_error(p.string());
  return {std::istreambuf_iterator<char>(f), {}};
}
static auto snapshot(const fs::path& p) {
  std::map<fs::path, std::string> files;
  for (const auto& f : fs::recursive_directory_iterator(p))
    if (f.is_regular_file()) files[f.path().lexically_relative(p)] = get(f.path());
  return files;
}
static std::string pak(const std::map<std::string, std::string>& files) {
  std::string out;
  auto word = [&](uint32_t n) {
    for (int i = 0; i < 4; ++i) out += char(n >> (8 * i));
  };
  word(0xbac04ac0);
  word(0);
  for (const auto& [name, data] : files) {
    out += '\0';
    out += char(name.size());
    out += name;
    word(uint32_t(data.size()));
    word(0);
    word(0);
  }
  out += char(0x80);
  for (const auto& [name, data] : files) out += data;
  for (char& c : out) c ^= char(0xf7);
  return out;
}

using Fields = std::map<std::string, std::vector<std::vector<std::string>>>;
static Fields fields(const fs::path& p) {
  Fields result;
  const auto groups = ui::cfgParse(get(p));
  if (groups.size() == 1 && groups.front().isGroup)
    for (const auto& row : groups.front().kids) result[row.key].push_back(ui::cfgValues(row.label));
  return result;
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const auto root = fs::path(argv[1]) / "editor_stage_fixture";
  fs::remove_all(root);
  Editor fixture;
  fixture.place(Tool::PegBlue, {220, 200});
  const auto bytes = writeLevel(*fixture.level);
  const std::string board(bytes.begin(), bytes.end());
  const fs::path idx = "levels/mod_levels.cfg", dat = "levels/Board.dat", ini = "levels/Board.ini";
  const std::string old = "Level: Board, \"Old\", 54321, 8";
  const std::string index = "// campaign\r\nStage\r\n{\r\n Name: Prelude\r\n Level: Far, Other\r\n}\r\nStage\r\n{\r\n Name: Bonus\r\n Desc: Final stage\r\n Level: Near, Earlier\r\n Dialog: 1, Not ours\r\n Dialog: 2, Too early\r\n " + old + "\r\n Dialog: 2, \"End, now\", Caption, 7\r\n Level: Last, Final\r\n Dialog: 3, Not ours either\r\n}\r\n";
  std::string err;
  auto setup = [&](const std::string& name, const std::string& cfg, bool linked) {
    auto p = root / name;
    put(p / idx, cfg);
    put(p / dat, board);
    put(p / ini, (linked ? std::string("base = _donor\n") : "") + "title = Sidecar\n");
    put(p / "levels/_base.dat", board);
    if (linked) put(p / "levels/_donor.dat", board);
    put(p / "mod.ini", "id = " + name + "\n");
    return p;
  };
  auto open = [&](Editor& e, Vfs& v, const fs::path& pack) {
    if (!v.mountMod(pack, &err)) throw std::runtime_error(err);
    e.setVfs(&v);
    if (!e.loadFromVfs(v, "Board", &err)) throw std::runtime_error(err);
  };
  const Fields originalBoard = {{"Name", {{"Bonus"}}}, {"Desc", {{"Final stage"}}}, {"Level", {{"Board", "Old", "54321", "8"}}}, {"Dialog", {{"1", "End, now", "Caption", "7"}}}};
  for (bool main : {false, true}) {
    auto game = root / (main ? "main" : "levelpack");
    std::map<std::string, std::string> files = {{"levels/_base.dat", board}};
    if (main) { files["levels/Board.dat"] = board; files["levels/stages.cfg"] = index; }
    else put(game / "levelpacks/bonus.pak", pak({{"levels/Board.dat", board}, {"bonus.cfg", index}}));
    put(game / "main.pak", pak(files));
    Vfs v;
    Editor e;
    if (!v.mountGame(game, &err)) throw std::runtime_error(err);
    e.setVfs(&v);
    check(e.loadFromVfs(v, "Board", &err) && e.saveToMod(game / "copy", &err) && fields(game / "copy" / idx) == originalBoard, main ? "main stage single-board export" : "level pack single-board export");
  }
  for (bool linked : {false, true}) {
    const std::string label = linked ? "linked" : "stock";
    const auto pack = setup(label, index, linked);
    Editor e;
    Vfs v;
    open(e, v, pack);
    {
      Editor disk, imported;
      imported.setVfs(&v);
      disk.setVfs(&v);
      const auto file = root / (label + "-source.json"), fresh = root / (label + "-source");
      check(disk.loadFromVfs(v, (pack / dat).string(), &err) && disk.saveJson(file, &err) && imported.loadJson(file, &err) && imported.saveToMod(fresh, &err) && fields(fresh / idx) == originalBoard && readIni(get(fresh / ini))["title"] == "Sidecar", label + " disk open to JSON before any mod save retains distinct titles and selected settings");
    }
    e.place(Tool::PegOrange, {340, 200});
    check(e.saveToMod(pack, &err) && get(pack / idx) == index && !e.dirty(), label + " geometry preserves authored bytes despite different sidecar title");
    e.setMeta("title", "Edit Two");
    e.setMeta("balls", "12");
    e.place(Tool::PegBlue, {390, 250});
    auto expected = index;
    expected.replace(expected.find("\"Old\""), 5, "\"Edit Two\"");
    bool ok = false;
    e.exec("save", &ok);
    LevelBundle reopened;
    check(ok && !e.dirty() && get(pack / idx) == expected && loadLevelBundle(v, "Board", reopened, &err) && reopened.title == "Edit Two" && reopened.cfg.balls == 12 && reopened.level->entries.size() == 3, label + " ordinary Save persists selected stage title, sidecar and geometry");
    for (bool json : {false, true}) {
      Editor imported;
      imported.setVfs(&v);
      auto file = root / (label + ".json");
      if (json && (!e.saveJson(file, &err) || !imported.loadJson(file, &err))) throw std::runtime_error(err);
      Editor& selected = json ? imported : e;
      selected.setMeta("name", "Copy");
      auto fresh = root / (label + (json ? "Json" : "Fresh"));
      const Fields want = {{"Name", {{"Bonus"}}}, {"Desc", {{"Final stage"}}}, {"Level", {{"Copy", "Edit Two", "54321", "8"}}}, {"Dialog", {{"1", "End, now", "Caption", "7"}}}};
      check(selected.saveToMod(fresh, &err) && fields(fresh / idx) == want && !fs::exists(fresh / "levels/Near.dat"), label + (json ? " JSON" : " direct") + " exports only owned stage fields, row and renumbered dialog");
    }
  }
  for (const std::string shape : {"simple", "comment", "duplicate"}) {
    auto input = index;
    auto pos = input.find(old);
    if (shape == "comment") input.insert(pos, "/* old " + old + " */ ");
    if (shape == "duplicate") input.insert(pos, old + "\r\n ");
    const auto pack = setup(shape, input, true);
    Editor e;
    Vfs v;
    open(e, v, pack);
    e.place(Tool::PegBlue, {310, 210});
    e.setMeta("title", "New, title");
    const auto depth = e.undoDepth();
    const auto before = snapshot(pack);
    err.clear();
    auto expected = input;
    expected.replace(expected.rfind("\"Old\""), 5, "\"New, title\"");
    const bool saved = e.saveToMod(pack, &err);
    const bool refused = !saved && !err.empty() && e.dirty() && e.metaDirty && e.undoDepth() == depth && e.savedPath.empty() && snapshot(pack) == before;
    check((shape != "duplicate" && saved && get(pack / idx) == expected) || (shape != "simple" && refused), shape + " title edit touches only verified live span or refuses ambiguity");
  }
  const std::string generated = "Stage\n{\nName: \"Tuned\"\nDesc: \"Levels made with the PegglePort level editor\"\n\t" + old + "\n}\n";
  const auto tuned = setup("Tuned", generated, true);
  Editor e;
  Vfs v;
  open(e, v, tuned);
  e.place(Tool::PegOrange, {410, 220});
  check(e.saveToMod(tuned, &err) && get(tuned / idx) == generated, "editor headings do not authorize resetting a tuned row");
  e.setMeta("title", "Pending");
  e.place(Tool::PegBlue, {330, 280});
  fs::rename(tuned / idx, tuned / "backup.cfg");
  fs::create_directory(tuned / idx);
  auto before = snapshot(tuned);
  auto depth = e.undoDepth();
  bool ok = false;
  auto message = e.exec("save", &ok);
  check(!ok && !message.empty() && e.dirty() && e.metaDirty && e.undoDepth() == depth && e.savedPath == tuned / dat && snapshot(tuned) == before, "ordinary Save failure does not publish the DAT pair or clear the edit");
  fs::remove(tuned / idx);
  fs::rename(tuned / "backup.cfg", tuned / idx);
  e.exec("save", &ok);
  check(ok && !e.dirty() && fields(tuned / idx)["Level"].front() == std::vector<std::string>{"Board", "Pending", "54321", "8"}, "ordinary retry updates the original mod");
  const auto guarded = setup("Guarded", index, false);
  Editor renamed;
  Vfs mount;
  open(renamed, mount, guarded);
  renamed.setMeta("name", "Near");
  renamed.setMeta("title", "Wrong row");
  const auto untouched = snapshot(guarded);
  err.clear();
  check(!renamed.saveToMod(guarded, &err) && !err.empty() && snapshot(guarded) == untouched && renamed.dirty(), "renaming onto a neighboring registered board is refused");
  auto spaces = index;
  spaces.replace(spaces.find(old), old.size(), "Level: \" Board \", Old, 54321, 8");
  const auto padded = setup("Padded", spaces, false);
  Editor fromPadded;
  Vfs pv;
  open(fromPadded, pv, padded);
  check(fromPadded.saveToMod(root / "PaddedCopy", &err) && fields(root / "PaddedCopy" / idx) == originalBoard, "stage lookup trims the file field as Quick Play does");
  const auto exchange = root / "ChangedStage.json";
  Editor source, changed;
  source.setVfs(&pv);
  changed.setVfs(&pv);
  if (!source.loadFromVfs(pv, "Board", &err) || !source.saveJson(exchange, &err)) throw std::runtime_error(err);
  auto json = get(exchange);
  json.replace(json.find("54321"), 5, "54322");
  put(exchange, json);
  if (!changed.loadJson(exchange, &err)) throw std::runtime_error(err);
  changed.place(Tool::PegBlue, {330, 280});
  const auto prior = snapshot(padded);
  err.clear();
  check(!changed.saveToMod(padded, &err) && !err.empty() && changed.dirty() && snapshot(padded) == prior, "conflicting JSON stage settings are not discarded to overwrite a registered board");
  auto editedBoard = originalBoard;
  editedBoard["Level"][0][2] = "54322";
  check(changed.saveToMod(root / "ChangedCopy", &err) && fields(root / "ChangedCopy" / idx) == editedBoard, "fresh export keeps the JSON row setting after a refused overwrite");
  return failures ? 1 : 0;
}
