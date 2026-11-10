#pragma once
#include <string>
#include <vector>
#include "ui/dialog.h"

namespace pp::ui {

enum { D_ModConsent = 60 };

class ConsentList : public Widget {
 public:
  struct Row {
    std::string text;
    bool bullet = false;
    bool first = false;
  };
  std::vector<Row> rows;
  int top = 0;
  bool dragging = false;
  float wheelAcc = 0;
  int shown() const;
  int maxTop() const;
  bool scrolls() const { return maxTop() > 0; }
  void scrollTo(int row);
  void scrollBy(int count) { scrollTo(top + count); }
  void wheel(float d);
  bool pressKey(int sdlKey);
  void dragTo(float ly);
  float thumbHeight() const;
  static float heightFor(int count);
  static int rowsFor(float height);
  void draw(Gfx& g) override;
  bool mouseDown(float lx, float ly, int button, int clicks) override;
  bool mouseUp(float lx, float ly, int button) override;
  void mouseMove(float lx, float ly) override;
};

class ConsentDialog : public Dialog {
 public:
  struct Body {
    std::string head;
    std::vector<std::string> before, findings, after;
    bool notesMayScroll = false;
  };
  struct Line {
    float y = 0;
    std::string text;
  };
  ConsentDialog(App& a, const std::string& modId, bool ask);
  void onButton(int id) override;
  float preferredHeight(float width) override;
  void drawLayer(Gfx& g) override;
  bool input(const Input& in) override;
  std::string modId, hash;
  bool ask;
  int step = 0;
  bool followUp = false;
  bool closed = false;
  bool hosted = false;
  float maxHeight = 570;
  Body body;
  ConsentList* list = nullptr;
  std::vector<Line> fixed;
  std::string hint;
  static int stepFor(const std::string& modId, bool ask);
  static Body describe(const std::string& modId, int step);
  float arrange(float width, float limit);

 private:
  float laidW_ = -1, laidLimit_ = -1, laidH_ = -1, hintY_ = 0;
  bool build(float width, float limit, bool notesInList, float& height);
};

}
