#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum outcome {
    OUTCOME_COMMAND,
    OUTCOME_TIMEOUT_TERM,
    OUTCOME_TIMEOUT_KILL,
    OUTCOME_INTERRUPTED,
};

struct timeout_config {
    int timeout;
    int signal;
    int grace;
};

static volatile sig_atomic_t caller_signal;

static void record_signal(int signal_number)
{
    if (caller_signal == 0) {
        caller_signal = signal_number;
    }
}

static bool install_handlers(void)
{
    const int signals[] = {SIGHUP, SIGINT, SIGTERM, SIGQUIT};
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = record_signal;
    sigemptyset(&action.sa_mask);

    for (size_t index = 0; index < sizeof(signals) / sizeof(signals[0]); ++index) {
        if (sigaction(signals[index], &action, NULL) < 0) {
            return false;
        }
    }

    return true;
}

static void restore_child_signals(void)
{
    const int signals[] = {SIGHUP, SIGINT, SIGTERM, SIGQUIT};
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);

    for (size_t index = 0; index < sizeof(signals) / sizeof(signals[0]); ++index) {
        if (sigaction(signals[index], &action, NULL) < 0) {
            _exit(125);
        }
    }
}

static bool parse_seconds(const char *text, int *seconds)
{
    char *suffix = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &suffix, 10);
    if (errno != 0 || suffix == text || strcmp(suffix, "s") != 0 || value > INT_MAX) {
        return false;
    }

    *seconds = (int)value;
    return true;
}

static bool monotonic_now(struct timespec *now)
{
    return clock_gettime(CLOCK_MONOTONIC, now) == 0;
}

static struct timespec deadline_after(struct timespec now, int seconds)
{
    now.tv_sec += seconds;
    return now;
}

static bool deadline_reached(struct timespec now, struct timespec deadline)
{
    return now.tv_sec > deadline.tv_sec ||
           (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec);
}

static bool signal_group(pid_t group, int signal_number)
{
    if (kill(-group, signal_number) == 0 || errno == ESRCH) {
        return true;
    }

    return false;
}

static int child_state(pid_t child)
{
    siginfo_t information;

    memset(&information, 0, sizeof(information));
    if (waitid(P_PID, (id_t)child, &information, WEXITED | WNOHANG | WNOWAIT) < 0) {
        return errno == EINTR ? 0 : -1;
    }

    return information.si_pid == child ? 1 : 0;
}

static int command_status(int status)
{
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return 125;
}

static int finish_child(pid_t child, enum outcome outcome, int interruption)
{
    int status;
    bool cleanup_succeeded;
    pid_t waited;

    cleanup_succeeded = signal_group(child, SIGKILL);
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (!cleanup_succeeded || waited != child) {
        return 125;
    }

    switch (outcome) {
    case OUTCOME_COMMAND:
        return command_status(status);
    case OUTCOME_TIMEOUT_TERM:
        return 124;
    case OUTCOME_TIMEOUT_KILL:
        return 137;
    case OUTCOME_INTERRUPTED:
        return 128 + interruption;
    }

    return 125;
}

static int supervisor_failure(pid_t child)
{
    int status;

    signal_group(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return 125;
}

static int supervise(pid_t child, struct timeout_config config)
{
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = 10000000};
    struct timespec now;
    struct timespec deadline;
    enum outcome outcome = OUTCOME_COMMAND;
    int interruption = 0;
    int state;

    if (!monotonic_now(&now)) {
        return supervisor_failure(child);
    }
    deadline = deadline_after(now, config.timeout);

    for (;;) {
        state = child_state(child);
        if (state < 0) {
            return supervisor_failure(child);
        }
        if (state > 0) {
            return finish_child(child, outcome, interruption);
        }

        if (caller_signal != 0 && outcome != OUTCOME_INTERRUPTED) {
            interruption = caller_signal;
            outcome = OUTCOME_INTERRUPTED;
            if (!signal_group(child, interruption) || !monotonic_now(&now)) {
                return supervisor_failure(child);
            }
            deadline = deadline_after(now, config.grace);
        }

        if (!monotonic_now(&now)) {
            return supervisor_failure(child);
        }

        if (deadline_reached(now, deadline)) {
            if (outcome == OUTCOME_COMMAND) {
                if (!signal_group(child, config.signal)) {
                    return supervisor_failure(child);
                }
                if (config.signal == SIGKILL) {
                    outcome = OUTCOME_TIMEOUT_KILL;
                } else {
                    outcome = OUTCOME_TIMEOUT_TERM;
                    deadline = deadline_after(now, config.grace);
                }
            } else if (outcome == OUTCOME_TIMEOUT_TERM) {
                if (!signal_group(child, SIGKILL)) {
                    return supervisor_failure(child);
                }
                outcome = OUTCOME_TIMEOUT_KILL;
            } else if (outcome == OUTCOME_INTERRUPTED) {
                if (!signal_group(child, SIGKILL)) {
                    return supervisor_failure(child);
                }
            }
        }

        nanosleep(&pause, NULL);
    }
}

int main(int argument_count, char **arguments)
{
    struct timeout_config config;
    pid_t child;

    if (argument_count < 5 || !parse_seconds(arguments[1], &config.timeout) ||
        !parse_seconds(arguments[3], &config.grace)) {
        fprintf(stderr, "usage: timeout-supervisor TIMEOUT SIGNAL GRACE COMMAND [ARG ...]\n");
        return 125;
    }

    if (strcmp(arguments[2], "TERM") == 0) {
        config.signal = SIGTERM;
    } else if (strcmp(arguments[2], "KILL") == 0) {
        config.signal = SIGKILL;
    } else {
        fprintf(stderr, "timeout-supervisor: unsupported signal: %s\n", arguments[2]);
        return 125;
    }

    if (!install_handlers()) {
        perror("timeout-supervisor: sigaction");
        return 125;
    }

    child = fork();
    if (child < 0) {
        perror("timeout-supervisor: fork");
        return 125;
    }
    if (child == 0) {
        if (setpgid(0, 0) < 0) {
            _exit(125);
        }
        restore_child_signals();
        execvp(arguments[4], &arguments[4]);
        _exit(errno == ENOENT ? 127 : 126);
    }

    if (setpgid(child, child) < 0 && errno != EACCES && errno != ESRCH) {
        signal_group(child, SIGKILL);
        waitpid(child, NULL, 0);
        return 125;
    }

    return supervise(child, config);
}
