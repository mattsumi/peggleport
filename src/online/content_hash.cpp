#include "online/content_hash.h"
#include <cstdint>

#include "mods/simhooks.h"

namespace pp::online {

uint64_t modSetHash() { return pp::g_modSetHash; }

uint64_t matchContentHash(const LevelBundle& lb) { return lb.contentHash; }

}
