#ifndef LOWTASK_PLATFORM_TERMINAL_H
#define LOWTASK_PLATFORM_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <termios.h>

typedef struct {
    bool truecolor;
    bool color256;
    bool unicode;
} TerminalCapabilities;

typedef struct {
    int input_fd;
    int output_fd;
    size_t columns;
    size_t rows;
    struct termios original;
    TerminalCapabilities capabilities;
    bool raw_enabled;
} Terminal;

void terminal_detect_capabilities(TerminalCapabilities *capabilities);
bool terminal_open(Terminal *terminal, int input_fd, int output_fd);
void terminal_close(Terminal *terminal);
bool terminal_refresh_size(Terminal *terminal);
bool terminal_install_signal_handlers(void);
bool terminal_stop_requested(void);
bool terminal_take_resize(void);
double terminal_monotonic_seconds(void);

#endif
