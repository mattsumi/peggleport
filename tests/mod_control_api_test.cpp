#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include "mods/manifest.h"
#include "mods/luamod.h"
#include "mods/manager.h"
#include "mods/scanner.h"

int main(int argc, char** argv) {
  if (argc != 3) return 2;
  int checks = 0, failures = 0;
  auto check = [&](bool pass, const char* name) {
    ++checks;
    if (!pass) { ++failures; std::cerr << "FAIL " << name << '\n'; }
  };
  pp::mods::Manifest manifest;
  std::string error;
  check(pp::mods::parseManifest("name = MCP Agent\npermissions = control, fs\nscripts = main.lua\ndeterminism = cosmetic\n", false, "mcp_agent", manifest, &error), "control manifest parses");
  check(manifest.warnings.empty(), "control permission is recognized");
  const auto fixture = std::filesystem::path(argv[1]) / "mod-control-api-fixture";
  std::filesystem::create_directories(fixture);
  auto scan = pp::mods::scanMod(fixture, manifest);
  check(scan.needsConsent(), "control permission needs explicit player consent");
  check(!scan.usesNet(), "local control does not request network access");
  check(scan.summary().find("player control") != std::string::npos, "scan summary names player control");
  manifest.scripts.clear();
  pp::mods::HostServices host;
  pp::mods::LuaMod allowed(manifest, fixture, host);
  check(allowed.open(&error), "control sandbox opens");
  std::string result;
  check(allowed.eval("return type(control)", &result).empty() && result == "table", "control permission exposes control API");
  check(allowed.eval("return control.state()", &result).find("on_control") != std::string::npos, "control API cannot escape the input hook");
  pp::mods::Manifest plain;
  pp::mods::LuaMod denied(plain, fixture, host);
  check(denied.open(&error), "ordinary sandbox opens");
  check(denied.eval("return type(control)", &result).empty() && result == "nil", "ordinary sandbox has no control API");
  const auto source = fixture / "controller";
  std::filesystem::create_directories(source);
  std::ofstream(source / "mod.ini") << "id = controller\npermissions = control, fs\nscripts = main.lua\ndeterminism = cosmetic\n";
  std::ofstream(source / "main.lua") << "function on_control() fs.write('state.json', control.state()); fs.write('response.json', control.command('{\"action\":\"fire\"}')) end\n";
  pp::mods::ModManager manager;
  manager.setUserDir(fixture / "profile");
  manager.discover({source.string()});
  auto* controller = manager.find("controller");
  manager.activate(nullptr);
  check(controller && controller->state == pp::mods::ModInfo::NeedsConsent && !controller->lua, "unconsented controller never executes Lua");
  check(manager.setConsent("controller", true), "consent can be granted for exact mod hash");
  manager.activate(nullptr);
  check(controller && controller->state == pp::mods::ModInfo::Active, "consented controller loads");
  int requests = 0;
  manager.controlTick([&](const std::string& request) {
    ++requests;
    return request.empty() ? "{\"armed\":false}" : "{\"ok\":false}";
  });
  const auto data = fixture / "profile" / "mod-data" / "controller";
  std::string stored;
  check(requests == 2 && pp::mods::readFile(data / "state.json", stored) && stored == "{\"armed\":false}", "input hook receives live state callback");
  check(pp::mods::readFile(data / "response.json", stored) && stored == "{\"ok\":false}", "input hook returns actual command result");
  if (controller && controller->lua)
    check(controller->lua->eval("return control.state()", &result).find("on_control") != std::string::npos, "input handler is cleared after the hook");
  manager.setEnabled("controller", false);
  manager.controlTick([&](const std::string&) { ++requests; return "{}"; });
  check(requests == 2, "disabled controller stops input hooks before deferred unload");
  manager.setEnabled("controller", true);
  manager.setConsent("controller", false);
  manager.activate(nullptr);
  manager.controlTick([&](const std::string&) { ++requests; return "{}"; });
  check(requests == 2, "revoking consent stops controller input hooks");
  pp::mods::Manifest mcp;
  check(pp::mods::loadManifest(argv[2], mcp, &error) && mcp.id == "mcp_agent" && mcp.control && mcp.fs && !mcp.net && mcp.cosmetic, "bundled MCP mod asks only for local files and player control");
  pp::mods::ModManager bridge;
  bridge.setUserDir(fixture / "mcp-profile");
  bridge.discover({argv[2]});
  bridge.setConsent("mcp_agent", true);
  bridge.activate(nullptr);
  auto* agent = bridge.find("mcp_agent");
  check(agent && agent->lua && agent->state == pp::mods::ModInfo::Active, "bundled MCP mod loads after approval");
  const auto mcpData = fixture / "mcp-profile" / "mod-data" / "mcp_agent";
  bridge.controlTick([](const std::string&) { return "{\"session\":\"test\",\"armed\":false}"; });
  check(pp::mods::readFile(mcpData / "state.json", stored) && stored == "{\"session\":\"test\",\"armed\":false}", "bundled MCP mod exports live state");
  std::filesystem::create_directories(mcpData);
  std::ofstream(mcpData / "request.json") << "{\"id\":\"one\",\"action\":\"fire\"}";
  int commands = 0;
  auto handler = [&](const std::string& request) {
    if (request.empty()) return std::string("{\"session\":\"test\",\"armed\":false}");
    ++commands;
    return std::string("{\"id\":\"one\",\"ok\":false,\"error\":\"disarmed\"}");
  };
  bridge.controlTick(handler);
  bridge.controlTick(handler);
  check(commands == 1 && !std::filesystem::exists(mcpData / "request.json"), "bundled MCP mod consumes each command once");
  check(pp::mods::readFile(mcpData / "response.json", stored) && stored == "{\"id\":\"one\",\"ok\":false,\"error\":\"disarmed\"}", "bundled MCP mod returns command rejection unchanged");
  std::cout << "MOD_CONTROL_API checks=" << checks << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
