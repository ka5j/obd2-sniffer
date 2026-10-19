#include <string.h>

#include "line_editor.h"
#include "test_support.h"

static line_editor_t le;

void setUp(void)
{
    line_editor_init(&le);
}

void tearDown(void)
{
}

/* Feed a string, returning the event from the last byte. */
static line_event_t feed(const char *s)
{
    line_event_t ev = LE_NONE;
    while (*s != '\0') {
        ev = line_editor_feed(&le, *s++);
    }
    return ev;
}

static void test_printable_bytes_are_echoed(void)
{
    TEST_ASSERT_EQUAL(LE_ECHO, line_editor_feed(&le, 'a'));
    TEST_ASSERT_EQUAL(LE_ECHO, line_editor_feed(&le, ' '));
    TEST_ASSERT_EQUAL(LE_ECHO, line_editor_feed(&le, '~'));
    TEST_ASSERT_EQUAL_size_t(3U, le.len);
}

static void test_cr_completes_line(void)
{
    feed("led on");
    TEST_ASSERT_EQUAL(LE_LINE_READY, line_editor_feed(&le, '\r'));
    TEST_ASSERT_EQUAL_STRING("led on", line_editor_line(&le));
}

static void test_lf_completes_line(void)
{
    feed("status");
    TEST_ASSERT_EQUAL(LE_LINE_READY, line_editor_feed(&le, '\n'));
    TEST_ASSERT_EQUAL_STRING("status", line_editor_line(&le));
}

static void test_crlf_counts_as_one_line_end(void)
{
    TEST_ASSERT_EQUAL(LE_LINE_READY, feed("help\r"));
    TEST_ASSERT_EQUAL(LE_NONE, line_editor_feed(&le, '\n'));
    TEST_ASSERT_EQUAL(LE_LINE_READY, feed("version\r"));
    TEST_ASSERT_EQUAL_STRING("version", line_editor_line(&le));
}

static void test_lf_lf_gives_two_lines(void)
{
    TEST_ASSERT_EQUAL(LE_LINE_READY, line_editor_feed(&le, '\n'));
    TEST_ASSERT_EQUAL(LE_LINE_READY, line_editor_feed(&le, '\n'));
    TEST_ASSERT_EQUAL_STRING("", line_editor_line(&le));
}

static void test_cr_cr_gives_two_lines(void)
{
    TEST_ASSERT_EQUAL(LE_LINE_READY, line_editor_feed(&le, '\r'));
    TEST_ASSERT_EQUAL(LE_LINE_READY, line_editor_feed(&le, '\r'));
}

static void test_next_byte_after_line_starts_fresh_line(void)
{
    feed("abc\r");
    feed("x");
    TEST_ASSERT_EQUAL(LE_LINE_READY, line_editor_feed(&le, '\r'));
    TEST_ASSERT_EQUAL_STRING("x", line_editor_line(&le));
}

static void test_backspace_and_delete_remove_last_char(void)
{
    feed("ledx");
    TEST_ASSERT_EQUAL(LE_ERASE, line_editor_feed(&le, '\b'));
    feed(" onn");
    TEST_ASSERT_EQUAL(LE_ERASE, line_editor_feed(&le, '\x7f'));
    feed("\r");
    TEST_ASSERT_EQUAL_STRING("led on", line_editor_line(&le));
}

static void test_backspace_on_empty_line_does_nothing(void)
{
    TEST_ASSERT_EQUAL(LE_NONE, line_editor_feed(&le, '\b'));
    TEST_ASSERT_EQUAL_size_t(0U, le.len);
}

static void test_ctrl_c_discards_line(void)
{
    feed("reset");
    TEST_ASSERT_EQUAL(LE_CANCEL, line_editor_feed(&le, '\x03'));
    feed("help\r");
    TEST_ASSERT_EQUAL_STRING("help", line_editor_line(&le));
}

static void test_control_and_non_ascii_bytes_are_ignored(void)
{
    TEST_ASSERT_EQUAL(LE_NONE, line_editor_feed(&le, '\x1b')); /* ESC */
    TEST_ASSERT_EQUAL(LE_NONE, line_editor_feed(&le, '\t'));
    TEST_ASSERT_EQUAL(LE_NONE, line_editor_feed(&le, (char)0xC3)); /* UTF-8 lead byte */
    TEST_ASSERT_EQUAL_size_t(0U, le.len);
}

static void fill_line(void)
{
    for (size_t i = 0; i < LINE_EDITOR_MAX; i++) {
        TEST_ASSERT_EQUAL(LE_ECHO, line_editor_feed(&le, 'a'));
    }
}

static void test_full_line_is_kept(void)
{
    fill_line();
    TEST_ASSERT_EQUAL(LE_LINE_READY, line_editor_feed(&le, '\r'));
    TEST_ASSERT_EQUAL_size_t(LINE_EDITOR_MAX, strlen(line_editor_line(&le)));
}

static void test_overflowed_line_is_rejected_when_it_ends(void)
{
    fill_line();
    TEST_ASSERT_EQUAL(LE_OVERFLOW, line_editor_feed(&le, 'b'));
    TEST_ASSERT_EQUAL(LE_TOO_LONG, line_editor_feed(&le, '\r'));
    TEST_ASSERT_EQUAL(LE_NONE, line_editor_feed(&le, '\n')); /* CR LF still counts once */
    (void)feed("ok");
    TEST_ASSERT_EQUAL(LE_LINE_READY, line_editor_feed(&le, '\r'));
    TEST_ASSERT_EQUAL_STRING("ok", line_editor_line(&le));
}

static void test_backspace_does_not_rescue_an_overflowed_line(void)
{
    fill_line();
    TEST_ASSERT_EQUAL(LE_OVERFLOW, line_editor_feed(&le, 'b'));
    TEST_ASSERT_EQUAL(LE_ERASE, line_editor_feed(&le, '\b'));
    TEST_ASSERT_EQUAL(LE_TOO_LONG, line_editor_feed(&le, '\r'));
}

static void test_ctrl_c_clears_overflow(void)
{
    fill_line();
    TEST_ASSERT_EQUAL(LE_OVERFLOW, line_editor_feed(&le, 'b'));
    TEST_ASSERT_EQUAL(LE_CANCEL, line_editor_feed(&le, '\x03'));
    (void)feed("ok");
    TEST_ASSERT_EQUAL(LE_LINE_READY, line_editor_feed(&le, '\r'));
    TEST_ASSERT_EQUAL_STRING("ok", line_editor_line(&le));
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_printable_bytes_are_echoed);
    UNIT_TEST(test_cr_completes_line);
    UNIT_TEST(test_lf_completes_line);
    UNIT_TEST(test_crlf_counts_as_one_line_end);
    UNIT_TEST(test_lf_lf_gives_two_lines);
    UNIT_TEST(test_cr_cr_gives_two_lines);
    UNIT_TEST(test_next_byte_after_line_starts_fresh_line);
    UNIT_TEST(test_backspace_and_delete_remove_last_char);
    UNIT_TEST(test_backspace_on_empty_line_does_nothing);
    UNIT_TEST(test_ctrl_c_discards_line);
    UNIT_TEST(test_control_and_non_ascii_bytes_are_ignored);
    UNIT_TEST(test_full_line_is_kept);
    UNIT_TEST(test_overflowed_line_is_rejected_when_it_ends);
    UNIT_TEST(test_backspace_does_not_rescue_an_overflowed_line);
    UNIT_TEST(test_ctrl_c_clears_overflow);
    return UNITY_END();
}
