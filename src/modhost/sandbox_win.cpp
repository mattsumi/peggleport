#include "modhost/sandbox.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>

#include <string>
#include <vector>

namespace pp::modhost {

namespace {

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}

bool tokenInfo(TOKEN_INFORMATION_CLASS cls, std::vector<BYTE>& out) {
  HANDLE tok = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
  DWORD need = 0;
  GetTokenInformation(tok, cls, nullptr, 0, &need);
  out.assign(need ? need : 1, 0);
  BOOL ok = need && GetTokenInformation(tok, cls, out.data(), need, &need);
  CloseHandle(tok);
  return ok != 0;
}

bool integrityRid(DWORD* rid) {
  std::vector<BYTE> buf;
  if (!tokenInfo(TokenIntegrityLevel, buf)) return false;
  TOKEN_MANDATORY_LABEL* l = (TOKEN_MANDATORY_LABEL*)buf.data();
  DWORD n = *GetSidSubAuthorityCount(l->Label.Sid);
  if (n == 0) return false;
  *rid = *GetSidSubAuthority(l->Label.Sid, n - 1);
  return true;
}

void dropToUntrusted() {
  HANDLE tok = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_DEFAULT | TOKEN_QUERY, &tok)) return;
  PSID sid = nullptr;
  if (ConvertStringSidToSidW(L"S-1-16-0", &sid)) {
    TOKEN_MANDATORY_LABEL tml;
    ZeroMemory(&tml, sizeof tml);
    tml.Label.Attributes = SE_GROUP_INTEGRITY;
    tml.Label.Sid = sid;
    SetTokenInformation(tok, TokenIntegrityLevel, &tml, (DWORD)(sizeof tml + GetLengthSid(sid)));
    LocalFree(sid);
  }
  CloseHandle(tok);
}

bool isAppContainer() {
  std::vector<BYTE> buf;
  if (!tokenInfo(TokenIsAppContainer, buf) || buf.size() < sizeof(DWORD)) return false;
  return *(DWORD*)buf.data() != 0;
}

bool jobLimited() {
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
  ZeroMemory(&info, sizeof info);
  if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &info, sizeof info, nullptr)) return false;
  DWORD f = info.BasicLimitInformation.LimitFlags;
  if (!(f & JOB_OBJECT_LIMIT_ACTIVE_PROCESS) || info.BasicLimitInformation.ActiveProcessLimit != 1) return false;
  if (f & (JOB_OBJECT_LIMIT_BREAKAWAY_OK | JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK)) return false;
  return true;
}

bool createDenied(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    CloseHandle(h);
    DeleteFileW(path.c_str());
    return false;
  }
  return GetLastError() == ERROR_ACCESS_DENIED;
}

bool noFileWrite(const std::string& pluginDir) {
  std::wstring name = L".pp-modhost-probe-" + std::to_wstring((unsigned long)GetCurrentProcessId());
  wchar_t tmp[MAX_PATH + 2];
  DWORD n = GetTempPathW(MAX_PATH, tmp);
  if (n == 0 || n > MAX_PATH) return false;
  if (!createDenied(std::wstring(tmp) + name)) return false;
  if (!pluginDir.empty()) {
    std::wstring d = widen(pluginDir);
    if (!d.empty() && d.back() != L'\\' && d.back() != L'/') d += L'\\';
    if (!createDenied(d + name)) return false;
  }
  return true;
}

}

SandboxReport applySandbox(const std::string& pluginDir) {
  SandboxReport r;
  dropToUntrusted();
  DWORD rid = SECURITY_MANDATORY_MEDIUM_RID;
  const bool known = integrityRid(&rid);
  const bool untrusted = known && rid < SECURITY_MANDATORY_LOW_RID;
  const bool low = known && rid <= SECURITY_MANDATORY_LOW_RID;
  const bool job = jobLimited();
  const bool app = isAppContainer();
  if (job) r.protections |= kNoSpawn;
  if ((untrusted || app) && noFileWrite(pluginDir)) r.protections |= kNoFileWrite;
  r.detail = std::string("Windows token: ") + (app ? "app container" : untrusted ? "untrusted integrity" : low ? "low integrity" : "normal integrity") + ", job limit " + (job ? "on" : "off");
  return r;
}

}

#endif
