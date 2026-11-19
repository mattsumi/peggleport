#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <limits>

namespace {
thread_local bool boundedImage = false;
#ifndef STBI_MALLOC
void* imageMalloc(size_t n) { return boundedImage && n > 33554432 ? nullptr : std::malloc(n); }
void* imageRealloc(void* p, size_t n) { return boundedImage && n > 33554432 ? nullptr : std::realloc(p, n); }
#define STBI_MALLOC(n) imageMalloc(n)
#define STBI_REALLOC(p,n) imageRealloc(p,n)
#define STBI_FREE(p) std::free(p)
#endif
}

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#include "stb_image.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <openjpeg.h>

namespace pp {

namespace {
bool imageSize(uint64_t w, uint64_t h) {
  return w > 0 && h > 0 && w <= 2048 && h <= 2048 && w * h <= 1048576;
}
bool j2kSize(const std::vector<uint8_t>& b) {
  size_t pos = 0, end = b.size();
  auto u16 = [&](size_t p) { return (uint32_t)b[p] * 256 + b[p + 1]; };
  auto u32 = [&](size_t p) { return ((uint64_t)u16(p) << 16) | u16(p + 2); };
  auto box = [&](size_t at, size_t stop, size_t& body, size_t& next) {
    if (stop - at < 8) return false;
    uint64_t len = u32(at), head = 8;
    if (len == 1) {
      if (stop - at < 16) return false;
      len = (u32(at + 8) << 32) | u32(at + 12);
      head = 16;
    } else if (len == 0) len = stop - at;
    if (len < head || len > stop - at) return false;
    body = at + (size_t)head;
    next = at + (size_t)len;
    return true;
  };
  if (b.size() >= 12 && b[0] == 0) {
    size_t stream = 0, streamEnd = 0;
    while (pos < end) {
      size_t body, next;
      if (!box(pos, end, body, next)) return false;
      const auto* type = b.data() + pos + 4;
      if (std::memcmp(type, "ihdr", 4) == 0 || std::memcmp(type, "bpcc", 4) == 0 ||
          std::memcmp(type, "pclr", 4) == 0 || std::memcmp(type, "cmap", 4) == 0 || std::memcmp(type, "cdef", 4) == 0) return false;
      if (std::memcmp(type, "jp2h", 4) == 0) {
        for (size_t p = body; p < next;) {
          size_t data, stop;
          if (!box(p, next, data, stop)) return false;
          size_t len = stop - data;
          const auto* type = b.data() + p + 4;
          if (std::memcmp(type, "ihdr", 4) == 0) {
            if (len != 14 || !imageSize(u32(data + 4), u32(data)) || u16(data + 8) == 0 || u16(data + 8) > 4 ||
                (b[data + 10] != 255 && (b[data + 10] & 127) > 15)) return false;
          } else if (std::memcmp(type, "bpcc", 4) == 0) {
            if (len == 0 || len > 4) return false;
            for (size_t i = data; i < stop; i++) if ((b[i] & 127) > 15) return false;
          } else if (std::memcmp(type, "pclr", 4) == 0) {
            if (len < 3 || b[data + 2] == 0 || b[data + 2] > 4 || len < 3 + b[data + 2]) return false;
            for (size_t i = 0; i < b[data + 2]; i++) if ((b[data + 3 + i] & 127) > 15) return false;
          } else if (std::memcmp(type, "cdef", 4) == 0) {
            if (len < 2 || u16(data) > 4 || len != 2 + 6 * u16(data)) return false;
          } else if (std::memcmp(type, "cmap", 4) == 0) {
            if (len == 0 || len > 16 || len % 4 != 0) return false;
          }
          p = stop;
        }
      }
      if (std::memcmp(type, "jp2c", 4) == 0) {
        if (streamEnd != 0) return false;
        stream = body;
        streamEnd = next;
      }
      pos = next;
    }
    if (streamEnd == 0) return false;
    pos = stream;
    end = streamEnd;
  }
  if (end - pos < 42 || u16(pos) != 0xff4f || u16(pos + 2) != 0xff51) return false;
  uint64_t x = u32(pos + 8), y = u32(pos + 12), x0 = u32(pos + 16), y0 = u32(pos + 20);
  uint64_t tw = u32(pos + 24), th = u32(pos + 28), tx = u32(pos + 32), ty = u32(pos + 36);
  uint32_t nc = u16(pos + 40);
  if (x <= x0 || y <= y0 || x > 2048 || y > 2048 || !imageSize(x - x0, y - y0) ||
      tw == 0 || th == 0 || tw > 2048 || th > 2048 || tx > x0 || ty > y0 || tx + tw <= x0 || ty + th <= y0 ||
      ((x - tx + tw - 1) / tw) * ((y - ty + th - 1) / th) > 256 || nc == 0 || nc > 4 ||
      u16(pos + 4) != 38 + 3 * nc || end - pos < 42 + 3 * nc) return false;
  for (uint32_t c = 0; c < nc; c++) {
    size_t p = pos + 42 + 3 * c;
    if ((b[p] & 127) > 15 || b[p + 1] == 0 || b[p + 2] == 0) return false;
  }
  size_t first = pos + 2, tileEnd = 0;
  unsigned parts = 0;
  pos += 2;
  while (pos < end) {
    if (end - pos < 2) return false;
    size_t markerStart = pos;
    uint32_t marker = u16(pos);
    pos += 2;
    if (marker == 0xffd9) return pos == end;
    if (marker == 0xff93) {
      if (tileEnd < pos || tileEnd > end) return false;
      pos = tileEnd;
      tileEnd = 0;
      continue;
    }
    if (end - pos < 2) return false;
    size_t len = u16(pos);
    if (len < 2 || len > end - pos) return false;
    size_t data = pos + 2, count = len - 2;
    if (marker == 0xff51) {
      if (markerStart != first) return false;
    } else if (marker == 0xff90) {
      if (len != 10 || ++parts > 256 || tileEnd != 0) return false;
      uint64_t length = u32(data + 2);
      if (length == 0) {
        if (end < 2 || u16(end - 2) != 0xffd9) return false;
        tileEnd = end - 2;
      } else {
        if (length < 14 || length > end - markerStart) return false;
        tileEnd = markerStart + (size_t)length;
      }
    } else if (marker == 0xff52 || marker == 0xff53) {
      bool cod = marker == 0xff52;
      size_t offset = cod ? 5 : 2;
      if (count < offset + 5) return false;
      uint32_t flags = b[data + (cod ? 0 : 1)], levels = b[data + offset] + 1;
      if ((cod && (u16(data + 2) == 0 || u16(data + 2) > 16)) || levels > 8 ||
          b[data + offset + 1] < 2 || b[data + offset + 2] < 2) return false;
      if (flags & 1) {
        if (count != offset + 5 + levels) return false;
        for (uint32_t i = 0; i < levels; i++) {
          uint8_t precinct = b[data + offset + 5 + i];
          if ((precinct & 15) < 4 || (precinct >> 4) < 4) return false;
        }
      }
    } else if (marker != 0xff5c && marker != 0xff5d && marker != 0xff64) return false;
    pos += len;
    if (tileEnd != 0 && pos > tileEnd) return false;
  }
  return false;
}
struct MemSrc {
  const uint8_t* d;
  size_t n, pos;
};
OPJ_SIZE_T rd(void* buf, OPJ_SIZE_T nb, void* u) {
  auto* m = (MemSrc*)u;
  if (m->pos >= m->n) return (OPJ_SIZE_T)-1;
  size_t k = std::min((size_t)nb, m->n - m->pos);
  std::memcpy(buf, m->d + m->pos, k);
  m->pos += k;
  return k;
}
OPJ_OFF_T sk(OPJ_OFF_T nb, void* u) {
  auto* m = (MemSrc*)u;
  if (nb < 0 && (size_t)(-nb) > m->pos) nb = -(OPJ_OFF_T)m->pos;
  if (nb > 0 && m->pos + (size_t)nb > m->n) nb = (OPJ_OFF_T)(m->n - m->pos);
  m->pos += nb;
  return nb;
}
OPJ_BOOL se(OPJ_OFF_T nb, void* u) {
  auto* m = (MemSrc*)u;
  if (nb < 0 || (size_t)nb > m->n) return OPJ_FALSE;
  m->pos = (size_t)nb;
  return OPJ_TRUE;
}
void quiet(const char*, void*) {}

bool decodeJ2K(const std::vector<uint8_t>& b, Image& out, std::string* err, bool limited) {
  if (limited && !j2kSize(b)) return false;
  static const uint8_t jp2sig[12] = {0, 0, 0, 0x0c, 'j', 'P', ' ', ' ', 0x0d, 0x0a, 0x87, 0x0a};
  bool jp2 = b.size() >= 12 && std::memcmp(b.data(), jp2sig, 12) == 0;
  opj_codec_t* codec = opj_create_decompress(jp2 ? OPJ_CODEC_JP2 : OPJ_CODEC_J2K);
  opj_set_info_handler(codec, quiet, nullptr);
  opj_set_warning_handler(codec, quiet, nullptr);
  opj_set_error_handler(codec, quiet, nullptr);
  opj_dparameters_t prm;
  opj_set_default_decoder_parameters(&prm);
  opj_setup_decoder(codec, &prm);
  MemSrc ms{b.data(), b.size(), 0};
  opj_stream_t* st = opj_stream_create(OPJ_J2K_STREAM_CHUNK_SIZE, OPJ_TRUE);
  opj_stream_set_user_data(st, &ms, nullptr);
  opj_stream_set_user_data_length(st, b.size());
  opj_stream_set_read_function(st, rd);
  opj_stream_set_skip_function(st, sk);
  opj_stream_set_seek_function(st, se);
  opj_image_t* img = nullptr;
  bool ok = opj_read_header(st, codec, &img);
  if (ok && limited) {
    ok = img && img->numcomps > 0 && img->numcomps <= 4;
    for (uint32_t c = 0; ok && c < img->numcomps; c++)
      ok = imageSize(img->comps[c].w, img->comps[c].h) && img->comps[c].prec > 0 && img->comps[c].prec <= 16;
  }
  ok = ok && opj_decode(codec, st, img) && opj_end_decompress(codec, st);
  if (ok && img && img->numcomps > 0) {
    int W = (int)img->comps[0].w, H = (int)img->comps[0].h;
    int nc = (int)img->numcomps;
    out.w = W;
    out.h = H;
    out.rgba.assign((size_t)W * H * 4, 255);
    auto sample = [&](int c, int x, int y) -> uint8_t {
      const auto& cp = img->comps[c];
      int cx = (int)((int64_t)x * cp.w / W), cy = (int)((int64_t)y * cp.h / H);
      int v = cp.data[(size_t)cy * cp.w + cx];
      if (cp.sgnd) v += 1 << (cp.prec - 1);
      if (cp.prec > 8) v >>= (cp.prec - 8);
      else if (cp.prec < 8) v <<= (8 - cp.prec);
      return (uint8_t)std::clamp(v, 0, 255);
    };
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        uint8_t* p = &out.rgba[((size_t)y * W + x) * 4];
        if (nc >= 3) {
          p[0] = sample(0, x, y);
          p[1] = sample(1, x, y);
          p[2] = sample(2, x, y);
        } else {
          p[0] = p[1] = p[2] = sample(0, x, y);
        }
      }
  } else {
    ok = false;
    if (err) *err = "jpeg2000 decode failed";
  }
  if (img) opj_image_destroy(img);
  opj_stream_destroy(st);
  opj_destroy_codec(codec);
  return ok;
}

const char* EXTS[] = {".tga", ".jpg", ".png", ".gif", ".j2k", ".jp2"};

bool hasKnownExt(const std::string& p) {
  auto dot = p.find_last_of('.');
  auto sl = p.find_last_of("/\\");
  if (dot == std::string::npos || (sl != std::string::npos && dot < sl)) return false;
  std::string e = p.substr(dot);
  for (auto& c : e) c = (char)std::tolower((unsigned char)c);
  for (auto x : EXTS)
    if (e == x) return true;
  return false;
}

bool loadFileImage(const Vfs& vfs, const std::string& path, Image& im, bool limited, bool* bakedAlpha) {
  auto h = vfs.read(path, limited ? 8388608 : std::numeric_limits<size_t>::max());
  if (!h || !decodeImage(h->bytes, im, nullptr, limited)) return false;
  im.scale = h->layer ? h->layer->imageScale : 1.0f;
  im.layer = h->layer ? h->layer->label : "";
  if (bakedAlpha && h->layer) {
    auto a = h->layer->meta.find("image_alpha");
    *bakedAlpha = a != h->layer->meta.end() && a->second == "baked";
  }
  return true;
}

bool tryLoad(const Vfs& vfs, const std::string& base, Image& im, bool limited, bool* bakedAlpha = nullptr) {
  for (auto e : EXTS)
    if (loadFileImage(vfs, base + e, im, limited, bakedAlpha)) return true;
  return false;
}
}

void upscale2(Image& im, int rows, int cols) {
  const int w = im.w, h = im.h, W = w * 2, H = h * 2;
  const int cw = (cols > 1 && w % cols == 0) ? w / cols : w, chh = (rows > 1 && h % rows == 0) ? h / rows : h;
  std::vector<float> src((size_t)w * h * 4), tmp((size_t)W * h * 4);
  for (size_t i = 0; i < (size_t)w * h; i++) {
    float a = im.rgba[i * 4 + 3];
    for (int c = 0; c < 3; c++) src[i * 4 + c] = im.rgba[i * 4 + c] * a / 255.0f;
    src[i * 4 + 3] = a;
  }
  auto cr = [](float t, float p0, float p1, float p2, float p3) {
    return 0.5f * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
  };
  for (int x = 0; x < W; x++) {
    float u = ((float)x + 0.5f) * 0.5f - 0.5f;
    int i1 = (int)std::floor(u);
    float t = u - (float)i1;
    int n = std::clamp((int)std::floor(u + 0.5f), 0, w - 1), lo = n / cw * cw, hi = std::min(w - 1, lo + cw - 1);
    int id[4];
    for (int k = 0; k < 4; k++) id[k] = std::clamp(i1 + k - 1, lo, hi);
    for (int y = 0; y < h; y++)
      for (int c = 0; c < 4; c++)
        tmp[((size_t)y * W + x) * 4 + c] = cr(t, src[((size_t)y * w + id[0]) * 4 + c], src[((size_t)y * w + id[1]) * 4 + c], src[((size_t)y * w + id[2]) * 4 + c], src[((size_t)y * w + id[3]) * 4 + c]);
  }
  std::vector<uint8_t> out((size_t)W * H * 4);
  for (int y = 0; y < H; y++) {
    float v = ((float)y + 0.5f) * 0.5f - 0.5f;
    int j1 = (int)std::floor(v);
    float t = v - (float)j1;
    int n = std::clamp((int)std::floor(v + 0.5f), 0, h - 1), lo = n / chh * chh, hi = std::min(h - 1, lo + chh - 1);
    int id[4];
    for (int k = 0; k < 4; k++) id[k] = std::clamp(j1 + k - 1, lo, hi);
    for (int x = 0; x < W; x++) {
      float p[4];
      for (int c = 0; c < 4; c++)
        p[c] = cr(t, tmp[((size_t)id[0] * W + x) * 4 + c], tmp[((size_t)id[1] * W + x) * 4 + c], tmp[((size_t)id[2] * W + x) * 4 + c], tmp[((size_t)id[3] * W + x) * 4 + c]);
      float a = std::clamp(p[3], 0.0f, 255.0f);
      uint8_t* o = &out[((size_t)y * W + x) * 4];
      for (int c = 0; c < 3; c++) o[c] = a > 0.5f ? (uint8_t)std::lround(std::clamp(p[c], 0.0f, a) * 255.0f / a) : 0;
      o[3] = (uint8_t)std::lround(a);
    }
  }
  im.rgba = std::move(out);
  im.w = W;
  im.h = H;
}

bool decodeImage(const std::vector<uint8_t>& b, Image& out, std::string* err) { return decodeImage(b, out, err, false); }

bool decodeImage(const std::vector<uint8_t>& b, Image& out, std::string* err, bool limited) {
  if (limited && b.size() > 8388608) return false;
  if (b.size() >= 4 && ((b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0x0c) || (b[0] == 0xff && b[1] == 0x4f))) return decodeJ2K(b, out, err, limited);
  int w, h, n;
  if (limited && (!stbi_info_from_memory(b.data(), (int)b.size(), &w, &h, &n) || !imageSize(w, h))) return false;
  bool previous = boundedImage;
  boundedImage = limited;
  stbi_uc* px = stbi_load_from_memory(b.data(), (int)b.size(), &w, &h, &n, 4);
  boundedImage = previous;
  if (!px) {
    if (err) *err = stbi_failure_reason();
    return false;
  }
  out.w = w;
  out.h = h;
  out.rgba.assign(px, px + (size_t)w * h * 4);
  stbi_image_free(px);
  return true;
}

std::optional<Image> loadGameImage(const Vfs& vfs, const std::string& path, const std::string& explicitMask) { return loadGameImage(vfs, path, explicitMask, false); }

std::optional<Image> loadGameImage(const Vfs& vfs, const std::string& path0, const std::string& explicitMask, bool limited) {
  std::string path = path0;
  Image col;
  bool haveCol = false, bakedAlpha = false;
  if (hasKnownExt(path)) {
    haveCol = loadFileImage(vfs, path, col, limited, &bakedAlpha);
    path = path.substr(0, path.find_last_of('.'));
  }
  if (!haveCol) haveCol = tryLoad(vfs, path, col, limited, &bakedAlpha);
  if (bakedAlpha) return col;
  auto sl = path.find_last_of("/\\");
  std::string dir = sl == std::string::npos ? "" : path.substr(0, sl + 1);
  std::string file = sl == std::string::npos ? path : path.substr(sl + 1);
  Image mask;
  bool haveMask = false;
  if (!explicitMask.empty()) {
    std::string mp = explicitMask.find('/') == std::string::npos && explicitMask.find('\\') == std::string::npos ? dir + explicitMask : explicitMask;
    haveMask = hasKnownExt(mp) ? (tryLoad(vfs, mp.substr(0, mp.find_last_of('.')), mask, limited)) : tryLoad(vfs, mp, mask, limited);
  }
  if (!haveMask) haveMask = tryLoad(vfs, dir + "_" + file, mask, limited) || tryLoad(vfs, path + "_", mask, limited);
  if (!haveCol && !haveMask) return std::nullopt;
  if (!haveCol) {
    col.w = mask.w;
    col.h = mask.h;
    col.scale = mask.scale;
    col.rgba.assign((size_t)col.w * col.h * 4, 255);
  }
  if (haveMask && mask.w == col.w && mask.h == col.h) {
    for (size_t i = 0; i < (size_t)col.w * col.h; i++) col.rgba[i * 4 + 3] = mask.rgba[i * 4 + 2];
  } else if (haveMask && mask.w > 0 && mask.h > 0 && mask.scale == col.scale && col.w % mask.w == 0 && col.h % mask.h == 0) {
    for (int y = 0; y < col.h; y++)
      for (int x = 0; x < col.w; x++)
        col.rgba[((size_t)y * col.w + x) * 4 + 3] = mask.rgba[((size_t)(y % mask.h) * mask.w + (x % mask.w)) * 4 + 2];
  }
  return col;
}

}
