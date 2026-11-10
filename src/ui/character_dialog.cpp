#include "ui/character_dialog.h"

#include <algorithm>
#include <utility>
#include <memory>
#include <string>
#include <vector>

namespace pp::ui {

static const float kW = 520, kGridX[2] = {30, 360}, kGridY = 105, kCellW = 61, kCellH = 62, kPitchX = 71, kPitchY = 72, kRandW = 130, kRandH = 25;
static const float kInfoY = 72, kInfoW = 184, kInfoBottom = 394;

static std::string trimQ(std::string s) {
  size_t a = s.find_first_not_of(" \t\r\n,"), b = s.find_last_not_of(" \t\r\n,");
  if (a == std::string::npos) return "";
  s = s.substr(a, b - a + 1);
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
  return s;
}

CharacterDialog::CharacterDialog(App& a) : Dialog(a, D_Character, true, "Two Player Selection", "", "Play Now!", BUTTONS_FOOTER) {
  if (a.curProfile) {
    computer = a.curProfile->computerPlayer;
    skill = a.curProfile->computerSkill == 2 ? 2 : 1;
  }
  auto mk = [&](int id, const char* label, bool v) {
    auto c = std::make_unique<Checkbox>();
    c->id = id;
    c->image = a.gfx.img("IMAGE_DLG_BUTTON2");
    c->style = 1;
    c->label = label;
    c->checked = v;
    c->labelX = 30;
    c->w = a.gfx.textWidth("BUTTON", label) + 36;
    c->h = c->image ? c->image->lh() / 3 : 24;
    return add(std::move(c));
  };
  cbFriend = mk(0, "Play a Friend", !computer);
  cbComputer = mk(1, "Play Computer", computer);
  cbNormal = mk(2, "Normal", skill != 2);
  cbMaster = mk(3, "Master", skill == 2);
  count = a.game == Game::Nights ? 11 : 10;
  if (a.curProfile) {
    sel[0] = std::clamp(a.curProfile->char1, 0, 10);
    sel[1] = std::clamp(a.curProfile->char2, 0, 10);
  }
  if (auto h = a.vfs.read("characters/characters.cfg")) {
    std::string text(h->bytes.begin(), h->bytes.end()), line;
    size_t p = 0;
    while (p < text.size()) {
      size_t e = text.find('\n', p);
      line = text.substr(p, e == std::string::npos ? std::string::npos : e - p);
      p = e == std::string::npos ? text.size() : e + 1;
      size_t s = line.find_first_not_of(" \t\r");
      if (s == std::string::npos) continue;
      line = line.substr(s);
      if (line.compare(0, 10, "Character ") == 0) info.push_back({trimQ(line.substr(10)), "", ""});
      else if (!info.empty() && line.compare(0, 8, "Powerup:") == 0) info.back().power = trimQ(line.substr(8));
      else if (!info.empty() && line.compare(0, 5, "Desc:") == 0) info.back().desc = trimQ(line.substr(5));
    }
  }
  mods = masters::all();
  if (a.curProfile && !mods.empty())
    for (int r = 0; r < 2; r++) {
      const std::string key = a.profiles.modMaster(*a.curProfile, r);
      for (size_t k = 0; k < mods.size(); k++)
        if (!key.empty() && mods[k].key() == key) modSel[r] = (int)k;
    }
  w = kW;
  layoutBoxes();
}

float CharacterDialog::preferredHeight(float) { return app.game == Game::Nights ? 590.0f : 572.0f; }

void CharacterDialog::layoutBoxes() {
  float x = kW / 2 - 92, y = 399;
  cbFriend->x = x; cbFriend->y = y;
  cbComputer->x = x; cbComputer->y = y + 28;
  cbNormal->x = x + 14; cbNormal->y = y + 56;
  cbMaster->x = x + 14; cbMaster->y = y + 84;
  cbNormal->visible = cbMaster->visible = computer;
}

void CharacterDialog::checkboxChanged(int id, bool on) {
  if (id == 0 || id == 1) {
    computer = id == 1 ? on : !on;
    cbFriend->checked = !computer;
    cbComputer->checked = computer;
  } else if (id == 2 || id == 3) {
    skill = id == 3 ? (on ? 2 : 1) : (on ? 1 : 2);
    cbNormal->checked = skill != 2;
    cbMaster->checked = skill == 2;
  }
  layoutBoxes();
  app.playSample("BUTTON2");
}

void CharacterDialog::accept() {
  if (!app.curProfile) return;
  app.curProfile->char1 = sel[0];
  app.curProfile->char2 = sel[1];
  app.curProfile->computerPlayer = computer;
  app.curProfile->computerSkill = skill;
  app.curProfile->dirty = true;
  for (int r = 0; r < 2; r++)
    if (picked[r]) app.profiles.setModMaster(*app.curProfile, r, modSel[r] >= 0 ? mods[(size_t)modSel[r]].key() : std::string());
  if (!app.cfg.noSave) app.profiles.saveModMasters();
}

void CharacterDialog::pagerRect(int player, float* x, float* y, float* pw, float* ph) const {
  *x = kGridX[player] + kPitchX;
  *y = kGridY + 5 * kPitchY + (app.game == Game::Deluxe ? 0 : (kCellH - kRandH) / 2);
  *pw = kCellW;
  *ph = kRandH;
}

bool CharacterDialog::onPager(float lx, float ly) const {
  if (mods.empty()) return false;
  for (int r = 0; r < 2; r++) {
    float x, y, pw, ph;
    pagerRect(r, &x, &y, &pw, &ph);
    if (lx >= x && lx < x + pw && ly >= y && ly < y + ph) return true;
  }
  return false;
}

void CharacterDialog::cellRect(int player, int idx, float* x, float* y, float* cw, float* ch) const {
  if (idx >= kModCell) idx = (idx - kModCell) % 10;
  else if (idx == 10 && app.game == Game::Deluxe) {
    *x = kGridX[player]; *y = kGridY + 5 * kPitchY; *cw = mods.empty() ? kRandW : kCellW; *ch = kRandH;
    return;
  }
  *x = kGridX[player] + (idx % 2) * kPitchX;
  *y = kGridY + (idx / 2) * kPitchY;
  *cw = kCellW; *ch = kCellH;
}

bool CharacterDialog::cellAt(float lx, float ly, int* player, int* idx) const {
  const int n = page == 0 ? (app.game == Game::Deluxe ? 11 : count) : std::min(10, (int)mods.size() - (page - 1) * 10);
  for (int r = 0; r < 2; r++)
    for (int i = 0; i < n; i++) {
      const int id = page == 0 ? i : kModCell + (page - 1) * 10 + i;
      float x, y, cw, ch;
      cellRect(r, id, &x, &y, &cw, &ch);
      if (lx >= x && lx < x + cw && ly >= y && ly < y + ch) { *player = r; *idx = id; return true; }
    }
  return false;
}

static void drawModIcon(Gfx& g, const ModMaster& m, float x, float y, float w, float h, bool big) {
  if (!m.portraitFile.empty())
    if (Tex* t = g.imgFile(m.portraitFile)) {
      const float k = std::min(w / t->lw(), h / t->lh());
      const float dw = t->lw() * k, dh = t->lh() * k;
      g.drawStretch(t, x + (w - dw) / 2, y + (h - dh) / 2, dw, dh);
      return;
    }
  const uint32_t c = m.color;
  const uint32_t dark = ((((c >> 16) & 255) / 2) << 16) | ((((c >> 8) & 255) / 2) << 8) | ((c & 255) / 2);
  g.fill(x, y, w, h, dark);
  g.frame(x, y, w, h, c);
  g.frame(x + 3, y + 3, w - 6, h - 6, c, 0.6f);
  const char* font = big ? "OVERLOAD24GRAD1" : "OVERLOAD16OUTLINE";
  g.drawString(font, x + w / 2, y + h / 2 + g.fontAscent(font) / 2 - 2, masters::initial(m), 0xffffff, 1, Align::Center);
}

void CharacterDialog::drawLayer(Gfx& g) {
  layoutBoxes();
  Dialog::drawLayer(g);
  Tex* chars = g.img("IMAGE_CHR_CHARACTERS");
  Tex* shadows = g.img("IMAGE_CHR_CHARACTERSHADOWS");
  Tex* frame = g.img("IMAGE_DLG_BLUEFRAME");
  Tex* selFrame = g.img("IMAGE_DLG_SELECTORFRAME");
  const int unlocked = app.curProfile ? app.curProfile->maxStage : 0;
  const bool deluxe = app.game == Game::Deluxe;
  const char* kRow[2] = {"Player 1", computer ? "Computer" : "Player 2"};
  const int n = deluxe ? 11 : count;
  for (int r = 0; r < 2; r++) {
    g.drawString("PLAIN2", kGridX[r] + (kPitchX + kCellW) / 2, kGridY - 14, kRow[r], 0xffff80, 1, Align::Center);
    if (page > 0) {
      const int first = (page - 1) * 10, m = std::min(10, (int)mods.size() - first);
      for (int k = 0; k < m; k++) {
        const int id = kModCell + first + k;
        float cx, cy, cw, ch;
        cellRect(r, id, &cx, &cy, &cw, &ch);
        drawModIcon(g, mods[(size_t)(first + k)], cx, cy, cw, ch, false);
        const bool isSel = modSel[r] == first + k;
        if (isSel || id == hover) {
          if (Tex* fr = isSel ? selFrame : frame) g.drawBox(fr, cx - 4, cy - 4, cw + 8, ch + 8);
        }
      }
    } else {
      for (int i = 0; i < n; i++) {
        float cx, cy, cw, ch;
        cellRect(r, i, &cx, &cy, &cw, &ch);
        bool open = i < 10 ? i <= unlocked : (deluxe || unlocked >= 10 || (app.curProfile && app.curProfile->advCompletions > 0));
        if (i == 10 && deluxe) {
          if (frame) g.drawBox(frame, cx, cy, cw, ch);
          g.drawString("PLAIN2", cx + cw / 2, cy + ch / 2 + g.fontAscent("PLAIN2") / 2 - 2, "Random", 0xffffff, 1, Align::Center);
        } else if (i < 10) {
          Tex* t = open ? chars : shadows;
          if (t) g.drawCel(t, cx, cy, i, 0);
          if (!open && chars) g.fill(cx, cy, chars->cw(), chars->ch(), 0x000000, 110 / 255.0f);
        } else if (Tex* q = g.img("IMAGE_DLG_QUESTIONMARK")) {
          g.draw(q, cx, cy);
        }
        if ((i == sel[r] && modSel[r] < 0) || (open && i == hover)) {
          if (Tex* fr = (i == sel[r] && modSel[r] < 0) ? selFrame : frame) g.drawBox(fr, cx - 4, cy - 4, cw + 8, ch + 8);
        }
      }
    }
    if (!mods.empty()) {
      float px, py, pw, ph;
      pagerRect(r, &px, &py, &pw, &ph);
      if (frame) g.drawBox(frame, px, py, pw, ph);
      const int next = (page + 1) % pageCount();
      const char* label = next == 0 ? "< Back" : page == 0 ? "Mods >" : "More >";
      g.drawString("PLAIN2", px + pw / 2, py + ph / 2 + g.fontAscent("PLAIN2") / 2 - 2, label, 0xffffff, 1, Align::Center);
    }
  }
  const int selCell0 = modSel[0] >= 0 ? kModCell + modSel[0] : sel[0];
  int who = hover >= 0 ? hover : selCell0;
  float ix = (kW - kInfoW) / 2;
  if (Tex* fr = g.img("IMAGE_DLG_BLUEFRAME")) g.drawBox(fr, ix - 5, kInfoY - 5, kInfoW + 10, kInfoW + 10);
  std::string name, power, desc, powerDesc;
  if (who >= kModCell) {
    const size_t k = (size_t)(who - kModCell);
    if (k < mods.size()) {
      drawModIcon(g, mods[k], ix, kInfoY, kInfoW, kInfoW, true);
      name = mods[k].name;
      power = mods[k].powerName;
      desc = mods[k].description;
      powerDesc = mods[k].powerDescription;
    }
  } else {
    Tex* big = g.img("IMAGE_CHR_CHARACTER" + std::to_string(who + 1));
    if (big) g.draw(big, ix + (kInfoW - big->lw()) / 2, kInfoY);
    if (who == 10 && deluxe) {
      name = "Random";
      desc = "Choose a random character.";
    } else if (who < (int)info.size()) {
      name = info[who].name;
      desc = info[who].desc;
      static const char* kPower[10] = {"Super Guide", "Multiball", "Pyramid", "Space Blast", "Flippers", "Spooky Ball", "Flower Power", "Lucky Spin", "Fireball", "Zen Ball"};
      power = who < 10 && deluxe ? kPower[who] : info[who].power;
    }
  }
  float ty = kInfoY + kInfoW + 25;
  g.drawString("OVERLOAD16OUTLINE", kW / 2, ty + g.fontAscent("OVERLOAD16OUTLINE"), name, 0xffffff, 1, Align::Center);
  ty += g.fontHeight("OVERLOAD16OUTLINE") + 2;
  if (!power.empty()) {
    g.drawString("PLAIN2", kW / 2, ty + g.fontAscent("PLAIN2"), power, 0x88ff88, 1, Align::Center);
    ty += g.fontHeight("PLAIN2") + 4;
  }
  if (who < kModCell) {
    g.drawWrapped("PLAIN", {ix + 4, ty, kInfoW - 8, 60}, desc, 0xffffff, 1, Align::Center);
    return;
  }
  std::vector<std::pair<std::string, uint32_t>> lines;
  for (auto& l : g.wrap("PLAIN", kInfoW - 8, desc)) lines.push_back({l, 0xffffff});
  for (auto& l : g.wrap("PLAIN", kInfoW - 8, powerDesc)) lines.push_back({l, 0xb0ffb0});
  const float ls = g.fontLineSpacing("PLAIN");
  const int fit = ls > 0 ? std::max(1, (int)((kInfoBottom - ty) / ls)) : 4;
  for (int i = 0; i < (int)lines.size() && i < fit; i++) {
    std::string l = lines[(size_t)i].first;
    if (i == fit - 1 && (int)lines.size() > fit) l += "...";
    g.drawString("PLAIN", kW / 2, ty + g.fontAscent("PLAIN") + ls * (float)i, l, lines[(size_t)i].second, 1, Align::Center);
  }
}

bool CharacterDialog::input(const Input& in) {
  if (in.type == Input::MouseMove) {
    int p, i;
    hover = cellAt(in.x - x, in.y - y, &p, &i) ? i : -1;
  } else if (in.type == Input::MouseDown && in.button == 1) {
    int r, i;
    if (onPager(in.x - x, in.y - y)) {
      page = (page + 1) % pageCount();
      hover = -1;
      app.playSample("BUTTON1");
      return true;
    }
    if (cellAt(in.x - x, in.y - y, &r, &i)) {
      if (i >= kModCell) {
        modSel[r] = i - kModCell;
        picked[r] = true;
        app.playSample("BUTTON1");
        return true;
      }
      const int unlocked = app.curProfile ? app.curProfile->maxStage : 0;
      bool open = i < 10 ? i <= unlocked : (app.game == Game::Deluxe || unlocked >= 10 || (app.curProfile && app.curProfile->advCompletions > 0));
      if (open) { sel[r] = i; modSel[r] = -1; picked[r] = true; app.playSample("BUTTON1"); }
      return true;
    }
  }
  return Dialog::input(in);
}

}
