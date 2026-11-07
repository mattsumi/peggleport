#pragma once
#include <memory>
#include <string>

#include "ui/app.h"
#include "ui/consent_dialog.h"

namespace pp::ui {

class ModsScreen : public Screen {
 public:
  explicit ModsScreen(App& a);
  const char* name() const override { return "mods"; }
  void update() override;
  void drawLayer(Gfx& g) override;
  void onButton(int id) override;
  void checkboxChanged(int id, bool checked) override;
  void keyDown(int key, int mod) override;
  bool input(const Input& in) override;
  void drawOverlay(Gfx& g) override;
  void openConsent(const std::string& modId, bool ask);
  void openFolder();

 private:
  static constexpr int kRowH = 76, kPage = 5, kTop = 92;
  int page_ = 0, sig_ = -1, seen_ = 0;
  bool dirty_ = true, wasFocused_ = true;
  std::string status_, touched_;
  std::unique_ptr<ConsentDialog> dlg_;
  int signature() const;
  void rebuild();
  void followApply();
  void changed(const std::string& id);
};

}
