#include "online/discord.h"
#include "mods/json.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace pp::online {
namespace {
using mods::Json;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr std::size_t MaxBody = 64 * 1024 - 8;

bool utf8(std::string_view text) {
  for (std::size_t i = 0; i < text.size();) {
    auto c = static_cast<unsigned char>(text[i++]);
    if (c < 0x80) continue;
    unsigned count = 0;
    std::uint32_t cp = 0;
    if (c >= 0xc2 && c <= 0xdf) { count = 1; cp = c & 0x1f; }
    else if (c >= 0xe0 && c <= 0xef) { count = 2; cp = c & 0x0f; }
    else if (c >= 0xf0 && c <= 0xf4) { count = 3; cp = c & 7; }
    else return false;
    if (i + count > text.size()) return false;
    for (unsigned k = 0; k < count; ++k) {
      auto next = static_cast<unsigned char>(text[i++]);
      if ((next & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (next & 0x3f);
    }
    if ((count == 1 && cp < 0x80) || (count == 2 && cp < 0x800) ||
        (count == 3 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
  }
  return true;
}

bool field(std::string_view text, std::size_t limit = 128) {
  return text.size() <= limit && utf8(text) &&
         std::none_of(text.begin(), text.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

bool normalize(std::string_view text, std::string& out) {
  if (!utf8(text)) return false;
  auto digit = [](char c) { return c >= '0' && c <= '9'; };
  std::size_t i = 0;
  auto hex = [&](unsigned& cp) {
    if (text.size() - i < 4) return false;
    auto result = std::from_chars(text.data() + i, text.data() + i + 4, cp, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + i + 4) return false;
    i += 4;
    return true;
  };
  while (i < text.size()) {
    char c = text[i++];
    if (c == '"') {
      out += c;
      bool ended = false;
      while (i < text.size()) {
        c = text[i++];
        if (static_cast<unsigned char>(c) < 0x20) return false;
        if (c == '"') { out += c; ended = true; break; }
        if (c != '\\') { out += c; continue; }
        auto start = i - 1;
        if (i == text.size()) return false;
        c = text[i++];
        if (c != 'u') {
          if (std::string_view("\"\\/bfnrt").find(c) == std::string_view::npos) return false;
          out.append(text.substr(start, 2));
          continue;
        }
        unsigned cp = 0;
        if (!hex(cp)) return false;
        if (cp >= 0xdc00 && cp <= 0xdfff) return false;
        if (cp >= 0xd800 && cp <= 0xdbff) {
          if (text.substr(i, 2) != "\\u") return false;
          i += 2;
          unsigned low = 0;
          if (!hex(low) || low < 0xdc00 || low > 0xdfff) return false;
          cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
          out += static_cast<char>(0xf0 | (cp >> 18));
          out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
          out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
          out += static_cast<char>(0x80 | (cp & 0x3f));
        } else out.append(text.substr(start, i - start));
      }
      if (!ended) return false;
    } else if (c == '-' || digit(c)) {
      auto start = i - 1;
      i = start;
      if (text[i] == '-') ++i;
      if (i == text.size() || !digit(text[i])) return false;
      if (text[i++] != '0') while (i < text.size() && digit(text[i])) ++i;
      if (i < text.size() && digit(text[i])) return false;
      if (i < text.size() && text[i] == '.') {
        ++i;
        auto fraction = i;
        while (i < text.size() && digit(text[i])) ++i;
        if (fraction == i) return false;
      }
      if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) ++i;
        auto exponent = i;
        while (i < text.size() && digit(text[i])) ++i;
        if (exponent == i) return false;
      }
      if (i < text.size() && std::string_view(" \t\r\n,]}").find(text[i]) == std::string_view::npos) return false;
      out.append(text.substr(start, i - start));
    } else {
      if (c == '+' || c == '.') return false;
      out += c;
    }
  }
  return true;
}

bool validTree(const Json& j) {
  if (j.t == Json::Num) return std::isfinite(j.n);
  if (j.t == Json::Str) return utf8(j.s);
  for (const auto& value : j.a) if (!validTree(value)) return false;
  std::unordered_set<std::string_view> keys;
  for (const auto& [key, value] : j.o) {
    if (!utf8(key) || !keys.insert(key).second || !validTree(value)) return false;
  }
  return true;
}

struct Pipe {
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
#else
  int handle = -1;
  bool connecting = false;
#endif
  ~Pipe() { close(); }
  bool valid() const {
#ifdef _WIN32
    return handle != INVALID_HANDLE_VALUE;
#else
    return handle >= 0;
#endif
  }
  void close() {
    if (!valid()) return;
#ifdef _WIN32
    CloseHandle(handle);
    handle = INVALID_HANDLE_VALUE;
#else
    ::close(handle);
    handle = -1;
    connecting = false;
#endif
  }
  bool open() {
#ifdef _WIN32
    for (int i = 0; i < 10; ++i) {
      auto path = std::wstring(L"\\\\?\\pipe\\discord-ipc-") + wchar_t(L'0' + i);
      handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | FILE_WRITE_ATTRIBUTES,
                           0, nullptr, OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
      if (!valid()) continue;
      DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
      if (SetNamedPipeHandleState(handle, &mode, nullptr, nullptr)) return true;
      close();
    }
#else
    const char* root = nullptr;
    for (auto key : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
      auto value = std::getenv(key);
      if (value && *value) { root = value; break; }
    }
    if (!root) root = "/tmp";
    if (std::strlen(root) > sizeof(sockaddr_un::sun_path) - 16) return false;
    for (int i = 0; i < 10; ++i) {
      handle = socket(AF_UNIX, SOCK_STREAM, 0);
      if (!valid()) return false;
      if (fcntl(handle, F_SETFL, O_NONBLOCK) < 0 || fcntl(handle, F_SETFD, FD_CLOEXEC) < 0) {
        close();
        return false;
      }
#ifdef SO_NOSIGPIPE
      int enabled = 1;
      if (setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) < 0) {
        close();
        return false;
      }
#endif
      sockaddr_un addr{};
      addr.sun_family = AF_UNIX;
      auto path = std::string(root) + "/discord-ipc-" + char('0' + i);
      std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
      if (::connect(handle, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0) return true;
      if (errno == EINPROGRESS) { connecting = true; return true; }
      close();
    }
#endif
    return false;
  }
  int connected() {
#ifndef _WIN32
    if (connecting) {
      pollfd fd{handle, POLLOUT, 0};
      int result = poll(&fd, 1, 0);
      if (result == 0 || (result < 0 && errno == EINTR)) return 0;
      if (result < 0) return -1;
      int error = 0;
      socklen_t size = sizeof(error);
      if (getsockopt(handle, SOL_SOCKET, SO_ERROR, &error, &size) < 0 || error) return -1;
      connecting = false;
    }
#endif
    return 1;
  }
  int write(const char* data, std::size_t size) {
#ifdef _WIN32
    DWORD count = 0;
    if (WriteFile(handle, data, static_cast<DWORD>(size), &count, nullptr)) return static_cast<int>(count);
    return GetLastError() == ERROR_NO_DATA ? 0 : -1;
#else
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    auto count = send(handle, data, size, flags);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
    return count <= 0 ? -1 : static_cast<int>(count);
#endif
  }
  int read(char* data, std::size_t size) {
#ifdef _WIN32
    DWORD count = 0;
    if (ReadFile(handle, data, static_cast<DWORD>(size), &count, nullptr)) return count ? static_cast<int>(count) : -1;
    return GetLastError() == ERROR_NO_DATA ? 0 : -1;
#else
    auto count = recv(handle, data, size, 0);
    if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
    return count <= 0 ? -1 : static_cast<int>(count);
#endif
  }
};

std::string frame(std::uint32_t opcode, std::string_view body) {
  std::string out(8, '\0');
  auto size = static_cast<std::uint32_t>(body.size());
  for (unsigned i = 0; i < 4; ++i) {
    out[i] = static_cast<char>(opcode >> (8 * i));
    out[i + 4] = static_cast<char>(size >> (8 * i));
  }
  out.append(body);
  return out;
}
}

struct Discord::Impl {
  std::string applicationId;
  Pipe pipe;
  Status status;
  std::deque<std::string> output;
  std::size_t written = 0;
  std::size_t queuedBytes = 0;
  std::array<char, 8> header{};
  std::size_t headerRead = 0;
  std::string body;
  std::size_t bodyRead = 0;
  std::uint32_t opcode = 0;
  std::uint32_t length = 0;
  std::uint64_t nonce = 0;
  std::string subscriptionNonce;
  std::string activityNonce;
  std::string desired = "null";
  std::string submitted;
  std::string join;
  Clock::time_point joinTime{};
  Clock::time_point retryAt{};
  Clock::time_point deadline{};
  Clock::time_point activityDeadline{};
  Clock::time_point subscriptionDeadline{};
  Clock::time_point stopDeadline{};
  Clock::time_point progressAt{};
  std::chrono::seconds backoff{1};
  bool cleared = false;
  bool stopping = false;

  explicit Impl(std::string id) {
    if (id.empty() || id.size() > 20) { status.state = State::Failed; status.error = "Invalid application ID"; return; }
    std::uint64_t value = 0;
    auto result = std::from_chars(id.data(), id.data() + id.size(), value);
    if (id.front() < '1' || id.front() > '9' ||
        result.ec != std::errc{} || result.ptr != id.data() + id.size() || value == 0) {
      status.state = State::Failed;
      status.error = "Invalid application ID";
      return;
    }
    applicationId = std::move(id);
  }

  void disconnect(std::string error, bool fatal = false) {
    pipe.close();
    output.clear();
    queuedBytes = written = headerRead = bodyRead = 0;
    body.clear();
    subscriptionNonce.clear();
    activityNonce.clear();
    submitted.clear();
    join.clear();
    status.activityConfirmed = status.joinSubscribed = false;
    if (!error.empty()) status.error = std::move(error);
    status.state = stopping ? State::Stopped : fatal ? State::Failed : State::Disconnected;
    retryAt = Clock::now() + backoff;
    backoff = std::min(backoff * 2, 30s);
  }

  bool enqueue(std::uint32_t op, std::string_view text) {
    if (text.size() > MaxBody || output.size() >= 8 || queuedBytes + text.size() + 8 > 2 * (MaxBody + 8)) {
      disconnect("IPC output limit exceeded");
      return false;
    }
    output.push_back(frame(op, text));
    queuedBytes += output.back().size();
    return true;
  }

  std::string nextNonce() { return std::to_string(++nonce); }

  void activity(bool force = false) {
    if (!pipe.valid() || (!force && (!activityNonce.empty() || submitted == desired))) return;
    auto command = Json::object();
    command.set("cmd", Json::str("SET_ACTIVITY"));
    auto args = Json::object();
#ifdef _WIN32
    args.set("pid", Json::num(GetCurrentProcessId()));
#else
    args.set("pid", Json::num(getpid()));
#endif
    Json value;
    if (!Json::parse(desired, value, nullptr)) { disconnect("Invalid activity", true); return; }
    args.set("activity", std::move(value));
    command.set("args", std::move(args));
    activityNonce = nextNonce();
    command.set("nonce", Json::str(activityNonce));
    status.activityConfirmed = false;
    activityDeadline = Clock::now() + 10s;
    submitted = desired;
    enqueue(1, command.dump());
  }

  static std::string_view string(const Json* value) {
    return value && value->t == Json::Str ? std::string_view(value->s) : std::string_view{};
  }

  bool message() {
    if (opcode == 3) return enqueue(4, body);
    if (opcode == 4) return true;
    if (opcode != 1 && opcode != 2) { disconnect("Invalid IPC opcode"); return false; }
    Json j;
    std::string normalized;
    if (!normalize(body, normalized) || !Json::parse(normalized, j, nullptr) || j.t != Json::Obj || !validTree(j)) { disconnect("Malformed IPC JSON"); return false; }
    auto event = string(j.get("evt"));
    auto command = string(j.get("cmd"));
    auto data = j.get("data");
    auto nonceField = j.get("nonce");
    if (nonceField && nonceField->t != Json::Null && nonceField->t != Json::Str) {
      disconnect("Invalid IPC nonce"); return false;
    }
    if (opcode == 2 || event == "ERROR") {
      auto error = opcode == 2 ? &j : data;
      status.errorCode = 0;
      if (error) {
        auto code = error->get("code");
        if (code && code->t == Json::Num && code->n >= 0 && code->n <= std::numeric_limits<int>::max() && std::floor(code->n) == code->n)
          status.errorCode = static_cast<int>(code->n);
      }
      status.errorCommand = field(command, 64) ? std::string(command) : std::string{};
      auto text = error ? string(error->get("message")) : std::string_view{};
      disconnect(text.empty() || !field(text, 512) ? "Discord rejected IPC request" : std::string(text), event == "ERROR" || status.errorCode != 0);
      return false;
    }
    if (status.state == State::Connecting) {
      if (command != "DISPATCH" || event != "READY") { disconnect("Expected READY"); return false; }
      status.state = State::Ready;
      status.error.clear();
      status.errorCommand.clear();
      status.errorCode = 0;
      auto sub = Json::object();
      sub.set("cmd", Json::str("SUBSCRIBE"));
      sub.set("evt", Json::str("ACTIVITY_JOIN"));
      subscriptionNonce = nextNonce();
      sub.set("nonce", Json::str(subscriptionNonce));
      subscriptionDeadline = Clock::now() + 10s;
      if (!enqueue(1, sub.dump())) return false;
      activity();
      return pipe.valid();
    }
    auto responseNonce = string(j.get("nonce"));
    if (!subscriptionNonce.empty() && responseNonce == subscriptionNonce && command == "SUBSCRIBE" && data && string(data->get("evt")) == "ACTIVITY_JOIN") {
      status.joinSubscribed = true;
      subscriptionNonce.clear();
      backoff = 1s;
    } else if (!activityNonce.empty() && responseNonce == activityNonce && command == "SET_ACTIVITY") {
      activityNonce.clear();
      status.activityConfirmed = submitted == desired;
      backoff = 1s;
      if (stopping && status.activityConfirmed) { disconnect({}); return false; }
    } else if (!stopping && !cleared && status.joinSubscribed && command == "DISPATCH" && event == "ACTIVITY_JOIN" && (!nonceField || nonceField->t == Json::Null)) {
      auto secret = data ? data->get("secret") : nullptr;
      if (!secret || secret->t != Json::Str || secret->s.empty() || !field(secret->s)) {
        disconnect("Invalid join event"); return false;
      }
      join = secret->s;
      joinTime = Clock::now();
    }
    return true;
  }

  void flush() {
    for (int i = 0; i < 16 && pipe.valid() && !output.empty(); ++i) {
      auto& front = output.front();
      int count = pipe.write(front.data() + written, front.size() - written);
      if (count < 0) { disconnect("IPC write failed"); return; }
      if (!count) return;
      progressAt = Clock::now();
      written += static_cast<std::size_t>(count);
      if (written == front.size()) { queuedBytes -= front.size(); output.pop_front(); written = 0; }
    }
  }

  void receive() {
    std::size_t budget = MaxBody + 8;
    int frames = 0;
    for (int i = 0; i < 32 && budget && frames < 16 && pipe.valid(); ++i) {
      bool readingHeader = headerRead < header.size();
      auto remaining = readingHeader ? header.size() - headerRead : length - bodyRead;
      auto destination = readingHeader ? header.data() + headerRead : body.data() + bodyRead;
      int count = pipe.read(destination, std::min(remaining, budget));
      if (count < 0) { disconnect("IPC peer closed"); return; }
      if (!count) return;
      progressAt = Clock::now();
      budget -= static_cast<std::size_t>(count);
      if (readingHeader) {
        headerRead += static_cast<std::size_t>(count);
        if (headerRead != header.size()) continue;
        auto le = [&](unsigned offset) {
          std::uint32_t value = 0;
          for (unsigned k = 0; k < 4; ++k) value |= std::uint32_t(static_cast<unsigned char>(header[offset + k])) << (k * 8);
          return value;
        };
        opcode = le(0);
        length = le(4);
        if (length > MaxBody) { disconnect("IPC frame too large"); return; }
        body.assign(length, '\0');
        bodyRead = 0;
      } else bodyRead += static_cast<std::size_t>(count);
      if (headerRead == header.size() && bodyRead == length) {
        ++frames;
        if (!message()) return;
        headerRead = bodyRead = 0;
        body.clear();
      }
    }
  }

  void pump() {
    auto now = Clock::now();
    if (status.state == State::Stopped || status.state == State::Failed) return;
    if (stopping && now >= stopDeadline) { disconnect({}); return; }
    if (!join.empty() && now - joinTime >= 5s) join.clear();
    if (status.state == State::Disconnected) {
      if (stopping) { disconnect({}); return; }
      if (now < retryAt) return;
      if (!pipe.open()) { disconnect("Discord IPC unavailable"); return; }
      status.state = State::Connecting;
      deadline = now + 10s;
      progressAt = now;
      auto handshake = Json::object();
      handshake.set("v", Json::num(1));
      handshake.set("client_id", Json::str(applicationId));
      if (!enqueue(0, handshake.dump())) return;
    }
    int connection = pipe.connected();
    if (connection < 0) { disconnect("IPC connect failed"); return; }
    if (now >= deadline && status.state == State::Connecting) { disconnect("IPC READY timeout"); return; }
    if (!connection) return;
    if ((!output.empty() || headerRead) && now - progressAt >= 10s) { disconnect("IPC progress timeout"); return; }
    if ((!activityNonce.empty() && now >= activityDeadline) || (!subscriptionNonce.empty() && now >= subscriptionDeadline)) {
      disconnect("IPC command timeout"); return;
    }
    flush();
    if (!pipe.valid()) return;
    receive();
    if (!pipe.valid()) return;
    if (status.state == State::Ready || status.state == State::Stopping) activity();
    flush();
  }
};

Discord::Discord(std::string applicationId) : impl_(std::make_unique<Impl>(std::move(applicationId))) {}
Discord::~Discord() { stop(); }
bool Discord::update(const Presence& presence) {
  auto& i = *impl_;
  if (i.stopping || i.status.state == State::Stopped || i.status.state == State::Failed) return false;
  if (!field(presence.details) || !field(presence.state) || !field(presence.partyId) || !field(presence.joinSecret)) return false;
  if (presence.joinable && (presence.partyId.empty() || presence.joinSecret.empty() || presence.partySize < 1 ||
                            presence.partyMax <= presence.partySize)) return false;
  auto a = Json::object();
  a.set("type", Json::num(0));
  if (!presence.details.empty()) a.set("details", Json::str(presence.details));
  if (!presence.state.empty()) a.set("state", Json::str(presence.state));
  if (presence.joinable) {
    auto party = Json::object();
    party.set("id", Json::str(presence.partyId));
    auto size = Json::array();
    size.a.push_back(Json::num(presence.partySize));
    size.a.push_back(Json::num(presence.partyMax));
    party.set("size", std::move(size));
    a.set("party", std::move(party));
    auto secrets = Json::object();
    secrets.set("join", Json::str(presence.joinSecret));
    a.set("secrets", std::move(secrets));
    a.set("instance", Json::boolean(true));
  }
  auto text = a.dump();
  if (i.cleared) {
    i.disconnect({});
    i.retryAt = Clock::now();
    i.cleared = false;
  }
  if (i.desired != text) { i.desired = std::move(text); i.status.activityConfirmed = false; i.join.clear(); }
  return true;
}
void Discord::pump() { impl_->pump(); }
Discord::Status Discord::status() const { return impl_->status; }
std::string Discord::takeJoinRequest() {
  auto& i = *impl_;
  if (i.cleared || i.stopping || i.status.state != State::Ready || Clock::now() - i.joinTime >= 5s) i.join.clear();
  return std::exchange(i.join, {});
}
void Discord::clear() {
  auto& i = *impl_;
  i.join.clear();
  if (i.cleared) return;
  i.cleared = true;
  i.desired = "null";
  i.status.activityConfirmed = false;
  if (i.status.state == State::Ready) i.activity(true);
}
void Discord::stop() {
  auto& i = *impl_;
  if (i.stopping || i.status.state == State::Stopped) return;
  if (i.status.state != State::Ready) { i.stopping = true; i.desired = "null"; i.disconnect({}); return; }
  bool alreadyClear = i.desired == "null" && i.status.activityConfirmed;
  clear();
  i.stopping = true;
  i.status.state = i.pipe.valid() ? State::Stopping : State::Stopped;
  i.stopDeadline = Clock::now() + 250ms;
  if (i.pipe.valid()) i.flush();
  if (alreadyClear || !i.pipe.valid()) i.disconnect({});
}

}
