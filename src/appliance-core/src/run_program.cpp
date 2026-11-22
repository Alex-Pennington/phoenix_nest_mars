#include "run_program.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace pna {

RunResult run_program(const std::vector<std::string>& argv, int timeout_ms) {
    RunResult r;
    if (argv.empty()) return r;
    int out_p[2], err_p[2];
    if (pipe2(out_p, O_CLOEXEC) != 0) return r;
    if (pipe2(err_p, O_CLOEXEC) != 0) {
        close(out_p[0]);
        close(out_p[1]);
        return r;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, out_p[1], 1);
    posix_spawn_file_actions_adddup2(&fa, err_p[1], 2);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    std::vector<char*> args;
    for (auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    pid_t pid = 0;
    int rc = posix_spawnp(&pid, args[0], &fa, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(out_p[1]);
    close(err_p[1]);
    if (rc != 0) {
        close(out_p[0]);
        close(err_p[0]);
        r.err = std::strerror(rc);
        return r;
    }
    r.started = true;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    pollfd fds[2] = {{out_p[0], POLLIN, 0}, {err_p[0], POLLIN, 0}};
    int open_n = 2;
    char buf[4096];
    while (open_n > 0) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            r.timed_out = true;
            kill(pid, SIGKILL);
            break;
        }
        int n = poll(fds, 2, static_cast<int>(left.count()));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) break;
        for (int k = 0; k < 2; ++k) {
            if (fds[k].fd < 0 || !(fds[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            ssize_t got = read(fds[k].fd, buf, sizeof buf);
            if (got <= 0) {
                close(fds[k].fd);
                fds[k].fd = -1;
                --open_n;
                continue;
            }
            std::string& dst = k == 0 ? r.out : r.err;
            if (dst.size() < 65536) dst.append(buf, static_cast<size_t>(got));
        }
    }
    for (auto& f : fds)
        if (f.fd >= 0) close(f.fd);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (!r.timed_out && WIFEXITED(status)) r.exit_code = WEXITSTATUS(status);
    return r;
}

}  // namespace pna
