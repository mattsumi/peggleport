#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#define PP_LOG_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define PP_LOG_PRINTF(fmt, args)
#endif

namespace pp::log {

enum class Level { Info, Warn, Error };

struct Entry {
  uint64_t sequence = 0;
  int hour = 0, minute = 0, second = 0, milli = 0;
  Level level = Level::Info;
  std::string source;
  std::string text;
};

constexpr size_t kCapacity = 5000;

void write(Level level, const std::string& source, const std::string& text);
void writef(Level level, const char* source, const char* fmt, ...) PP_LOG_PRINTF(3, 4);

void writeOnce(Level level, const std::string& source, const std::string& text);

inline void info(const std::string& source, const std::string& text) { write(Level::Info, source, text); }
inline void warn(const std::string& source, const std::string& text) { write(Level::Warn, source, text); }
inline void error(const std::string& source, const std::string& text) { write(Level::Error, source, text); }

std::vector<Entry> snapshot(uint64_t sinceSequence = 0);
uint64_t lastSequence();
uint64_t clearCount();
void clear();

const char* levelName(Level level);
std::string timeText(const Entry& e, bool withMillis = false);
std::string formatLine(const Entry& e);
bool exportToFile(const std::filesystem::path& file, std::string* err = nullptr);

}
