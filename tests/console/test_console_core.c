#include <string.h>

#include "console/console_core.h"
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

static int light_state;

static cli_status_t cmd_light(int argc, char *argv[], void *ctx)
{
    (void)argc;
    (void)ctx;
    if (cli_streq(argv[1], "on")) {
        light_state = 1;
        return CLI_OK;
    }
    if (cli_streq(argv[1], "off")) {
        light_state = 0;
        return CLI_OK;
    }
    return CLI_ERR_INVALID;
}

static cli_status_t cmd_help(int argc, char *argv[], void *ctx)
{
    return console_core_help(ctx, (argc > 1) ? argv[1] : NULL);
}

static cli_status_t cmd_show(int argc, char *argv[], void *ctx)
{
    (void)argc;
    (void)argv;
    console_core_field(ctx, "uptime_ms", "1234");
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
    {"light", "on|off", "Set the light", 1, 1, cmd_light},
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
    light_state = -1;
    clear_output();
}

void tearDown(void)
{
}

/* ---- commands --------------------------------------------------------------- */

static void test_command_runs_with_no_echo(void)
{
    feed("light on");
    TEST_ASSERT_EQUAL_STRING("", output);
    feed("\r");
    TEST_ASSERT_EQUAL_INT(1, light_state);
    TEST_ASSERT_EQUAL_STRING("OK\r\n", output);
}

static void test_crlf_runs_command_once(void)
{
    feed("light off\r\n");
    TEST_ASSERT_EQUAL_INT(0, light_state);
    TEST_ASSERT_EQUAL_STRING("OK\r\n", output);
}

static void test_ctrl_c_cancels_line(void)
{
    feed("light on\x03\r");
    TEST_ASSERT_EQUAL_INT(-1, light_state);
    TEST_ASSERT_EQUAL_STRING("", output); /* what's left is an empty line */
}

static void test_empty_line_is_ignored(void)
{
    feed("\r\n\r");
    TEST_ASSERT_EQUAL_STRING("", output);
}

/* "light on" padded with spaces to exactly the line limit: if the extra byte
 * were simply dropped, the line would still run `light on`. */
static void feed_overlong_light_on(void)
{
    feed("light on");
    for (size_t i = 8U; i < LINE_READER_MAX; i++) {
        console_core_feed(&con, ' ');
    }
    feed("x");
}

static void test_overlong_line_is_rejected(void)
{
    feed_overlong_light_on();
    TEST_ASSERT_EQUAL_STRING("", output);
    feed("\r");
    TEST_ASSERT_EQUAL_STRING("ERR line too long\r\n", output);
    TEST_ASSERT_EQUAL_INT(-1, light_state); /* never run cut short */
}

static void test_feed_reports_when_a_line_has_ended(void)
{
    TEST_ASSERT_FALSE(console_core_feed(&con, 'l'));
    feed("ight on");
    TEST_ASSERT_TRUE(console_core_feed(&con, '\r')); /* command ran */
    TEST_ASSERT_EQUAL_INT(1, light_state);
    TEST_ASSERT_FALSE(console_core_feed(&con, '\n')); /* second half of CR LF */
    TEST_ASSERT_FALSE(console_core_feed(&con, '\x03'));
    feed_overlong_light_on();
    TEST_ASSERT_TRUE(console_core_feed(&con, '\r')); /* rejected line */
}

static void test_errors(void)
{
    feed("reboot\r");
    TEST_ASSERT_EQUAL_STRING("ERR unknown command\r\n", output);
    clear_output();
    feed("light\r");
    TEST_ASSERT_EQUAL_STRING("ERR wrong number of arguments (usage: light on|off)\r\n", output);
    clear_output();
    feed("light blue\r");
    TEST_ASSERT_EQUAL_STRING("ERR invalid argument\r\n", output);
}

/* ---- fields, text and events ---------------------------------------------- */

static void test_field_is_key_equals_value(void)
{
    feed("show\r");
    TEST_ASSERT_EQUAL_STRING("uptime_ms=1234\r\nOK\r\n", output);
}

static void test_text_is_written_verbatim(void)
{
    console_core_text(&con, "hello\r\n");
    TEST_ASSERT_EQUAL_STRING("hello\r\n", output);
}

static void test_event(void)
{
    console_core_event(&con, "button");
    TEST_ASSERT_EQUAL_STRING("!button\r\n", output);
}

static void test_event_during_command_comes_before_its_reply_ends(void)
{
    feed("ping\r");
    TEST_ASSERT_EQUAL_STRING("!pong\r\nOK\r\n", output);
}

/* ---- help ----------------------------------------------------------------- */

static void test_help_lists_commands(void)
{
    feed("help\r");
    TEST_ASSERT_NOT_NULL(strstr(output, "  light on|off            Set the light\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(output, "  show                    Show a field\r\n"));
    /* Name and usage too wide for the column: the text goes below, aligned. */
    TEST_ASSERT_NOT_NULL(strstr(output, "  averyveryverylongname with-args\r\n"
                                        "                          Long entry\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(output, "OK\r\n"));
}

static void test_help_for_one_command(void)
{
    feed("help LIGHT\r");
    TEST_ASSERT_EQUAL_STRING("usage: light on|off\r\n  Set the light\r\nOK\r\n", output);
}

static void test_help_for_unknown_command(void)
{
    feed("help nope\r");
    TEST_ASSERT_EQUAL_STRING("ERR invalid argument\r\n", output);
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_command_runs_with_no_echo);
    UNIT_TEST(test_crlf_runs_command_once);
    UNIT_TEST(test_ctrl_c_cancels_line);
    UNIT_TEST(test_empty_line_is_ignored);
    UNIT_TEST(test_overlong_line_is_rejected);
    UNIT_TEST(test_feed_reports_when_a_line_has_ended);
    UNIT_TEST(test_errors);
    UNIT_TEST(test_field_is_key_equals_value);
    UNIT_TEST(test_text_is_written_verbatim);
    UNIT_TEST(test_event);
    UNIT_TEST(test_event_during_command_comes_before_its_reply_ends);
    UNIT_TEST(test_help_lists_commands);
    UNIT_TEST(test_help_for_one_command);
    UNIT_TEST(test_help_for_unknown_command);
    return UNITY_END();
}
