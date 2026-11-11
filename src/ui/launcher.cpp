#include "ui/launcher.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "gamefind.h"
#include "gamepaths.h"
#include "log.h"
#include "online/invite.h"
#include "online/online_ui.h"

namespace pp::ui {

namespace {

namespace fs = std::filesystem;

constexpr float kNeedW = 62.0f;
constexpr float kNeedH = 33.0f;

constexpr uint32_t kBackground = 0x10131c;
constexpr uint32_t kTopBar = 0xffb21e;
constexpr uint32_t kHeading = 0xffc43d;
constexpr uint32_t kText = 0xf4f6fb;
constexpr uint32_t kSubtle = 0xaab4cf;
constexpr uint32_t kFooter = 0x7983a0;
constexpr uint32_t kCard = 0x1a2030;
constexpr uint32_t kCardSelected = 0x222b45;
constexpr uint32_t kBorder = 0x2f3a58;
constexpr uint32_t kBorderSelected = 0xffb21e;
constexpr uint32_t kButton = 0x2e3858;
constexpr uint32_t kButtonHover = 0x3b4a78;
constexpr uint32_t kButtonDown = 0x252e4a;
constexpr uint32_t kButtonOff = 0x1f2536;
constexpr uint32_t kButtonOffText = 0x5b6480;
constexpr uint32_t kPlay = 0xffb21e;
constexpr uint32_t kPlayHover = 0xffc84d;
constexpr uint32_t kPlayDown = 0xd9961a;
constexpr uint32_t kPlayText = 0x1a1405;
constexpr uint32_t kRed = 0xff6b6b;
constexpr uint32_t kGreen = 0x6fdc8c;
constexpr uint32_t kBlue = 0xc9d1e6;

constexpr Game kGames[2] = {Game::Deluxe, Game::Nights};

enum MessageKind { kInfo, kError, kOk };
enum Target { kNothing = -1, kPlayFirst = 0, kLocateFirst = 1, kPlaySecond = 2, kLocateSecond = 3, kCheckbox = 4 };

Uint32 g_dialogEvent = 0;

int indexOf(Game g) { return g == Game::Nights ? 1 : 0; }

std::string lowerCase(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

std::string asciiOnly(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    if (c >= 0xC0) out.push_back('?');
    else if (c >= 0x20 && c < 0x7F) out.push_back((char)c);
    else if (c < 0x20) out.push_back(' ');
  }
  return out;
}

std::vector<std::string> wrapWords(const std::string& text, size_t width, size_t maxLines) {
  std::vector<std::string> lines;
  std::string cur;
  size_t i = 0;
  while (i < text.size()) {
    size_t e = text.find(' ', i);
    if (e == std::string::npos) e = text.size();
    std::string word = text.substr(i, e - i);
    i = e + 1;
    if (word.empty()) continue;
    while (word.size() > width) {
      if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
      lines.push_back(word.substr(0, width));
      word = word.substr(width);
    }
    if (cur.empty()) cur = word;
    else if (cur.size() + 1 + word.size() <= width) cur += " " + word;
    else { lines.push_back(cur); cur = word; }
  }
  if (!cur.empty()) lines.push_back(cur);
  if (lines.size() > maxLines) {
    lines.resize(maxLines);
    std::string& last = lines.back();
    if (last.size() + 3 > width) last.resize(width > 3 ? width - 3 : 0);
    last += "...";
  }
  return lines;
}

std::vector<std::string> pathLines(const fs::path& p, size_t width, size_t maxLines) {
  std::string s = asciiOnly(pathToUtf8(p));
  size_t room = width * maxLines;
  if (s.size() > room) s = "..." + s.substr(s.size() - (room - 3));
  std::vector<std::string> lines;
  for (size_t i = 0; i < s.size(); i += width) lines.push_back(s.substr(i, width));
  return lines;
}

struct Store {
  fs::path dir[2];
  int last = -1;
  bool autoStart = false;
  fs::path file;

  static fs::path defaultFile() {
    char* p = SDL_GetPrefPath("PegglePort", "PegglePort");
    if (!p) return {};
    fs::path r = pathFromUtf8(p) / "launcher.ini";
    SDL_free(p);
    return r;
  }

  void load() {
    file = defaultFile();
    if (file.empty()) return;
    std::ifstream f(file);
    std::string line;
    while (std::getline(f, line)) {
      while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
      auto eq = line.find('=');
      if (eq == std::string::npos) continue;
      std::string k = lowerCase(line.substr(0, eq)), v = line.substr(eq + 1);
      if (k == "deluxe") dir[0] = pathFromUtf8(v);
      else if (k == "nights") dir[1] = pathFromUtf8(v);
      else if (k == "last") last = lowerCase(v) == "deluxe" ? 0 : lowerCase(v) == "nights" ? 1 : -1;
      else if (k == "autostart") autoStart = v == "1" || lowerCase(v) == "true";
    }
    for (auto& d : dir)
      if (!hasMainPak(d)) d.clear();
    if (last >= 0 && dir[last].empty()) last = -1;
  }

  void save() const {
    if (file.empty()) return;
    fs::path tmp = file;
    tmp += ".tmp";
    {
      std::ofstream o(tmp, std::ios::trunc);
      if (!o) return;
      o << "Deluxe=" << pathToUtf8(dir[0]) << "\nNights=" << pathToUtf8(dir[1]) << "\nLast=" << (last == 0 ? "Deluxe" : last == 1 ? "Nights" : "")
        << "\nAutoStart=" << (autoStart ? 1 : 0) << "\n";
      if (!o) return;
    }
    std::error_code ec;
    fs::rename(tmp, file, ec);
    if (ec) fs::remove(tmp, ec);
  }
};

struct Layout {
  float cell = 16, scale = 2;
  float width = 0, height = 0;
  float left = 0, top = 0, span = 0;
  SDL_FRect card[2]{}, play[2]{}, locate[2]{}, checkBox{}, checkHit{};
  int pathChars = 26;
  int messageChars = 58;
};

bool inside(const SDL_FRect& r, float x, float y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

void SDLCALL folderChosen(void* userdata, const char* const* files, int) {
  SDL_Event e;
  SDL_zero(e);
  e.type = g_dialogEvent;
  e.user.code = (Sint32)(intptr_t)userdata;
  int status = !files ? 2 : !files[0] ? 1 : 0;
  e.user.data1 = status == 0 ? new std::string(files[0]) : nullptr;
  e.user.data2 = (void*)(intptr_t)status;
  if (!SDL_PushEvent(&e)) delete static_cast<std::string*>(e.user.data1);
}

class Launcher {
 public:
  LauncherResult run(bool allowAutoStart) {
    store_.load();
    {
      const char* base = SDL_GetBasePath();
      FindSources sources = systemFindSources(base && *base ? pathFromUtf8(base) : fs::path());
      for (int i = 0; i < 2; i++) sources.remembered[i] = store_.dir[i];
      FoundGames found = findGames(sources);
      for (auto& line : searchReport(found)) pp::log::info("start", line);
      for (int i = 0; i < 2; i++)
        if (store_.dir[i].empty()) store_.dir[i] = found.dir[i];
    }
    if (store_.last >= 0 && store_.dir[store_.last].empty()) store_.last = -1;
    sel_ = store_.last >= 0 ? store_.last : !store_.dir[0].empty() ? 0 : !store_.dir[1].empty() ? 1 : 0;

    if (allowAutoStart && store_.autoStart && store_.last >= 0) {
      result_.play = true;
      result_.game = kGames[store_.last];
      result_.dir = store_.dir[store_.last];
      return result_;
    }

    if (!open()) return result_;
    while (!done_) {
      layout();
      SDL_Event e;
      if (SDL_WaitEventTimeout(&e, 100)) {
        handle(e);
        while (!done_ && SDL_PollEvent(&e)) handle(e);
      }
      if (done_) break;
      layout();
      draw();
    }
    close();
    return result_;
  }

 private:
  SDL_Window* win_ = nullptr;
  SDL_Renderer* ren_ = nullptr;
  Store store_;
  Layout L_;
  LauncherResult result_;
  int sel_ = 0;
  int hover_ = kNothing, down_ = kNothing;
  bool done_ = false, dialogOpen_ = false;
  std::string message_;
  MessageKind messageKind_ = kInfo;

  static void windowSize(int* w, int* h) {
    SDL_DisplayID id = SDL_GetPrimaryDisplay();
    float density = 1.0f, content = 1.0f;
    if (const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(id); m && m->pixel_density > 0) density = m->pixel_density;
    if (float cs = SDL_GetDisplayContentScale(id); cs > 0) content = cs;
    int t = std::max(1, (int)std::lround(2.0f * density * content));
    SDL_Rect area;
    bool haveArea = SDL_GetDisplayUsableBounds(id, &area);
    for (; t > 1 && haveArea; t--)
      if (kNeedW * 8 * t / density <= area.w * 0.96f && kNeedH * 8 * t / density <= area.h * 0.92f) break;
    *w = (int)std::ceil(kNeedW * 8 * t / density);
    *h = (int)std::ceil(kNeedH * 8 * t / density);
  }

  bool open() {
    if (!g_dialogEvent) g_dialogEvent = SDL_RegisterEvents(1);
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    int w = 992, h = 528;
    windowSize(&w, &h);
    if (!SDL_CreateWindowAndRenderer("PegglePort", w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &win_, &ren_)) {
      result_.error = std::string("The window could not be opened: ") + SDL_GetError();
      return false;
    }
    SDL_SetRenderVSync(ren_, 1);
    float density = SDL_GetWindowPixelDensity(win_);
    if (density <= 0) density = 1.0f;
    SDL_SetWindowMinimumSize(win_, (int)std::ceil(kNeedW * 8 / density), (int)std::ceil(kNeedH * 8 / density));
    SDL_SetWindowPosition(win_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    return true;
  }

  void close() {
    if (ren_) SDL_DestroyRenderer(ren_);
    if (win_) SDL_DestroyWindow(win_);
    ren_ = nullptr;
    win_ = nullptr;
  }

  void setMessage(MessageKind k, const std::string& text) {
    messageKind_ = k;
    message_ = text;
  }

  void layout() {
    int pw = 0, ph = 0;
    SDL_GetCurrentRenderOutputSize(ren_, &pw, &ph);
    Layout& L = L_;
    L.width = (float)pw;
    L.height = (float)ph;
    int t = std::max(1, (int)std::floor(std::min(pw / (kNeedW * 8), ph / (kNeedH * 8))));
    float c = 8.0f * (float)t;
    L.scale = (float)t;
    L.cell = c;
    L.span = std::min((float)pw, 90.0f * c);
    L.left = std::floor(((float)pw - L.span) / 2);
    L.top = std::max(0.0f, std::floor(((float)ph - kNeedH * c) / 2));
    float cardW = std::floor((L.span - 6 * c) / 2);
    for (int i = 0; i < 2; i++) {
      float cx = L.left + 2 * c + (float)i * (cardW + 2 * c);
      float cy = L.top + 7 * c;
      L.card[i] = {cx, cy, cardW, 13 * c};
      float bw = std::floor((cardW - 3 * c) / 2);
      L.play[i] = {cx + c, cy + 9 * c, bw, 3 * c};
      L.locate[i] = {cx + 2 * c + bw, cy + 9 * c, bw, 3 * c};
    }
    L.checkBox = {L.left + 2 * c, L.top + 21.5f * c, 1.5f * c, 1.5f * c};
    L.checkHit = {L.left + 2 * c, L.top + 21.5f * c, 41 * c, 1.5f * c};
    L.pathChars = std::max(8, (int)std::floor((cardW - 2 * c) / c));
    L.messageChars = std::max(16, (int)std::floor((L.span - 4 * c) / c));
  }

  int targetAt(float x, float y) const {
    for (int i = 0; i < 2; i++) {
      if (inside(L_.play[i], x, y)) return 2 * i;
      if (inside(L_.locate[i], x, y)) return 2 * i + 1;
    }
    if (inside(L_.checkHit, x, y)) return kCheckbox;
    return kNothing;
  }

  int cardAt(float x, float y) const {
    for (int i = 0; i < 2; i++)
      if (inside(L_.card[i], x, y)) return i;
    return -1;
  }

  void setFolder(int i, const fs::path& dir, bool announce) {
    store_.dir[i] = dir;
    sel_ = i;
    store_.save();
    if (announce) setMessage(kOk, std::string(gameTitle(kGames[i])) + " is ready to play.");
  }

  void chooseFolder(int i, const std::string& utf8Path) {
    Game g = kGames[i];
    fs::path chosen = pathFromUtf8(utf8Path);
    fs::path dir = resolveGameDir(chosen);
    bool direct = !dir.empty();
    if (!direct) dir = findGameIn(chosen, g);
    if (dir.empty()) {
      setMessage(kError, std::string("That folder does not contain main.pak. Choose the folder where ") + gameTitle(g) + " is installed.");
      return;
    }
    Game seen = classifyGameDir(dir);
    if (seen == Game::Deluxe && direct) seen = classifyGameDir(chosen);
    if (seen != g) {
      setMessage(kError, std::string("That folder looks like ") + gameTitle(seen) + ", not " + gameTitle(g) + ".");
      return;
    }
    setFolder(i, dir, true);
  }

  void dropFolder(const std::string& utf8Path) {
    fs::path p = pathFromUtf8(utf8Path);
    std::error_code ec;
    if (fs::is_regular_file(p, ec)) {
      if (lowerCase(pathToUtf8(p.filename())) != "main.pak") {
        setMessage(kError, "That is a file. Drop the game folder, the one that contains main.pak.");
        return;
      }
      p = p.parent_path();
    }
    if (!fs::is_directory(p, ec)) {
      setMessage(kError, "That is not a folder.");
      return;
    }
    fs::path direct = resolveGameDir(p);
    if (!direct.empty()) {
      Game g = classifyGameDir(direct);
      if (g == Game::Deluxe) g = classifyGameDir(p);
      setFolder(indexOf(g), direct, true);
      return;
    }
    int found = 0;
    for (int i = 0; i < 2; i++) {
      fs::path d = findGameIn(p, kGames[i]);
      if (d.empty()) continue;
      setFolder(i, d, false);
      found++;
    }
    if (found == 2) setMessage(kOk, "Peggle Deluxe and Peggle Nights are ready to play.");
    else if (found == 1) setMessage(kOk, std::string(gameTitle(kGames[sel_])) + " is ready to play.");
    else setMessage(kError, "No game found in that folder. Drop the folder that contains main.pak.");
  }

  void play(int i) {
    sel_ = i;
    if (store_.dir[i].empty()) {
      setMessage(kError, std::string(gameTitle(kGames[i])) + " is not set up yet. Use Locate... or drop its folder onto this window.");
      return;
    }
    if (!hasMainPak(store_.dir[i])) {
      store_.dir[i].clear();
      store_.save();
      setMessage(kError, std::string("The ") + gameTitle(kGames[i]) + " folder is no longer there. Please locate it again.");
      return;
    }
    store_.last = i;
    store_.save();
    result_.play = true;
    result_.game = kGames[i];
    result_.dir = store_.dir[i];
    done_ = true;
  }

  void locate(int i) {
    sel_ = i;
    if (dialogOpen_) return;
    dialogOpen_ = true;
    std::string start;
    if (!store_.dir[i].empty()) start = pathToUtf8(store_.dir[i].parent_path());
    setMessage(kInfo, std::string("Choose the folder where ") + gameTitle(kGames[i]) + " is installed.");
    SDL_ShowOpenFolderDialog(folderChosen, (void*)(intptr_t)i, win_, start.empty() ? nullptr : start.c_str(), false);
  }

  void activate(int target) {
    if (target == kCheckbox) {
      store_.autoStart = !store_.autoStart;
      store_.save();
    } else if (target % 2 == 0) {
      play(target / 2);
    } else {
      locate(target / 2);
    }
  }

  void handle(SDL_Event& e) {
    if (g_dialogEvent && e.type == g_dialogEvent) {
      std::unique_ptr<std::string> path(static_cast<std::string*>(e.user.data1));
      int status = (int)(intptr_t)e.user.data2;
      dialogOpen_ = false;
      if (status == 2) setMessage(kError, "The folder chooser is not available. Drop the game folder onto this window instead.");
      else if (status == 0 && path) chooseFolder(e.user.code, *path);
      else setMessage(kInfo, "");
      return;
    }
    switch (e.type) {
      case SDL_EVENT_QUIT:
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED: done_ = true; break;
      case SDL_EVENT_KEY_DOWN:
        switch (e.key.key) {
          case SDLK_ESCAPE: done_ = true; break;
          case SDLK_LEFT: sel_ = 0; break;
          case SDLK_RIGHT: sel_ = 1; break;
          case SDLK_TAB: sel_ = 1 - sel_; break;
          case SDLK_RETURN:
          case SDLK_KP_ENTER:
            if (!e.key.repeat) play(sel_);
            break;
          default: break;
        }
        break;
      case SDL_EVENT_MOUSE_MOTION: {
        SDL_ConvertEventToRenderCoordinates(ren_, &e);
        hover_ = targetAt(e.motion.x, e.motion.y);
        if (int c = cardAt(e.motion.x, e.motion.y); c >= 0) sel_ = c;
        break;
      }
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT) {
          SDL_ConvertEventToRenderCoordinates(ren_, &e);
          down_ = targetAt(e.button.x, e.button.y);
          if (int c = cardAt(e.button.x, e.button.y); c >= 0) sel_ = c;
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT) {
          SDL_ConvertEventToRenderCoordinates(ren_, &e);
          int up = targetAt(e.button.x, e.button.y);
          int was = down_;
          down_ = kNothing;
          if (was != kNothing && was == up) activate(up);
        }
        break;
      case SDL_EVENT_WINDOW_MOUSE_LEAVE: hover_ = kNothing; break;
      case SDL_EVENT_DROP_FILE:
      case SDL_EVENT_DROP_TEXT:
        if (e.drop.data) {
          std::string d = e.drop.data;
          if (pp::online::isInviteLike(d)) {
            pp::ui::online::setPendingInvite(d);
            setMessage(kOk, "Invite link received. It opens when you start a game.");
          } else if (e.type == SDL_EVENT_DROP_FILE) {
            dropFolder(d);
          }
        }
        break;
      default: break;
    }
  }

  void color(uint32_t rgb, int a = 255) { SDL_SetRenderDrawColor(ren_, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, (Uint8)a); }

  void fill(const SDL_FRect& r, uint32_t rgb, int a = 255) {
    color(rgb, a);
    SDL_RenderFillRect(ren_, &r);
  }

  void outline(const SDL_FRect& r, float t, uint32_t rgb) {
    fill({r.x, r.y, r.w, t}, rgb);
    fill({r.x, r.y + r.h - t, r.w, t}, rgb);
    fill({r.x, r.y + t, t, r.h - 2 * t}, rgb);
    fill({r.x + r.w - t, r.y + t, t, r.h - 2 * t}, rgb);
  }

  void text(float x, float y, const std::string& s, int scale, uint32_t rgb, int a = 255) {
    if (s.empty()) return;
    float k = (float)scale;
    color(rgb, a);
    SDL_SetRenderScale(ren_, k, k);
    SDL_RenderDebugText(ren_, std::round(x / k), std::round(y / k), s.c_str());
    SDL_SetRenderScale(ren_, 1.0f, 1.0f);
  }

  float textWidth(const std::string& s, int scale) const { return (float)s.size() * 8.0f * (float)scale; }

  void centered(float y, const std::string& s, int scale, uint32_t rgb) { text(std::floor((L_.width - textWidth(s, scale)) / 2), y, s, scale, rgb); }

  void drawButton(const SDL_FRect& r, const std::string& label, int id, bool enabled, bool primary) {
    bool hot = enabled && hover_ == id;
    bool pressed = hot && down_ == id;
    uint32_t fillc, textc;
    if (!enabled) { fillc = kButtonOff; textc = kButtonOffText; }
    else if (primary) { fillc = pressed ? kPlayDown : hot ? kPlayHover : kPlay; textc = kPlayText; }
    else { fillc = pressed ? kButtonDown : hot ? kButtonHover : kButton; textc = kText; }
    fill(r, fillc);
    int s = (int)L_.scale;
    float tw = textWidth(label, s);
    text(r.x + std::floor((r.w - tw) / 2), r.y + std::floor((r.h - L_.cell) / 2), label, s, textc);
  }

  void draw() {
    const Layout& L = L_;
    const float c = L.cell;
    const int t = (int)L.scale;
    color(kBackground);
    SDL_RenderClear(ren_);
    fill({0, 0, L.width, (float)t * 2}, kTopBar);

    std::string heading = "PegglePort";
    float hx = std::floor((L.width - textWidth(heading, 3 * t)) / 2);
    text(hx + (float)t, L.top + 1.5f * c + (float)t, heading, 3 * t, 0x000000, 140);
    text(hx, L.top + 1.5f * c, heading, 3 * t, kHeading);
    centered(L.top + 5 * c, "Choose a game to play", t, kSubtle);

    for (int i = 0; i < 2; i++) {
      const SDL_FRect& card = L.card[i];
      bool selected = sel_ == i;
      fill(card, selected ? kCardSelected : kCard);
      outline(card, selected ? (float)t : std::max(1.0f, (float)t / 2), selected ? kBorderSelected : kBorder);
      text(card.x + c, card.y + c, gameTitle(kGames[i]), 2 * t, kText);
      fill({card.x + c, card.y + 3.5f * c, card.w - 2 * c, std::max(1.0f, (float)t / 2)}, kBorder);
      bool have = !store_.dir[i].empty();
      if (have) {
        auto lines = pathLines(store_.dir[i], (size_t)L.pathChars, 3);
        for (size_t k = 0; k < lines.size(); k++) text(card.x + c, card.y + 4.25f * c + (float)k * 1.25f * c, lines[k], t, kSubtle);
      } else {
        text(card.x + c, card.y + 4.25f * c, "Not found", t, kRed);
        text(card.x + c, card.y + 5.5f * c, "Use Locate... to pick it.", t, kFooter);
      }
      drawButton(L.play[i], "Play", 2 * i, have, true);
      drawButton(L.locate[i], have ? "Change..." : "Locate...", 2 * i + 1, true, false);
    }

    outline(L.checkBox, std::max(1.0f, (float)t), kSubtle);
    if (store_.autoStart) {
      float in = 3.0f * (float)t;
      fill({L.checkBox.x + in, L.checkBox.y + in, L.checkBox.w - 2 * in, L.checkBox.h - 2 * in}, kTopBar);
    }
    bool hot = hover_ == kCheckbox;
    text(L.checkBox.x + 2.5f * c, L.checkBox.y + 0.25f * c, "Open the last game directly next time", t, hot ? kText : kSubtle);

    if (!message_.empty()) {
      uint32_t col = messageKind_ == kError ? kRed : messageKind_ == kOk ? kGreen : kBlue;
      auto lines = wrapWords(asciiOnly(message_), (size_t)L.messageChars, 3);
      for (size_t k = 0; k < lines.size(); k++) text(L.left + 2 * c, L.top + 24.25f * c + (float)k * 1.25f * c, lines[k], t, col);
    }

    text(L.left + 2 * c, L.top + 29.25f * c, "Left / Right: choose   Enter: play   Esc: quit", t, kFooter);
    text(L.left + 2 * c, L.top + 30.5f * c, "You can also drop a game folder onto this window.", t, kFooter);
    SDL_RenderPresent(ren_);
  }
};

}

LauncherResult runLauncher(bool allowAutoStart) {
  Launcher launcher;
  return launcher.run(allowAutoStart);
}

}
