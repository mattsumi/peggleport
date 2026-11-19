#pragma once
#include <cstdint>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
using socklen_arg = int;
inline void closeSock(sock_t s) { closesocket(s); }
inline void nonBlock(sock_t s) { u_long m = 1; ioctlsocket(s, FIONBIO, &m); }
inline bool wouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#define BAD_SOCK INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
using socklen_arg = size_t;
inline void closeSock(sock_t s) { close(s); }
inline void nonBlock(sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
inline bool wouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS; }
#define BAD_SOCK (-1)
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

inline void tuneSock(sock_t s) {
  int one = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
#ifdef SO_NOSIGPIPE
  setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  nonBlock(s);
}

inline void netStartup() {
#ifdef _WIN32
  static const bool done = [] {
    WSADATA w;
    return WSAStartup(MAKEWORD(2, 2), &w) == 0;
  }();
  (void)done;
#endif
}
