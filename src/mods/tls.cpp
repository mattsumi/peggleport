#include "mods/tls.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
extern "C" {
#include "mbedtls/psa_util.h"
}

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace pp::mods {
namespace {

using Clock = std::chrono::steady_clock;

std::mutex gTlsMu;

int certCount(const mbedtls_x509_crt* c) {
  int n = 0;
  if (!c->raw.p) return 0;
  for (const mbedtls_x509_crt* p = c; p; p = p->next) n++;
  return n;
}

#ifdef __APPLE__
void loadSystemRoots(mbedtls_x509_crt* chain) {
  CFArrayRef anchors = nullptr;
  if (SecTrustCopyAnchorCertificates(&anchors) != errSecSuccess || !anchors) return;
  CFIndex n = CFArrayGetCount(anchors);
  for (CFIndex i = 0; i < n; i++) {
    SecCertificateRef cert = (SecCertificateRef)CFArrayGetValueAtIndex(anchors, i);
    CFDataRef der = cert ? SecCertificateCopyData(cert) : nullptr;
    if (!der) continue;
    mbedtls_x509_crt_parse_der(chain, CFDataGetBytePtr(der), (size_t)CFDataGetLength(der));
    CFRelease(der);
  }
  CFRelease(anchors);
}
#elif defined(_WIN32)
void loadSystemRoots(mbedtls_x509_crt* chain) {
  HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
  if (!store) return;
  for (PCCERT_CONTEXT c = CertEnumCertificatesInStore(store, nullptr); c; c = CertEnumCertificatesInStore(store, c))
    mbedtls_x509_crt_parse_der(chain, c->pbCertEncoded, (size_t)c->cbCertEncoded);
  CertCloseStore(store, 0);
}
#else
void loadSystemRoots(mbedtls_x509_crt* chain) {
  static const char* const files[] = {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem", "/etc/ssl/ca-bundle.pem",
                                      "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem", "/usr/local/share/certs/ca-root-nss.crt"};
  for (const char* f : files) {
    mbedtls_x509_crt_parse_file(chain, f);
    if (certCount(chain) > 0) return;
  }
}
#endif

struct Roots {
  mbedtls_x509_crt ca;
  mbedtls_ssl_config conf;
  bool ok = false;
  std::string why;
};

Roots* buildRoots() {
  Roots* r = new Roots;
  std::lock_guard<std::mutex> g(gTlsMu);
  mbedtls_x509_crt_init(&r->ca);
  mbedtls_ssl_config_init(&r->conf);
  if (psa_crypto_init() != PSA_SUCCESS) {
    r->why = "TLS is unavailable: the crypto library could not start";
    return r;
  }
  loadSystemRoots(&r->ca);
  if (certCount(&r->ca) == 0) {
    r->why = "TLS is unavailable: no trusted root certificates were found on this computer";
    return r;
  }
  if (mbedtls_ssl_config_defaults(&r->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
    r->why = "TLS is unavailable: the TLS settings could not be set up";
    return r;
  }
  mbedtls_ssl_conf_authmode(&r->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_min_tls_version(&r->conf, MBEDTLS_SSL_VERSION_TLS1_2);
  mbedtls_ssl_conf_max_tls_version(&r->conf, MBEDTLS_SSL_VERSION_TLS1_3);
  mbedtls_ssl_conf_ca_chain(&r->conf, &r->ca, nullptr);
  mbedtls_ssl_conf_rng(&r->conf, mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);
  r->ok = true;
  return r;
}

Roots& roots() {
  static Roots* r = buildRoots();
  return *r;
}

bool lastIsRetry() {
#ifdef _WIN32
  return false;
#else
  return errno == EINTR;
#endif
}

int bioSend(void* ctx, const unsigned char* buf, size_t len) {
  sock_t s = *(sock_t*)ctx;
  int n = (int)send(s, (const char*)buf, (int)std::min<size_t>(len, 1u << 20), MSG_NOSIGNAL);
  if (n >= 0) return n;
  if (wouldBlock() || lastIsRetry()) return MBEDTLS_ERR_SSL_WANT_WRITE;
  return MBEDTLS_ERR_NET_SEND_FAILED;
}

int bioRecv(void* ctx, unsigned char* buf, size_t len) {
  sock_t s = *(sock_t*)ctx;
  int n = (int)recv(s, (char*)buf, (int)std::min<size_t>(len, 1u << 20), 0);
  if (n >= 0) return n;
  if (wouldBlock() || lastIsRetry()) return MBEDTLS_ERR_SSL_WANT_READ;
  return MBEDTLS_ERR_NET_RECV_FAILED;
}

void waitSock(sock_t s, bool forWrite, int ms) {
  if (ms < 0) ms = 0;
  fd_set f;
  FD_ZERO(&f);
  FD_SET(s, &f);
  timeval tv{ms / 1000, (ms % 1000) * 1000};
  select((int)s + 1, forWrite ? nullptr : &f, forWrite ? &f : nullptr, nullptr, &tv);
}

int msLeft(Clock::time_point deadline) {
  auto d = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
  return d < 0 ? 0 : (d > 1000000 ? 1000000 : (int)d);
}

std::string certProblems(uint32_t f, const std::string& host) {
  std::string o;
  auto add = [&](const std::string& s) {
    if (!o.empty()) o += "; ";
    o += s;
  };
  if (f & MBEDTLS_X509_BADCERT_EXPIRED) add("the certificate has expired");
  if (f & MBEDTLS_X509_BADCERT_FUTURE) add("the certificate is not valid yet (check the date on this computer)");
  if (f & MBEDTLS_X509_BADCERT_CN_MISMATCH) add("the certificate is not valid for " + host);
  if (f & MBEDTLS_X509_BADCERT_NOT_TRUSTED) add("the certificate is not signed by a trusted authority");
  if (f & MBEDTLS_X509_BADCERT_REVOKED) add("the certificate was revoked");
  if (f & (MBEDTLS_X509_BADCERT_BAD_MD | MBEDTLS_X509_BADCERT_BAD_PK | MBEDTLS_X509_BADCERT_BAD_KEY)) add("the certificate uses a weak signature or key");
  if (f & (MBEDTLS_X509_BADCERT_KEY_USAGE | MBEDTLS_X509_BADCERT_EXT_KEY_USAGE)) add("the certificate is not meant for a web server");
  if (f & MBEDTLS_X509_BADCERT_MISSING) add("the server sent no certificate");
  if (o.empty()) {
    char buf[256];
    int n = mbedtls_x509_crt_verify_info(buf, sizeof buf, "", f);
    std::string t = n > 0 ? std::string(buf, (size_t)n) : std::string();
    while (!t.empty() && (t.back() == '\n' || t.back() == ' ')) t.pop_back();
    add(t.empty() ? "the certificate is not acceptable" : t);
  }
  return o;
}

std::string describe(int ret, uint32_t verify, const std::string& host, bool handshake) {
  if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) return "TLS certificate check failed: " + certProblems(verify, host);
  switch (ret) {
    case MBEDTLS_ERR_SSL_BAD_PROTOCOL_VERSION:
      return "TLS handshake failed: the server does not offer TLS 1.2 or newer";
    case MBEDTLS_ERR_SSL_HANDSHAKE_FAILURE:
      return "TLS handshake failed: no encryption settings in common with the server";
    case MBEDTLS_ERR_SSL_CONN_EOF:
      return handshake ? "TLS handshake failed: the server closed the connection" : "TLS: the server closed the connection without a close_notify";
    case MBEDTLS_ERR_NET_CONN_RESET:
    case MBEDTLS_ERR_NET_RECV_FAILED:
    case MBEDTLS_ERR_NET_SEND_FAILED:
      return handshake ? "TLS handshake failed: the connection was lost" : "TLS: the connection was lost";
    case MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE:
      return handshake ? "TLS handshake failed: the server refused the connection (TLS alert)" : "TLS: the server sent an error alert";
    default:
      break;
  }
  char buf[200];
  mbedtls_strerror(ret, buf, sizeof buf);
  std::string text = buf;
  size_t dash = text.find(" - ");
  if (dash != std::string::npos && dash <= 8) text = text.substr(dash + 3);
  return std::string(handshake ? "TLS handshake failed: " : "TLS error: ") + text;
}

}

struct TlsStream::Impl {
  sock_t sock = BAD_SOCK;
  const std::atomic<bool>* stop = nullptr;
  mbedtls_ssl_context ssl;
  bool live = false;
  bool ended = false;
  bool notified = false;
  Status last = Status::Ok;
  std::string lastErr;
  ~Impl() {
    if (!live) return;
    std::lock_guard<std::mutex> g(gTlsMu);
    mbedtls_ssl_free(&ssl);
  }
  bool stopped() const { return stop && stop->load(); }
  Status end(Status s, const std::string& e, std::string* err) {
    ended = true;
    last = s;
    lastErr = e;
    if (err) *err = e;
    return s;
  }
};

TlsStream::TlsStream(std::unique_ptr<Impl> p) : p_(std::move(p)) {}

TlsStream::~TlsStream() = default;

std::unique_ptr<TlsStream> TlsStream::connect(sock_t s, const std::string& host, int timeoutMs, const std::atomic<bool>* stop, std::string* err) {
  Roots& r = roots();
  if (!r.ok) {
    *err = r.why;
    return nullptr;
  }
  if (host.empty() || host.size() > MBEDTLS_SSL_MAX_HOST_NAME_LEN) {
    *err = "TLS: bad host name";
    return nullptr;
  }
  std::unique_ptr<Impl> im(new Impl);
  im->sock = s;
  im->stop = stop;
  int setup;
  {
    std::lock_guard<std::mutex> g(gTlsMu);
    mbedtls_ssl_init(&im->ssl);
    im->live = true;
    setup = mbedtls_ssl_setup(&im->ssl, &r.conf);
    if (setup == 0) setup = mbedtls_ssl_set_hostname(&im->ssl, host.c_str());
    if (setup == 0) mbedtls_ssl_set_bio(&im->ssl, &im->sock, bioSend, bioRecv, nullptr);
  }
  if (setup != 0) {
    *err = describe(setup, 0, host, true);
    return nullptr;
  }
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
  for (;;) {
    int ret;
    uint32_t vr = 0;
    {
      std::lock_guard<std::mutex> g(gTlsMu);
      ret = mbedtls_ssl_handshake(&im->ssl);
      if (ret != 0) vr = mbedtls_ssl_get_verify_result(&im->ssl);
    }
    if (ret == 0) break;
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
      if (im->stopped()) {
        *err = "TLS handshake cancelled";
        return nullptr;
      }
      int left = msLeft(deadline);
      if (left <= 0) {
        *err = "TLS handshake timed out";
        return nullptr;
      }
      waitSock(s, ret == MBEDTLS_ERR_SSL_WANT_WRITE, std::min(left, 50));
      continue;
    }
    *err = describe(ret, vr, host, true);
    return nullptr;
  }
  uint32_t vr;
  {
    std::lock_guard<std::mutex> g(gTlsMu);
    vr = mbedtls_ssl_get_verify_result(&im->ssl);
  }
  if (vr != 0) {
    *err = "TLS certificate check failed: " + certProblems(vr, host);
    return nullptr;
  }
  return std::unique_ptr<TlsStream>(new TlsStream(std::move(im)));
}

bool TlsStream::randomBytes(unsigned char* data, size_t len) {
  if (!roots().ok) return false;
  std::lock_guard<std::mutex> g(gTlsMu);
  return mbedtls_psa_get_random(MBEDTLS_PSA_RANDOM_STATE, data, len) == 0;
}

void TlsStream::clearCancellation() { p_->stop = nullptr; }

TlsStream::Status TlsStream::writeSome(const char* data, size_t len, size_t* sent, std::string* err) {
  Impl& m = *p_;
  *sent = 0;
  if (m.ended) {
    if (err) *err = m.lastErr;
    return m.last;
  }
  int ret;
  {
    std::lock_guard<std::mutex> g(gTlsMu);
    ret = mbedtls_ssl_write(&m.ssl, (const unsigned char*)data, std::min<size_t>(len, 16384));
  }
  if (ret > 0) { *sent = (size_t)ret; return Status::Ok; }
  if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE || ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) return Status::Idle;
  return m.end(Status::Failed, describe(ret, 0, std::string(), false), err);
}

TlsStream::Status TlsStream::read(char* buf, size_t cap, size_t* got, int waitMs, std::string* err) {
  Impl& m = *p_;
  *got = 0;
  if (m.ended) {
    if (err) *err = m.lastErr;
    return m.last;
  }
  const auto deadline = Clock::now() + std::chrono::milliseconds(waitMs < 0 ? 0 : waitMs);
  for (;;) {
    int ret;
    {
      std::lock_guard<std::mutex> g(gTlsMu);
      ret = mbedtls_ssl_read(&m.ssl, (unsigned char*)buf, cap);
    }
    if (ret > 0) {
      *got = (size_t)ret;
      return Status::Ok;
    }
    if (ret == 0 || ret == MBEDTLS_ERR_SSL_CONN_EOF) return m.end(Status::Cut, "TLS: the server closed the connection without a close_notify", err);
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return m.end(Status::Closed, "", err);
    if (ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
      if (m.stopped()) return Status::Idle;
      int left = msLeft(deadline);
      if (left <= 0) return Status::Idle;
      waitSock(m.sock, ret == MBEDTLS_ERR_SSL_WANT_WRITE, std::min(left, 50));
      continue;
    }
    return m.end(Status::Failed, describe(ret, 0, std::string(), false), err);
  }
}

bool TlsStream::writeAll(const char* data, size_t len, int idleMs, std::string* err) {
  Impl& m = *p_;
  if (m.ended) {
    if (err) *err = m.last == Status::Closed ? "TLS: the server closed the connection" : m.lastErr;
    return false;
  }
  size_t off = 0;
  auto progress = Clock::now();
  while (off < len) {
    size_t chunk = std::min<size_t>(len - off, 16384);
    int ret;
    {
      std::lock_guard<std::mutex> g(gTlsMu);
      ret = mbedtls_ssl_write(&m.ssl, (const unsigned char*)data + off, chunk);
    }
    if (ret > 0) {
      off += (size_t)ret;
      progress = Clock::now();
      continue;
    }
    if (ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
      if (m.stopped()) {
        if (err) *err = "send cancelled";
        return false;
      }
      if (Clock::now() - progress > std::chrono::milliseconds(idleMs)) {
        if (err) *err = "TLS: sending timed out";
        return false;
      }
      waitSock(m.sock, ret == MBEDTLS_ERR_SSL_WANT_WRITE, 20);
      continue;
    }
    m.end(Status::Failed, describe(ret, 0, std::string(), false), err);
    return false;
  }
  return true;
}

void TlsStream::shutdown() {
  Impl& m = *p_;
  if (m.notified || (m.ended && m.last != Status::Closed)) return;
  m.notified = true;
  const auto deadline = Clock::now() + std::chrono::milliseconds(300);
  for (;;) {
    int ret;
    {
      std::lock_guard<std::mutex> g(gTlsMu);
      ret = mbedtls_ssl_close_notify(&m.ssl);
    }
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) return;
    int left = msLeft(deadline);
    if (left <= 0 || m.stopped()) return;
    waitSock(m.sock, ret == MBEDTLS_ERR_SSL_WANT_WRITE, std::min(left, 20));
  }
}

}
