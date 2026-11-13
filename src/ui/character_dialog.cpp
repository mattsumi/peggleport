#include "ui/character_dialog.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <memory>
#include <string>
#include <vector>

namespace pp::ui {

static const float kW = 520, kGridX[2] = {30, 360}, kGridY = 105, kCellW = 61, kCellH = 62, kPitchX = 71, kPitchY = 72, kRandW = 130, kRandH = 25;
static const float kInfoW = 184;
static const float kTwoH = 525, kBoxX = 175, kBoxY = 110, kBoxW = 170, kBoxH = 34, kLevelDX = 20, kLevelW = 130, kLevelH = 27, kLevelGap = 5;
static const float kNameX[2] = {90, 425}, kNameY = 100, kJointDX = 8, kFootX = 172;
static const float kNightsPagerY = 420;
static const float kPanelX = 167, kPanelY = 297, kPanelW = 186, kPanelBottom = 410;
static const bool kInfoAlways = false;
static const float kSoloW = 400, kSoloGridY = 70, kSoloInfoX = 180, kSoloInfoY = 80, kSoloPagerGap = 9;
static const int kNightsOrder[12] = {0, 1, 5, 2, 3, 4, 6, 8, 7, 9, 10, 11};
static const char* kChosenSound[11] = {"POWERUP_GUIDE", "POWERUP_MULTIBALL", "POWERUP_PYRAMID", "POWERUP_SPACEBLAST", "POWERUP_FLIPPERS", "POWERUP_SPOOKYBALL",
                                       "POWERUP_FLOWERPOWER", "POWERUP_LUCKYSPIN", "POWERUP_FIREBALL", "POWERUP_ZEN", "POWERUP_LIGHTNING"};
static const char* kPower[11] = {"Super Guide", "Multiball", "Pyramid", "Space Blast", "Flippers", "Spooky Ball", "Flower Power", "Lucky Spin", "Fireball", "Zen Ball", "Electrobolt"};

static std::string trimQ(std::string s) {
  size_t a = s.find_first_not_of(" \t\r\n,"), b = s.find_last_not_of(" \t\r\n,;");
  if (a == std::string::npos) return "";
  s = s.substr(a, b - a + 1);
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
  return s;
}

namespace {
struct JointsWidget : Widget {
  Tex* art = nullptr;
  void draw(Gfx& g) override {
    if (!art) return;
    for (int i = 0; i < 3; i++) g.draw(art, 0, kLevelH * (float)i);
  }
};
}

int CharacterDialog::mastersMet(Game g, const PlayerInfo* p) {
  return std::clamp((p ? p->maxStage : 0) + 1, 1, g == Game::Nights ? 11 : 10);
}

int CharacterDialog::masterMetAt(Game g, int k) {
  return g == Game::Nights ? (k >= 0 && k < 12 ? kNightsOrder[k] : 0) : k;
}

CharacterDialog::CharacterDialog(App& a, bool twoPlayers)
    : Dialog(a, D_Character, true, twoPlayers ? "Two Player Selection" : "Choose Character", "", "Play Now!", BUTTONS_FOOTER), two(twoPlayers) {
  if (a.curProfile) {
    computer = a.curProfile->computerPlayer;
    const int stored = a.curProfile->computerSkill;
    skill = stored >= -1 && stored <= 1 ? stored : 2;
  }
  auto mk = [&](int id, const char* label, bool v, bool level) {
    auto c = std::make_unique<Checkbox>();
    c->id = id;
    c->image = a.gfx.img(level ? "IMAGE_DLG_BUTTON2" : "IMAGE_DLG_BUTTON1");
    c->style = level ? 1 : 2;
    c->label = label;
    c->checked = v;
    c->w = level ? kLevelW : kBoxW;
    c->h = level ? kLevelH : kBoxH;
    c->labelX = level ? (c->w - a.gfx.textWidth("BUTTON", label)) / 2 : 40;
    if (!level) c->labelY = 3;
    return add(std::move(c));
  };
  if (two) {
    auto j = std::make_unique<JointsWidget>();
    j->art = a.gfx.img("IMAGE_DLG_LJOINT");
    j->id = -1;
    j->mouseVisible = false;
    joints = add(std::move(j));
    cbFriend = mk(0, "Play a Friend", !computer, false);
    cbComputer = mk(1, "Play Computer", computer, false);
    static const char* kLevel[4] = {"Easy", "Normal", "Hard", "Master"};
    for (int i = 0; i < 4; i++) cbSkill[i] = mk(2 + i, kLevel[i], skill == i - 1, true);
  }
  count = a.game == Game::Nights ? 11 : 10;
  if (two && a.curProfile) {
    const int last = randomChoice(a.game);
    sel[0] = a.curProfile->char1 > last ? 0 : std::clamp(a.curProfile->char1, 0, last);
    sel[1] = a.curProfile->char2 > last ? 0 : std::clamp(a.curProfile->char2, 0, last);
  }
  if (!two) {
    sel[0] = 0;
    if (a.curProfile)
      for (int i = 0; i < cellCount(); i++)
        if (cellMaster(i) == a.curProfile->char1 && cellOpen(i)) sel[0] = cellMaster(i);
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
    for (int r = 0; r < players(); r++) {
      const std::string key = a.profiles.modMaster(*a.curProfile, r);
      for (size_t k = 0; k < mods.size(); k++)
        if (!key.empty() && mods[k].key() == key) modSel[r] = (int)k;
    }
  w = two ? kW : kSoloW;
  if (yesBtn)
    if (Tex* pb = a.gfx.img(two ? "IMAGE_DLG_PLAYBUTTON2" : "IMAGE_DLG_PLAYBUTTON1")) {
      yesBtn->image = pb;
      yesBtn->kind = Button::Kind::Embedded;
      yesBtn->labelOnEmbedded = true;
      yesBtn->textOffY = -4;
    }
  layoutBoxes();
}

float CharacterDialog::preferredHeight(float) {
  const bool nights = app.game == Game::Nights;
  if (two) return nights ? kTwoH + 40 : kTwoH;
  return (nights ? 520.0f : 480.0f) + (mods.empty() ? 0.0f : kRandH + kSoloPagerGap);
}

void CharacterDialog::place() {
  const float hgt = preferredHeight(w);
  const float top = two ? 40.0f : std::max(8.0f, std::min(60.0f, 596 - hgt));
  resize((800 - w) / 2, top, w, hgt);
  layoutBoxes();
}

float CharacterDialog::gridX(int player) const { return two ? kGridX[player] : kGridX[0]; }
float CharacterDialog::gridY() const { return two ? kGridY : kSoloGridY; }

int CharacterDialog::cellCount() const {
  return app.game == Game::Deluxe ? 11 : 12;
}

int CharacterDialog::cellMaster(int idx) const {
  return masterMetAt(app.game, idx);
}

bool CharacterDialog::cellRandom(int idx) const {
  return idx == randomChoice(app.game);
}

bool CharacterDialog::cellOpen(int idx) const {
  const PlayerInfo* p = app.curProfile;
  if (cellRandom(idx)) return true;
  if (!two) return idx < mastersMet(app.game, p);
  const int unlocked = p ? p->maxStage : 0;
  return idx < 10 ? idx <= unlocked : (unlocked >= 10 || (p && p->advCompletions > 0));
}

void CharacterDialog::layoutBoxes() {
  if (!two) {
    if (yesBtn && yesBtn->kind == Button::Kind::Embedded && yesBtn->image) {
      yesBtn->w = yesBtn->image->lw();
      yesBtn->h = yesBtn->image->lh() / 2;
      yesBtn->x = 217;
      yesBtn->y = h - 61;
    }
    return;
  }
  if (yesBtn && yesBtn->kind == Button::Kind::Embedded && yesBtn->image) {
    yesBtn->w = yesBtn->image->lw();
    yesBtn->h = yesBtn->image->lh() / 2;
    yesBtn->x = kFootX + 32;
    yesBtn->y = h - 112 + 24;
  }
  cbFriend->x = kBoxX; cbFriend->y = kBoxY;
  cbComputer->x = kBoxX; cbComputer->y = kBoxY + kBoxH;
  const float ly = kBoxY + 2 * kBoxH + kLevelGap;
  for (int i = 0; i < 4; i++) {
    cbSkill[i]->x = kBoxX + kLevelDX;
    cbSkill[i]->y = ly + kLevelH * (float)i;
    cbSkill[i]->visible = computer;
  }
  joints->x = kBoxX + kJointDX;
  joints->y = ly - 20.5f;
  joints->w = 21;
  joints->h = kLevelH * 3 + 15;
  joints->visible = computer;
}

void CharacterDialog::checkboxChanged(int id, bool on) {
  if (!two) return;
  if (id == 0 || id == 1) {
    computer = id == 1 ? on : !on;
    cbFriend->checked = !computer;
    cbComputer->checked = computer;
  } else if (id >= 2 && id <= 5) {
    skill = id - 3;
    for (int i = 0; i < 4; i++) cbSkill[i]->checked = i == id - 2;
  }
  layoutBoxes();
  app.playSample("BUTTON2");
}

void CharacterDialog::accept() {
  if (!app.curProfile) return;
  app.curProfile->char1 = sel[0];
  if (two) {
    app.curProfile->char2 = sel[1];
    app.curProfile->computerPlayer = computer;
    app.curProfile->computerSkill = skill;
  }
  app.curProfile->dirty = true;
  for (int r = 0; r < players(); r++)
    if (picked[r]) app.profiles.setModMaster(*app.curProfile, r, modSel[r] >= 0 ? mods[(size_t)modSel[r]].key() : std::string());
  if (!app.cfg.noSave) app.profiles.saveModMasters();
}

void CharacterDialog::pagerRect(int player, float* x, float* y, float* pw, float* ph) const {
  const bool deluxe = app.game == Game::Deluxe;
  if (!two) {
    *x = gridX(player);
    *y = gridY() + 5 * kPitchY + (deluxe ? kRandH : kCellH) + kSoloPagerGap;
    *pw = kRandW;
    *ph = kRandH;
    return;
  }
  if (!deluxe) {
    *x = kBoxX; *y = kNightsPagerY; *pw = kBoxW; *ph = kRandH;
    return;
  }
  *x = kGridX[player] + kPitchX;
  *y = kGridY + 5 * kPitchY;
  *pw = kCellW;
  *ph = kRandH;
}

bool CharacterDialog::onPager(float lx, float ly) const {
  if (mods.empty()) return false;
  for (int r = 0; r < players(); r++) {
    float x, y, pw, ph;
    pagerRect(r, &x, &y, &pw, &ph);
    if (lx >= x && lx < x + pw && ly >= y && ly < y + ph) return true;
  }
  return false;
}

void CharacterDialog::cellRect(int player, int idx, float* x, float* y, float* cw, float* ch) const {
  if (idx >= kModCell) idx = (idx - kModCell) % 10;
  else if (idx == 10 && app.game == Game::Deluxe) {
    *x = gridX(player); *y = gridY() + 5 * kPitchY; *cw = mods.empty() || !two ? kRandW : kCellW; *ch = kRandH;
    return;
  }
  *x = gridX(player) + (idx % 2) * kPitchX;
  *y = gridY() + (idx / 2) * kPitchY;
  *cw = kCellW; *ch = kCellH;
}

bool CharacterDialog::cellAt(float lx, float ly, int* player, int* idx) const {
  const int n = page == 0 ? cellCount() : std::min(10, (int)mods.size() - (page - 1) * 10);
  for (int r = 0; r < players(); r++)
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

bool CharacterDialog::randomChosen(int r) const {
  return two && modSel[r] < 0 && sel[r] == randomChoice(app.game);
}

void CharacterDialog::update() {
  Dialog::update();
  if (!two) return;
  const float mx = app.mouseX - (app.canvasW() - 800) * 0.5f - x, my = app.mouseY - y;
  for (int r = 0; r < 2; r++) {
    float x0, y0, w0, h0, x9, y9, w9, h9;
    cellRect(r, 0, &x0, &y0, &w0, &h0);
    cellRect(r, 9, &x9, &y9, &w9, &h9);
    if (mx >= x0 && mx < x9 + w9 && my >= y0 && my < y9 + h9) {
      fade[r] = (float)((double)fade[r] - (double)0.2f);
      fadeWait[r] = 20;
    } else if (fadeWait[r] > 0) {
      fadeWait[r]--;
    } else {
      fade[r] = (float)((double)fade[r] + (double)0.1f);
    }
    fade[r] = std::clamp(fade[r], 0.0f, 1.0f);
  }
}

void CharacterDialog::drawCell(Gfx& g, int r, int i) {
  Tex* chars = g.img("IMAGE_CHR_CHARACTERS");
  Tex* shadows = g.img("IMAGE_CHR_CHARACTERSHADOWS");
  Tex* frame = g.img("IMAGE_DLG_BLUEFRAME");
  Tex* selFrame = g.img("IMAGE_DLG_SELECTORFRAME");
  const bool open = cellOpen(i);
  const bool chosen = modSel[r] < 0 && cellMaster(i) == sel[r];
  float cx, cy, cw, ch;
  cellRect(r, i, &cx, &cy, &cw, &ch);
  const Mode look = chosen || (open && i == hover) ? Mode() : Mode(1.0f, 0x9b9b9b);
  if (two && app.game == Game::Deluxe && cellRandom(i) && !mods.empty()) {
    if (frame) g.drawBox(frame, cx, cy, cw, ch);
    g.drawString("PLAIN2", cx + cw / 2, cy + ch / 2 + g.fontAscent("PLAIN2") / 2 - 2, "Random", 0xffffff, 1, Align::Center);
    return;
  }
  if (cellRandom(i)) {
    if (Tex* t = g.img("IMAGE_DLG_RANDOM")) g.draw(t, cx, cy, look);
  } else if (Tex* t = open ? chars : shadows) {
    if (two && r == 1) g.drawCelMirror(t, cx, cy, cellMaster(i), 0, look);
    else g.drawCel(t, cx, cy, cellMaster(i), 0, look);
  }
  if (!open && !cellRandom(i))
    if (Tex* lock = g.img("IMAGE_LS_LOCK_SMALL")) g.draw(lock, cx, cy, Mode(60 / 255.0f));
  if (randomChosen(r) && !cellRandom(i)) {
    const int alpha = (int)((double)fade[r] * 255.0);
    if (alpha > 0)
      if (Tex* q = g.img("IMAGE_DLG_QUESTIONMARK")) g.draw(q, cx + std::floor((cw - q->lw()) / 2), cy + std::floor((ch - q->lh()) / 2), Mode(alpha / 255.0f));
  }
  if (Tex* fr = chosen ? selFrame : frame) g.drawBox(fr, cx - 8, cy - 8, cw + 14, ch + 14);
}

void CharacterDialog::drawLayer(Gfx& g) {
  layoutBoxes();
  Dialog::drawLayer(g);
  Tex* frame = g.img("IMAGE_DLG_BLUEFRAME");
  Tex* selFrame = g.img("IMAGE_DLG_SELECTORFRAME");
  const std::string kRow[2] = {app.curProfile && !app.curProfile->name.empty() ? app.curProfile->name : std::string("Player 1"), "Player 2"};
  for (int r = 0; r < players(); r++) {
    if (two) g.drawString("OVERLOAD16OUTLINE", kNameX[r], kNameY, kRow[r], 0xffffff, 1, Align::Center);
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
      for (int i = 0; i < cellCount(); i++) drawCell(g, r, i);
    }
    if (!mods.empty() && !(two && app.game == Game::Nights && r == 1)) {
      float px, py, pw, ph;
      pagerRect(r, &px, &py, &pw, &ph);
      if (frame) g.drawBox(frame, px, py, pw, ph);
      const int next = (page + 1) % pageCount();
      const char* label = next == 0 ? "< Back" : page == 0 ? "Mods >" : "More >";
      g.drawString("PLAIN2", px + pw / 2, py + ph / 2 + g.fontAscent("PLAIN2") / 2 - 2, label, 0xffffff, 1, Align::Center);
    }
  }
  drawInfo(g);
  if (two)
    if (Tex* pw = g.img("IMAGE_DLG_PLAYWIDGET2")) g.draw(pw, kFootX, h - pw->lh() - 6);
}

void CharacterDialog::drawInfo(Gfx& g) {
  const bool deluxe = app.game == Game::Deluxe;
  const int selCell0 = modSel[0] >= 0 ? kModCell + modSel[0] : sel[0];
  const int who = two && hover >= 0 ? hover : selCell0;
  const bool random = who < kModCell && (two ? who == 10 && deluxe : who == randomChoice(app.game));
  if (two && who < kModCell && !kInfoAlways) return;
  const float ix = two ? kPanelX : kSoloInfoX, iy = two ? kPanelY : kSoloInfoY;
  const float mid = ix + kInfoW / 2;
  if (two) {
    if (Tex* fr = g.img("IMAGE_DLG_BLUEFRAME")) g.drawBox(fr, ix - 4, iy - 4, kCellW + 8, kCellH + 8);
  } else {
    if (Tex* fr = g.img("IMAGE_DLG_SELECTORFRAME")) g.drawBox(fr, ix - 8, iy - 8, kInfoW + 16, kInfoW + 16);
  }
  std::string name, power, desc, powerDesc;
  if (who >= kModCell) {
    const size_t k = (size_t)(who - kModCell);
    if (k < mods.size()) {
      if (two) drawModIcon(g, mods[k], ix, iy, kCellW, kCellH, false);
      else drawModIcon(g, mods[k], ix, iy, kInfoW, kInfoW, true);
      name = mods[k].name;
      power = mods[k].powerName;
      desc = mods[k].description;
      powerDesc = mods[k].powerDescription;
    }
  } else {
    Tex* art = !two && random ? g.img("IMAGE_LS_RANDOMLEVEL") : nullptr;
    if (art) {
      const float sw = std::min(art->cw(), kInfoW);
      g.drawSrc(art, (art->cw() - sw) / 2, 0, sw, art->ch(), ix + (kInfoW - sw) / 2, iy);
    } else if (two) {
      if (who < 10)
        if (Tex* chars = g.img("IMAGE_CHR_CHARACTERS")) g.drawCel(chars, ix, iy, who, 0);
    } else if (Tex* big = g.img("IMAGE_CHR_CHARACTER" + std::to_string(who + 1))) {
      g.draw(big, ix + (kInfoW - big->lw()) / 2, iy);
    }
    if (random) {
      name = "Random";
      desc = "Choose a random character.";
    } else if (who < (int)info.size()) {
      name = info[who].name;
      desc = info[who].desc;
      power = who < 11 && (deluxe || !two) ? kPower[who] : info[who].power;
    }
  }
  if (!two) {
    const char* small = "OVERLOAD12OUTLINE";
    const float base = iy + kInfoW + 8 + 25;
    g.drawString("OVERLOAD16OUTLINE", mid, base, name, 0xffffff, 1, Align::Center);
    if (!power.empty()) g.drawString(small, mid, base + 10, power, 0xffffff, 1, Align::Center);
    std::vector<std::string> lines = g.wrap(small, kInfoW - 4, desc);
    for (auto& l : g.wrap(small, kInfoW - 4, powerDesc)) lines.push_back(l);
    const float ls = g.fontLineSpacing(small), top = 330;
    Tex* pw = g.img("IMAGE_DLG_PLAYWIDGET1");
    const float room = h - (pw ? pw->lh() : 76.0f) - 6 - top;
    const int fit = who < kModCell || ls <= 0 ? (int)lines.size() : std::max(1, (int)(room / ls));
    for (int i = 0; i < (int)lines.size() && i < fit; i++) {
      std::string l = lines[(size_t)i];
      if (i == fit - 1 && (int)lines.size() > fit) l += "...";
      g.drawString(small, mid, top + g.fontAscent(small) - g.fontAscentPadding(small) + ls * (float)i, l, 0xffffff, 1, Align::Center);
    }
    if (pw) g.draw(pw, 185, h - pw->lh() - 6);
    return;
  }
  const float tx = ix + kCellW + 10;
  g.drawString("OVERLOAD12OUTLINE", tx, iy + g.fontAscent("OVERLOAD12OUTLINE"), name, 0xffffff, 1);
  if (!power.empty()) g.drawString("PLAIN2", tx, iy + g.fontHeight("OVERLOAD12OUTLINE") + g.fontAscent("PLAIN2") + 2, power, 0x88ff88, 1);
  std::vector<std::pair<std::string, uint32_t>> lines;
  for (auto& l : g.wrap("PLAIN", kPanelW, desc)) lines.push_back({l, 0xffffff});
  for (auto& l : g.wrap("PLAIN", kPanelW, powerDesc)) lines.push_back({l, 0xb0ffb0});
  const float ls = g.fontLineSpacing("PLAIN"), ty = iy + kCellH + 8;
  const int fit = ls > 0 ? std::max(1, (int)((kPanelBottom - ty) / ls)) : 3;
  for (int i = 0; i < (int)lines.size() && i < fit; i++) {
    std::string l = lines[(size_t)i].first;
    if (i == fit - 1 && (int)lines.size() > fit) l += "...";
    g.drawString("PLAIN", ix, ty + g.fontAscent("PLAIN") + ls * (float)i, l, lines[(size_t)i].second, 1);
  }
}

bool CharacterDialog::input(const Input& in) {
  if (in.type == Input::KeyDown && in.key == SDLK_ESCAPE) return true;
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
      if (cellOpen(i)) {
        sel[r] = cellMaster(i);
        modSel[r] = -1;
        picked[r] = true;
        const char* pick = cellRandom(i) ? "LEVELDONE" : kChosenSound[std::clamp(sel[r], 0, 10)];
        if (app.audio && !pickSound.empty()) app.audio->stopSample(pickSound);
        app.playSample(pick);
        pickSound = pick;
        if (!two && in.clicks == 2) onButton(ID_FOOTER);
      }
      return true;
    }
  }
  return Dialog::input(in);
}

}
