#include "mods/hostproc.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "mods/hostproto.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace fs = std::filesystem;
namespace pp::mods {

using Clock = std::chrono::steady_clock;

struct HostProcess::Impl {
  pid_t pid = -1;
  int fd = -1;
  bool reaped = false;
  int status = 0;
  std::vector<uint8_t> rbuf;
  size_t rpos = 0, rend = 0;
  std::string bad;
};

namespace {
int msLeft(Clock::time_point deadline) {
  auto d = std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now()).count();
  if (d <= 0) return 0;
  return (int)std::min<long long>((d + 999) / 1000, 1 << 30);
}

void reap(HostProcess::Impl& I, bool block) {
  if (I.pid <= 0 || I.reaped) return;
  for (;;) {
    int st = 0;
    pid_t r = waitpid(I.pid, &st, block ? 0 : WNOHANG);
    if (r == I.pid) {
      I.reaped = true;
      I.status = st;
      return;
    }
    if (r < 0 && errno == EINTR) continue;
    if (r < 0) {
      I.reaped = true;
      return;
    }
    return;
  }
}
}

std::unique_ptr<HostProcess> HostProcess::start(const fs::path& exe, std::string* err) {
  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
    if (err) *err = std::string("socketpair failed: ") + std::strerror(errno);
    return nullptr;
  }
  fcntl(sv[0], F_SETFD, FD_CLOEXEC);
  fcntl(sv[1], F_SETFD, FD_CLOEXEC);
  int buf = 1 << 18;
  for (int s : {sv[0], sv[1]}) {
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, &buf, sizeof buf);
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, &buf, sizeof buf);
  }
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(sv[0], SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  int child = fcntl(sv[1], F_DUPFD_CLOEXEC, 10);
  close(sv[1]);
  if (child < 0) {
    close(sv[0]);
    if (err) *err = std::string("fcntl failed: ") + std::strerror(errno);
    return nullptr;
  }

  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
  posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
  posix_spawn_file_actions_adddup2(&fa, child, 3);
  posix_spawnattr_t at;
  posix_spawnattr_init(&at);
  sigset_t none, all;
  sigemptyset(&none);
  sigfillset(&all);
  posix_spawnattr_setsigmask(&at, &none);
  posix_spawnattr_setsigdefault(&at, &all);
  short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
  flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
  posix_spawnattr_setflags(&at, flags);

  std::string path = exe.string(), pid = std::to_string((long)getpid());
  std::string a1 = "--serve", a2 = "3";
  char* argv[] = {path.data(), a1.data(), a2.data(), pid.data(), nullptr};
  char* envp[] = {nullptr};
  pid_t cp = -1;
  int rc = posix_spawn(&cp, path.c_str(), &fa, &at, argv, envp);
  posix_spawn_file_actions_destroy(&fa);
  posix_spawnattr_destroy(&at);
  close(child);
  if (rc != 0) {
    close(sv[0]);
    if (err) *err = "cannot start " + path + ": " + std::strerror(rc);
    return nullptr;
  }
  fcntl(sv[0], F_SETFL, fcntl(sv[0], F_GETFL, 0) | O_NONBLOCK);
  auto p = std::unique_ptr<HostProcess>(new HostProcess());
  p->im_ = std::make_unique<Impl>();
  p->im_->pid = cp;
  p->im_->fd = sv[0];
  p->im_->rbuf.resize(1 << 16);
  return p;
}

HostProcess::~HostProcess() {
  if (im_) stop();
}

void HostProcess::stop(bool force) {
  Impl& I = *im_;
  if (I.fd >= 0) {
    close(I.fd);
    I.fd = -1;
  }
  if (I.pid > 0 && !I.reaped) {
    if (force) kill(I.pid, SIGKILL);
    reap(I, false);
    if (!I.reaped && !force) {
      auto until = Clock::now() + std::chrono::milliseconds(400);
      while (!I.reaped && Clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        reap(I, false);
      }
    }
    if (!I.reaped) {
      kill(I.pid, SIGKILL);
      reap(I, true);
    }
  }
}

bool HostProcess::running() const {
  Impl& I = *im_;
  if (I.fd < 0 || I.pid <= 0 || I.reaped) return false;
  reap(I, false);
  return !I.reaped;
}

std::string HostProcess::problem() const { return im_->bad; }

std::string HostProcess::exitText() {
  Impl& I = *im_;
  if (I.pid > 0 && !I.reaped) {
    auto until = Clock::now() + std::chrono::milliseconds(200);
    while (!I.reaped && Clock::now() < until) {
      reap(I, false);
      if (!I.reaped) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
  if (!I.reaped) return "the helper process did not exit";
  if (WIFSIGNALED(I.status)) {
    int s = WTERMSIG(I.status);
    const char* n = strsignal(s);
    return "the helper process was stopped by signal " + std::to_string(s) + (n ? std::string(" (") + n + ")" : std::string());
  }
  if (WIFEXITED(I.status)) return "the helper process exited with code " + std::to_string(WEXITSTATUS(I.status));
  return "the helper process ended";
}

IoResult HostProcess::send(const std::vector<uint8_t>& frame, int timeoutMs) {
  Impl& I = *im_;
  if (I.fd < 0) return IoResult::Closed;
  auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
  const uint8_t* p = frame.data();
  size_t n = frame.size();
  while (n) {
    ssize_t w = ::send(I.fd, p, n, MSG_NOSIGNAL);
    if (w > 0) {
      p += w;
      n -= (size_t)w;
      continue;
    }
    if (w < 0 && errno == EINTR) continue;
    if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      int left = msLeft(deadline);
      if (left <= 0) return IoResult::Timeout;
      pollfd pf;
      pf.fd = I.fd;
      pf.events = POLLOUT;
      pf.revents = 0;
      int r = poll(&pf, 1, left);
      if (r == 0) return IoResult::Timeout;
      if (r < 0 && errno != EINTR) return IoResult::Closed;
      if (r > 0 && (pf.revents & (POLLERR | POLLHUP | POLLNVAL))) return IoResult::Closed;
      continue;
    }
    return IoResult::Closed;
  }
  return IoResult::Ok;
}

IoResult HostProcess::receive(std::vector<uint8_t>& out, int timeoutMs) {
  Impl& I = *im_;
  if (I.fd < 0) return IoResult::Closed;
  auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
  auto spinUntil = Clock::now() + std::chrono::microseconds(40);
  for (;;) {
    size_t have = I.rend - I.rpos;
    if (have >= 4) {
      uint32_t len;
      std::memcpy(&len, I.rbuf.data() + I.rpos, 4);
      if (len < 1 || len > pp::modhost::kMaxFrame) {
        I.bad = "the helper sent a frame with a bad length";
        return IoResult::Closed;
      }
      if (have >= 4 + (size_t)len) {
        out.assign(I.rbuf.begin() + (long)I.rpos + 4, I.rbuf.begin() + (long)I.rpos + 4 + (long)len);
        I.rpos += 4 + len;
        if (I.rpos == I.rend) I.rpos = I.rend = 0;
        return IoResult::Ok;
      }
      size_t need = 4 + (size_t)len;
      if (I.rbuf.size() - I.rpos < need) {
        std::memmove(I.rbuf.data(), I.rbuf.data() + I.rpos, have);
        I.rend = have;
        I.rpos = 0;
        if (I.rbuf.size() < need) I.rbuf.resize(need + (1 << 14));
      }
    } else if (I.rpos > 0) {
      std::memmove(I.rbuf.data(), I.rbuf.data() + I.rpos, have);
      I.rend = have;
      I.rpos = 0;
    }
    if (I.rend == I.rbuf.size()) I.rbuf.resize(I.rbuf.size() * 2);
    ssize_t n = ::recv(I.fd, I.rbuf.data() + I.rend, I.rbuf.size() - I.rend, 0);
    if (n > 0) {
      I.rend += (size_t)n;
      continue;
    }
    if (n == 0) return IoResult::Closed;
    if (errno == EINTR) continue;
    if (errno != EAGAIN && errno != EWOULDBLOCK) return IoResult::Closed;
    if (Clock::now() < spinUntil) continue;
    int left = msLeft(deadline);
    if (left <= 0) return IoResult::Timeout;
    pollfd pf;
    pf.fd = I.fd;
    pf.events = POLLIN;
    pf.revents = 0;
    int r = poll(&pf, 1, left);
    if (r == 0) return IoResult::Timeout;
    if (r < 0 && errno != EINTR) return IoResult::Closed;
  }
}

fs::path findModHost() {
  std::error_code ec;
  fs::path self;
#ifdef __APPLE__
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buf(size + 1, '\0');
  if (_NSGetExecutablePath(buf.data(), &size) == 0) self = fs::path(buf.c_str());
#else
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n > 0) self = fs::path(std::string(buf, (size_t)n));
#endif
  if (self.empty()) return {};
  fs::path canon = fs::canonical(self, ec);
  if (!ec) self = canon;
  fs::path host = self.parent_path() / "pp_modhost";
  if (fs::is_regular_file(host, ec)) return host;
  return {};
}

}
