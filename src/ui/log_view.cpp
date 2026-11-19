#include "ui/log_view.h"
#include "gamepaths.h"
#include "mods/text.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace pp::ui {

namespace {

const char* const kFont = "PLAIN";
const char* const kButtonFont = "BUTTON";

constexpr uint32_t kColInfo = 0xffffff;
constexpr uint32_t kColWarn = 0xffe060;
constexpr uint32_t kColError = 0xff6868;
constexpr uint32_t kColTime = 0x8c9ab0;
constexpr uint32_t kColSource = 0x9fc0e8;
constexpr uint32_t kColOn = 0x80ff80;
constexpr uint32_t kColOff = 0xffffff;
constexpr uint32_t kColHover = 0xffff99;
constexpr float kPad = 8.0f;
constexpr float kBar = 8.0f;

enum ButtonId { B_All = 1, B_Warn, B_Err, B_Source, B_Follow, B_Clear, B_Copy, B_Save, B_Close };

uint32_t levelColor(log::Level l) {
  switch (l) {
    case log::Level::Warn: return kColWarn;
    case log::Level::Error: return kColError;
    default: return kColInfo;
  }
}

size_t alignDown(const std::string& s, size_t n) {
  if (n >= s.size()) return s.size();
  while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
  return n;
}

}

LogView::LogView(App& a) : Panel(a) {
  auto mk = [&](int id, const char* label) {
    Button* b = patchButton(id, label, "IMAGE_DLG_BUTTON3", 0, 0);
    b->labelOver = kColHover;
    buttons_.push_back(b);
    return b;
  };
  bAll_ = mk(B_All, "All");
  bWarn_ = mk(B_Warn, "Warn+Err");
  bErr_ = mk(B_Err, "Errors");
  bSource_ = mk(B_Source, "All sources");
  bFollow_ = mk(B_Follow, "Follow");
  bClear_ = mk(B_Clear, "Clear");
  bCopy_ = mk(B_Copy, "Copy");
  bSave_ = mk(B_Save, "Save...");
  bClose_ = mk(B_Close, "Close");
  epoch_ = log::clearCount();
}

float LogView::measure(const std::string& s, const char* font) { return app.gfx.fontProvider().width(font ? font : kFont, s); }

std::string LogView::fitText(const std::string& s, float maxW, bool keepEnd, const char* font) {
  if (maxW <= 0) return "";
  const char* f = font ? font : kFont;
  FontProvider& fp = app.gfx.fontProvider();
  if (fp.width(f, s) <= maxW) return s;
  std::string t = s;
  while (t.size() > 1) {
    if (keepEnd) {
      size_t cut = 1;
      while (cut < t.size() && ((unsigned char)t[cut] & 0xC0) == 0x80) cut++;
      t.erase(0, cut);
      if (fp.width(f, "..." + t) <= maxW) return "..." + t;
    } else {
      size_t cut = alignDown(t, t.size() - 1);
      t.resize(cut);
      if (fp.width(f, t + "...") <= maxW) return t + "...";
    }
  }
  return t;
}

std::vector<std::string> LogView::wrapText(const std::string& s, float maxW) {
  std::vector<std::string> out;
  if (maxW < 20 || measure(s) <= maxW) {
    out.push_back(s);
    return out;
  }
  size_t pos = 0;
  while (pos < s.size()) {
    std::string rest = s.substr(pos);
    if (measure(rest) <= maxW) {
      out.push_back(rest);
      break;
    }
    size_t lo = 1, hi = rest.size();
    while (lo < hi) {
      size_t mid = (lo + hi + 1) / 2;
      size_t cut = alignDown(rest, mid);
      if (cut > 0 && measure(rest.substr(0, cut)) <= maxW) lo = mid;
      else hi = mid - 1;
    }
    size_t cut = alignDown(rest, lo);
    if (cut == 0) {
      cut = 1;
      while (cut < rest.size() && ((unsigned char)rest[cut] & 0xC0) == 0x80) cut++;
    }
    size_t take = cut, skip = cut;
    if (cut < rest.size()) {
      size_t brk = rest[cut] == ' ' ? cut : rest.find_last_of(" /\\", cut - 1);
      if (brk != std::string::npos && brk > cut / 2) {
        bool space = rest[brk] == ' ';
        take = skip = brk + (space ? 0 : 1);
        if (space) while (skip < rest.size() && rest[skip] == ' ') skip++;
      }
    }
    out.push_back(rest.substr(0, take));
    pos += skip;
  }
  if (out.empty()) out.push_back("");
  return out;
}

std::string LogView::filterName() const {
  switch (filter_) {
    case ShowWarnings: return "warnings and errors";
    case ShowErrors: return "errors";
    default: return "all levels";
  }
}

bool LogView::passes(const log::Entry& e) const {
  if (filter_ == ShowWarnings && e.level == log::Level::Info) return false;
  if (filter_ == ShowErrors && e.level != log::Level::Error) return false;
  if (!sourceFilter_.empty() && e.source != sourceFilter_) return false;
  return true;
}

void LogView::addSource(const std::string& s) {
  if (std::find(sources_.begin(), sources_.end(), s) != sources_.end()) return;
  sources_.push_back(s);
  std::sort(sources_.begin(), sources_.end());
  sourcesChanged_ = true;
}

long LogView::maxTop() const { return std::max(0L, (long)rows_.size() - visRows_); }

void LogView::setMode(Mode m) {
  if (m == mode_) return;
  mode_ = m;
  hover = nullptr;
  capture = nullptr;
  dragging_ = false;
  downOwned_ = false;
  wheelAcc_ = 0;
  if (m != Mode::Hidden) {
    follow_ = true;
    layout(app.gfx);
    sync();
    layout(app.gfx);
  }
}

void LogView::toggleDocked() { setMode(mode_ == Mode::Hidden ? Mode::Docked : Mode::Hidden); }

void LogView::say(const std::string& s) {
  status_ = s;
  statusTicks_ = 600;
}

void LogView::update() {
  Panel::update();
  if (statusTicks_ > 0) statusTicks_--;
}

void LogView::layout(Gfx& g) {
  canvasW_ = (float)g.canvasW();
  canvasH_ = (float)g.canvasH();
  const bool full = mode_ == Mode::Full;
  const float margin = full ? 14.0f : 0.0f;
  w = canvasW_ - margin * 2;
  h = full ? canvasH_ - margin * 2 : std::max(180.0f, std::floor(canvasH_ / 2));
  x = margin;
  y = full ? margin : canvasH_ - h;

  rowH_ = std::max(g.fontLineSpacing(kFont), g.fontHeight(kFont)) + 1;
  ascent_ = g.fontAscent(kFont);

  bAll_->label = "All";
  bWarn_->label = "Warn+Err";
  bErr_->label = "Errors";
  bSource_->label = fitText(sourceFilter_.empty() ? "All sources" : sourceFilter_, 150, false, kButtonFont);
  bFollow_->label = "Follow";
  bAll_->labelColor = filter_ == ShowAll ? kColOn : kColOff;
  bWarn_->labelColor = filter_ == ShowWarnings ? kColOn : kColOff;
  bErr_->labelColor = filter_ == ShowErrors ? kColOn : kColOff;
  bSource_->labelColor = sourceFilter_.empty() ? kColOff : kColOn;
  bFollow_->labelColor = follow_ ? kColOn : kColOff;

  float bh = 0;
  for (Button* b : buttons_) bh = std::max(bh, b->h);
  if (bh <= 0) bh = g.fontHeight(kButtonFont) + 12;
  float cx = kPad, cy = kPad;
  for (Button* b : buttons_) {
    b->h = bh;
    const float capWidth = b->image ? 2 * std::floor(b->image->lw() / 3) : 0.0f;
    b->w = std::max({70.0f, capWidth, g.textWidth(kButtonFont, b->label) + 22});
    if (cx + b->w > w - kPad && cx > kPad) {
      cx = kPad;
      cy += bh + 4;
    }
    b->x = cx;
    b->y = cy;
    cx += b->w + 4;
  }
  const float toolbarBottom = cy + bh;

  const float statusH = rowH_ + 4;
  statusY_ = h - kPad - statusH;
  areaX_ = kPad;
  areaY_ = toolbarBottom + 8;
  areaW_ = w - kPad * 2 - kBar - 2;
  areaH_ = std::max(rowH_, statusY_ - 4 - areaY_);
  visRows_ = std::max(1L, (long)std::floor(areaH_ / rowH_));

  timeW_ = measure("00:00:00") + 8;
  float widest = 0;
  for (auto& s : sources_) widest = std::max(widest, measure("[" + s + "]"));
  srcW_ = std::min(widest + 8, std::floor(areaW_ * 0.22f));
  if (srcW_ < 40) srcW_ = 40;
  textW_ = std::max(60.0f, areaW_ - timeW_ - srcW_ - 6);

  if (std::fabs(textW_ - builtTextW_) > 0.5f || std::fabs(srcW_ - builtSrcW_) > 0.5f) rebuildRows();

  if (follow_) top_ = maxTop();
  top_ = std::clamp(top_, 0L, maxTop());
}

void LogView::appendRows(const log::Entry& e) {
  std::string label = fitText("[" + e.source + "]", srcW_ - 6, false);
  std::vector<std::string> parts = wrapText(mods::gameText(e.text), textW_);
  for (size_t i = 0; i < parts.size(); i++) {
    Row r;
    r.seq = e.sequence;
    r.level = e.level;
    r.first = i == 0;
    if (i == 0) {
      r.time = log::timeText(e);
      r.src = label;
    }
    r.text = std::move(parts[i]);
    rows_.push_back(std::move(r));
  }
}

void LogView::rebuildRows() {
  uint64_t anchor = top_ >= 0 && (size_t)top_ < rows_.size() ? rows_[(size_t)top_].seq : 0;
  rows_.clear();
  shown_ = 0;
  builtTextW_ = textW_;
  builtSrcW_ = srcW_;
  for (auto& e : entries_) {
    if (!passes(e)) continue;
    appendRows(e);
    shown_++;
  }
  if (anchor) {
    long i = 0;
    for (; (size_t)i < rows_.size(); i++)
      if (rows_[(size_t)i].seq >= anchor) break;
    top_ = i;
  } else {
    top_ = 0;
  }
}

void LogView::sync() {
  uint64_t ep = log::clearCount();
  if (ep != epoch_) {
    epoch_ = ep;
    entries_.clear();
    rows_.clear();
    sources_.clear();
    sourceFilter_.clear();
    lastSeq_ = 0;
    shown_ = 0;
    top_ = 0;
    sourcesChanged_ = true;
  }
  std::vector<log::Entry> fresh = log::snapshot(lastSeq_);
  for (auto& e : fresh) {
    lastSeq_ = e.sequence;
    addSource(e.source);
    if (passes(e)) {
      appendRows(e);
      shown_++;
    }
    entries_.push_back(std::move(e));
  }
  while (entries_.size() > log::kCapacity) {
    const log::Entry& f = entries_.front();
    if (passes(f)) shown_--;
    uint64_t seq = f.sequence;
    entries_.pop_front();
    long removed = 0;
    while (!rows_.empty() && rows_.front().seq <= seq) {
      rows_.pop_front();
      removed++;
    }
    top_ = std::max(0L, top_ - removed);
  }
}

void LogView::drawView(Gfx& g) {
  if (!visible()) return;
  layout(g);
  sync();
  if (sourcesChanged_) {
    sourcesChanged_ = false;
    layout(g);
  }
  if (follow_) top_ = maxTop();
  top_ = std::clamp(top_, 0L, maxTop());
  Panel::draw(g);
}

void LogView::drawLayer(Gfx& g) {
  FontProvider& fp = g.fontProvider();
  if (mode_ == Mode::Full) g.fill(-x, -y, canvasW_, canvasH_, 0x000000, 0.55f);
  g.fill(0, 0, w, h, 0x00001a, 0.97f);
  g.frame(0, 0, w, h, 0x6a86b0, 0.9f);
  g.fill(areaX_ - 3, areaY_ - 3, areaW_ + kBar + 8, areaH_ + 6, 0x000000, 0.5f);

  g.pushClip(areaX_, areaY_, areaW_, areaH_);
  if (rows_.empty()) {
    const char* msg = entries_.empty() ? "No log lines yet." : "No lines match the current filters.";
    fp.draw(kFont, g.originX() + areaX_ + 4, g.originY() + areaY_ + ascent_ + 2, msg, kColTime, 1.0f);
  }
  for (long i = 0; i < visRows_; i++) {
    long idx = top_ + i;
    if (idx < 0 || (size_t)idx >= rows_.size()) break;
    const Row& r = rows_[(size_t)idx];
    float base = g.originY() + areaY_ + (float)i * rowH_ + ascent_;
    float ox = g.originX() + areaX_;
    if (r.first || i == 0) {
      size_t header = (size_t)idx;
      while (header > 0 && !rows_[header].first) --header;
      fp.draw(kFont, ox, base, rows_[header].time, kColTime, 1.0f);
      fp.draw(kFont, ox + timeW_, base, rows_[header].src, kColSource, 1.0f);
    }
    fp.draw(kFont, ox + timeW_ + srcW_ + 6, base, r.text, levelColor(r.level), 1.0f);
  }
  g.popClip();

  const Rect thumb = scrollThumb();
  g.fill(thumb.x, areaY_, thumb.w, areaH_, 0xffffff, 0.08f);
  if (maxTop() > 0) g.fill(thumb.x, thumb.y, thumb.w, thumb.h, follow_ ? 0x80ff80 : 0xb0c8ff, 0.7f);

  std::string left;
  if (statusTicks_ > 0 && !status_.empty()) {
    left = status_;
  } else {
    left = std::to_string(shown_) + " of " + std::to_string(entries_.size()) + " entries match, " + filterName();
    if (!sourceFilter_.empty()) left += ", " + sourceFilter_;
    left += follow_ ? ", following" : ", scrolled back";
  }
  const std::string right = mode_ == Mode::Full ? "Esc or F10 closes" : "F10 closes";
  const float rw = measure(right);
  const bool showRight = measure(left) + rw + 24 < areaW_;
  if (showRight) fp.draw(kFont, g.originX() + areaX_ + areaW_ + kBar - rw, g.originY() + statusY_ + 2 + ascent_, right, kColTime, 1.0f);
  left = fitText(left, showRight ? areaW_ - rw - 16 : areaW_ + kBar, statusTicks_ > 0);
  fp.draw(kFont, g.originX() + areaX_, g.originY() + statusY_ + 2 + ascent_, left, statusTicks_ > 0 ? 0xffffaa : 0xd0d8e8, 1.0f);
}

bool LogView::inTrack(float px, float py) const {
  const float lx = px - x, ly = py - y;
  return lx >= areaX_ + areaW_ - 2 && lx < areaX_ + areaW_ + kBar + 6 && ly >= areaY_ && ly < areaY_ + areaH_;
}

Rect LogView::scrollThumb() const {
  const long last = maxTop();
  const float h = last > 0 ? std::min(areaH_, std::max(18.0f, areaH_ * visRows_ / rows_.size())) : areaH_;
  const float y = areaY_ + (last > 0 ? (areaH_ - h) * top_ / last : 0.0f);
  return {areaX_ + areaW_ + 2, y, kBar, h};
}

void LogView::scrollTo(long row) {
  long t = std::clamp(row, 0L, maxTop());
  top_ = t;
  follow_ = t >= maxTop();
}

void LogView::scrollBy(long rows) { scrollTo(top_ + rows); }

void LogView::wheelScroll(float wheel) {
  wheelAcc_ += wheel * 3.0f;
  int n = (int)std::trunc(wheelAcc_);
  wheelAcc_ -= (float)n;
  if (n != 0) scrollBy(-n);
}

void LogView::dragTo(float py) {
  const float travel = areaH_ - scrollThumb().h;
  if (travel <= 0) return;
  const float f = std::clamp((py - y - areaY_ - dragOffset_) / travel, 0.0f, 1.0f);
  scrollTo((long)std::lround(f * maxTop()));
}

void LogView::pressKey(int key) {
  switch (key) {
    case SDLK_PAGEUP: scrollBy(-std::max(1L, visRows_ - 1)); break;
    case SDLK_PAGEDOWN: scrollBy(std::max(1L, visRows_ - 1)); break;
    case SDLK_HOME: scrollTo(0); break;
    case SDLK_END: scrollTo(maxTop()); break;
    case SDLK_UP: scrollBy(-1); break;
    case SDLK_DOWN: scrollBy(1); break;
    default: break;
  }
}

bool LogView::handle(const Input& in) {
  if (!visible()) return false;
  layout(app.gfx);
  const bool full = mode_ == Mode::Full;
  const bool pointerEvent = in.type == Input::MouseMove || in.type == Input::MouseDown || in.type == Input::MouseUp || in.type == Input::Wheel;
  const bool over = pointerEvent ? contains(in.x, in.y) : contains(app.mouseX, app.mouseY);
  switch (in.type) {
    case Input::MouseMove:
      if (dragging_) {
        dragTo(in.y);
        return true;
      }
      Panel::input(in);
      return full || over || capture != nullptr;
    case Input::MouseDown: {
      if (!over) {
        Panel::input(in);
        if (!full) return false;
        downOwned_ = true;
        return true;
      }
      bool used = Panel::input(in);
      if (!used && in.button == 1 && maxTop() > 0 && inTrack(in.x, in.y)) {
        const Rect thumb = scrollThumb();
        const float grab = in.y - y - thumb.y;
        dragOffset_ = grab >= 0 && grab < thumb.h ? grab : thumb.h / 2;
        dragging_ = true;
        dragTo(in.y);
      }
      downOwned_ = true;
      return true;
    }
    case Input::MouseUp:
      if (dragging_) {
        dragging_ = false;
        downOwned_ = false;
        return true;
      }
      if (capture) {
        Panel::input(in);
        downOwned_ = false;
        return true;
      }
      if (downOwned_) {
        downOwned_ = false;
        return true;
      }
      return full;
    case Input::Wheel:
      if (full || over) {
        wheelScroll(in.wheel);
        return true;
      }
      return false;
    case Input::KeyDown:
      if (in.key == SDLK_PAGEUP || in.key == SDLK_PAGEDOWN || in.key == SDLK_HOME || in.key == SDLK_END) {
        if (full || over) {
          pressKey(in.key);
          return true;
        }
        return false;
      }
      if (full) {
        if (in.key == SDLK_UP || in.key == SDLK_DOWN) pressKey(in.key);
        else if (in.key == SDLK_ESCAPE) setMode(Mode::Hidden);
        return true;
      }
      return false;
    case Input::KeyUp:
    case Input::Text:
      return full;
    case Input::MouseLeave:
      Panel::input(in);
      return false;
    case Input::FocusLost:
      dragging_ = false;
      downOwned_ = false;
      capture = nullptr;
      return false;
    default:
      return false;
  }
}

void LogView::onButton(int id) {
  switch (id) {
    case B_All:
    case B_Warn:
    case B_Err:
      filter_ = id == B_All ? ShowAll : id == B_Warn ? ShowWarnings : ShowErrors;
      rebuildRows();
      follow_ = true;
      break;
    case B_Source: {
      if (sources_.empty()) break;
      if (sourceFilter_.empty()) {
        sourceFilter_ = sources_.front();
      } else {
        auto it = std::find(sources_.begin(), sources_.end(), sourceFilter_);
        if (it == sources_.end() || ++it == sources_.end()) sourceFilter_.clear();
        else sourceFilter_ = *it;
      }
      rebuildRows();
      follow_ = true;
      break;
    }
    case B_Follow:
      follow_ = true;
      top_ = maxTop();
      break;
    case B_Clear: doClear(); break;
    case B_Copy: doCopy(); break;
    case B_Save: doSave(); break;
    case B_Close: setMode(Mode::Hidden); break;
    default: break;
  }
}

void LogView::doClear() {
  log::clear();
  sync();
  say("Log cleared.");
}

void LogView::doCopy() {
  std::string text;
  long n = 0;
  for (auto& e : entries_) {
    if (!passes(e)) continue;
    text += log::formatLine(e);
    text += '\n';
    n++;
  }
  if (n == 0) {
    say("Nothing to copy.");
    return;
  }
  if (SDL_SetClipboardText(text.c_str())) say("Copied " + std::to_string(n) + " line" + (n == 1 ? "" : "s") + " to the clipboard.");
  else say(std::string("Could not copy: ") + SDL_GetError());
}

void LogView::doSave() {
  std::time_t t = std::time(nullptr);
  std::tm tmv{};
#ifdef _WIN32
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  char stamp[32];
  std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tmv);
  fs::path file = app.userDir / "logs" / (std::string("peggleport-") + stamp + ".log");
  std::string err;
  if (log::exportToFile(file, &err)) {
    say("Saved to " + mods::gameText(pathToUtf8(file)));
    log::info("ui", "log saved to " + pathToUtf8(file));
  } else {
    say("Could not save the log: " + err);
    log::error("ui", "could not save the log: " + err);
  }
}

}
