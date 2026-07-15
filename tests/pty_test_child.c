#define _XOPEN_SOURCE 700

#include "tests/pty_test_api.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static bool configure_child_environment(const char *root, bool reduced, bool ascii) {
    return setenv("XDG_DATA_HOME", root, 1) == 0 && setenv("HOME", root, 1) == 0 &&
           setenv("TERM", "xterm-256color", 1) == 0 && setenv("LC_ALL", pty_test_utf8_locale(), 1) == 0 &&
           setenv("LOWTASK_REDUCE_MOTION", reduced ? "1" : "0", 1) == 0 &&
           setenv("LOWTASK_ASCII", ascii ? "1" : "0", 1) == 0 && unsetenv("COLORTERM") == 0 &&
           unsetenv("WT_SESSION") == 0 && unsetenv("KITTY_WINDOW_ID") == 0 &&
           unsetenv("WEZTERM_EXECUTABLE") == 0 && unsetenv("GHOSTTY_RESOURCES_DIR") == 0;
}

Child spawn_child(int master, int slave, const char *root, bool reduced, bool ascii) {
    Child child = {.pid = fork()};
    if (child.pid != 0) return child;
    (void)close(master);
    if (dup2(slave, STDIN_FILENO) < 0 || dup2(slave, STDOUT_FILENO) < 0 ||
        dup2(slave, STDERR_FILENO) < 0) _exit(126);
    if (slave > STDERR_FILENO) (void)close(slave);
    if (!configure_child_environment(root, reduced, ascii)) _exit(126);
    execl("./lowtask", "lowtask", (char *)NULL);
    _exit(127);
}

Child spawn_contender(const char *root, int output_fd) {
    Child child = {.pid = fork()};
    if (child.pid != 0) return child;
    const int null_input = open("/dev/null", O_RDONLY);
    if (null_input < 0 || dup2(null_input, STDIN_FILENO) < 0 ||
        dup2(output_fd, STDOUT_FILENO) < 0 || dup2(output_fd, STDERR_FILENO) < 0 ||
        !configure_child_environment(root, true, true)) _exit(126);
    execl("./lowtask", "lowtask", (char *)NULL);
    _exit(127);
}

void terminate_and_reap(Child *child) {
    if (child->pid <= 0 || child->reaped) return;
    (void)kill(child->pid, SIGTERM);
    const int64_t deadline = monotonic_milliseconds() + TERMINATE_GRACE_MS;
    while (!child->reaped && monotonic_milliseconds() < deadline) {
        (void)child_poll_status(child);
        if (!child->reaped) (void)poll(NULL, 0U, 10);
    }
    if (!child->reaped) {
        (void)kill(child->pid, SIGKILL);
        while (waitpid(child->pid, &child->status, 0) < 0 && errno == EINTR) {}
        child->reaped = true;
    }
}

bool pty_test_remove_tree(const char *root, const char *data_directory) {
    bool ok = true;
    DIR *directory = opendir(data_directory);
    if (directory != NULL) {
        struct dirent *entry;
        while ((entry = readdir(directory)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
            char path[512];
            const int written = snprintf(path, sizeof(path), "%s/%s", data_directory, entry->d_name);
            if (written < 0 || (size_t)written >= sizeof(path) || unlink(path) != 0) ok = false;
        }
        if (closedir(directory) != 0 || rmdir(data_directory) != 0) ok = false;
    } else if (errno != ENOENT) {
        ok = false;
    }
    if (rmdir(root) != 0) ok = false;
    return ok;
}
