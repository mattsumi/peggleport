#pragma once
#include <istream>
#include <string>

namespace pp::online {

template<class Selected>
bool desktopOwnsInvites(std::istream& desktop, const std::string& exe, Selected selected) {
  if (exe.empty() || exe.front() != '/' || exe.find_first_of("\"\\%$`\r\n") != exe.npos) return false;
  const std::string expected = "\"" + exe + "\" %u";
  bool main = false, seen = false, exec = false, application = false;
  std::string line;
  while (std::getline(desktop, line)) {
    const auto first = line.find_first_not_of(" \t\r");
    if (first == line.npos || line[first] == '#') continue;
    line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
    if (line.front() == '[') {
      main = line == "[Desktop Entry]";
      if (main && seen) return false;
      seen |= main;
    } else if (main) {
      const auto eq = line.find('=');
      if (eq == line.npos) return false;
      auto key = line.substr(0, eq);
      key.erase(key.find_last_not_of(" \t") + 1);
      const auto pos = line.find_first_not_of(" \t", eq + 1);
      const auto value = pos == line.npos ? std::string() : line.substr(pos);
      if (key == "Exec") {
        if (exec || value != expected) return false;
        exec = true;
      } else if (key == "Type") {
        if (application || value != "Application") return false;
        application = true;
      } else if ((key == "Hidden" || key == "DBusActivatable") && value != "false") return false;
    }
  }
  return desktop.eof() && !desktop.bad() && application && exec &&
         selected("x-scheme-handler/peggleport") && selected("x-scheme-handler/discord-1571022918392225882");
}

}
