#include "tests/pty_test_api.h"

#include <errno.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/wait.h>

int main(void) {
    struct rlimit core_limit;
    if (!install_interruption_handlers() || getrlimit(RLIMIT_CORE, &core_limit) != 0 ||
        core_limit.rlim_cur != 0 || select_utf8_locale() == NULL) {
        fputs("test_pty: FAIL: process preconditions\n", stderr);
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
    puts("test_pty: persistence=exact-v3 dynamic-dates=yes lock-unchanged=yes sigterm-save=yes");
    puts("test_pty: termios=restored children=all-reaped temp=clean core-dumps=disabled");
    puts("test_pty: PASS");
    return 0;
}
