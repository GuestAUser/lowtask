#include "controller_test_suites.h"

void run_controller_drag_behavior_tests(void) {
    run_controller_drag_resolution_tests();
    run_controller_drag_interruption_tests();
    run_controller_drag_outcome_tests();
}
