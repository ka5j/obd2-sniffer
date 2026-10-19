#include <string.h>

#include "cli.h"
#include "test_support.h"

/* ---- a small command table with recording handlers ---------------------- */

static int calls;
static int last_argc;
static char last_args[CLI_MAX_ARGS][16];
static void *last_ctx;

static cli_status_t record(int argc, char *argv[], void *ctx)
{
    calls++;
    last_argc = argc;
    last_ctx = ctx;
    for (int i = 0; i < argc; i++) {
        strncpy(last_args[i], argv[i], sizeof last_args[i] - 1U);
    }
    return CLI_OK;
}

static cli_status_t reject(int argc, char *argv[], void *ctx)
{
    (void)argc;
    (void)argv;
    (void)ctx;
    return CLI_ERR_INVALID;
}

static const cli_command_t table[] = {
    {"help", "[command]", "show help", 0, 1, record},
    {"led", "on|off|auto", "set the LED", 1, 1, record},
    {"bad", "", "always rejects", 0, 0, reject},
};
#define TABLE_LEN (sizeof table / sizeof table[0])

static char line[64];

static cli_status_t exec(const char *text, const cli_command_t **found)
{
    strncpy(line, text, sizeof line - 1U);
    line[sizeof line - 1U] = '\0';
    return cli_execute(table, TABLE_LEN, line, &calls, found);
}

void setUp(void)
{
    calls = 0;
    last_argc = 0;
    last_ctx = NULL;
    memset(last_args, 0, sizeof last_args);
}

void tearDown(void)
{
}

/* ---- tokenizer ------------------------------------------------------------ */

static void test_tokenize_splits_on_spaces_and_tabs(void)
{
    char buf[] = "  led \t on  ";
    char *argv[4];
    TEST_ASSERT_EQUAL_INT(2, cli_tokenize(buf, argv, 4));
    TEST_ASSERT_EQUAL_STRING("led", argv[0]);
    TEST_ASSERT_EQUAL_STRING("on", argv[1]);
}

static void test_tokenize_empty_and_blank_lines(void)
{
    char empty[] = "";
    char blank[] = " \t ";
    char *argv[2];
    TEST_ASSERT_EQUAL_INT(0, cli_tokenize(empty, argv, 2));
    TEST_ASSERT_EQUAL_INT(0, cli_tokenize(blank, argv, 2));
}

static void test_tokenize_rejects_too_many_words(void)
{
    char buf[] = "a b c";
    char *argv[2];
    TEST_ASSERT_EQUAL_INT(-1, cli_tokenize(buf, argv, 2));
}

static void test_tokenize_exactly_max_words(void)
{
    char buf[] = "a b";
    char *argv[2];
    TEST_ASSERT_EQUAL_INT(2, cli_tokenize(buf, argv, 2));
}

/* ---- lookup ---------------------------------------------------------------- */

static void test_streq_is_case_insensitive(void)
{
    TEST_ASSERT_TRUE(cli_streq("LED", "led"));
    TEST_ASSERT_TRUE(cli_streq("Help", "hELP"));
    TEST_ASSERT_TRUE(cli_streq("", ""));
    TEST_ASSERT_TRUE(cli_streq("a1[", "A1[")); /* non-letters compare as-is */
    TEST_ASSERT_FALSE(cli_streq("led", "les"));
    TEST_ASSERT_FALSE(cli_streq("le", "led")); /* first is a prefix */
    TEST_ASSERT_FALSE(cli_streq("led", "le")); /* second is a prefix */
    TEST_ASSERT_FALSE(cli_streq("[", "{"));    /* outside A-Z: not folded */
}

static void test_find_returns_entry_or_null(void)
{
    TEST_ASSERT_EQUAL_PTR(&table[1], cli_find(table, TABLE_LEN, "LED"));
    TEST_ASSERT_NULL(cli_find(table, TABLE_LEN, "reboot"));
    TEST_ASSERT_NULL(cli_find(table, 0U, "help"));
}

/* ---- execute --------------------------------------------------------------- */

static void test_execute_runs_handler_with_args_and_ctx(void)
{
    const cli_command_t *found = NULL;
    TEST_ASSERT_EQUAL(CLI_OK, exec("Led ON", &found));
    TEST_ASSERT_EQUAL_PTR(&table[1], found);
    TEST_ASSERT_EQUAL_INT(1, calls);
    TEST_ASSERT_EQUAL_INT(2, last_argc);
    TEST_ASSERT_EQUAL_STRING("Led", last_args[0]);
    TEST_ASSERT_EQUAL_STRING("ON", last_args[1]);
    TEST_ASSERT_EQUAL_PTR(&calls, last_ctx);
}

static void test_execute_optional_argument_range(void)
{
    TEST_ASSERT_EQUAL(CLI_OK, exec("help", NULL));
    TEST_ASSERT_EQUAL(CLI_OK, exec("help led", NULL));
    TEST_ASSERT_EQUAL_INT(2, calls);
}

static void test_execute_too_few_arguments(void)
{
    TEST_ASSERT_EQUAL(CLI_ERR_ARGS, exec("led", NULL));
    TEST_ASSERT_EQUAL_INT(0, calls);
}

static void test_execute_too_many_arguments_for_command(void)
{
    TEST_ASSERT_EQUAL(CLI_ERR_ARGS, exec("led on off", NULL));
    TEST_ASSERT_EQUAL_INT(0, calls);
}

static void test_execute_unknown_command(void)
{
    const cli_command_t *found = &table[0];
    TEST_ASSERT_EQUAL(CLI_ERR_UNKNOWN, exec("reboot now", &found));
    TEST_ASSERT_NULL(found);
}

static void test_execute_blank_line(void)
{
    TEST_ASSERT_EQUAL(CLI_ERR_EMPTY, exec("   ", NULL));
}

static void test_execute_more_words_than_max_args(void)
{
    TEST_ASSERT_EQUAL(CLI_ERR_TOO_MANY, exec("help 1 2 3 4 5 6 7 8", NULL));
    TEST_ASSERT_EQUAL_INT(0, calls);
}

static void test_execute_returns_handler_status(void)
{
    TEST_ASSERT_EQUAL(CLI_ERR_INVALID, exec("bad", NULL));
}

static void test_status_strings(void)
{
    TEST_ASSERT_EQUAL_STRING("ok", cli_status_str(CLI_OK));
    TEST_ASSERT_EQUAL_STRING("empty line", cli_status_str(CLI_ERR_EMPTY));
    TEST_ASSERT_EQUAL_STRING("unknown command", cli_status_str(CLI_ERR_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("wrong number of arguments", cli_status_str(CLI_ERR_ARGS));
    TEST_ASSERT_EQUAL_STRING("too many arguments", cli_status_str(CLI_ERR_TOO_MANY));
    TEST_ASSERT_EQUAL_STRING("invalid argument", cli_status_str(CLI_ERR_INVALID));
    TEST_ASSERT_EQUAL_STRING("command failed", cli_status_str(CLI_ERR_FAILED));
    TEST_ASSERT_EQUAL_STRING("line too long", cli_status_str(CLI_ERR_TOO_LONG));
    TEST_ASSERT_EQUAL_STRING("unknown error", cli_status_str((cli_status_t)99));
}

/* ---- number parsing ------------------------------------------------------- */

static void test_parse_decimal(void)
{
    uint32_t v = 0U;
    TEST_ASSERT_TRUE(cli_parse_u32("0", &v));
    TEST_ASSERT_EQUAL_UINT32(0U, v);
    TEST_ASSERT_TRUE(cli_parse_u32("2024", &v));
    TEST_ASSERT_EQUAL_UINT32(2024U, v);
    TEST_ASSERT_TRUE(cli_parse_u32("012", &v)); /* leading zero is still decimal */
    TEST_ASSERT_EQUAL_UINT32(12U, v);
    TEST_ASSERT_TRUE(cli_parse_u32("4294967295", &v));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, v);
}

static void test_parse_hex(void)
{
    uint32_t v = 0U;
    TEST_ASSERT_TRUE(cli_parse_u32("0x7DF", &v));
    TEST_ASSERT_EQUAL_UINT32(0x7DFU, v);
    TEST_ASSERT_TRUE(cli_parse_u32("0Xabcdef", &v));
    TEST_ASSERT_EQUAL_UINT32(0xABCDEFU, v);
    TEST_ASSERT_TRUE(cli_parse_u32("0xFFFFFFFF", &v));
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, v);
}

static void test_parse_rejects_bad_input_without_writing(void)
{
    uint32_t v = 123U;
    TEST_ASSERT_FALSE(cli_parse_u32("", &v));
    TEST_ASSERT_FALSE(cli_parse_u32("0x", &v));
    TEST_ASSERT_FALSE(cli_parse_u32("-1", &v));
    TEST_ASSERT_FALSE(cli_parse_u32("12a", &v));  /* hex digit in decimal */
    TEST_ASSERT_FALSE(cli_parse_u32("0x1G", &v)); /* past 'f' */
    TEST_ASSERT_FALSE(cli_parse_u32("1:", &v));   /* just past '9' */
    TEST_ASSERT_FALSE(cli_parse_u32("0x@", &v));  /* just before 'A' */
    TEST_ASSERT_FALSE(cli_parse_u32("4294967296", &v));
    TEST_ASSERT_FALSE(cli_parse_u32("0x100000000", &v));
    TEST_ASSERT_EQUAL_UINT32(123U, v);
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_tokenize_splits_on_spaces_and_tabs);
    UNIT_TEST(test_tokenize_empty_and_blank_lines);
    UNIT_TEST(test_tokenize_rejects_too_many_words);
    UNIT_TEST(test_tokenize_exactly_max_words);
    UNIT_TEST(test_streq_is_case_insensitive);
    UNIT_TEST(test_find_returns_entry_or_null);
    UNIT_TEST(test_execute_runs_handler_with_args_and_ctx);
    UNIT_TEST(test_execute_optional_argument_range);
    UNIT_TEST(test_execute_too_few_arguments);
    UNIT_TEST(test_execute_too_many_arguments_for_command);
    UNIT_TEST(test_execute_unknown_command);
    UNIT_TEST(test_execute_blank_line);
    UNIT_TEST(test_execute_more_words_than_max_args);
    UNIT_TEST(test_execute_returns_handler_status);
    UNIT_TEST(test_status_strings);
    UNIT_TEST(test_parse_decimal);
    UNIT_TEST(test_parse_hex);
    UNIT_TEST(test_parse_rejects_bad_input_without_writing);
    return UNITY_END();
}
