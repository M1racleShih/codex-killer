// A disposable Codex-shaped process. Never contacts a model or reads user data.
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
int main(int argc, char **argv) {
    bool child = false, ignore = false;
    int fd = -1;
    for (int n = 1; n < argc; ++n) {
        if (!strcmp(argv[n], "--ignore-term")) ignore = true;
        if (!strcmp(argv[n], "--fork-child")) child = true;
        if (!strcmp(argv[n], "--rollout") && n + 1 < argc) fd = open(argv[++n], O_RDONLY);
    }
    if (ignore) signal(SIGTERM, SIG_IGN);
    if (child && fork() == 0) {
        execl("/usr/bin/sleep", "sleep", "120", nullptr);
        _exit(127);
    }
    std::puts("ready"); std::fflush(stdout);
    while (true) pause();
    if (fd >= 0) close(fd);
}
