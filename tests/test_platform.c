#include "platform/terminal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static void clear_truecolor_markers(void) {
    assert(unsetenv("WT_SESSION") == 0);
    assert(unsetenv("KITTY_WINDOW_ID") == 0);
    assert(unsetenv("WEZTERM_EXECUTABLE") == 0);
    assert(unsetenv("GHOSTTY_RESOURCES_DIR") == 0);
}

static void test_capabilities(void) {
    TerminalCapabilities capabilities;
    clear_truecolor_markers();
    assert(setenv("COLORTERM", "truecolor", 1) == 0);
    assert(setenv("TERM", "xterm-256color", 1) == 0);
    assert(setenv("LANG", "en_US.UTF-8", 1) == 0);
    assert(unsetenv("LOWTASK_ASCII") == 0);
    terminal_detect_capabilities(&capabilities);
    assert(capabilities.truecolor);
    assert(capabilities.color256);
    assert(capabilities.unicode);

    assert(unsetenv("COLORTERM") == 0);
    assert(setenv("LOWTASK_ASCII", "1", 1) == 0);
    terminal_detect_capabilities(&capabilities);
    assert(!capabilities.truecolor);
    assert(capabilities.color256);
    assert(!capabilities.unicode);

    assert(setenv("TERM", "xterm-kitty", 1) == 0);
    terminal_detect_capabilities(&capabilities);
    assert(capabilities.truecolor);
    assert(capabilities.color256);

    assert(setenv("TERM", "xterm-256color", 1) == 0);
    assert(setenv("WT_SESSION", "terminal-session", 1) == 0);
    terminal_detect_capabilities(&capabilities);
    assert(capabilities.truecolor);
    assert(capabilities.color256);
}

int main(void) {
    test_capabilities();
    puts("test_platform: PASS");
    return 0;
}
