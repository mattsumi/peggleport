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
  int step = 0;
  bool followUp = false;
  bool closed = false;
  bool hosted = false;
  static int stepFor(const std::string& modId, bool ask);
  static std::string text(const std::string& modId, int step);
};

}
