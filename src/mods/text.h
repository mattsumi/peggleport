#pragma once
#include <string>

namespace pp::mods {

inline std::string gameText(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    const unsigned char lead = (unsigned char)s[i];
    size_t more = 0;
    unsigned cp = lead;
    unsigned least = 0;
    if (lead >= 0xF0 && lead < 0xF8) { more = 3; cp = lead & 0x07u; least = 0x10000; }
    else if (lead >= 0xE0 && lead < 0xF0) { more = 2; cp = lead & 0x0Fu; least = 0x800; }
    else if (lead >= 0xC2 && lead < 0xE0) { more = 1; cp = lead & 0x1Fu; least = 0x80; }
    bool whole = more > 0 && i + more < s.size();
    for (size_t k = 1; whole && k <= more; k++) {
      const unsigned char c = (unsigned char)s[i + k];
      whole = (c & 0xC0) == 0x80;
      cp = (cp << 6) | (c & 0x3Fu);
    }
    if (whole && cp >= least) {
      o += cp >= 0xA0 && cp <= 0xFF ? (char)cp : '?';
      i += more + 1;
    } else {
      o += s[i++];
    }
  }
  return o;
}

}
