#include "mods/hostproc.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mods/hostproto.h"

namespace fs = std::filesystem;
namespace pp::mods {

using Clock = std::chrono::steady_clock;

struct HostProcess::Impl {
  HANDLE process = nullptr;
  HANDLE job = nullptr;
  HANDLE toChild = nullptr;
  HANDLE fromChild = nullptr;
  std::thread reader;
  std::mutex mu;
  std::condition_variable cv;
  std::vector<uint8_t> inbuf;
  bool eof = false;
  bool bad = false;
  std::string badText;
  bool exited = false;
  DWORD exitCode = 0;
};

namespace {

std::string hex(uintptr_t v) {
  char b[32];
  std::snprintf(b, sizeof b, "%llx", (unsigned long long)v);
  return b;
}

struct SidBuf {
  BYTE data[SECURITY_MAX_SID_SIZE];
  DWORD size = SECURITY_MAX_SID_SIZE;
};

bool buildRestrictedToken(HANDLE* out, std::string* err) {
  HANDLE self = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY | TOKEN_ADJUST_DEFAULT, &self)) {
    if (err) *err = "OpenProcessToken failed (" + std::to_string(GetLastError()) + ")";
    return false;
  }
  DWORD need = 0;
  GetTokenInformation(self, TokenGroups, nullptr, 0, &need);
  std::vector<BYTE> gbuf(need ? need : 1);
  std::vector<SID_AND_ATTRIBUTES> disable;
  static const WELL_KNOWN_SID_TYPE kDangerous[] = {WinBuiltinAdministratorsSid, WinBuiltinPowerUsersSid, WinBuiltinBackupOperatorsSid, WinBuiltinAccountOperatorsSid,
                                                   WinBuiltinSystemOperatorsSid, WinBuiltinPrintOperatorsSid, WinBuiltinReplicatorSid, WinBuiltinNetworkConfigurationOperatorsSid,
                                                   WinBuiltinCryptoOperatorsSid};
  if (need && GetTokenInformation(self, TokenGroups, gbuf.data(), need, &need)) {
    TOKEN_GROUPS* g = (TOKEN_GROUPS*)gbuf.data();
    for (DWORD i = 0; i < g->GroupCount; i++) {
      if (g->Groups[i].Attributes & (SE_GROUP_LOGON_ID | SE_GROUP_INTEGRITY)) continue;
      for (WELL_KNOWN_SID_TYPE t : kDangerous) {
        if (IsWellKnownSid(g->Groups[i].Sid, t)) {
          SID_AND_ATTRIBUTES sa;
          sa.Sid = g->Groups[i].Sid;
          sa.Attributes = 0;
          disable.push_back(sa);
          break;
        }
      }
    }
  }
  HANDLE restricted = nullptr;
  BOOL ok = CreateRestrictedToken(self, DISABLE_MAX_PRIVILEGE, (DWORD)disable.size(), disable.empty() ? nullptr : disable.data(), 0, nullptr, 0, nullptr, &restricted);
  CloseHandle(self);
  if (!ok) {
    if (err) *err = "CreateRestrictedToken failed (" + std::to_string(GetLastError()) + ")";
    return false;
  }
  PSID low = nullptr;
  if (!ConvertStringSidToSidW(L"S-1-16-4096", &low)) {
    CloseHandle(restricted);
    if (err) *err = "cannot build the low integrity SID";
    return false;
  }
  TOKEN_MANDATORY_LABEL tml;
  ZeroMemory(&tml, sizeof tml);
  tml.Label.Attributes = SE_GROUP_INTEGRITY;
  tml.Label.Sid = low;
  BOOL set = SetTokenInformation(restricted, TokenIntegrityLevel, &tml, (DWORD)(sizeof tml + GetLengthSid(low)));
  LocalFree(low);
  if (!set) {
    CloseHandle(restricted);
    if (err) *err = "cannot lower the integrity level (" + std::to_string(GetLastError()) + ")";
    return false;
  }
  *out = restricted;
  return true;
}

HANDLE buildJob() {
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (!job) return nullptr;
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION lim;
  ZeroMemory(&lim, sizeof lim);
  lim.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
  lim.BasicLimitInformation.ActiveProcessLimit = 1;
  if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &lim, sizeof lim)) {
    CloseHandle(job);
    return nullptr;
  }
  JOBOBJECT_BASIC_UI_RESTRICTIONS ui;
  ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_DESKTOP | JOB_OBJECT_UILIMIT_DISPLAYSETTINGS | JOB_OBJECT_UILIMIT_EXITWINDOWS | JOB_OBJECT_UILIMIT_GLOBALATOMS |
                           JOB_OBJECT_UILIMIT_HANDLES | JOB_OBJECT_UILIMIT_READCLIPBOARD | JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS | JOB_OBJECT_UILIMIT_WRITECLIPBOARD;
  SetInformationJobObject(job, JobObjectBasicUIRestrictions, &ui, sizeof ui);
  return job;
}

std::wstring systemRootEnv() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetEnvironmentVariableW(L"SystemRoot", buf, MAX_PATH);
  std::wstring root = (n == 0 || n >= MAX_PATH) ? std::wstring(L"C:\\Windows") : std::wstring(buf, n);
  std::wstring block = L"SystemRoot=";
  block += root;
  block.push_back(L'\0');
  block += L"windir=";
  block += root;
  block.push_back(L'\0');
  block.push_back(L'\0');
  return block;
}

void readerLoop(HostProcess::Impl* I) {
  std::vector<uint8_t> tmp(1 << 16);
  for (;;) {
    DWORD got = 0;
    if (!ReadFile(I->fromChild, tmp.data(), (DWORD)tmp.size(), &got, nullptr) || got == 0) break;
    std::lock_guard<std::mutex> lk(I->mu);
    I->inbuf.insert(I->inbuf.end(), tmp.begin(), tmp.begin() + got);
    I->cv.notify_all();
  }
  std::lock_guard<std::mutex> lk(I->mu);
  I->eof = true;
  I->cv.notify_all();
}

}

std::unique_ptr<HostProcess> HostProcess::start(const fs::path& exe, std::string* err) {
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;
  HANDLE childRead = nullptr, parentWrite = nullptr, parentRead = nullptr, childWrite = nullptr;
  if (!CreatePipe(&childRead, &parentWrite, &sa, 1 << 20) || !CreatePipe(&parentRead, &childWrite, &sa, 1 << 20)) {
    if (err) *err = "cannot create pipes (" + std::to_string(GetLastError()) + ")";
    return nullptr;
  }
  SetHandleInformation(parentWrite, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(parentRead, HANDLE_FLAG_INHERIT, 0);

  HANDLE token = nullptr;
  std::string terr;
  if (!buildRestrictedToken(&token, &terr)) {
    if (err) *err = terr;
    CloseHandle(childRead); CloseHandle(parentWrite); CloseHandle(parentRead); CloseHandle(childWrite);
    return nullptr;
  }
  HANDLE job = buildJob();

  SIZE_T attrSize = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
  std::vector<BYTE> attrBuf(attrSize);
  LPPROC_THREAD_ATTRIBUTE_LIST attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)attrBuf.data();
  HANDLE inherit[2] = {childRead, childWrite};
  bool attrOk = InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
                UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof inherit, nullptr, nullptr);

  STARTUPINFOEXW si;
  ZeroMemory(&si, sizeof si);
  si.StartupInfo.cb = sizeof si;
  si.lpAttributeList = attrs;
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof pi);
  const std::string hin = hex((uintptr_t)childRead), hout = hex((uintptr_t)childWrite);
  std::wstring cmd = L"\"" + exe.wstring() + L"\" --serve " + std::wstring(hin.begin(), hin.end()) + L" " + std::wstring(hout.begin(), hout.end()) + L" 0";
  std::wstring env = systemRootEnv();
  std::wstring dir = exe.parent_path().wstring();
  BOOL created = attrOk && CreateProcessAsUserW(token, exe.c_str(), &cmd[0], nullptr, nullptr, TRUE,
                                                CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT, (LPVOID)env.data(),
                                                dir.c_str(), &si.StartupInfo, &pi);
  DWORD createErr = GetLastError();
  if (attrOk) DeleteProcThreadAttributeList(attrs);
  CloseHandle(token);
  CloseHandle(childRead);
  CloseHandle(childWrite);
  if (!created) {
    if (err) *err = "cannot start " + exe.string() + " (" + std::to_string(createErr) + ")";
    CloseHandle(parentWrite);
    CloseHandle(parentRead);
    if (job) CloseHandle(job);
    return nullptr;
  }
  if (job && !AssignProcessToJobObject(job, pi.hProcess)) {
    CloseHandle(job);
    job = nullptr;
  }
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);

  auto p = std::unique_ptr<HostProcess>(new HostProcess());
  p->im_ = std::make_unique<Impl>();
  Impl& I = *p->im_;
  I.process = pi.hProcess;
  I.job = job;
  I.toChild = parentWrite;
  I.fromChild = parentRead;
  I.reader = std::thread(readerLoop, &I);
  return p;
}

HostProcess::~HostProcess() {
  if (im_) stop();
}

void HostProcess::stop(bool force) {
  Impl& I = *im_;
  if (I.toChild) {
    CloseHandle(I.toChild);
    I.toChild = nullptr;
  }
  if (I.process && !I.exited) {
    DWORD wait = force ? 0 : 400;
    if (WaitForSingleObject(I.process, wait) != WAIT_OBJECT_0) {
      TerminateProcess(I.process, 1);
      WaitForSingleObject(I.process, 2000);
    }
    DWORD code = 0;
    if (GetExitCodeProcess(I.process, &code)) I.exitCode = code;
    I.exited = true;
  }
  if (I.job) {
    CloseHandle(I.job);
    I.job = nullptr;
  }
  if (I.reader.joinable()) {
    if (I.fromChild) {
      CancelIoEx(I.fromChild, nullptr);
    }
    I.reader.join();
  }
  if (I.fromChild) {
    CloseHandle(I.fromChild);
    I.fromChild = nullptr;
  }
  if (I.process) {
    CloseHandle(I.process);
    I.process = nullptr;
  }
}

bool HostProcess::running() const {
  Impl& I = *im_;
  if (!I.process || I.exited) return false;
  DWORD code = 0;
  if (!GetExitCodeProcess(I.process, &code) || code != STILL_ACTIVE) {
    I.exited = true;
    I.exitCode = code;
    return false;
  }
  return true;
}

std::string HostProcess::problem() const { return im_->badText; }

std::string HostProcess::exitText() {
  Impl& I = *im_;
  if (I.process && !I.exited) {
    if (WaitForSingleObject(I.process, 200) == WAIT_OBJECT_0) {
      DWORD code = 0;
      if (GetExitCodeProcess(I.process, &code)) I.exitCode = code;
      I.exited = true;
    }
  }
  if (!I.exited) return "the helper process did not exit";
  char b[64];
  std::snprintf(b, sizeof b, "the helper process ended with code 0x%08lx", (unsigned long)I.exitCode);
  return b;
}

IoResult HostProcess::send(const std::vector<uint8_t>& frame, int timeoutMs) {
  Impl& I = *im_;
  (void)timeoutMs;
  if (!I.toChild) return IoResult::Closed;
  const uint8_t* p = frame.data();
  size_t n = frame.size();
  while (n) {
    DWORD put = 0;
    if (!WriteFile(I.toChild, p, (DWORD)std::min<size_t>(n, 1u << 20), &put, nullptr) || put == 0) return IoResult::Closed;
    p += put;
    n -= put;
  }
  return IoResult::Ok;
}

IoResult HostProcess::receive(std::vector<uint8_t>& out, int timeoutMs) {
  Impl& I = *im_;
  std::unique_lock<std::mutex> lk(I.mu);
  auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
  for (;;) {
    if (I.inbuf.size() >= 4) {
      uint32_t len;
      std::memcpy(&len, I.inbuf.data(), 4);
      if (len < 1 || len > pp::modhost::kMaxFrame) {
        I.badText = "the helper sent a frame with a bad length";
        return IoResult::Closed;
      }
      if (I.inbuf.size() >= 4 + (size_t)len) {
        out.assign(I.inbuf.begin() + 4, I.inbuf.begin() + 4 + (long)len);
        I.inbuf.erase(I.inbuf.begin(), I.inbuf.begin() + 4 + (long)len);
        return IoResult::Ok;
      }
    }
    if (I.eof) return IoResult::Closed;
    if (Clock::now() >= deadline) return IoResult::Timeout;
    I.cv.wait_until(lk, deadline);
  }
}

fs::path findModHost() {
  std::error_code ec;
  wchar_t envbuf[MAX_PATH * 2];
  DWORD n = GetEnvironmentVariableW(L"PEGGLEPORT_MODHOST", envbuf, MAX_PATH * 2);
  if (n > 0 && n < MAX_PATH * 2 && fs::is_regular_file(fs::path(envbuf), ec)) return fs::path(envbuf);
  wchar_t self[MAX_PATH * 2];
  DWORD len = GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
  if (len == 0 || len >= MAX_PATH * 2) return {};
  fs::path host = fs::path(self).parent_path() / L"pp_modhost.exe";
  if (fs::is_regular_file(host, ec)) return host;
  return {};
}

}

#endif
