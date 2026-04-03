/*
 * Unit tests for utils/src/cli.c
 *
 * Compilation strategy
 * --------------------
 * cli.c has two embedded-only dependencies:
 *   - printf.h  (mpaland)   – stubs/printf.h redirects to libc stdio
 *   - printf_dma.h          – stubs/printf_dma.h provides no-op stubs
 *
 * Both stub headers live in tests/cli/stubs/ which is placed first on the
 * include search path in the Makefile, so cli.c picks up the stubs without
 * any source modification.  strlen/strncmp/memcpy come from the host libc
 * (cli.c's string_utils.h declarations are compatible with those signatures).
 */

#include <string.h>
#include <stddef.h>
#include "unity.h"
#include "cli.h"

/* ===================================================================
 * Test fixtures
 * =================================================================== */

#define BUF_SIZE 128
static char              input_buf[BUF_SIZE];
static cli_context_t     ctx;

/* Tracks how many times the test command handler was called and with
 * what argument string. */
static int  handler_call_count;
static char handler_last_args[BUF_SIZE];

static int mock_cmd_handler(const char *args) {
    handler_call_count++;
    strncpy(handler_last_args, args ? args : "", BUF_SIZE - 1);
    handler_last_args[BUF_SIZE - 1] = '\0';
    return 0;
}

static int mock_cmd2_handler(const char *args) {
    (void)args;
    handler_call_count++;
    return 0;
}

/* Echo capture – records every character passed to the echo callback. */
static char   echoed[BUF_SIZE];
static size_t echoed_len;

static void mock_echo(char c) {
    if (echoed_len < BUF_SIZE - 1) {
        echoed[echoed_len++] = c;
    }
}

/* Helper: type a string into the CLI one character at a time. */
static void type_string(const char *s) {
    while (*s) {
        cli_process_char(&ctx, *s++, NULL);
    }
}

/* Helper: set up the buffer manually without going through process_char,
 * so we can test history/execute without triggering echoing side-effects. */
static void set_buffer(const char *cmd) {
    size_t len = strlen(cmd);
    if (len >= BUF_SIZE) len = BUF_SIZE - 1;
    memcpy(input_buf, cmd, len);
    ctx.buffer_pos = len;
}

/* Default single-command registration used by most tests. */
static const cli_command_t single_cmd[] = {
    { "testcmd", "A test command", mock_cmd_handler }
};

void setUp(void) {
    memset(input_buf, 0, sizeof(input_buf));
    memset(&ctx, 0, sizeof(ctx));
    memset(echoed, 0, sizeof(echoed));
    echoed_len          = 0;
    handler_call_count  = 0;
    handler_last_args[0] = '\0';

    cli_init(&ctx, single_cmd, 1, input_buf, BUF_SIZE);
}

void tearDown(void) {}

/* ===================================================================
 * cli_init tests
 * =================================================================== */

void test_cli_init_registers_user_commands(void) {
    /* User command "testcmd" must appear in the list */
    TEST_ASSERT_EQUAL_STRING("testcmd", ctx.command_list[0].name);
}

void test_cli_init_adds_builtin_help(void) {
    /* Built-in help is appended after user commands */
    TEST_ASSERT_EQUAL_STRING("help", ctx.command_list[1].name);
}

void test_cli_init_total_command_count(void) {
    /* 1 user command + 1 built-in help */
    TEST_ASSERT_EQUAL_size_t(2, ctx.num_commands);
}

void test_cli_init_buffer_and_size(void) {
    TEST_ASSERT_EQUAL_PTR(input_buf, ctx.buffer);
    TEST_ASSERT_EQUAL_size_t(BUF_SIZE, ctx.buffer_size);
    TEST_ASSERT_EQUAL_size_t(0, ctx.buffer_pos);
}

void test_cli_init_history_empty(void) {
    TEST_ASSERT_EQUAL_size_t(0, ctx.history_count);
    TEST_ASSERT_EQUAL_INT(-1, ctx.history_browse);
}

/* ===================================================================
 * cli_process_char – printable characters
 * =================================================================== */

void test_process_char_adds_printable_to_buffer(void) {
    cli_process_char(&ctx, 'a', NULL);
    TEST_ASSERT_EQUAL_CHAR('a', ctx.buffer[0]);
    TEST_ASSERT_EQUAL_size_t(1, ctx.buffer_pos);
}

void test_process_char_echoes_printable(void) {
    cli_process_char(&ctx, 'z', mock_echo);
    TEST_ASSERT_EQUAL_size_t(1, echoed_len);
    TEST_ASSERT_EQUAL_CHAR('z', echoed[0]);
}

void test_process_char_accumulates_multiple_chars(void) {
    type_string("led");
    TEST_ASSERT_EQUAL_size_t(3, ctx.buffer_pos);
    TEST_ASSERT_EQUAL_CHAR('l', ctx.buffer[0]);
    TEST_ASSERT_EQUAL_CHAR('e', ctx.buffer[1]);
    TEST_ASSERT_EQUAL_CHAR('d', ctx.buffer[2]);
}

void test_process_char_ignores_below_space(void) {
    /* Characters below 0x20 (except handled ones) should not enter buffer */
    cli_process_char(&ctx, 0x01, NULL); /* SOH */
    cli_process_char(&ctx, 0x07, NULL); /* BEL */
    TEST_ASSERT_EQUAL_size_t(0, ctx.buffer_pos);
}

void test_process_char_ignores_del_127_as_backspace_on_empty(void) {
    /* DEL on empty buffer – nothing happens */
    cli_process_char(&ctx, 127, NULL);
    TEST_ASSERT_EQUAL_size_t(0, ctx.buffer_pos);
}

/* ===================================================================
 * cli_process_char – backspace
 * =================================================================== */

void test_process_char_backspace_removes_last_char(void) {
    type_string("ab");
    cli_process_char(&ctx, '\b', NULL);
    TEST_ASSERT_EQUAL_size_t(1, ctx.buffer_pos);
}

void test_process_char_backspace_echoes_erase_sequence(void) {
    /* Expected echo: '\b', ' ', '\b' */
    cli_process_char(&ctx, 'x', NULL);   /* put something in buffer first */
    cli_process_char(&ctx, '\b', mock_echo);
    TEST_ASSERT_EQUAL_size_t(3, echoed_len);
    TEST_ASSERT_EQUAL_CHAR('\b', echoed[0]);
    TEST_ASSERT_EQUAL_CHAR(' ',  echoed[1]);
    TEST_ASSERT_EQUAL_CHAR('\b', echoed[2]);
}

void test_process_char_backspace_on_empty_no_change(void) {
    cli_process_char(&ctx, '\b', NULL);
    TEST_ASSERT_EQUAL_size_t(0, ctx.buffer_pos);
}

/* ===================================================================
 * cli_process_char – buffer boundary
 * =================================================================== */

void test_process_char_stops_at_buffer_limit(void) {
    /* Fill buffer to exactly buffer_size-1 chars, then one more */
    for (size_t i = 0; i < BUF_SIZE - 1; i++) {
        cli_process_char(&ctx, 'a', NULL);
    }
    cli_process_char(&ctx, 'b', NULL); /* should be rejected */
    TEST_ASSERT_EQUAL_size_t(BUF_SIZE - 1, ctx.buffer_pos);
}

/* ===================================================================
 * cli_process_char – newline
 * =================================================================== */

void test_process_char_newline_echoes_lf(void) {
    cli_process_char(&ctx, '\n', mock_echo);
    TEST_ASSERT_EQUAL_size_t(1, echoed_len);
    TEST_ASSERT_EQUAL_CHAR('\n', echoed[0]);
}

void test_process_char_newline_does_not_change_buffer(void) {
    type_string("ab");
    cli_process_char(&ctx, '\n', NULL);
    /* buffer_pos must still be 2 – app calls cli_execute then cli_history_save */
    TEST_ASSERT_EQUAL_size_t(2, ctx.buffer_pos);
}

/* ===================================================================
 * cli_execute_command
 * =================================================================== */

void test_execute_exact_match_calls_handler(void) {
    set_buffer("testcmd");
    cli_execute_command(&ctx);
    TEST_ASSERT_EQUAL_INT(1, handler_call_count);
}

void test_execute_passes_empty_args_for_bare_command(void) {
    set_buffer("testcmd");
    cli_execute_command(&ctx);
    TEST_ASSERT_EQUAL_STRING("", handler_last_args);
}

void test_execute_passes_args_after_space(void) {
    set_buffer("testcmd hello 42");
    cli_execute_command(&ctx);
    TEST_ASSERT_EQUAL_INT(1, handler_call_count);
    TEST_ASSERT_EQUAL_STRING("hello 42", handler_last_args);
}

void test_execute_strips_leading_spaces_from_args(void) {
    set_buffer("testcmd   value");
    cli_execute_command(&ctx);
    TEST_ASSERT_EQUAL_STRING("value", handler_last_args);
}

void test_execute_empty_buffer_calls_no_handler(void) {
    ctx.buffer_pos = 0;
    cli_execute_command(&ctx);
    TEST_ASSERT_EQUAL_INT(0, handler_call_count);
}

void test_execute_unknown_command_calls_no_handler(void) {
    set_buffer("notacommand");
    cli_execute_command(&ctx);
    TEST_ASSERT_EQUAL_INT(0, handler_call_count);
}

void test_execute_prefix_only_does_not_match(void) {
    /* "testcm" is a prefix of "testcmd" but not an exact command */
    set_buffer("testcm");
    cli_execute_command(&ctx);
    TEST_ASSERT_EQUAL_INT(0, handler_call_count);
}

/* ===================================================================
 * cli_history_save
 * =================================================================== */

void test_history_save_stores_command(void) {
    set_buffer("testcmd");
    cli_history_save(&ctx);
    TEST_ASSERT_EQUAL_size_t(1, ctx.history_count);
}

void test_history_save_empty_buffer_not_saved(void) {
    ctx.buffer_pos = 0;
    cli_history_save(&ctx);
    TEST_ASSERT_EQUAL_size_t(0, ctx.history_count);
}

void test_history_save_skips_consecutive_duplicate(void) {
    set_buffer("testcmd");
    cli_history_save(&ctx);
    cli_history_save(&ctx);  /* same command again */
    TEST_ASSERT_EQUAL_size_t(1, ctx.history_count);
}

void test_history_save_stores_different_commands(void) {
    set_buffer("testcmd");
    cli_history_save(&ctx);
    set_buffer("help");
    cli_history_save(&ctx);
    TEST_ASSERT_EQUAL_size_t(2, ctx.history_count);
}

void test_history_save_wraps_ring_buffer(void) {
    /* Save CLI_HISTORY_SIZE + 2 distinct commands – count must not exceed cap */
    char cmd[16];
    for (int i = 0; i < CLI_HISTORY_SIZE + 2; i++) {
        /* Use snprintf from libc for convenience in the test itself */
        snprintf(cmd, sizeof(cmd), "cmd%d", i);
        set_buffer(cmd);
        cli_history_save(&ctx);
    }
    TEST_ASSERT_EQUAL_size_t(CLI_HISTORY_SIZE, ctx.history_count);
}

void test_history_save_resets_browse_state(void) {
    /* Force browse state, then save – should reset to -1 */
    ctx.history_browse = 2;
    set_buffer("testcmd");
    cli_history_save(&ctx);
    TEST_ASSERT_EQUAL_INT(-1, ctx.history_browse);
}

/* ===================================================================
 * Tab completion
 * =================================================================== */

/* Register two commands with a shared prefix to exercise common-prefix logic. */
static const cli_command_t tab_cmds[] = {
    { "led_on",  "Turn LED on",  mock_cmd_handler },
    { "led_off", "Turn LED off", mock_cmd2_handler }
};

static void setup_tab_ctx(void) {
    memset(input_buf, 0, sizeof(input_buf));
    memset(&ctx, 0, sizeof(ctx));
    echoed_len = 0;
    cli_init(&ctx, tab_cmds, 2, input_buf, BUF_SIZE);
}

void test_tab_unique_prefix_completes_to_command_with_space(void) {
    /* Uses the default setUp context which has "testcmd" as the only user
     * command (plus built-in "help").  Typing a partial prefix and pressing
     * TAB must complete to the full command name AND append a trailing space
     * because there is exactly one match. */
    type_string("test");
    cli_process_char(&ctx, '\t', mock_echo);
    /* Buffer must hold "testcmd " — 7 chars + 1 space = pos 8 */
    TEST_ASSERT_EQUAL_size_t(8, ctx.buffer_pos);
    TEST_ASSERT_EQUAL_CHAR(' ', ctx.buffer[7]);
}

void test_tab_common_prefix_extends_to_shared_prefix(void) {
    setup_tab_ctx();
    /* Typing "led" and TAB: both led_on and led_off share "led_o" */
    type_string("led");
    cli_process_char(&ctx, '\t', mock_echo);
    /* "led_o" is the longest common prefix – buffer should be at least 5 chars */
    TEST_ASSERT_GREATER_OR_EQUAL(5, ctx.buffer_pos);
    TEST_ASSERT_EQUAL_CHAR('l', ctx.buffer[0]);
    TEST_ASSERT_EQUAL_CHAR('e', ctx.buffer[1]);
    TEST_ASSERT_EQUAL_CHAR('d', ctx.buffer[2]);
    TEST_ASSERT_EQUAL_CHAR('_', ctx.buffer[3]);
    TEST_ASSERT_EQUAL_CHAR('o', ctx.buffer[4]);
}

void test_tab_no_match_leaves_buffer_unchanged(void) {
    setup_tab_ctx();
    type_string("xyz");
    cli_process_char(&ctx, '\t', NULL);
    TEST_ASSERT_EQUAL_size_t(3, ctx.buffer_pos);
}

void test_tab_empty_buffer_does_nothing(void) {
    setup_tab_ctx();
    /* buffer_pos == 0 → no completion possible */
    cli_process_char(&ctx, '\t', NULL);
    TEST_ASSERT_EQUAL_size_t(0, ctx.buffer_pos);
}

/* ===================================================================
 * History navigation (Up / Down arrow keys)
 * Arrow keys arrive as three-byte ANSI sequences: ESC '[' 'A'/'B'
 * =================================================================== */

static void send_up(void) {
    cli_process_char(&ctx, '\x1B', NULL);
    cli_process_char(&ctx, '[',    NULL);
    cli_process_char(&ctx, 'A',    mock_echo);
}

static void send_down(void) {
    cli_process_char(&ctx, '\x1B', NULL);
    cli_process_char(&ctx, '[',    NULL);
    cli_process_char(&ctx, 'B',    mock_echo);
}

void test_history_up_no_history_does_nothing(void) {
    /* history_count == 0 – Up should leave buffer empty */
    send_up();
    TEST_ASSERT_EQUAL_size_t(0, ctx.buffer_pos);
}

void test_history_up_recalls_most_recent_command(void) {
    set_buffer("testcmd");
    cli_history_save(&ctx);
    ctx.buffer_pos = 0;  /* simulate cleared buffer before Up */

    send_up();
    ctx.buffer[ctx.buffer_pos] = '\0';
    TEST_ASSERT_EQUAL_STRING("testcmd", ctx.buffer);
}

void test_history_up_twice_recalls_older_command(void) {
    set_buffer("first");
    cli_history_save(&ctx);
    set_buffer("second");
    cli_history_save(&ctx);
    ctx.buffer_pos = 0;

    send_up(); /* should show "second" */
    send_up(); /* should show "first"  */
    ctx.buffer[ctx.buffer_pos] = '\0';
    TEST_ASSERT_EQUAL_STRING("first", ctx.buffer);
}

void test_history_up_stops_at_oldest_entry(void) {
    set_buffer("only");
    cli_history_save(&ctx);
    ctx.buffer_pos = 0;

    send_up(); /* shows "only" */
    send_up(); /* already at oldest – should stay on "only" */
    ctx.buffer[ctx.buffer_pos] = '\0';
    TEST_ASSERT_EQUAL_STRING("only", ctx.buffer);
}

void test_history_down_restores_stash(void) {
    set_buffer("saved");
    cli_history_save(&ctx);

    /* Start typing a new command, then browse up and back down */
    set_buffer("partial");
    send_up();   /* recalls "saved", stashes "partial" */
    send_down(); /* should restore "partial"           */
    ctx.buffer[ctx.buffer_pos] = '\0';
    TEST_ASSERT_EQUAL_STRING("partial", ctx.buffer);
}

void test_history_down_when_not_browsing_does_nothing(void) {
    type_string("abc");
    send_down(); /* history_browse == -1, nothing should change */
    TEST_ASSERT_EQUAL_size_t(3, ctx.buffer_pos);
}

/* ===================================================================
 * ANSI escape sequence state machine
 * =================================================================== */

void test_esc_state_machine_ignores_incomplete_sequence(void) {
    /* ESC followed by a non-'[' char: state resets, no side effect */
    cli_process_char(&ctx, '\x1B', NULL);
    cli_process_char(&ctx, 'X',    NULL); /* not '[' */
    /* esc_state should be back to 0 */
    TEST_ASSERT_EQUAL_UINT8(0, ctx.esc_state);
    TEST_ASSERT_EQUAL_size_t(0, ctx.buffer_pos);
}

void test_esc_state_machine_ignores_other_csi_codes(void) {
    /* ESC '[' 'C' (right arrow) – not handled, should not crash or add chars */
    cli_process_char(&ctx, '\x1B', NULL);
    cli_process_char(&ctx, '[',    NULL);
    cli_process_char(&ctx, 'C',    NULL);
    TEST_ASSERT_EQUAL_size_t(0, ctx.buffer_pos);
    TEST_ASSERT_EQUAL_UINT8(0, ctx.esc_state);
}

/* ===================================================================
 * Test runner
 * =================================================================== */

int main(void) {
    UNITY_BEGIN();

    /* cli_init */
    RUN_TEST(test_cli_init_registers_user_commands);
    RUN_TEST(test_cli_init_adds_builtin_help);
    RUN_TEST(test_cli_init_total_command_count);
    RUN_TEST(test_cli_init_buffer_and_size);
    RUN_TEST(test_cli_init_history_empty);

    /* process_char – printable */
    RUN_TEST(test_process_char_adds_printable_to_buffer);
    RUN_TEST(test_process_char_echoes_printable);
    RUN_TEST(test_process_char_accumulates_multiple_chars);
    RUN_TEST(test_process_char_ignores_below_space);
    RUN_TEST(test_process_char_ignores_del_127_as_backspace_on_empty);

    /* process_char – backspace */
    RUN_TEST(test_process_char_backspace_removes_last_char);
    RUN_TEST(test_process_char_backspace_echoes_erase_sequence);
    RUN_TEST(test_process_char_backspace_on_empty_no_change);

    /* process_char – buffer boundary */
    RUN_TEST(test_process_char_stops_at_buffer_limit);

    /* process_char – newline */
    RUN_TEST(test_process_char_newline_echoes_lf);
    RUN_TEST(test_process_char_newline_does_not_change_buffer);

    /* execute_command */
    RUN_TEST(test_execute_exact_match_calls_handler);
    RUN_TEST(test_execute_passes_empty_args_for_bare_command);
    RUN_TEST(test_execute_passes_args_after_space);
    RUN_TEST(test_execute_strips_leading_spaces_from_args);
    RUN_TEST(test_execute_empty_buffer_calls_no_handler);
    RUN_TEST(test_execute_unknown_command_calls_no_handler);
    RUN_TEST(test_execute_prefix_only_does_not_match);

    /* history_save */
    RUN_TEST(test_history_save_stores_command);
    RUN_TEST(test_history_save_empty_buffer_not_saved);
    RUN_TEST(test_history_save_skips_consecutive_duplicate);
    RUN_TEST(test_history_save_stores_different_commands);
    RUN_TEST(test_history_save_wraps_ring_buffer);
    RUN_TEST(test_history_save_resets_browse_state);

    /* tab completion */
    RUN_TEST(test_tab_unique_prefix_completes_to_command_with_space);
    RUN_TEST(test_tab_common_prefix_extends_to_shared_prefix);
    RUN_TEST(test_tab_no_match_leaves_buffer_unchanged);
    RUN_TEST(test_tab_empty_buffer_does_nothing);

    /* history navigation */
    RUN_TEST(test_history_up_no_history_does_nothing);
    RUN_TEST(test_history_up_recalls_most_recent_command);
    RUN_TEST(test_history_up_twice_recalls_older_command);
    RUN_TEST(test_history_up_stops_at_oldest_entry);
    RUN_TEST(test_history_down_restores_stash);
    RUN_TEST(test_history_down_when_not_browsing_does_nothing);

    /* ANSI escape state machine */
    RUN_TEST(test_esc_state_machine_ignores_incomplete_sequence);
    RUN_TEST(test_esc_state_machine_ignores_other_csi_codes);

    return UNITY_END();
}
