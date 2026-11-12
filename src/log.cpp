#include "log.h"
#include "gamepaths.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <mutex>
#include <set>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pp::log {
namespace {

constexpr size_t kMaxLine = 4000;

struct State {
  std::mutex mutex;
  std::deque<Entry> entries;
  uint64_t next = 1;
  uint64_t cleared = 0;
  std::set<std::string> once;
};

State& state() {
  static State* s = new State();
  return *s;
}

void localClock(int& h, int& m, int& s, int& ms) {
  using namespace std::chrono;
  auto now = system_clock::now();
  std::time_t t = system_clock::to_time_t(now);
  std::tm tmv{};
#ifdef _WIN32
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  h = tmv.tm_hour;
  m = tmv.tm_min;
  s = tmv.tm_sec;
  ms = (int)(duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000);
}

std::string clean(const std::string& line) {
  std::string o;
  o.reserve(line.size());
  for (char c : line) {
    unsigned char u = (unsigned char)c;
    if (c == '\t') o += "    ";
    else if (u < 0x20 || u == 0x7f) o += '?';
    else o += c;
  }
  while (!o.empty() && o.back() == ' ') o.pop_back();
  if (o.size() > kMaxLine) {
    size_t cut = kMaxLine - 3;
    while (cut > 0 && ((unsigned char)o[cut] & 0xC0) == 0x80) cut--;
    o.resize(cut);
    o += "...";
  }
  return o;
}

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> out;
  size_t p = 0;
  while (p <= text.size()) {
    size_t e = text.find('\n', p);
    std::string line = text.substr(p, e == std::string::npos ? std::string::npos : e - p);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    line = clean(line);
    if (!line.empty()) out.push_back(std::move(line));
    if (e == std::string::npos) break;
    p = e + 1;
  }
  return out;
}

void mirror(const std::string& line) {
  std::fputs(line.c_str(), stderr);
  std::fputc('\n', stderr);
#ifdef _WIN32
  OutputDebugStringA((line + "\n").c_str());
#endif
}

}

const char* levelName(Level level) {
  switch (level) {
    case Level::Info: return "INFO";
    case Level::Warn: return "WARN";
    case Level::Error: return "ERROR";
  }
  return "INFO";
}

std::string timeText(const Entry& e, bool withMillis) {
  char b[24];
  if (withMillis) std::snprintf(b, sizeof b, "%02d:%02d:%02d.%03d", e.hour, e.minute, e.second, e.milli);
  else std::snprintf(b, sizeof b, "%02d:%02d:%02d", e.hour, e.minute, e.second);
  return b;
}

std::string formatLine(const Entry& e) {
  return timeText(e, true) + " " + levelName(e.level) + " [" + e.source + "] " + e.text;
}

void write(Level level, const std::string& source, const std::string& text) {
  std::vector<std::string> lines = splitLines(text);
  if (lines.empty()) return;
  State& st = state();
  std::vector<Entry> made;
  made.reserve(lines.size());
  int h, m, s, ms;
  localClock(h, m, s, ms);
  for (auto& l : lines) {
    Entry e;
    e.hour = h;
    e.minute = m;
    e.second = s;
    e.milli = ms;
    e.level = level;
    e.source = source.empty() ? "game" : source;
    e.text = std::move(l);
    made.push_back(std::move(e));
  }
  {
    std::lock_guard<std::mutex> g(st.mutex);
    for (auto& e : made) {
      e.sequence = st.next++;
      st.entries.push_back(e);
    }
    while (st.entries.size() > kCapacity) st.entries.pop_front();
  }
  for (auto& e : made) mirror(formatLine(e));
}

void writeOnce(Level level, const std::string& source, const std::string& text) {
  State& st = state();
  {
    std::lock_guard<std::mutex> g(st.mutex);
    if (st.once.size() >= 4000 || !st.once.insert(source + "\n" + text).second) return;
  }
  write(level, source, text);
}

void writef(Level level, const char* source, const char* fmt, ...) {
  char stack[1024];
  va_list ap;
  va_start(ap, fmt);
  va_list copy;
  va_copy(copy, ap);
  int n = std::vsnprintf(stack, sizeof stack, fmt, ap);
  va_end(ap);
  std::string text;
  if (n < 0) {
    text = fmt;
  } else if ((size_t)n < sizeof stack) {
    text.assign(stack, (size_t)n);
  } else {
    text.resize((size_t)n + 1);
    std::vsnprintf(text.data(), text.size(), fmt, copy);
    text.resize((size_t)n);
  }
  va_end(copy);
  write(level, source ? source : "game", text);
}

std::vector<Entry> snapshot(uint64_t sinceSequence) {
  State& st = state();
  std::lock_guard<std::mutex> g(st.mutex);
  std::vector<Entry> out;
  if (st.entries.empty()) return out;
  uint64_t first = st.entries.front().sequence;
  size_t start = sinceSequence + 1 > first ? (size_t)(sinceSequence + 1 - first) : 0;
  if (start >= st.entries.size()) return out;
  out.reserve(st.entries.size() - start);
  for (size_t i = start; i < st.entries.size(); i++) out.push_back(st.entries[i]);
  return out;
}

uint64_t lastSequence() {
  State& st = state();
  std::lock_guard<std::mutex> g(st.mutex);
  return st.next - 1;
}

uint64_t clearCount() {
  State& st = state();
  std::lock_guard<std::mutex> g(st.mutex);
  return st.cleared;
}

void clear() {
  State& st = state();
  std::lock_guard<std::mutex> g(st.mutex);
  st.entries.clear();
  st.cleared++;
}

bool exportToFile(const std::filesystem::path& file, std::string* err) {
  std::vector<Entry> all = snapshot(0);
  std::error_code ec;
  if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
  std::ofstream f(file, std::ios::binary);
  if (!f) {
    if (err) *err = "cannot write " + pathToUtf8(file);
    return false;
  }
  for (auto& e : all) f << formatLine(e) << "\n";
  f.flush();
  if (!f) {
    if (err) *err = "cannot write " + pathToUtf8(file);
    return false;
  }
  return true;
}

}
