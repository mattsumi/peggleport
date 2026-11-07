#pragma once
#include <string>
#include "ui/dialog.h"

namespace pp::ui {

enum { D_ModConsent = 60 };

class ConsentDialog : public Dialog {
 public:
  ConsentDialog(App& a, const std::string& modId, bool ask);
  void onButton(int id) override;
  std::string modId, hash;
  bool ask;
  bool closed = false;
  bool hosted = false;
  static std::string text(const std::string& modId, bool ask = true);
};

}
