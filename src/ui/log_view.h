#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "log.h"
#include "ui/app.h"

namespace pp::ui {

class LogView : public Panel {
 public:
  enum class Mode { Hidden, Docked, Full };

  explicit LogView(App& a);

  Mode mode() const { return mode_; }
  bool visible() const { return mode_ != Mode::Hidden; }
  void setMode(Mode m);
  void toggleDocked();

  bool handle(const Input& in);
  void update() override;
  void drawView(Gfx& g);

  void drawLayer(Gfx& g) override;
  void onButton(int id) override;

 private:
  enum Filter { ShowAll, ShowWarnings, ShowErrors };
  struct Row {
    uint64_t seq = 0;
    log::Level level = log::Level::Info;
    bool first = true;
    std::string time, src, text;
  };

  Mode mode_ = Mode::Hidden;
  Filter filter_ = ShowAll;
  std::string sourceFilter_;
  bool sourcesChanged_ = false;
  bool follow_ = true;
  bool dragging_ = false;
  bool downOwned_ = false;
  float wheelAcc_ = 0;
  float dragOffset_ = 0;
  long top_ = 0;
  int statusTicks_ = 0;
  std::string status_;

  std::deque<log::Entry> entries_;
  std::deque<Row> rows_;
  std::vector<std::string> sources_;
  uint64_t lastSeq_ = 0;
  uint64_t epoch_ = 0;
  long shown_ = 0;

  std::vector<Button*> buttons_;
  Button *bAll_ = nullptr, *bWarn_ = nullptr, *bErr_ = nullptr, *bSource_ = nullptr, *bFollow_ = nullptr, *bClear_ = nullptr, *bCopy_ = nullptr,
         *bSave_ = nullptr, *bClose_ = nullptr;

  float areaX_ = 0, areaY_ = 0, areaW_ = 0, areaH_ = 0;
  float rowH_ = 14, ascent_ = 10;
  float timeW_ = 0, srcW_ = 0, textW_ = 0;
  float builtTextW_ = -1, builtSrcW_ = -1;
  long visRows_ = 1;
  float statusY_ = 0;
  float canvasW_ = 800, canvasH_ = 600;

  bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  bool inTrack(float px, float py) const;
  Rect scrollThumb() const;
  long maxTop() const;
  void layout(Gfx& g);
  void sync();
  bool passes(const log::Entry& e) const;
  void addSource(const std::string& s);
  void appendRows(const log::Entry& e);
  void rebuildRows();
  std::vector<std::string> wrapText(const std::string& s, float maxW);
  std::string fitText(const std::string& s, float maxW, bool keepEnd, const char* font = nullptr);
  float measure(const std::string& s, const char* font = nullptr);
  void scrollBy(long rows);
  void scrollTo(long row);
  void wheelScroll(float wheel);
  void dragTo(float py);
  void pressKey(int key);
  void say(const std::string& s);
  void doCopy();
  void doSave();
  void doClear();
  std::string filterName() const;
};

}
