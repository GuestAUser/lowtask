#include "state_test_lifecycle.h"
#include "state_test_projection.h"

#include <stdio.h>

int main(void) {
    run_state_test_projection_suite();
    run_state_test_lifecycle_suite();
    puts("test_state: PASS");
    return 0;
}
