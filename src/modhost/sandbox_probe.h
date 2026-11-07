#pragma once
#ifndef _WIN32
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <spawn.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdint>
#include <string>

extern char** environ;

namespace pp::modhost::probe {

inline bool denied(int e) { return e == EPERM || e == EACCES || e == EROFS; }

struct Before {
  std::string readable;
};

inline Before before() {
  Before b;
  for (const char* p : {"/etc/passwd", "/etc/hosts", "/bin/sh"}) {
    int fd = open(p, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
      close(fd);
      b.readable = p;
      break;
    }
  }
  return b;
}

inline bool connectDenied(int family) {
  int s = socket(family, SOCK_STREAM, 0);
  if (s < 0) return denied(errno) || errno == EAFNOSUPPORT;
  int rc = -1, e = 0;
  if (family == AF_INET) {
    sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(1);
    a.sin_addr.s_addr = htonl(0x7f000001u);
    rc = connect(s, (sockaddr*)&a, sizeof a);
    e = errno;
  } else if (family == AF_INET6) {
    sockaddr_in6 a;
    memset(&a, 0, sizeof a);
    a.sin6_family = AF_INET6;
    a.sin6_port = htons(1);
    a.sin6_addr = in6addr_loopback;
    rc = connect(s, (sockaddr*)&a, sizeof a);
    e = errno;
  } else {
    sockaddr_un a;
    memset(&a, 0, sizeof a);
    a.sun_family = AF_UNIX;
    strncpy(a.sun_path, "/var/run/pp-modhost-probe", sizeof a.sun_path - 1);
    rc = connect(s, (sockaddr*)&a, sizeof a);
    e = errno;
  }
  close(s);
  return rc != 0 && denied(e);
}

inline bool bindDenied() {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return denied(errno);
  sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = 0;
  a.sin_addr.s_addr = htonl(0x7f000001u);
  int rc = bind(s, (sockaddr*)&a, sizeof a);
  int e = errno;
  close(s);
  return rc != 0 && denied(e);
}

inline bool sendDenied() {
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) return denied(errno);
  sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(9);
  a.sin_addr.s_addr = htonl(0x7f000001u);
  ssize_t rc = sendto(s, "x", 1, 0, (sockaddr*)&a, sizeof a);
  int e = errno;
  close(s);
  return rc < 0 && denied(e);
}

inline bool noNetwork() {
  return connectDenied(AF_INET) && connectDenied(AF_INET6) && connectDenied(AF_UNIX) && bindDenied() && sendDenied();
}

inline bool writeDenied(const std::string& path) {
  int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd >= 0) {
    close(fd);
    unlink(path.c_str());
    return false;
  }
  return denied(errno);
}

inline bool noFileWrite(const std::string& pluginDir) {
  std::string tag = ".pp-modhost-probe-" + std::to_string((long)getpid());
  if (!writeDenied("/tmp/" + tag)) return false;
  if (!pluginDir.empty() && !writeDenied(pluginDir + "/" + tag)) return false;
  std::string d = "/tmp/" + tag + "d";
  if (mkdir(d.c_str(), 0700) == 0) {
    rmdir(d.c_str());
    return false;
  }
  return denied(errno);
}

inline bool noSpawn() {
  pid_t f = fork();
  if (f == 0) _exit(0);
  if (f > 0) {
    int st = 0;
    waitpid(f, &st, 0);
    return false;
  }
  if (!denied(errno)) return false;
  pid_t sp = 0;
  char a0[] = "sh", a1[] = "-c", a2[] = "exit 0";
  char* argv[] = {a0, a1, a2, nullptr};
  int rc = posix_spawn(&sp, "/bin/sh", nullptr, nullptr, argv, environ);
  if (rc == 0) {
    int st = 0;
    waitpid(sp, &st, 0);
    return false;
  }
  return denied(rc);
}

inline bool readRestricted(const Before& b) {
  if (b.readable.empty()) return false;
  int fd = open(b.readable.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd >= 0) {
    close(fd);
    return false;
  }
  if (!denied(errno)) return false;
  DIR* d = opendir("/");
  if (d) {
    closedir(d);
    return false;
  }
  return denied(errno);
}

}
#endif
