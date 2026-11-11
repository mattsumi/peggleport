#include <cstring>
#include <fstream>
#include <cstdint>
#include <vector>
#include <string>
#include <filesystem>

#include "mods/manifest.h"

namespace fs = std::filesystem;
namespace pp::mods {
namespace {
struct Bits {
  const uint8_t* p;
  size_t n, pos = 0;
  uint32_t buf = 0;
  int cnt = 0;
  bool bad = false;
  int bit() {
    if (cnt == 0) {
      if (pos >= n) { bad = true; return 0; }
      buf = p[pos++];
      cnt = 8;
    }
    int b = buf & 1;
    buf >>= 1;
    cnt--;
    return b;
  }
  uint32_t bits(int k) { uint32_t v = 0; for (int i = 0; i < k; i++) v |= (uint32_t)bit() << i; return v; }
};
struct Huff {
  uint16_t count[16]{}, symbol[320]{};
};
bool build(Huff& h, const uint8_t* len, int n) {
  std::memset(h.count, 0, sizeof h.count);
  for (int i = 0; i < n; i++) h.count[len[i]]++;
  if (h.count[0] == n) return true;
  int left = 1;
  for (int l = 1; l < 16; l++) { left <<= 1; left -= h.count[l]; if (left < 0) return false; }
  uint16_t offs[16];
  offs[1] = 0;
  for (int l = 1; l < 15; l++) offs[l + 1] = (uint16_t)(offs[l] + h.count[l]);
  for (int i = 0; i < n; i++) if (len[i]) h.symbol[offs[len[i]]++] = (uint16_t)i;
  return true;
}
int decode(Bits& s, const Huff& h) {
  int code = 0, first = 0, index = 0;
  for (int l = 1; l < 16; l++) {
    code |= s.bit();
    int c = h.count[l];
    if (code - c < first) return h.symbol[index + (code - first)];
    index += c;
    first += c;
    first <<= 1;
    code <<= 1;
    if (s.bad) return -1;
  }
  return -1;
}
const uint16_t kLBase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint16_t kLExt[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDBase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint16_t kDExt[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
bool codes(Bits& s, std::vector<uint8_t>& out, const Huff& lc, const Huff& dc, size_t limit) {
  for (;;) {
    int sym = decode(s, lc);
    if (sym < 0 || s.bad) return false;
    if (sym < 256) {
      if (out.size() >= limit) return false;
      out.push_back((uint8_t)sym);
    } else if (sym == 256) return true;
    else {
      sym -= 257;
      if (sym >= 29) return false;
      int len = kLBase[sym] + (int)s.bits(kLExt[sym]);
      int ds = decode(s, dc);
      if (ds < 0 || ds >= 30) return false;
      size_t dist = kDBase[ds] + s.bits(kDExt[ds]);
      if (dist > out.size() || out.size() + (size_t)len > limit) return false;
      for (int i = 0; i < len; i++) out.push_back(out[out.size() - dist]);
    }
  }
}
}

bool inflateRaw(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out, size_t expect) {
  Bits s{in, inLen};
  out.clear();
  out.reserve(expect);
  size_t limit = expect + 1;
  int last;
  do {
    last = s.bit();
    int type = (int)s.bits(2);
    if (s.bad) return false;
    if (type == 0) {
      s.cnt = 0;
      if (s.pos + 4 > s.n) return false;
      unsigned len = in[s.pos] | (in[s.pos + 1] << 8), nlen = in[s.pos + 2] | (in[s.pos + 3] << 8);
      s.pos += 4;
      if (len != (~nlen & 0xffff) || s.pos + len > s.n || out.size() + len > limit) return false;
      out.insert(out.end(), in + s.pos, in + s.pos + len);
      s.pos += len;
    } else if (type == 1) {
      uint8_t l[288];
      int i = 0;
      for (; i < 144; i++) l[i] = 8;
      for (; i < 256; i++) l[i] = 9;
      for (; i < 280; i++) l[i] = 7;
      for (; i < 288; i++) l[i] = 8;
      Huff lc, dc;
      build(lc, l, 288);
      uint8_t d[30];
      for (i = 0; i < 30; i++) d[i] = 5;
      build(dc, d, 30);
      if (!codes(s, out, lc, dc, limit)) return false;
    } else if (type == 2) {
      static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
      int nlen = (int)s.bits(5) + 257, ndist = (int)s.bits(5) + 1, ncode = (int)s.bits(4) + 4;
      if (nlen > 286 || ndist > 30) return false;
      uint8_t lengths[320]{};
      for (int i = 0; i < ncode; i++) lengths[order[i]] = (uint8_t)s.bits(3);
      Huff lencode;
      if (!build(lencode, lengths, 19)) return false;
      int idx = 0;
      uint8_t ll[320]{};
      while (idx < nlen + ndist) {
        int sym = decode(s, lencode);
        if (sym < 0) return false;
        if (sym < 16) ll[idx++] = (uint8_t)sym;
        else {
          int len = 0, rep;
          if (sym == 16) { if (idx == 0) return false; len = ll[idx - 1]; rep = 3 + (int)s.bits(2); }
          else if (sym == 17) rep = 3 + (int)s.bits(3);
          else rep = 11 + (int)s.bits(7);
          if (idx + rep > nlen + ndist) return false;
          while (rep--) ll[idx++] = (uint8_t)len;
        }
      }
      Huff lc, dc;
      if (!build(lc, ll, nlen) || !build(dc, ll + nlen, ndist)) return false;
      if (!codes(s, out, lc, dc, limit)) return false;
    } else return false;
  } while (!last);
  return out.size() == expect;
}

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool extractZip(const fs::path& zip, const fs::path& outDir, std::string* err) {
  std::string raw;
  if (!readFile(zip, raw, 256u << 20)) { if (err) *err = "cannot read " + zip.string(); return false; }
  const uint8_t* d = (const uint8_t*)raw.data();
  size_t n = raw.size();
  if (n < 22) { if (err) *err = "not a zip file"; return false; }
  size_t eocd = std::string::npos;
  for (size_t i = n - 22 + 1; i-- > 0 && n - i < 22 + 65536;)
    if (rd32(d + i) == 0x06054b50u) { eocd = i; break; }
  if (eocd == std::string::npos) { if (err) *err = "zip end record not found"; return false; }
  unsigned count = rd16(d + eocd + 10);
  size_t cd = rd32(d + eocd + 16);
  std::error_code ec;
  fs::create_directories(outDir, ec);
  std::string skipped;
  size_t total = 0;
  for (unsigned i = 0; i < count; i++) {
    if (cd + 46 > n || rd32(d + cd) != 0x02014b50u) { if (err) *err = "bad zip central directory"; return false; }
    unsigned method = rd16(d + cd + 10), nl = rd16(d + cd + 28), el = rd16(d + cd + 30), cl = rd16(d + cd + 32);
    uint32_t csize = rd32(d + cd + 20), usize = rd32(d + cd + 24), lho = rd32(d + cd + 42);
    if (cd + 46 + nl > n) { if (err) *err = "bad zip entry"; return false; }
    std::string name((const char*)d + cd + 46, nl);
    cd += 46 + nl + el + cl;
    if (name.empty() || name.back() == '/') continue;
    if (!safeRelPath(name)) { skipped += " " + name; continue; }
    if (lho > n || n - lho < 30 || rd32(d + lho) != 0x04034b50u) { if (err) *err = "bad zip local header"; return false; }
    size_t data = lho + 30 + rd16(d + lho + 26) + rd16(d + lho + 28);
    if (data + csize > n || usize > (128u << 20) || (total += usize) > (512u << 20)) { if (err) *err = "zip entry too large or truncated: " + name; return false; }
    std::vector<uint8_t> bytes;
    if (method == 0) bytes.assign(d + data, d + data + csize);
    else if (method == 8) {
      if (!inflateRaw(d + data, csize, bytes, usize)) { if (err) *err = "cannot inflate " + name; return false; }
    } else { if (err) *err = "unsupported zip method for " + name; return false; }
    fs::path dst = outDir / fs::path(name);
    fs::create_directories(dst.parent_path(), ec);
    std::ofstream f(dst, std::ios::binary);
    f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
  }
  if (err && !skipped.empty()) *err = "skipped unsafe entries:" + skipped;
  return true;
}

}
