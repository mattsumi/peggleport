#include "image.h"

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <optional>
#include <cctype>

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

bool decodeJ2K(const std::vector<uint8_t>& b, Image& out, std::string* err) {
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
  bool ok = opj_read_header(st, codec, &img) && opj_decode(codec, st, img) && opj_end_decompress(codec, st);
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

bool tryLoad(const Vfs& vfs, const std::string& base, Image& im) {
  for (auto e : EXTS) {
    auto h = vfs.read(base + e);
    if (h && decodeImage(h->bytes, im)) {
      im.scale = h->layer ? h->layer->imageScale : 1.0f;
      im.layer = h->layer ? h->layer->label : "";
      return true;
    }
  }
  return false;
}
}

bool decodeImage(const std::vector<uint8_t>& b, Image& out, std::string* err) {
  if (b.size() >= 4 && ((b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0x0c) || (b[0] == 0xff && b[1] == 0x4f))) return decodeJ2K(b, out, err);
  int w, h, n;
  stbi_uc* px = stbi_load_from_memory(b.data(), (int)b.size(), &w, &h, &n, 4);
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

std::optional<Image> loadGameImage(const Vfs& vfs, const std::string& path0, const std::string& explicitMask) {
  std::string path = path0;
  Image col;
  bool haveCol = false;
  if (hasKnownExt(path)) {
    if (auto h = vfs.read(path); h && decodeImage(h->bytes, col)) {
      haveCol = true;
      col.scale = h->layer ? h->layer->imageScale : 1.0f;
      col.layer = h->layer ? h->layer->label : "";
    }
    path = path.substr(0, path.find_last_of('.'));
  }
  if (!haveCol) haveCol = tryLoad(vfs, path, col);
  auto sl = path.find_last_of("/\\");
  std::string dir = sl == std::string::npos ? "" : path.substr(0, sl + 1);
  std::string file = sl == std::string::npos ? path : path.substr(sl + 1);
  Image mask;
  bool haveMask = false;
  if (!explicitMask.empty()) {
    std::string mp = explicitMask.find('/') == std::string::npos && explicitMask.find('\\') == std::string::npos ? dir + explicitMask : explicitMask;
    haveMask = hasKnownExt(mp) ? (tryLoad(vfs, mp.substr(0, mp.find_last_of('.')), mask)) : tryLoad(vfs, mp, mask);
  }
  if (!haveMask) haveMask = tryLoad(vfs, dir + "_" + file, mask) || tryLoad(vfs, path + "_", mask);
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
