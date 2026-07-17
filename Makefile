CC ?= cc
OBJCOPY ?= objcopy
PREFIX ?= /usr/local
unexport PREFIX DESTDIR
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -I.
CFLAGS ?= -O2 -std=c17 -Wall -Wextra -Werror -Wpedantic
LDFLAGS ?=
LDLIBS ?=
TEST_TIMEOUT ?= 5s
PTY_TIMEOUT ?= 10s
PTY_KILL_AFTER ?= 2s
ifeq ($(origin HOST_OS),undefined)
HOST_OS := $(shell uname -s)
endif
unexport HOST_OS
HOST_OS_LITERAL := $(value HOST_OS)
PLATFORM_CPPFLAGS :=

ifeq ($(HOST_OS_LITERAL),Darwin)
PLATFORM_CPPFLAGS := -D_DARWIN_C_SOURCE
endif

TIMEOUT_SUPERVISOR := build/timeout-supervisor
TIMEOUT_RUNNER := ./$(TIMEOUT_SUPERVISOR)

APP_SOURCES := \
	main.c \
	app/runtime.c \
	core/date.c \
	core/text.c \
	core/text_input.c \
	core/task.c \
	core/persistence.c \
	core/persistence_format.c \
	core/state.c \
	core/view_order.c \
	core/view_sort.c \
	input/input.c \
	input/mouse_decoder.c \
	input/controller.c \
	input/controller_modal.c \
	input/controller_text.c \
	input/controller_help.c \
	input/controller_navigation.c \
	input/controller_drag.c \
	platform/terminal.c \
	tui/animation.c \
	tui/color.c \
	tui/layout.c \
	tui/hit_test.c \
	tui/render.c \
	tui/task_geometry.c \
	tui/text_wrap.c \
	tui/view_common.c \
	tui/view_chrome.c \
	tui/view_details.c \
	tui/view_rows.c \
	tui/view_help.c \
	tui/view_editor.c \
	tui/view_overlay.c \
	tui/view.c
APP_OBJECTS := $(APP_SOURCES:%.c=build/%.o)
APP_DEPS := $(APP_OBJECTS:.o=.d)
APP_ICON := assets/lowtask-mark.svg
APP_LINK_OBJECTS := $(APP_OBJECTS)
APP_ICON_LDFLAGS :=

ifeq ($(HOST_OS_LITERAL),Darwin)
APP_ICON_LDFLAGS := -Wl,-sectcreate,__TEXT,__lowtask_icon,$(APP_ICON)
endif

TEST_CORE_SOURCES := tests/test_core.c core/date.c core/text.c core/task.c
TEST_PERSISTENCE_SOURCES := \
	tests/test_persistence.c \
	tests/persistence_test_support.c \
	tests/persistence_test_format.c \
	tests/persistence_test_compatibility.c \
	tests/persistence_test_malformed.c \
	tests/persistence_test_durability.c \
	tests/persistence_test_preflight.c \
	tests/persistence_test_locking.c \
	core/date.c core/text.c core/task.c core/persistence.c core/persistence_format.c
TEST_UI_SOURCES := tests/test_ui.c core/text.c input/input.c input/mouse_decoder.c tui/animation.c tui/color.c \
	tui/render.c
TEST_PLATFORM_SOURCES := tests/test_platform.c platform/terminal.c
TEST_MOUSE_SOURCES := tests/test_mouse.c input/input.c input/mouse_decoder.c platform/terminal.c
TEST_VIEW_SOURCES := tests/test_view.c core/date.c core/text.c core/text_input.c core/task.c \
	core/state.c core/view_order.c \
	core/view_sort.c tui/animation.c tui/color.c tui/layout.c tui/hit_test.c tui/render.c \
	tui/task_geometry.c tui/text_wrap.c \
	tui/view_common.c tui/view_chrome.c tui/view_details.c tui/view_rows.c tui/view_help.c tui/view_editor.c \
	tui/view_overlay.c tui/view.c
TEST_CONTROLLER_SOURCES := \
	tests/test_controller.c \
	tests/controller_test_support.c \
	tests/controller_test_actions.c \
	tests/controller_test_context.c \
	tests/controller_test_navigation.c \
	tests/controller_test_modal_workflows.c \
	tests/controller_test_modal_regressions.c \
	tests/controller_test_drag_interruptions.c \
	tests/controller_test_drag_resolution.c \
	tests/controller_test_drag_outcomes.c \
	core/date.c core/text.c core/text_input.c core/task.c core/state.c \
	core/view_order.c core/view_sort.c \
	input/controller.c input/controller_modal.c input/controller_text.c input/controller_help.c \
	input/controller_navigation.c input/controller_drag.c
TEST_STATE_SOURCES := \
	tests/test_state.c \
	tests/state_test_support.c \
	tests/state_test_projection.c \
	tests/state_test_lifecycle.c \
	core/date.c core/text.c core/task.c core/state.c core/view_order.c core/view_sort.c
TEST_PTY_SOURCES := \
	tests/test_pty.c \
	tests/pty_test_runtime.c \
	tests/pty_test_transcript.c \
	tests/pty_test_screen_parser.c \
	tests/pty_test_screen_queries.c \
	tests/pty_test_model.c \
	tests/pty_test_session_io.c \
	tests/pty_test_interactions.c \
	tests/pty_test_session.c \
	tests/pty_test_child.c \
	tests/pty_test_scenario_keyboard.c \
	tests/pty_test_scenario_context.c \
	tests/pty_test_scenario_mouse_help.c \
	tests/pty_test_scenario_drag.c \
	tests/pty_test_scenario_reduced_signal.c \
	tests/pty_test_scenario_legacy_lock.c \
	core/date.c

TEST_PERFORMANCE_SOURCES := tests/test_performance.c core/date.c core/text.c core/text_input.c core/task.c \
	core/state.c core/view_order.c \
	core/view_sort.c input/controller.c input/controller_modal.c input/controller_text.c input/controller_help.c input/controller_navigation.c input/controller_drag.c tui/animation.c tui/color.c tui/layout.c \
	tui/render.c tui/task_geometry.c tui/text_wrap.c tui/view_common.c tui/view_chrome.c tui/view_details.c tui/view_rows.c \
	tui/view_help.c tui/view_editor.c tui/view_overlay.c tui/view.c

TEST_CORE_OBJECTS := $(TEST_CORE_SOURCES:%.c=build/test-objects/%.o)
TEST_PERSISTENCE_OBJECTS := $(TEST_PERSISTENCE_SOURCES:%.c=build/test-objects/%.o)
TEST_UI_OBJECTS := $(TEST_UI_SOURCES:%.c=build/test-objects/%.o)
TEST_PLATFORM_OBJECTS := $(TEST_PLATFORM_SOURCES:%.c=build/test-objects/%.o)
TEST_MOUSE_OBJECTS := $(TEST_MOUSE_SOURCES:%.c=build/test-objects/%.o)
TEST_VIEW_OBJECTS := $(TEST_VIEW_SOURCES:%.c=build/test-objects/%.o)
TEST_CONTROLLER_OBJECTS := $(TEST_CONTROLLER_SOURCES:%.c=build/test-objects/%.o)
TEST_STATE_OBJECTS := $(TEST_STATE_SOURCES:%.c=build/test-objects/%.o)
TEST_PTY_OBJECTS := $(TEST_PTY_SOURCES:%.c=build/test-objects/%.o)
TEST_PERFORMANCE_OBJECTS := $(TEST_PERFORMANCE_SOURCES:%.c=build/performance-objects/%.o)

COMPONENT_TEST_BINARIES := \
	build/tests/test_core \
	build/tests/test_persistence \
	build/tests/test_ui \
	build/tests/test_platform \
	build/tests/test_mouse \
	build/tests/test_view \
	build/tests/test_controller \
	build/tests/test_state
PTY_TEST_BINARY := build/tests/test_pty
PERFORMANCE_TEST_BINARY := build/tests/test_performance
TEST_SOURCES := $(sort $(TEST_CORE_SOURCES) $(TEST_PERSISTENCE_SOURCES) $(TEST_UI_SOURCES) \
	$(TEST_PLATFORM_SOURCES) $(TEST_MOUSE_SOURCES) $(TEST_VIEW_SOURCES) \
	$(TEST_CONTROLLER_SOURCES) $(TEST_STATE_SOURCES) $(TEST_PTY_SOURCES))
TEST_OBJECTS := $(TEST_SOURCES:%.c=build/test-objects/%.o)
TEST_DEPS := $(TEST_OBJECTS:.o=.d)
TEST_PERFORMANCE_DEPS := $(TEST_PERFORMANCE_OBJECTS:.o=.d)
TEST_CFLAGS = $(CFLAGS) -UNDEBUG
SANITIZE_CFLAGS := -O1 -g3 -std=c17 -Wall -Wextra -Werror -Wpedantic -UNDEBUG \
	-fno-omit-frame-pointer -fno-sanitize-recover=all -fsanitize=address,undefined
SANITIZE_LDFLAGS := -fno-omit-frame-pointer -fno-sanitize-recover=all \
	-fsanitize=address,undefined

.PHONY: all clean install uninstall test perf-record perf-gate perf-record-run \
	perf-gate-run sanitize

all: lowtask

lowtask: $(APP_LINK_OBJECTS) $(APP_ICON)
	$(CC) $(LDFLAGS) $(APP_LINK_OBJECTS) $(APP_ICON_LDFLAGS) $(LDLIBS) -o $@

ifeq ($(HOST_OS_LITERAL),Linux)
	$(OBJCOPY) --add-section .lowtask.icon=$(APP_ICON) \
		--set-section-flags .lowtask.icon=contents,readonly $@
endif

install uninstall: export LOWTASK_PROJECT_DIR := $(CURDIR)
install uninstall: export LOWTASK_PREFIX := $(value PREFIX)
install uninstall: export LOWTASK_DESTDIR := $(value DESTDIR)
install uninstall: export LOWTASK_HOST_OS := $(HOST_OS_LITERAL)

install: lowtask
	sh scripts/install-files.sh install

uninstall:
	sh scripts/install-files.sh uninstall

$(TIMEOUT_SUPERVISOR): scripts/timeout-supervisor.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(PLATFORM_CPPFLAGS) $(CFLAGS) $< -o $@

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(PLATFORM_CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

build/test-objects/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(PLATFORM_CPPFLAGS) $(TEST_CFLAGS) -MMD -MP -c $< -o $@

build/performance-objects/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(PLATFORM_CPPFLAGS) $(TEST_CFLAGS) -MMD -MP -c $< -o $@

test: $(TIMEOUT_SUPERVISOR) $(COMPONENT_TEST_BINARIES) $(PTY_TEST_BINARY)
	@for test_binary in $(COMPONENT_TEST_BINARIES); do \
		ulimit -c 0; $(TIMEOUT_RUNNER) $(TEST_TIMEOUT) KILL 0s ./$$test_binary || exit $$?; \
	done
	@ulimit -c 0; $(TIMEOUT_RUNNER) $(PTY_TIMEOUT) TERM $(PTY_KILL_AFTER) ./$(PTY_TEST_BINARY)

build/tests/test_core: $(TEST_CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/tests/test_persistence: $(TEST_PERSISTENCE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/tests/test_ui: $(TEST_UI_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/tests/test_platform: $(TEST_PLATFORM_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/tests/test_mouse: $(TEST_MOUSE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/tests/test_view: $(TEST_VIEW_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/tests/test_controller: $(TEST_CONTROLLER_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/tests/test_state: $(TEST_STATE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/tests/test_pty: $(TEST_PTY_OBJECTS) lowtask
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $(TEST_PTY_OBJECTS) $(LDLIBS) -o $@

build/tests/test_performance: $(TEST_PERFORMANCE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

perf-record: $(TIMEOUT_SUPERVISOR)
	+@ulimit -c 0; $(TIMEOUT_RUNNER) 30s TERM 2s \
		$(MAKE) --no-print-directory perf-record-run

perf-gate: $(TIMEOUT_SUPERVISOR)
	+@ulimit -c 0; $(TIMEOUT_RUNNER) 30s TERM 2s \
		$(MAKE) --no-print-directory perf-gate-run

perf-record-run: $(PERFORMANCE_TEST_BINARY)
	@env LOWTASK_BENCH_MODE=record LOWTASK_BENCH_REQUIRE_BUDGET=0 ./$(PERFORMANCE_TEST_BINARY)

perf-gate-run: $(PERFORMANCE_TEST_BINARY)
	@env LOWTASK_BENCH_MODE=gate ./$(PERFORMANCE_TEST_BINARY)

sanitize:
	+$(MAKE) --no-print-directory clean
	+ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
	$(MAKE) --no-print-directory CC=gcc CFLAGS='$(SANITIZE_CFLAGS)' \
		LDFLAGS='$(SANITIZE_LDFLAGS)' TEST_TIMEOUT=20s PTY_TIMEOUT=30s all test

clean:
	rm -rf build lowtask

-include $(APP_DEPS) $(TEST_DEPS) $(TEST_PERFORMANCE_DEPS)
