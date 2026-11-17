#include "ui/slot_dialog.h"

#include <algorithm>
#include <cmath>

namespace pp::ui {

static const double kPi = 3.14159274101257;

void SpinWheel::calcLight() {
  float ang[4];
  for (int i = 0; i < 4; i++) ang[i] = (float)((45 + 90 * i) * kPi / 180.0);
  int l = 0;
  for (; l < 4; l++)
    if (angle < ang[l]) break;
  light = l % 4;
  int brightness = 30;
  if (phase != 0) {
    for (int i = 0; i < 40; i++) bright[i] = 0;
    if ((phase / 20) % 2 != 1) return;
    if (light > 0) {
      int s = light * 10 - 5;
      for (int i = s; i <= s + 10; i++) bright[i] = brightness;
    } else {
      for (int i = 0; i <= 5; i++) bright[i] = brightness;
      for (int i = 35; i < 40; i++) bright[i] = brightness;
    }
    return;
  }
  float off = (float)(-5 * kPi / 180.0);
  int o = (int)((angle - off) * 20.0 / kPi);
  if (o < 0) {
    int r = -o % 40;
    lit = r == 0 ? 0 : 40 - r;
  } else {
    lit = o % 40;
  }
  bright[lit] = 30;
}

bool SpinWheel::updatePointer(bool& ding) {
  bool stopping = false;
  int oldLit = lit;
  if (hold > 0) {
    hold--;
    stopping = true;
  } else {
    angle -= speed;
  }
  calcLight();
  if (angle > 6.2831854820251465) angle = (float)(angle - 6.2831854820251465);
  else if (angle < 0.0f) angle = (float)(angle + 6.2831854820251465);
  if (stopping) return false;
  if (timer > 0) timer--;
  if ((lit - 5) % 10 != 0) {
    if ((lit + 4) % 10 == 0 && oldLit != lit) {
      ding = true;
      dings = (dings + 1) % 4;
      if (timer == 0) speed = (float)((double)speed - 0.0016f);
    } else if (timer == 0) {
      speed = (float)((double)speed - 0.0008f);
    }
  }
  if (speed <= 0.0f) {
    speed = 0.0f;
    phase = 1;
    return true;
  }
  return false;
}

static const int kResults[4] = {2, 1, 0, 3};

SlotMachineDialog::SlotMachineDialog(App& a, int outcome, int power, int spinCode) : Dialog(a, D_Slot, true, "Lucky Spin!", "", "", BUTTONS_NONE) {
  result = outcome;
  switch (power) {
    case 1: cell = 0; break;
    case 2: cell = 4; break;
    case 3: cell = 1; break;
    case 4: cell = 2; break;
    case 5: cell = 3; break;
    case 6: cell = 5; break;
    case 7: cell = 8; break;
    case 9: cell = 6; break;
    case 12: cell = 7; break;
    default: cell = 0; break;
  }
  int wantLight = 0;
  for (int l = 0; l < 4; l++)
    if (kResults[l] == outcome) wantLight = l;
  if (spinCode > 0) {
    int sd = (spinCode - 1) % 360, t0 = (spinCode - 1) / 360;
    wheel = SpinWheel();
    wheel.angle = (float)(sd * kPi / 180.0);
    wheel.timer = t0;
    wheel.calcLight();
  } else {
  float start = (float)((a.rand() % 360) * kPi / 180.0);
  int timer0 = (int)(a.rand() % 100);
  for (int k = 0; k < 1440; k++) {
    SpinWheel t;
    t.angle = (float)(start + k * 0.25 * kPi / 180.0);
    if (t.angle > 6.2831854820251465f) t.angle = (float)(t.angle - 6.2831854820251465);
    t.timer = timer0;
    t.calcLight();
    bool ding = false;
    for (int i = 0; i < 20000; i++)
      if (t.updatePointer(ding)) break;
    if (t.phase > 0 && t.light == wantLight) {
      wheel = SpinWheel();
      wheel.angle = (float)(start + k * 0.25 * kPi / 180.0);
      if (wheel.angle > 6.2831854820251465f) wheel.angle = (float)(wheel.angle - 6.2831854820251465);
      wheel.timer = timer0;
      break;
    }
  }
  if (wheel.timer == 0 && wheel.angle == 0) { wheel.angle = start; wheel.timer = timer0; }
  wheel.calcLight();
  }
  Tex* sp = a.gfx.img("IMAGE_SPIN_SPINNER");
  float W = sp ? sp->lw() : 350, H = sp ? sp->lh() : 350;
  resize((800 - W) / 2, 0x8c, W, H);
  doScroll(true);
}

void SlotMachineDialog::skip() {
  if (wheel.phase == 0 && wheel.speed > 0) {
    bool ding = false;
    for (int i = 0; i < 20000 && wheel.speed > 0; i++) wheel.updatePointer(ding);
    for (int i = 0; i < 40; i++) wheel.bright[i] = 0;
  }
  if (wheel.phase > 0) playPrize();
}

void SlotMachineDialog::playPrize() {
  if (prizeDone) return;
  prizeDone = true;
  static const char* const kPrize[4] = {"TING", "FREEBALL", "AAH", "EXTRABALL"};
  if (app.audio && result >= 0 && result < 4) app.audio->play(kPrize[result], 0, 1, 1);
}

bool SlotMachineDialog::input(const Input& in) {
  if (in.type == Input::MouseDown && !app.boardPaused()) skip();
  return true;
}

void SlotMachineDialog::update() {
  if (app.boardPaused()) return;
  Dialog::update();
  if (exiting) {
    y += 40;
    if (y > 600) y = 600;
  }
  for (int i = 0; i < 40; i++)
    if (wheel.bright[i] != 0) wheel.bright[i]--;
  if (wheel.phase > 0) {
    wheel.phase++;
    if (wheel.phase == 120) {
      exiting = true;
      scrollActive = false;
      app.playSample("DIALOG_MOVE");
      wheel.calcLight();
    } else if (wheel.phase > 140) {
      gone = true;
    } else {
      wheel.calcLight();
    }
  } else {
    bool ding = false;
    const bool stopped = wheel.updatePointer(ding);
    if (ding) app.playSample("DING");
    if (stopped) playPrize();
  }
}

void SlotMachineDialog::drawLayer(Gfx& g) {
  if (Tex* sp = g.img("IMAGE_SPIN_SPINNER")) g.draw(sp, 0, 0);
  bool lightOn = !(wheel.phase > 0 && (wheel.phase / 20) % 2 == 0) && wheel.hold <= 0;
  Tex* randomText = g.img("IMAGE_SPIN_RANDOMTEXT");
  if (lightOn) {
    static const char* names[4] = {"IMAGE_SPIN_TRIPLEON", "IMAGE_SPIN_HATON", nullptr, "IMAGE_SPIN_EXTRAON"};
    static const float pos[4][2] = {{75, 19}, {19, 76}, {72, 212}, {206, 76}};
    int l = wheel.light;
    if (l == 2) {
      if (Tex* t = g.img("IMAGE_SPIN_RANDOMON")) g.draw(t, pos[2][0], pos[2][1]);
      randomText = g.img("IMAGE_SPIN_RANDOMTEXTON");
    } else if (Tex* t = g.img(names[l])) {
      g.draw(t, pos[l][0], pos[l][1]);
    }
  }
  if (randomText) g.drawCel(randomText, 95, 240, 0, cell);
  static const int P[40][2] = {{162, 4},  {138, 6},  {114, 12}, {91, 21},  {70, 34},  {48, 48},  {35, 69},  {22, 90},  {12, 113}, {7, 137},
                               {4, 162},  {7, 186},  {12, 211}, {22, 234}, {35, 255}, {48, 273}, {70, 290}, {91, 303}, {114, 312}, {137, 318},
                               {162, 320}, {187, 318}, {211, 312}, {234, 302}, {255, 290}, {273, 272}, {290, 255}, {303, 234}, {312, 211}, {318, 187},
                               {320, 162}, {318, 137}, {312, 113}, {303, 90},  {290, 69},  {274, 47},  {255, 34},  {234, 22},  {211, 12}, {187, 6}};
  Tex* rim = g.img("IMAGE_SPIN_RIMLIGHT");
  Tex* corner = g.img("IMAGE_SPIN_RIMLIGHT2");
  for (int i = 0; i < 40; i++) {
    if (wheel.bright[i] == 0) continue;
    float a = (float)wheel.bright[i] / 30.0f;
    if (i == 5 || i == 15 || i == 25 || i == 35) {
      if (!corner) continue;
      bool flipX = i == 25 || i == 35;
      int row = (i == 15 || i == 25) ? 1 : 0;
      float x = (float)P[i][0], yy = (float)P[i][1];
      if (flipX) {
        g.drawCelMirror(corner, x, yy, 0, row, Mode(a));
      } else {
        g.drawCel(corner, x, yy, 0, row, Mode(a));
      }
    } else if (rim) {
      g.draw(rim, (float)P[i][0], (float)P[i][1], Mode(a));
    }
  }
  if (Tex* ar = g.img("IMAGE_SPIN_ARROW")) {
    float th = wheel.angle;
    float c0x = 130 + ar->lw() / 2, c0y = 91 + ar->lh() / 2, px = 130 + 44, py = 91 + 84;
    float dx = c0x - px, dy = c0y - py;
    float rx = dx * std::cos(th) + dy * std::sin(th), ry = -dx * std::sin(th) + dy * std::cos(th);
    g.drawRot(ar, px + rx, py + ry, th, 1, 1);
  }
}

}
