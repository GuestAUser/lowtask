#include "platform/terminal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int input_fd;
    int output_fd;
    int output_flags;
    struct termios original;
    bool active;
} TerminalRestore;

static TerminalRestore restore_state;
static bool cleanup_registered;
static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t resize_requested;

static bool contains_case_insensitive(const char *text, const char *needle) {
    if (text == NULL || needle == NULL || needle[0] == '\0') {
        return false;
    }
    const size_t needle_length = strlen(needle);
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        size_t offset = 0U;
        while (offset < needle_length && text[index + offset] != '\0' &&
               tolower((unsigned char)text[index + offset]) == tolower((unsigned char)needle[offset])) {
            ++offset;
        }
        if (offset == needle_length) {
            return true;
        }
    }
    return false;
}

static bool environment_has_value(const char *name) {
    const char *value = getenv(name);
    return value != NULL && value[0] != '\0';
}

void terminal_detect_capabilities(TerminalCapabilities *capabilities) {
    if (capabilities == NULL) {
        return;
    }
    const char *color_term = getenv("COLORTERM");
    const char *term = getenv("TERM");
    const bool known_truecolor_term = contains_case_insensitive(term, "direct") ||
                                      contains_case_insensitive(term, "kitty") ||
                                      contains_case_insensitive(term, "alacritty") ||
                                      contains_case_insensitive(term, "wezterm") ||
                                      contains_case_insensitive(term, "foot") ||
                                      contains_case_insensitive(term, "ghostty");
    const bool known_truecolor_session = environment_has_value("WT_SESSION") ||
                                         environment_has_value("KITTY_WINDOW_ID") ||
                                         environment_has_value("WEZTERM_EXECUTABLE") ||
                                         environment_has_value("GHOSTTY_RESOURCES_DIR");
    const bool truecolor = contains_case_insensitive(color_term, "truecolor") ||
                           contains_case_insensitive(color_term, "24bit") ||
                           known_truecolor_term || known_truecolor_session;
    const bool color256 = truecolor || contains_case_insensitive(term, "256color");
    const char *locale = getenv("LC_ALL");
    if (locale == NULL || locale[0] == '\0') locale = getenv("LC_CTYPE");
    if (locale == NULL || locale[0] == '\0') locale = getenv("LANG");
    const bool utf8 = contains_case_insensitive(locale, "utf-8") ||
                      contains_case_insensitive(locale, "utf8");
    const char *ascii = getenv("LOWTASK_ASCII");
    *capabilities = (TerminalCapabilities){
        .truecolor = truecolor,
        .color256 = color256,
        .unicode = utf8 && !contains_case_insensitive(term, "dumb") &&
                   !(ascii != NULL && strcmp(ascii, "0") != 0),
    };
}

static bool write_all(int descriptor, const char *bytes, size_t length) {
    struct timespec started;
    if (clock_gettime(CLOCK_MONOTONIC, &started) != 0) return false;
    size_t offset = 0U;
    while (offset < length) {
        const ssize_t written = write(descriptor, bytes + offset, length - offset);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct timespec now;
            if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return false;
            const int64_t elapsed_ms = (int64_t)(now.tv_sec - started.tv_sec) * 1000LL +
                                       (int64_t)(now.tv_nsec - started.tv_nsec) / 1000000LL;
            if (elapsed_ms >= 250LL) return false;
            struct pollfd output = {.fd = descriptor, .events = POLLOUT};
            const int ready = poll(&output, 1U, (int)(250LL - elapsed_ms));
            if (ready < 0 && errno == EINTR) continue;
            if (ready <= 0 || (output.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) return false;
            continue;
        }
        if (written <= 0) {
            return false;
        }
        offset += (size_t)written;
    }
    return true;
}

static void restore_terminal(void) {
    if (!restore_state.active) {
        return;
    }
    static const char leave[] =
        "\x1b[?1006l\x1b[?1003l\x1b[?1002l\x1b[?1000l\x1b[0m\x1b[?25h\x1b[?7h\x1b[?1049l";
    /*
     * Restore input processing before attempting visual cleanup. Output may be
     * slow or broken, so escape cleanup is bounded best effort; canonical input
     * and signal behavior must not depend on those bytes being accepted.
     */
    (void)tcsetattr(restore_state.input_fd, TCSAFLUSH, &restore_state.original);
    (void)write_all(restore_state.output_fd, leave, sizeof(leave) - 1U);
    (void)fcntl(restore_state.output_fd, F_SETFL, restore_state.output_flags);
    restore_state.active = false;
}

bool terminal_refresh_size(Terminal *terminal) {
    if (terminal == NULL) {
        return false;
    }
    struct winsize window = {0};
    if (ioctl(terminal->output_fd, TIOCGWINSZ, &window) != 0 || window.ws_col == 0U || window.ws_row == 0U) {
        terminal->columns = 80U;
        terminal->rows = 24U;
        return false;
    }
    terminal->columns = window.ws_col > 1000U ? 1000U : window.ws_col;
    terminal->rows = window.ws_row > 1000U ? 1000U : window.ws_row;
    return true;
}

bool terminal_open(Terminal *terminal, int input_fd, int output_fd) {
    if (terminal == NULL || !isatty(input_fd) || !isatty(output_fd)) {
        errno = ENOTTY;
        return false;
    }
    *terminal = (Terminal){.input_fd = input_fd, .output_fd = output_fd};
    if (tcgetattr(input_fd, &terminal->original) != 0) {
        return false;
    }
    /*
     * Initialization is staged so every failure after raw mode is enabled can
     * restore the original terminal before returning. Once nonblocking output
     * is installed, terminal_close and atexit share the same idempotent restore
     * state, preventing duplicate teardown or a terminal left in raw mode.
     */
    struct termios raw = terminal->original;
    raw.c_iflag &= (tcflag_t)~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= (tcflag_t)~OPOST;
    raw.c_cflag |= CS8;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(input_fd, TCSAFLUSH, &raw) != 0) {
        return false;
    }
    terminal->raw_enabled = true;
    const int output_flags = fcntl(output_fd, F_GETFL);
    if (output_flags < 0 || fcntl(output_fd, F_SETFL, output_flags | O_NONBLOCK) != 0) {
        (void)tcsetattr(input_fd, TCSAFLUSH, &terminal->original);
        terminal->raw_enabled = false;
        return false;
    }
    terminal_detect_capabilities(&terminal->capabilities);
    (void)terminal_refresh_size(terminal);
    restore_state = (TerminalRestore){
        .input_fd = input_fd, .output_fd = output_fd,
        .output_flags = output_flags, .original = terminal->original, .active = true,
    };
    if (!cleanup_registered) {
        if (atexit(restore_terminal) != 0) {
            restore_terminal();
            terminal->raw_enabled = false;
            return false;
        }
        cleanup_registered = true;
    }
    static const char enter[] =
        "\x1b[?1049h\x1b[?25l\x1b[?7l\x1b[?1000h\x1b[?1002h\x1b[?1003h\x1b[?1006h\x1b[2J\x1b[H";
    if (!write_all(output_fd, enter, sizeof(enter) - 1U)) {
        restore_terminal();
        terminal->raw_enabled = false;
        return false;
    }
    return true;
}

void terminal_close(Terminal *terminal) {
    if (terminal != NULL && terminal->raw_enabled) {
        restore_terminal();
        terminal->raw_enabled = false;
    }
}

static void signal_handler(int signal_number) {
    /*
     * Signal handlers only publish sig_atomic_t flags. Terminal restoration,
     * allocation, rendering, and resize queries stay in the main loop because
     * none of those operations is async-signal-safe.
     */
    if (signal_number == SIGWINCH) {
        resize_requested = 1;
    } else {
        stop_requested = 1;
    }
}

bool terminal_install_signal_handlers(void) {
    const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGWINCH};
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    (void)sigemptyset(&action.sa_mask);
    for (size_t index = 0U; index < sizeof(signals) / sizeof(signals[0]); ++index) {
        if (sigaction(signals[index], &action, NULL) != 0) {
            return false;
        }
    }
    return true;
}

bool terminal_stop_requested(void) {
    return stop_requested != 0;
}

bool terminal_take_resize(void) {
    if (resize_requested == 0) {
        return false;
    }
    resize_requested = 0;
    return true;
}

double terminal_monotonic_seconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0.0;
    }
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}
