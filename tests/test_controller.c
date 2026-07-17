#include "controller_test_suites.h"

#include <stdio.h>

int main(void) {
    run_controller_navigation_tests();
    run_controller_action_tests();
    run_controller_context_tests();
    run_controller_modal_primary_tests();
    run_controller_modal_regression_tests();
    run_controller_drag_resolution_tests();
    run_controller_drag_interruption_tests();
    run_controller_drag_outcome_tests();
    puts("test_controller: PASS");
    return 0;
}
