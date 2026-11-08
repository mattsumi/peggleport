#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "profile_import.h"
#include "ui/dialogs.h"

namespace pp::ui {

class ImportDialog : public Dialog {
 public:
  explicit ImportDialog(App& a);
  ListBox* list;
  Button *folderBtn, *fileBtn;
  ImportScan scan;
  bool haveScan = false;
  std::string status;
  uint64_t ticket = 0;
  bool waiting = false;

  float preferredHeight(float width) override { return Dialog::preferredHeight(width) + 0xff + buttonH + 5 + 0x38; }
  void layout();
  void drawLayer(Gfx& g) override;
  void update() override;
  bool input(const Input& in) override;
  void onButton(int id) override;
  void choose(bool folder);
  void load(const std::filesystem::path& p);
  void refreshList();
  void doImport();
  int selected() const { return list->select; }
  std::vector<std::string> detailLines() const;
};

}
