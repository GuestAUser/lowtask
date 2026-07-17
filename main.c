#include "app/runtime.h"
#include "core/persistence.h"
#include "core/state.h"
#include "platform/terminal.h"
#include "tui/render.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool save_if_needed(AppState *state, const char *path, bool *blocked) {
    if (!state->dirty || *blocked) {
        return true;
    }
    char error[256];
    if (!persistence_save(path, state->tasks, error, sizeof(error))) {
        (void)snprintf(state->status, sizeof(state->status), "save failed: %.130s", error);
        *blocked = true;
        return false;
    }
    state->dirty = false;
    return true;
}

static bool set_local_today(AppState *state) {
    const time_t now = time(NULL);
    struct tm local;
    if (now == (time_t)-1 || localtime_r(&now, &local) == NULL) return false;
    char today[LOWTASK_DUE_DATE_LENGTH + 1U];
    return strftime(today, sizeof(today), "%Y-%m-%d", &local) == LOWTASK_DUE_DATE_LENGTH &&
           app_state_set_today(state, today);
}

int main(void) {
    int exit_code = 0;
    char state_path[4096];
    if (!persistence_default_path(state_path, sizeof(state_path))) {
        (void)fprintf(stderr, "lowtask: cannot resolve the data directory\n");
        return 1;
    }
    char lock_error[256] = {0};
    int state_lock = -1;
    if (!persistence_lock(state_path, &state_lock, lock_error, sizeof(lock_error))) {
        (void)fprintf(stderr, "lowtask: %s\n", lock_error);
        return 1;
    }

    TaskList tasks;
    task_list_init(&tasks);
    char load_error[256] = {0};
    const bool loaded = persistence_load(state_path, &tasks, load_error, sizeof(load_error));
    if (!loaded) {
        (void)fprintf(stderr, "lowtask: refusing to overwrite unreadable state: %s\n", load_error);
        task_list_free(&tasks);
        persistence_unlock(state_lock);
        return 1;
    }

    AppState state;
    if (!app_state_init(&state, &tasks)) {
        (void)fprintf(stderr, "lowtask: cannot allocate application state\n");
        task_list_free(&tasks);
        persistence_unlock(state_lock);
        return 1;
    }
    if (!set_local_today(&state)) {
        (void)snprintf(state.status, sizeof(state.status), "cannot read local calendar date");
    }
    if (!terminal_install_signal_handlers()) {
        (void)fprintf(stderr, "lowtask: cannot install signal handlers: %s\n", strerror(errno));
        app_state_dispose(&state);
        task_list_free(&tasks);
        persistence_unlock(state_lock);
        return 1;
    }
    Terminal terminal;
    if (!terminal_open(&terminal, STDIN_FILENO, STDOUT_FILENO)) {
        (void)fprintf(stderr, "lowtask: a compatible interactive terminal is required: %s\n", strerror(errno));
        app_state_dispose(&state);
        task_list_free(&tasks);
        persistence_unlock(state_lock);
        return 1;
    }
    Renderer renderer;
    if (!renderer_init(&renderer, terminal.columns, terminal.rows, terminal.capabilities.truecolor)) {
        terminal_close(&terminal);
        (void)fprintf(stderr, "lowtask: cannot allocate the terminal renderer\n");
        app_state_dispose(&state);
        task_list_free(&tasks);
        persistence_unlock(state_lock);
        return 1;
    }

    exit_code = app_runtime_run(&state, &terminal, &renderer);

    /*
     * Shutdown preserves terminal usability before persistence: finish the
     * delayed delete while state is live, release rendering, restore the tty,
     * then block handled signals around the final durable save. State, tasks,
     * and the process lock remain owned until saving has completed.
     */
    app_state_finish_pending_delete(&state);
    renderer_free(&renderer);
    terminal_close(&terminal);
    bool save_error_reported = false;
    if (state.dirty) {
        bool save_blocked = false;
        sigset_t blocked_signals;
        sigset_t previous_signals;
        (void)sigemptyset(&blocked_signals);
        (void)sigaddset(&blocked_signals, SIGWINCH);
        (void)sigaddset(&blocked_signals, SIGINT);
        (void)sigaddset(&blocked_signals, SIGTERM);
        (void)sigaddset(&blocked_signals, SIGHUP);
        (void)sigaddset(&blocked_signals, SIGQUIT);
        const bool signals_blocked = sigprocmask(SIG_BLOCK, &blocked_signals, &previous_signals) == 0;
        if (!save_if_needed(&state, state_path, &save_blocked)) {
            (void)fprintf(stderr, "lowtask: %s\n", state.status);
            save_error_reported = true;
            exit_code = 1;
        }
        if (signals_blocked) {
            (void)sigprocmask(SIG_SETMASK, &previous_signals, NULL);
        }
    }
    app_state_dispose(&state);
    task_list_free(&tasks);
    persistence_unlock(state_lock);
    if (exit_code != 0 && !save_error_reported) {
        (void)fprintf(stderr, "lowtask: terminated after an I/O error\n");
    }
    return exit_code;
}
