#include "modhost/sandbox.h"

#ifdef __linux__

#include <fcntl.h>
#include <limits.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "modhost/sandbox_probe.h"

#ifndef SECCOMP_SET_MODE_FILTER
#define SECCOMP_SET_MODE_FILTER 1
#endif
#ifndef SECCOMP_FILTER_FLAG_TSYNC
#define SECCOMP_FILTER_FLAG_TSYNC 1
#endif
#ifndef SECCOMP_RET_ERRNO
#define SECCOMP_RET_ERRNO 0x00050000U
#endif
#ifndef SECCOMP_RET_ALLOW
#define SECCOMP_RET_ALLOW 0x7fff0000U
#endif
#ifndef SECCOMP_RET_KILL_PROCESS
#define SECCOMP_RET_KILL_PROCESS 0x80000000U
#endif
#ifndef SECCOMP_RET_DATA
#define SECCOMP_RET_DATA 0x0000ffffU
#endif
#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif
#ifndef CLONE_THREAD
#define CLONE_THREAD 0x00010000
#endif

namespace pp::modhost {

namespace {

constexpr long kSysLandlockCreate = 444;
constexpr long kSysLandlockAdd = 445;
constexpr long kSysLandlockRestrict = 446;
constexpr uint32_t kLlCreateVersion = 1u;
constexpr int kLlRulePathBeneath = 1;

constexpr uint64_t kLlExecute = 1ull << 0;
constexpr uint64_t kLlReadFile = 1ull << 2;
constexpr uint64_t kLlReadDir = 1ull << 3;
constexpr uint64_t kLlRefer = 1ull << 13;
constexpr uint64_t kLlTruncate = 1ull << 14;
constexpr uint64_t kLlIoctlDev = 1ull << 15;
constexpr uint64_t kLlAllV1 = (1ull << 13) - 1;

struct LlRulesetAttr {
  uint64_t handled_access_fs;
};
struct __attribute__((packed)) LlPathBeneath {
  uint64_t allowed_access;
  int32_t parent_fd;
};

bool addPathRule(int ruleset, const std::string& path) {
  int fd = open(path.c_str(), O_PATH | O_CLOEXEC);
  if (fd < 0) return false;
  struct stat st;
  bool dir = fstat(fd, &st) == 0 && S_ISDIR(st.st_mode);
  LlPathBeneath pb;
  pb.allowed_access = dir ? (kLlReadFile | kLlReadDir | kLlExecute) : kLlReadFile;
  pb.parent_fd = fd;
  long rc = syscall(kSysLandlockAdd, ruleset, kLlRulePathBeneath, &pb, 0u);
  close(fd);
  return rc == 0;
}

std::vector<std::string> mappedLibraryDirs() {
  std::set<std::string> dirs;
  std::ifstream f("/proc/self/maps");
  std::string line;
  while (std::getline(f, line)) {
    size_t slash = line.find('/');
    if (slash == std::string::npos) continue;
    std::string path = line.substr(slash);
    if (path.find(".so") == std::string::npos) continue;
    size_t cut = path.find_last_of('/');
    if (cut == 0 || cut == std::string::npos) continue;
    dirs.insert(path.substr(0, cut));
  }
  return std::vector<std::string>(dirs.begin(), dirs.end());
}

bool applyLandlock(const std::string& pluginDir, std::string* note) {
  long abi = syscall(kSysLandlockCreate, nullptr, 0, kLlCreateVersion);
  if (abi < 1) {
    *note = "Landlock is not available";
    return false;
  }
  uint64_t handled = kLlAllV1;
  if (abi >= 2) handled |= kLlRefer;
  if (abi >= 3) handled |= kLlTruncate;
  if (abi >= 5) handled |= kLlIoctlDev;
  LlRulesetAttr attr;
  attr.handled_access_fs = handled;
  int rs = (int)syscall(kSysLandlockCreate, &attr, sizeof attr, 0u);
  if (rs < 0) {
    *note = "Landlock ruleset failed";
    return false;
  }
  std::vector<std::string> paths;
  if (!pluginDir.empty()) paths.push_back(pluginDir);
  for (const char* p : {"/usr", "/lib", "/lib32", "/lib64", "/libx32", "/etc/ld.so.cache", "/dev/null", "/dev/urandom", "/dev/random", "/sys/devices/system/cpu"}) paths.push_back(p);
  for (auto& d : mappedLibraryDirs()) paths.push_back(d);
  for (auto& p : paths) addPathRule(rs, p);
  long rc = syscall(kSysLandlockRestrict, rs, 0u);
  close(rs);
  if (rc != 0) {
    *note = "Landlock could not be enforced";
    return false;
  }
  return true;
}

#if defined(__x86_64__)
constexpr uint32_t kArch = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
constexpr uint32_t kArch = AUDIT_ARCH_AARCH64;
#else
constexpr uint32_t kArch = 0;
#endif

#ifdef O_TMPFILE
constexpr uint32_t kTmpFile = (uint32_t)(O_TMPFILE & ~O_DIRECTORY);
#else
constexpr uint32_t kTmpFile = 020000000u;
#endif
constexpr uint32_t kWriteBits = (uint32_t)(O_ACCMODE | O_CREAT | O_TRUNC | O_APPEND) | kTmpFile;

struct Filter {
  std::vector<sock_filter> ins;
  std::vector<size_t> toEperm, toEnosys;
  void stmt(uint16_t code, uint32_t k) { ins.push_back(BPF_STMT(code, k)); }
  void jeqTo(uint32_t k, std::vector<size_t>& list) {
    ins.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, k, 0, 0));
    list.push_back(ins.size() - 1);
  }
  void denyEperm(long nr) { if (nr >= 0) jeqTo((uint32_t)nr, toEperm); }
};

void collectDenied(Filter& f) {
#ifdef __NR_socket
  f.denyEperm(__NR_socket);
#endif
#ifdef __NR_socketpair
  f.denyEperm(__NR_socketpair);
#endif
#ifdef __NR_connect
  f.denyEperm(__NR_connect);
#endif
#ifdef __NR_bind
  f.denyEperm(__NR_bind);
#endif
#ifdef __NR_listen
  f.denyEperm(__NR_listen);
#endif
#ifdef __NR_accept
  f.denyEperm(__NR_accept);
#endif
#ifdef __NR_accept4
  f.denyEperm(__NR_accept4);
#endif
#ifdef __NR_sendto
  f.denyEperm(__NR_sendto);
#endif
#ifdef __NR_sendmsg
  f.denyEperm(__NR_sendmsg);
#endif
#ifdef __NR_sendmmsg
  f.denyEperm(__NR_sendmmsg);
#endif
#ifdef __NR_execve
  f.denyEperm(__NR_execve);
#endif
#ifdef __NR_execveat
  f.denyEperm(__NR_execveat);
#endif
#ifdef __NR_fork
  f.denyEperm(__NR_fork);
#endif
#ifdef __NR_vfork
  f.denyEperm(__NR_vfork);
#endif
#ifdef __NR_ptrace
  f.denyEperm(__NR_ptrace);
#endif
#ifdef __NR_process_vm_readv
  f.denyEperm(__NR_process_vm_readv);
#endif
#ifdef __NR_process_vm_writev
  f.denyEperm(__NR_process_vm_writev);
#endif
#ifdef __NR_kill
  f.denyEperm(__NR_kill);
#endif
#ifdef __NR_tkill
  f.denyEperm(__NR_tkill);
#endif
#ifdef __NR_pidfd_open
  f.denyEperm(__NR_pidfd_open);
#endif
#ifdef __NR_pidfd_send_signal
  f.denyEperm(__NR_pidfd_send_signal);
#endif
#ifdef __NR_pidfd_getfd
  f.denyEperm(__NR_pidfd_getfd);
#endif
#ifdef __NR_mount
  f.denyEperm(__NR_mount);
#endif
#ifdef __NR_umount2
  f.denyEperm(__NR_umount2);
#endif
#ifdef __NR_pivot_root
  f.denyEperm(__NR_pivot_root);
#endif
#ifdef __NR_chroot
  f.denyEperm(__NR_chroot);
#endif
#ifdef __NR_fsopen
  f.denyEperm(__NR_fsopen);
#endif
#ifdef __NR_fsconfig
  f.denyEperm(__NR_fsconfig);
#endif
#ifdef __NR_fsmount
  f.denyEperm(__NR_fsmount);
#endif
#ifdef __NR_fspick
  f.denyEperm(__NR_fspick);
#endif
#ifdef __NR_move_mount
  f.denyEperm(__NR_move_mount);
#endif
#ifdef __NR_open_tree
  f.denyEperm(__NR_open_tree);
#endif
#ifdef __NR_mount_setattr
  f.denyEperm(__NR_mount_setattr);
#endif
#ifdef __NR_unshare
  f.denyEperm(__NR_unshare);
#endif
#ifdef __NR_setns
  f.denyEperm(__NR_setns);
#endif
#ifdef __NR_bpf
  f.denyEperm(__NR_bpf);
#endif
#ifdef __NR_perf_event_open
  f.denyEperm(__NR_perf_event_open);
#endif
#ifdef __NR_userfaultfd
  f.denyEperm(__NR_userfaultfd);
#endif
#ifdef __NR_io_uring_setup
  f.denyEperm(__NR_io_uring_setup);
#endif
#ifdef __NR_io_uring_enter
  f.denyEperm(__NR_io_uring_enter);
#endif
#ifdef __NR_io_uring_register
  f.denyEperm(__NR_io_uring_register);
#endif
#ifdef __NR_kexec_load
  f.denyEperm(__NR_kexec_load);
#endif
#ifdef __NR_kexec_file_load
  f.denyEperm(__NR_kexec_file_load);
#endif
#ifdef __NR_init_module
  f.denyEperm(__NR_init_module);
#endif
#ifdef __NR_finit_module
  f.denyEperm(__NR_finit_module);
#endif
#ifdef __NR_delete_module
  f.denyEperm(__NR_delete_module);
#endif
#ifdef __NR_reboot
  f.denyEperm(__NR_reboot);
#endif
#ifdef __NR_swapon
  f.denyEperm(__NR_swapon);
#endif
#ifdef __NR_swapoff
  f.denyEperm(__NR_swapoff);
#endif
#ifdef __NR_keyctl
  f.denyEperm(__NR_keyctl);
#endif
#ifdef __NR_add_key
  f.denyEperm(__NR_add_key);
#endif
#ifdef __NR_request_key
  f.denyEperm(__NR_request_key);
#endif
#ifdef __NR_open_by_handle_at
  f.denyEperm(__NR_open_by_handle_at);
#endif
#ifdef __NR_name_to_handle_at
  f.denyEperm(__NR_name_to_handle_at);
#endif
#ifdef __NR_mkdir
  f.denyEperm(__NR_mkdir);
#endif
#ifdef __NR_mkdirat
  f.denyEperm(__NR_mkdirat);
#endif
#ifdef __NR_rmdir
  f.denyEperm(__NR_rmdir);
#endif
#ifdef __NR_unlink
  f.denyEperm(__NR_unlink);
#endif
#ifdef __NR_unlinkat
  f.denyEperm(__NR_unlinkat);
#endif
#ifdef __NR_rename
  f.denyEperm(__NR_rename);
#endif
#ifdef __NR_renameat
  f.denyEperm(__NR_renameat);
#endif
#ifdef __NR_renameat2
  f.denyEperm(__NR_renameat2);
#endif
#ifdef __NR_link
  f.denyEperm(__NR_link);
#endif
#ifdef __NR_linkat
  f.denyEperm(__NR_linkat);
#endif
#ifdef __NR_symlink
  f.denyEperm(__NR_symlink);
#endif
#ifdef __NR_symlinkat
  f.denyEperm(__NR_symlinkat);
#endif
#ifdef __NR_mknod
  f.denyEperm(__NR_mknod);
#endif
#ifdef __NR_mknodat
  f.denyEperm(__NR_mknodat);
#endif
#ifdef __NR_truncate
  f.denyEperm(__NR_truncate);
#endif
#ifdef __NR_chmod
  f.denyEperm(__NR_chmod);
#endif
#ifdef __NR_fchmod
  f.denyEperm(__NR_fchmod);
#endif
#ifdef __NR_fchmodat
  f.denyEperm(__NR_fchmodat);
#endif
#ifdef __NR_fchmodat2
  f.denyEperm(__NR_fchmodat2);
#endif
#ifdef __NR_chown
  f.denyEperm(__NR_chown);
#endif
#ifdef __NR_fchown
  f.denyEperm(__NR_fchown);
#endif
#ifdef __NR_lchown
  f.denyEperm(__NR_lchown);
#endif
#ifdef __NR_fchownat
  f.denyEperm(__NR_fchownat);
#endif
#ifdef __NR_setxattr
  f.denyEperm(__NR_setxattr);
#endif
#ifdef __NR_lsetxattr
  f.denyEperm(__NR_lsetxattr);
#endif
#ifdef __NR_fsetxattr
  f.denyEperm(__NR_fsetxattr);
#endif
#ifdef __NR_removexattr
  f.denyEperm(__NR_removexattr);
#endif
#ifdef __NR_lremovexattr
  f.denyEperm(__NR_lremovexattr);
#endif
#ifdef __NR_fremovexattr
  f.denyEperm(__NR_fremovexattr);
#endif
#ifdef __NR_utime
  f.denyEperm(__NR_utime);
#endif
#ifdef __NR_utimes
  f.denyEperm(__NR_utimes);
#endif
#ifdef __NR_futimesat
  f.denyEperm(__NR_futimesat);
#endif
#ifdef __NR_utimensat
  f.denyEperm(__NR_utimensat);
#endif
#ifdef __NR_creat
  f.denyEperm(__NR_creat);
#endif
}

bool applySeccomp(std::string* note) {
  if (kArch == 0) {
    *note = "seccomp filter not available for this CPU";
    return false;
  }
  Filter f;
  f.stmt(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch));
  f.ins.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, kArch, 1, 0));
  f.stmt(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
  f.stmt(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));
#if defined(__x86_64__)
  f.ins.push_back(BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000u, 0, 1));
  f.stmt(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS);
#endif
  collectDenied(f);
  std::vector<size_t> toClone, toOpenat, toOpen;
#ifdef __NR_clone3
  f.jeqTo((uint32_t)__NR_clone3, f.toEnosys);
#endif
#ifdef __NR_openat2
  f.jeqTo((uint32_t)__NR_openat2, f.toEnosys);
#endif
#ifdef __NR_clone
  f.jeqTo((uint32_t)__NR_clone, toClone);
#endif
#ifdef __NR_openat
  f.jeqTo((uint32_t)__NR_openat, toOpenat);
#endif
#ifdef __NR_open
  f.jeqTo((uint32_t)__NR_open, toOpen);
#endif
  f.stmt(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
  const size_t eperm = f.ins.size();
  f.stmt(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));
  const size_t enosys = f.ins.size();
  f.stmt(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (ENOSYS & SECCOMP_RET_DATA));
  const size_t cloneAt = f.ins.size();
  f.stmt(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[0]));
  f.ins.push_back(BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, (uint32_t)CLONE_THREAD, 0, 1));
  f.stmt(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
  f.stmt(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));
  const size_t openatAt = f.ins.size();
  f.stmt(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[2]));
  f.ins.push_back(BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, kWriteBits, 0, 1));
  f.stmt(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EACCES & SECCOMP_RET_DATA));
  f.stmt(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
  const size_t openAt = f.ins.size();
  f.stmt(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, args[1]));
  f.ins.push_back(BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, kWriteBits, 0, 1));
  f.stmt(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EACCES & SECCOMP_RET_DATA));
  f.stmt(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);

  auto setJump = [&](size_t at, size_t target) -> bool {
    if (target <= at) return false;
    size_t off = target - at - 1;
    if (off > 255) return false;
    f.ins[at].jt = (uint8_t)off;
    f.ins[at].jf = 0;
    return true;
  };
  bool ok = true;
  for (size_t at : f.toEperm) ok = setJump(at, eperm) && ok;
  for (size_t at : f.toEnosys) ok = setJump(at, enosys) && ok;
  for (size_t at : toClone) ok = setJump(at, cloneAt) && ok;
  for (size_t at : toOpenat) ok = setJump(at, openatAt) && ok;
  for (size_t at : toOpen) ok = setJump(at, openAt) && ok;
  if (!ok || f.ins.size() > 4000) {
    *note = "seccomp program too long";
    return false;
  }
  sock_fprog prog;
  prog.len = (unsigned short)f.ins.size();
  prog.filter = f.ins.data();
  long rc = syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_TSYNC, &prog);
  if (rc != 0) rc = prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog, 0, 0);
  if (rc != 0) {
    *note = "seccomp filter was refused";
    return false;
  }
  return true;
}

}

SandboxReport applySandbox(const std::string& pluginDir) {
  SandboxReport r;
  probe::Before pre = probe::before();
  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
    r.detail = "no_new_privs could not be set";
    return r;
  }
  std::string llNote, scNote;
  bool ll = applyLandlock(pluginDir, &llNote);
  bool sc = applySeccomp(&scNote);
  std::string missing;
  if (sc && probe::noNetwork()) r.protections |= kNoNetwork; else missing += " network";
  if ((ll || sc) && probe::noFileWrite(pluginDir)) r.protections |= kNoFileWrite; else missing += " file-write";
  if (sc && probe::noSpawn()) r.protections |= kNoSpawn; else missing += " process-spawn";
  if (ll && probe::readRestricted(pre)) r.protections |= kReadRestricted; else missing += " file-read";
  r.detail = std::string("Linux sandbox: ") + (ll ? "Landlock on" : llNote) + ", " + (sc ? "seccomp on" : scNote);
  if (!missing.empty()) r.detail += ", not confirmed:" + missing;
  return r;
}

}

#endif
