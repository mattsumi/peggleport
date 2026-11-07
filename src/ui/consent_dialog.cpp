#include "ui/consent_dialog.h"
#include <utility>
#include <memory>
#include <string>

#include "mods/manager.h"
#include "ui/mods_screen.h"

namespace pp::ui {

namespace {
std::string head(const mods::ModInfo& mi) { return mi.m.name + " " + mi.m.version + (mi.m.author.empty() ? "" : " by " + mi.m.author) + "\n"; }

std::string helperLine() {
  mods::NativeStatus ps = mods::NativeMod::probe();
  if (ps.helperMissing) return "The helper program that runs native plugins (pp_modhost) is missing, so this plugin cannot load.";
  if (!ps.probed) return "The helper process could not be started: " + ps.detail;
  if (ps.full()) return "It runs in a restricted helper process: " + mods::NativeMod::protectionText(ps.protections) + ".";
  return "This plugin could not be restricted on this system (" + mods::NativeMod::missingText(ps.protections) + "). It only loads if you also allow it to run without restrictions. You are asked about that separately.";
}
}

int ConsentDialog::stepFor(const std::string& id, bool ask) {
  auto* mm = mods::g_mods;
  mods::ModInfo* mi = mm ? mm->find(id) : nullptr;
  if (!mi || !ask) return 0;
  if (mi->needsConsent()) return 1;
  if (mm->needsUnrestrictedStep(*mi)) return 2;
  return 0;
}

std::string ConsentDialog::text(const std::string& id, int step) {
  auto* mm = mods::g_mods;
  mods::ModInfo* mi = mm ? mm->find(id) : nullptr;
  if (!mi) return "Unknown mod.";
  std::string t = head(*mi);
  if (step == 2) {
    mods::NativeStatus ps = mods::NativeMod::probe();
    t += "\nThis plugin could not be restricted on this system: " + mods::NativeMod::missingText(ps.protections) + ".\n";
    t += "\nIf you run it without these restrictions, its native code can do those things with your rights, like any program you start yourself. It stays switched off unless you allow this.\n";
    t += "\nThis choice is saved for this exact version of the plugin.";
    return t;
  }
  t += "\nThis mod ";
  std::string what;
  if (mi->scan.usesNet()) what += (mi->scan.declaredNet ? "asks for the network permission" : "contains code that looks like network access");
  if (mi->scan.hasNative) what += std::string(what.empty() ? "" : " and ") + "ships native code";
  if (what.empty()) what = "has nothing that needs your OK";
  t += what + ".\n";
  int shown = 0;
  for (auto& f : mi->scan.findings) {
    if (!(f.isNet() || f.kind == mods::Finding::Native)) continue;
    if (shown++ >= 4) { t += "  ...\n"; break; }
    t += "  - " + f.file + (f.line ? ":" + std::to_string(f.line) : "") + ": " + f.what.substr(0, 70) + "\n";
  }
  bool nativeFull = false;
  if (mi->scan.hasNative || !mi->m.native.empty()) {
    std::string line = helperLine();
    t += "\n" + line + "\n";
    nativeFull = mods::NativeMod::probe().full();
  }
  if (step == 1) t += nativeFull && !mi->scan.declaredNet && mi->m.scripts.empty() ? "\nThe scan is best effort. Allow only mods you trust." : "\nThis scan is best effort: a determined author can hide network use in the code. Allow only mods you trust.";
  else t += std::string("\n") + mods::kScanDisclaimer;
  return t;
}

ConsentDialog::ConsentDialog(App& a, const std::string& id, bool askConsent)
    : Dialog(a, D_ModConsent, true, stepFor(id, askConsent) == 2 ? "RUN WITHOUT LIMITS?" : askConsent ? "TRUST THIS MOD?" : "MOD SCAN", text(id, stepFor(id, askConsent)), askConsent ? "" : "OK", askConsent ? BUTTONS_YES_NO : BUTTONS_FOOTER), modId(id), ask(askConsent) {
  step = stepFor(id, askConsent);
  if (step == 0) ask = false;
  textAlign = -1;
  linesFont = "PLAIN2";
  if (step == 2) setYesNo("Run anyway", "Keep off");
  else if (askConsent) setYesNo("Allow", "Not now");
}

void ConsentDialog::onButton(int id) {
  if (scrollActive && !scrollingOff) return;
  if (mods::g_mods && step == 1 && id == ID_YES) {
    mods::g_mods->setConsent(modId, true);
    mods::ModInfo* mi = mods::g_mods->find(modId);
    followUp = mi && mods::g_mods->needsUnrestrictedStep(*mi);
  } else if (mods::g_mods && step == 2) {
    mods::g_mods->setUnrestricted(modId, id == ID_YES);
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
