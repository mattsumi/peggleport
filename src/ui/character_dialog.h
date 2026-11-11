#pragma once
#include <vector>
#include <string>
#include "masters.h"
#include "ui/dialog.h"

namespace pp::ui {

class CharacterDialog : public Dialog {
 public:
  CharacterDialog(App& a, bool twoPlayers);
  void drawLayer(Gfx& g) override;
  bool input(const Input& in) override;
  float preferredHeight(float width) override;
  void accept();
  void place();
  static int randomChoice(Game g) { return g == Game::Nights ? 11 : 10; }
  static int mastersMet(Game g, const PlayerInfo* p);
  static int masterMetAt(Game g, int k);
  struct Info { std::string name, power, desc; };
  std::vector<Info> info;
  bool two = true;
  int players() const { return two ? 2 : 1; }
  int hover = -1;
  bool cellAt(float lx, float ly, int* player, int* idx) const;
  void cellRect(int player, int idx, float* x, float* y, float* w, float* h) const;
  int cellCount() const;
  int cellMaster(int idx) const;
  bool cellRandom(int idx) const;
  bool cellOpen(int idx) const;
  int sel[2] = {0, 1};
  static constexpr int kModCell = 1000;
  std::vector<ModMaster> mods;
  int modSel[2] = {-1, -1};
  bool picked[2] = {false, false};
  int page = 0;
  int pageCount() const { return 1 + ((int)mods.size() + 9) / 10; }
  void pagerRect(int player, float* x, float* y, float* w, float* h) const;
  bool onPager(float lx, float ly) const;
  int count = 10;
  bool computer = true;
  int skill = 1;
  Checkbox *cbFriend = nullptr, *cbComputer = nullptr, *cbNormal = nullptr, *cbMaster = nullptr;
  void checkboxChanged(int id, bool on) override;
  void layoutBoxes();

 private:
  float gridX(int player) const;
  float gridY() const;
  void drawCell(Gfx& g, int player, int idx);
  void drawInfo(Gfx& g);
};

}
