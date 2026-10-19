#include <string.h>

#include "console_core.h"
#include "test_support.h"

/* ---- captured output ------------------------------------------------------- */

static char output[2048];
static size_t output_len;

static void capture(void *user, const char *data, size_t len)
{
    TEST_ASSERT_EQUAL_PTR(&output_len, user);
    TEST_ASSERT_TRUE(output_len + len < sizeof output);
    memcpy(&output[output_len], data, len);
    output_len += len;
    output[output_len] = '\0';
}

static void clear_output(void)
{
    output_len = 0U;
    output[0] = '\0';
}

/* ---- a small command set -------------------------------------------------- */

static int led_state;

static cli_status_t cmd_led(int argc, char *argv[], void *ctx)
{
    (void)argc;
    (void)ctx;
    if (cli_streq(argv[1], "on")) {
        led_state = 1;
        return CLI_OK;
    }
    if (cli_streq(argv[1], "off")) {
        led_state = 0;
        return CLI_OK;
    }
    return CLI_ERR_INVALID;
}

static cli_status_t cmd_mode(int argc, char *argv[], void *ctx)
{
    (void)argc;
    console_core_set_mode(ctx, cli_streq(argv[1], "host") ? CONSOLE_MODE_HOST
                                                          : CONSOLE_MODE_INTERACTIVE);
    return CLI_OK;
}

static cli_status_t cmd_help(int argc, char *argv[], void *ctx)
{
    return console_core_help(ctx, (argc > 1) ? argv[1] : NULL);
}

static cli_status_t cmd_show(int argc, char *argv[], void *ctx)
{
    (void)argc;
    (void)argv;
    console_core_field(ctx, "uptime_ms", "Uptime", "1234 ms");
    return CLI_OK;
}

static cli_status_t cmd_ping(int argc, char *argv[], void *ctx)
{
    (void)argc;
    (void)argv;
    console_core_event(ctx, "pong"); /* event raised while a command runs */
    return CLI_OK;
}

static const cli_command_t commands[] = {
    {"help", "[command]", "Show commands", 0, 1, cmd_help},
    {"led", "on|off", "Set the LED", 1, 1, cmd_led},
    {"mode", "interactive|host", "Select output mode", 1, 1, cmd_mode},
    {"show", "", "Show a field", 0, 0, cmd_show},
    {"ping", "", "Raise an event", 0, 0, cmd_ping},
    {"averyveryverylongname", "with-args", "Long entry", 0, 0, cmd_show},
};

static console_core_t con;

static void feed(const char *s)
{
    while (*s != '\0') {
        console_core_feed(&con, *s++);
    }
}

void setUp(void)
{
    console_core_init(&con, commands, sizeof commands / sizeof commands[0], capture, &output_len);
    led_state = -1;
    clear_output();
}

void tearDown(void)
{
}

/* ---- interactive mode ----------------------------------------------------- */

static void test_starts_interactive_and_echoes(void)
{
    TEST_ASSERT_EQUAL(CONSOLE_MODE_INTERACTIVE, console_core_mode(&con));
    feed("led");
    TEST_ASSERT_EQUAL_STRING("led", output);
}

static void test_interactive_command_runs_and_prompts(void)
{
    feed("led on\r");
    TEST_ASSERT_EQUAL_INT(1, led_state);
    TEST_ASSERT_EQUAL_STRING("led on\r\nobd> ", output);
}

static void test_crlf_runs_command_once(void)
{
    feed("led off\r\n");
    TEST_ASSERT_EQUAL_INT(0, led_state);
    TEST_ASSERT_EQUAL_STRING("led off\r\nobd> ", output);
}

static void test_backspace_erases_on_screen(void)
{
    feed("lex");
    clear_output();
    feed("\b");
    TEST_ASSERT_EQUAL_STRING("\b \b", output);
    clear_output();
    feed("\b\b\b"); /* two more erase, third has nothing to erase */
    TEST_ASSERT_EQUAL_STRING("\b \b\b \b", output);
}

static void test_ctrl_c_cancels_line(void)
{
    feed("led on\x03");
    TEST_ASSERT_EQUAL_STRING("led on^C\r\nobd> ", output);
    TEST_ASSERT_EQUAL_INT(-1, led_state);
}

/* "led on" padded with spaces to exactly the line limit: if the extra byte
 * were simply dropped, the line would still run `led on`. */
static void feed_overlong_led_on(void)
{
    feed("led on");
    for (size_t i = 6U; i < LINE_EDITOR_MAX; i++) {
        console_core_feed(&con, ' ');
    }
    clear_output();
    feed("x");
}

static void test_overflow_rings_bell_and_rejects_line(void)
{
    feed_overlong_led_on();
    TEST_ASSERT_EQUAL_STRING("\a", output);
    clear_output();
    feed("\r");
    TEST_ASSERT_EQUAL_STRING("\r\nerror: line too long\r\nobd> ", output);
    TEST_ASSERT_EQUAL_INT(-1, led_state); /* never run cut short */
}

static void test_host_mode_rejects_overlong_line(void)
{
    console_core_set_mode(&con, CONSOLE_MODE_HOST);
    feed_overlong_led_on();
    feed("\r");
    TEST_ASSERT_EQUAL_STRING("ERR line too long\r\n", output);
    TEST_ASSERT_EQUAL_INT(-1, led_state);
}

static void test_empty_line_shows_prompt(void)
{
    feed("\r");
    TEST_ASSERT_EQUAL_STRING("\r\nobd> ", output);
}

static void test_interactive_unknown_command_suggests_help(void)
{
    feed("reboot\r");
    TEST_ASSERT_EQUAL_STRING("reboot\r\nerror: unknown command (type 'help' for a list)\r\nobd> ",
                             output);
}

static void test_interactive_wrong_args_shows_usage(void)
{
    feed("led\r");
    TEST_ASSERT_EQUAL_STRING("led\r\nerror: wrong number of arguments (usage: led on|off)\r\nobd> ",
                             output);
}

static void test_interactive_invalid_value(void)
{
    feed("led blue\r");
    TEST_ASSERT_EQUAL_STRING("led blue\r\nerror: invalid argument\r\nobd> ", output);
}

/* ---- host mode ------------------------------------------------------------ */

static void test_switch_to_host_mode_replies_ok(void)
{
    feed("mode host\r");
    TEST_ASSERT_EQUAL(CONSOLE_MODE_HOST, console_core_mode(&con));
    /* Echoed while still interactive; the reply uses the new mode. */
    TEST_ASSERT_EQUAL_STRING("mode host\r\nOK\r\n", output);
}

static void test_host_mode_has_no_echo_or_prompt(void)
{
    console_core_set_mode(&con, CONSOLE_MODE_HOST);
    feed("led on\r");
    TEST_ASSERT_EQUAL_STRING("OK\r\n", output);
    TEST_ASSERT_EQUAL_INT(1, led_state);
}

static void test_host_mode_editing_keys_are_silent(void)
{
    console_core_set_mode(&con, CONSOLE_MODE_HOST);
    feed("ledx\b\x03");
    for (size_t i = 0; i <= LINE_EDITOR_MAX; i++) {
        console_core_feed(&con, 'x'); /* includes one overflow */
    }
    console_core_prompt(&con);
    TEST_ASSERT_EQUAL_STRING("", output);
}

static void test_host_mode_empty_line_is_ignored(void)
{
    console_core_set_mode(&con, CONSOLE_MODE_HOST);
    feed("\r\n\r");
    TEST_ASSERT_EQUAL_STRING("", output);
}

static void test_host_mode_errors(void)
{
    console_core_set_mode(&con, CONSOLE_MODE_HOST);
    feed("reboot\r");
    TEST_ASSERT_EQUAL_STRING("ERR unknown command\r\n", output);
    clear_output();
    feed("led\r");
    TEST_ASSERT_EQUAL_STRING("ERR wrong number of arguments (usage: led on|off)\r\n", output);
    clear_output();
    feed("led blue\r");
    TEST_ASSERT_EQUAL_STRING("ERR invalid argument\r\n", output);
}

static void test_switch_back_to_interactive_shows_prompt(void)
{
    console_core_set_mode(&con, CONSOLE_MODE_HOST);
    feed("mode interactive\r");
    TEST_ASSERT_EQUAL(CONSOLE_MODE_INTERACTIVE, console_core_mode(&con));
    TEST_ASSERT_EQUAL_STRING("obd> ", output);
}

/* ---- fields, text and events ---------------------------------------------- */

static void test_field_formats(void)
{
    console_core_field(&con, "uptime_ms", "Uptime", "1234 ms");
    TEST_ASSERT_EQUAL_STRING("  Uptime            1234 ms\r\n", output);

    clear_output();
    console_core_field(&con, "k", "A label longer than width", "v");
    TEST_ASSERT_EQUAL_STRING("  A label longer than width v\r\n", output);

    clear_output();
    console_core_set_mode(&con, CONSOLE_MODE_HOST);
    feed("show\r");
    TEST_ASSERT_EQUAL_STRING("uptime_ms=1234 ms\r\nOK\r\n", output);
}

static void test_text_is_written_verbatim(void)
{
    console_core_text(&con, "hello\r\n");
    TEST_ASSERT_EQUAL_STRING("hello\r\n", output);
}

static void test_event_in_host_mode(void)
{
    console_core_set_mode(&con, CONSOLE_MODE_HOST);
    console_core_event(&con, "button");
    TEST_ASSERT_EQUAL_STRING("!button\r\n", output);
}

static void test_event_restores_partial_line(void)
{
    feed("le");
    clear_output();
    console_core_event(&con, "button");
    TEST_ASSERT_EQUAL_STRING("\r\nbutton\r\nobd> le", output);
}

static void test_event_after_completed_line_shows_bare_prompt(void)
{
    feed("led on\r");
    clear_output();
    console_core_event(&con, "button");
    TEST_ASSERT_EQUAL_STRING("\r\nbutton\r\nobd> ", output);
}

static void test_event_during_command_is_inline(void)
{
    feed("ping\r");
    TEST_ASSERT_EQUAL_STRING("ping\r\npong\r\nobd> ", output);
}

/* ---- help ----------------------------------------------------------------- */

static void test_help_lists_commands(void)
{
    feed("help\r");
    TEST_ASSERT_NOT_NULL(strstr(output, "  led on|off              Set the LED\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(output, "  show                    Show a field\r\n"));
    /* Name and usage too wide for the column: the text goes below, aligned. */
    TEST_ASSERT_NOT_NULL(strstr(output, "  averyveryverylongname with-args\r\n"
                                        "                          Long entry\r\n"));
}

static void test_help_for_one_command(void)
{
    feed("help LED\r");
    TEST_ASSERT_EQUAL_STRING("help LED\r\nusage: led on|off\r\n  Set the LED\r\nobd> ", output);
}

static void test_help_for_unknown_command(void)
{
    console_core_set_mode(&con, CONSOLE_MODE_HOST);
    feed("help nope\r");
    TEST_ASSERT_EQUAL_STRING("ERR invalid argument\r\n", output);
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_starts_interactive_and_echoes);
    UNIT_TEST(test_interactive_command_runs_and_prompts);
    UNIT_TEST(test_crlf_runs_command_once);
    UNIT_TEST(test_backspace_erases_on_screen);
    UNIT_TEST(test_ctrl_c_cancels_line);
    UNIT_TEST(test_overflow_rings_bell_and_rejects_line);
    UNIT_TEST(test_host_mode_rejects_overlong_line);
    UNIT_TEST(test_empty_line_shows_prompt);
    UNIT_TEST(test_interactive_unknown_command_suggests_help);
    UNIT_TEST(test_interactive_wrong_args_shows_usage);
    UNIT_TEST(test_interactive_invalid_value);
    UNIT_TEST(test_switch_to_host_mode_replies_ok);
    UNIT_TEST(test_host_mode_has_no_echo_or_prompt);
    UNIT_TEST(test_host_mode_editing_keys_are_silent);
    UNIT_TEST(test_host_mode_empty_line_is_ignored);
    UNIT_TEST(test_host_mode_errors);
    UNIT_TEST(test_switch_back_to_interactive_shows_prompt);
    UNIT_TEST(test_field_formats);
    UNIT_TEST(test_text_is_written_verbatim);
    UNIT_TEST(test_event_in_host_mode);
    UNIT_TEST(test_event_restores_partial_line);
    UNIT_TEST(test_event_after_completed_line_shows_bare_prompt);
    UNIT_TEST(test_event_during_command_is_inline);
    UNIT_TEST(test_help_lists_commands);
    UNIT_TEST(test_help_for_one_command);
    UNIT_TEST(test_help_for_unknown_command);
    return UNITY_END();
}
