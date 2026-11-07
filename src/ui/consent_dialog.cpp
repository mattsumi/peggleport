#include "ui/consent_dialog.h"
#include <utility>
#include <memory>
#include <string>

#include "log.h"
#include "mods/manager.h"
#include "ui/mods_screen.h"

namespace pp::ui {

std::string ConsentDialog::text(const std::string& id, bool ask) {
  auto* mm = mods::g_mods;
  mods::ModInfo* mi = mm ? mm->find(id) : nullptr;
  if (!mi) return "Unknown mod.";
  std::string t = mi->m.name + " " + mi->m.version + (mi->m.author.empty() ? "" : " by " + mi->m.author) + "\n";
  t += "\nThis mod ";
  std::string what;
  if (mi->scan.usesNet()) what += (mi->scan.declaredNet ? "asks for the network permission" : "contains code that looks like network access");
  if (mi->scan.hasNative) what += std::string(what.empty() ? "" : " and ") + "ships native code that is NOT sandboxed";
  if (what.empty()) what = "has nothing that needs your OK";
  t += what + ".\n";
  int shown = 0;
  for (auto& f : mi->scan.findings) {
    if (!(f.isNet() || f.kind == mods::Finding::Native)) continue;
    if (shown++ >= 4) { t += "  ...\n"; break; }
    t += "  - " + f.file + (f.line ? ":" + std::to_string(f.line) : "") + ": " + f.what.substr(0, 70) + "\n";
  }
  t += ask ? "\nThis scan is best effort: a determined author can hide network use in native code. Allow only mods you trust." : std::string("\n") + mods::kScanDisclaimer;
  return t;
}

ConsentDialog::ConsentDialog(App& a, const std::string& id, bool askConsent)
    : Dialog(a, D_ModConsent, true, askConsent ? "TRUST THIS MOD?" : "MOD SCAN", text(id, askConsent), askConsent ? "" : "OK", askConsent ? BUTTONS_YES_NO : BUTTONS_FOOTER), modId(id), ask(askConsent) {
  if (mods::ModInfo* mi = mods::g_mods ? mods::g_mods->find(id) : nullptr) hash = mi->hashHex;
  textAlign = -1;
  linesFont = "PLAIN2";
  if (askConsent) setYesNo("Allow", "Not now");
}

void ConsentDialog::onButton(int id) {
  if (scrollActive && !scrollingOff) return;
  if (ask && id == ID_YES && mods::g_mods) {
    mods::ModInfo* mi = mods::g_mods->find(modId);
    if (mi && mi->hashHex == hash) {
      mods::g_mods->setConsent(modId, true);
      app.requestModApply();
    } else {
      log::warn("mod:" + modId, "the mod changed while its permission dialog was open, so nothing was allowed; press Allow... again");
    }
  }
  closed = true;
  if (!hosted) doScrollOff();
}

void App::showModConsent(const std::string& modId, bool ask) {
  if (auto* ms = dynamic_cast<ModsScreen*>(overlay_.get())) { ms->openConsent(modId, ask); return; }
  killDialog(D_ModConsent);
  auto d = std::make_unique<ConsentDialog>(*this, modId, ask);
  Dialog* raw = d.get();
  positionDialog(raw, 520);
  raw->doScroll(true);
  dialogs_.push_back(std::move(d));
}

void App::offerModConsent() {
  static bool offered = false;
  if (offered || !mods::g_mods) return;
  for (auto& m : mods::g_mods->mods())
    if (m->enabled && m->state == mods::ModInfo::NeedsConsent) {
      offered = true;
      showModConsent(m->m.id, true);
      return;
    }
}

}
