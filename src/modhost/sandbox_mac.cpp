#include "modhost/sandbox.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include "modhost/sandbox_probe.h"

extern "C" {
int sandbox_init_with_parameters(const char* profile, uint64_t flags, const char* const parameters[], char** errorbuf);
void sandbox_free_error(char* errorbuf);
}

namespace pp::modhost {

namespace {
const char* kProfile =
    "(version 1)\n"
    "(deny default)\n"
    "(allow file-read* file-map-executable (subpath (param \"PLUGIN_DIR\")))\n"
    "(allow file-read* file-map-executable (subpath \"/usr/lib\") (subpath \"/System/Library\") (subpath \"/Library/Apple/System/Library\"))\n"
    "(allow file-read* (literal \"/dev/urandom\") (literal \"/dev/random\"))\n"
    "(allow sysctl-read)\n"
    "(allow signal (target self))\n";
}

SandboxReport applySandbox(const std::string& pluginDir) {
  SandboxReport r;
  probe::Before pre = probe::before();
  char real[PATH_MAX];
  std::string dir = pluginDir.empty() ? std::string("/var/empty") : pluginDir;
  if (realpath(dir.c_str(), real)) dir = real;
  const char* params[] = {"PLUGIN_DIR", dir.c_str(), nullptr};
  char* err = nullptr;
  if (sandbox_init_with_parameters(kProfile, 0, params, &err) != 0) {
    r.detail = std::string("sandbox_init failed: ") + (err ? err : "unknown error");
    if (err) sandbox_free_error(err);
    return r;
  }
  std::string missing;
  if (probe::noNetwork()) r.protections |= kNoNetwork; else missing += " network";
  if (probe::noFileWrite(dir)) r.protections |= kNoFileWrite; else missing += " file-write";
  if (probe::noSpawn()) r.protections |= kNoSpawn; else missing += " process-spawn";
  if (probe::readRestricted(pre)) r.protections |= kReadRestricted; else missing += " file-read";
  r.detail = missing.empty() ? "macOS sandbox profile applied" : "macOS sandbox profile applied, not confirmed:" + missing;
  return r;
}

}
