#pragma once
#include <cstdint>

#include "content.h"

namespace pp::online {

uint64_t modSetHash();

uint64_t matchContentHash(const LevelBundle& lb);

}
