#include "controller_test_suites.h"

#include <stdio.h>

int main(void) {
    run_controller_navigation_actions_tests();
    run_controller_context_tests();
    run_controller_modal_workflow_tests();
    run_controller_drag_behavior_tests();
    puts("test_controller: PASS");
    return 0;
}
