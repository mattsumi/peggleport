#pragma once
#include <cstdint>
#include <string>

#include "mods/hostproto.h"

namespace pp::modhost {

struct SandboxReport {
  uint32_t protections = 0;
  std::string detail;
};

SandboxReport applySandbox(const std::string& pluginDir);

}
