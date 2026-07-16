#include "tests/pty_test_api.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static bool resize_preserves_split_csi(void) {
    Screen screen = {0};
    static const char prefix[] = "\x1b[38;";
    static const char suffix[] = "5;252mready";
    if (!screen_resize(&screen, 16U, 2U)) return false;
    screen_feed(&screen, prefix, sizeof(prefix) - 1U);
    const bool split = screen.parser_state == 2U && screen.csi_length > 0U;
    if (!screen_resize(&screen, 16U, 2U)) {
        free(screen.cells);
        return false;
    }
    screen_feed(&screen, suffix, sizeof(suffix) - 1U);
    size_t x = 0U;
    size_t y = 0U;
    const bool preserved = split && screen_find_ascii(&screen, "ready", &x, &y) &&
                           x == 0U && y == 0U;
    free(screen.cells);
    return preserved;
}

static bool wait_drains_current_frame(void) {
    int descriptors[2];
    if (pipe(descriptors) != 0) return false;
    const int flags = fcntl(descriptors[0], F_GETFL);
    if (flags < 0 || fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK) != 0) {
        (void)close(descriptors[0]);
        (void)close(descriptors[1]);
        return false;
    }
    Session session = {.master = descriptors[0], .slave = -1, .child = {.pid = fork()}};
    if (session.child.pid == 0) {
        (void)close(descriptors[0]);
        static const char first[] = "\x1b[1;1HREADY\x1b[38;";
        static const char second[] = "5;252m\x1b[2;1Hdone";
        const bool wrote = write_all(descriptors[1], first, sizeof(first) - 1U);
        (void)poll(NULL, 0U, 200);
        const bool finished = wrote && write_all(descriptors[1], second, sizeof(second) - 1U);
        (void)poll(NULL, 0U, 80);
        (void)close(descriptors[1]);
        _exit(finished ? 0 : 1);
    }
    (void)close(descriptors[1]);
    bool ok = session.child.pid > 0 && screen_resize(&session.screen, 16U, 2U) &&
              session_wait(&session, "READY", 500LL) &&
              screen_contains(&session.screen, "done");
    terminate_and_reap(&session.child);
    (void)close(session.master);
    free(session.screen.cells);
    return ok;
}

int main(void) {
    struct rlimit core_limit;
    if (!install_interruption_handlers() || getrlimit(RLIMIT_CORE, &core_limit) != 0 ||
        core_limit.rlim_cur != 0 || select_utf8_locale() == NULL) {
        fputs("test_pty: FAIL: process preconditions\n", stderr);
        return 1;
    }
    const bool resize_sync = resize_preserves_split_csi();
    const bool wait_sync = wait_drains_current_frame();
    if (!resize_sync || !wait_sync) {
        fprintf(stderr, "test_pty: FAIL: PTY harness frame synchronization resize=%s wait=%s\n",
                resize_sync ? "yes" : "no", wait_sync ? "yes" : "no");
        return 1;
    }
    bool ok = scenario_keyboard_workflow() && scenario_contextual_creation_and_title_edit() &&
              scenario_mouse_help_modal() &&
              scenario_drag_normal() && scenario_reduced_narrow_and_signal() &&
              scenario_legacy("LOWTASK\t1\nNEXT\t2\nTASK\t1\t3\t0\t6c6567616379206f6e65\n",
                              "legacy one") &&
              scenario_legacy("LOWTASK\t2\nNEXT\t2\nTASK\t1\t1\t0\t2026-07-11\t6c65676163792074776f\n",
                              "legacy two") && scenario_lock_loser();
    if (!ok) return 1;
    int stray_status = 0;
    errno = 0;
    if (waitpid(-1, &stray_status, WNOHANG) != -1 || errno != ECHILD) {
        fputs("test_pty: FAIL: an untracked child remains\n", stderr);
        return 1;
    }
    const WorkflowEvidence *evidence = pty_test_evidence();
    if (!evidence->saw_drag || !evidence->saw_moved || !evidence->saw_urgent_256 ||
        evidence->startup_hash == evidence->lift_hash || evidence->lift_hash == evidence->target_hash ||
        evidence->target_hash == evidence->success_hash) {
        fputs("test_pty: FAIL: semantic frame evidence incomplete\n", stderr);
        return 1;
    }
    printf("test_pty: workflow startup=%08x lift=%08x target=%08x success=%08x drag=%s moved=%s urgent256=%s\n",
           evidence->startup_hash, evidence->lift_hash, evidence->target_hash, evidence->success_hash,
           evidence->saw_drag ? "yes" : "no", evidence->saw_moved ? "yes" : "no",
           evidence->saw_urgent_256 ? "yes" : "no");
    printf("test_pty: scenarios=keyboard,context-add,title-edit,header-mouse,help,picker,modal,drag,animation,reduced,legacy-v1,legacy-v2,lock,signals bytes=%zu csi=%zu\n",
           evidence->transcript_bytes, evidence->csi_count);
    puts("test_pty: geometry=96x24,24x8 resize=help,picker,drag split-sgr=yes malformed-sgr=yes");
    puts("test_pty: persistence=exact-v4 dynamic-dates=yes lock-unchanged=yes sigterm-save=yes");
    puts("test_pty: termios=restored children=all-reaped temp=clean core-dumps=disabled");
    puts("test_pty: PASS");
    return 0;
}
