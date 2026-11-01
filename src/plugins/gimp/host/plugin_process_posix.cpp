// Linux and macOS: the plug-in gets its protocol pipes as descriptors 3 and 4, stdin is
// /dev/null and stdout/stderr go to one pipe the helper forwards. It leads its own
// process group, so ending the group ends whatever the plug-in started; on Linux it
// also dies with the helper (PR_SET_PDEATHSIG).

#include "plugins/gimp/host/plugin_process.hpp"

#include "plugins/gimp/gimp_protocol.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif

extern char** environ;

namespace patchy::gimp::host {

namespace {

class PosixPluginProcess final : public PluginProcess {
public:
  ~PosixPluginProcess() override {
    kill_tree();
    if (output_thread_.joinable()) {
      output_thread_.detach();
    }
    if (to_plugin_ >= 0) close(to_plugin_);
    if (from_plugin_ >= 0) close(from_plugin_);
    if (!reaped_) {
      int status = 0;
      waitpid(pid_, &status, 0);
    }
  }

  std::ptrdiff_t read(std::uint8_t* data, std::size_t size) override {
    for (;;) {
      const auto got = ::read(from_plugin_, data, size);
      if (got < 0 && errno == EINTR) continue;
      return got;
    }
  }

  bool write(const std::uint8_t* data, std::size_t size) override {
    while (size > 0) {
      const auto wrote = ::write(to_plugin_, data, size);
      if (wrote < 0 && errno == EINTR) continue;
      if (wrote <= 0) return false;
      data += wrote;
      size -= static_cast<std::size_t>(wrote);
    }
    return true;
  }

  void kill_tree() override {
    if (pid_ > 0 && !reaped_) {
      ::kill(-pid_, SIGKILL);
      ::kill(pid_, SIGKILL);
    }
  }

  bool wait(int timeout_ms) override {
    if (reaped_) return true;
    for (int waited = 0;; waited += 20) {
      int status = 0;
      const auto r = waitpid(pid_, &status, WNOHANG);
      if (r == pid_) {
        reaped_ = true;
        status_ = status;
        return true;
      }
      if (timeout_ms >= 0 && waited >= timeout_ms) return false;
      usleep(20000);
    }
  }

  int exit_code() const override { return WIFEXITED(status_) ? WEXITSTATUS(status_) : -1; }

  std::string exit_description() const override {
    if (!reaped_) return "still running";
    if (WIFSIGNALED(status_)) {
      const int sig = WTERMSIG(status_);
      return std::string("crashed (signal ") + std::to_string(sig) + (sig == SIGSEGV ? ", segmentation fault)" : sig == SIGABRT ? ", abort)" : ")");
    }
    return "exit code " + std::to_string(exit_code());
  }

  std::uint64_t pid() const override { return static_cast<std::uint64_t>(pid_); }

  pid_t pid_{-1};
  int to_plugin_{-1};
  int from_plugin_{-1};
  bool reaped_{false};
  int status_{0};
  std::thread output_thread_;
};

}  // namespace

std::unique_ptr<PluginProcess> start_plugin_process(const link::LaunchSpec& launch, const std::string& mode,
                                                    std::function<void(std::string)> output, std::string* error) {
  const auto fail = [error](std::string message) -> std::unique_ptr<PluginProcess> {
    if (error != nullptr) *error = std::move(message);
    return nullptr;
  };
  int to_child[2];
  int from_child[2];
  int out[2];
  if (pipe(to_child) != 0 || pipe(from_child) != 0 || pipe(out) != 0) {
    return fail("could not create the plug-in's pipes");
  }
  for (int fd : {to_child[1], from_child[0], out[0]}) {
    fcntl(fd, F_SETFD, FD_CLOEXEC);
  }

  // Everything the child needs is built before fork: only async-signal-safe calls after.
  std::vector<std::string> args{launch.program};
  args.insert(args.end(), launch.leading_args.begin(), launch.leading_args.end());
  for (const char* a : {"-gimp"}) args.emplace_back(a);
  args.push_back(std::to_string(kProtocolVersion));
  args.emplace_back("3");
  args.emplace_back("4");
  args.push_back(mode);
  args.emplace_back("0");
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back(a.data());
  argv.push_back(nullptr);

  std::map<std::string, std::string> vars;
  for (char** e = environ; e != nullptr && *e != nullptr; ++e) {
    std::string entry(*e);
    const auto eq = entry.find('=');
    if (eq != std::string::npos) vars[entry.substr(0, eq)] = entry.substr(eq + 1);
  }
  for (const auto& name : launch.unset) vars.erase(name);
  for (const auto& entry : launch.environment) {
    const auto eq = entry.find('=');
    if (eq != std::string::npos && eq > 0) vars[entry.substr(0, eq)] = entry.substr(eq + 1);
  }
  std::vector<std::string> env_strings;
  for (const auto& [name, value] : vars) env_strings.push_back(name + "=" + value);
  std::vector<char*> envp;
  for (auto& e : env_strings) envp.push_back(e.data());
  envp.push_back(nullptr);
  const int devnull = open("/dev/null", O_RDONLY);
  const std::string working_dir = launch.working_dir;
#if defined(__linux__)
  const pid_t parent = getpid();
#endif

  const pid_t pid = fork();
  if (pid < 0) {
    return fail("could not start the plug-in (fork failed)");
  }
  if (pid == 0) {
    setpgid(0, 0);
#if defined(__linux__)
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() != parent) _exit(127);
#endif
    // Move the pipe ends clear of 0..4 first, then into place.
    int r = fcntl(to_child[0], F_DUPFD, 10);
    int w = fcntl(from_child[1], F_DUPFD, 10);
    int o = fcntl(out[1], F_DUPFD, 10);
    dup2(devnull >= 0 ? devnull : r, 0);
    dup2(o, 1);
    dup2(o, 2);
    dup2(r, 3);
    dup2(w, 4);
    for (int fd = 5; fd < 1024; ++fd) close(fd);
    if (!working_dir.empty() && chdir(working_dir.c_str()) != 0) {
      // Keep going in the inherited directory.
    }
    execve(argv[0], argv.data(), envp.data());
    _exit(127);
  }
  setpgid(pid, pid);
  close(to_child[0]);
  close(from_child[1]);
  close(out[1]);
  if (devnull >= 0) close(devnull);

  auto process = std::make_unique<PosixPluginProcess>();
  process->pid_ = pid;
  process->to_plugin_ = to_child[1];
  process->from_plugin_ = from_child[0];
  const int out_read = out[0];
  process->output_thread_ = std::thread([out_read, output = std::move(output)] {
    char buffer[4096];
    for (;;) {
      const auto got = ::read(out_read, buffer, sizeof buffer);
      if (got < 0 && errno == EINTR) continue;
      if (got <= 0) break;
      if (output) output(std::string(buffer, static_cast<std::size_t>(got)));
    }
    close(out_read);
  });
  return process;
}

}  // namespace patchy::gimp::host
