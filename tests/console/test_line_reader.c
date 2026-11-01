#include <string.h>

#include "console/line_reader.h"
#include "test_support.h"

static line_reader_t lr;

void setUp(void)
{
    line_reader_init(&lr);
}

void tearDown(void)
{
}

/* Feed a string, returning the event from the last byte. */
static line_event_t feed(const char *s)
{
    line_event_t ev = LR_NONE;
    while (*s != '\0') {
        ev = line_reader_feed(&lr, *s++);
    }
    return ev;
}

static void test_printable_bytes_are_collected(void)
{
    TEST_ASSERT_EQUAL(LR_NONE, line_reader_feed(&lr, 'a'));
    TEST_ASSERT_EQUAL(LR_NONE, line_reader_feed(&lr, ' '));
    TEST_ASSERT_EQUAL(LR_NONE, line_reader_feed(&lr, '~'));
    TEST_ASSERT_EQUAL_size_t(3U, lr.len);
}

static void test_cr_completes_line(void)
{
    feed("fault clear");
    TEST_ASSERT_EQUAL(LR_LINE_READY, line_reader_feed(&lr, '\r'));
    TEST_ASSERT_EQUAL_STRING("fault clear", line_reader_line(&lr));
}

static void test_lf_completes_line(void)
{
    feed("status");
    TEST_ASSERT_EQUAL(LR_LINE_READY, line_reader_feed(&lr, '\n'));
    TEST_ASSERT_EQUAL_STRING("status", line_reader_line(&lr));
}

static void test_crlf_counts_as_one_line_end(void)
{
    TEST_ASSERT_EQUAL(LR_LINE_READY, feed("help\r"));
    TEST_ASSERT_EQUAL(LR_NONE, line_reader_feed(&lr, '\n'));
    TEST_ASSERT_EQUAL(LR_LINE_READY, feed("version\r"));
    TEST_ASSERT_EQUAL_STRING("version", line_reader_line(&lr));
}

static void test_lf_lf_gives_two_lines(void)
{
    TEST_ASSERT_EQUAL(LR_LINE_READY, line_reader_feed(&lr, '\n'));
    TEST_ASSERT_EQUAL(LR_LINE_READY, line_reader_feed(&lr, '\n'));
    TEST_ASSERT_EQUAL_STRING("", line_reader_line(&lr));
}

static void test_cr_cr_gives_two_lines(void)
{
    TEST_ASSERT_EQUAL(LR_LINE_READY, line_reader_feed(&lr, '\r'));
    TEST_ASSERT_EQUAL(LR_LINE_READY, line_reader_feed(&lr, '\r'));
}

static void test_next_byte_after_line_starts_fresh_line(void)
{
    feed("abc\r");
    feed("x");
    TEST_ASSERT_EQUAL(LR_LINE_READY, line_reader_feed(&lr, '\r'));
    TEST_ASSERT_EQUAL_STRING("x", line_reader_line(&lr));
}

static void test_ctrl_c_discards_line(void)
{
    feed("reset");
    TEST_ASSERT_EQUAL(LR_NONE, line_reader_feed(&lr, '\x03'));
    feed("help\r");
    TEST_ASSERT_EQUAL_STRING("help", line_reader_line(&lr));
}

static void test_control_and_non_ascii_bytes_are_ignored(void)
{
    TEST_ASSERT_EQUAL(LR_NONE, line_reader_feed(&lr, '\x1b')); /* ESC */
    TEST_ASSERT_EQUAL(LR_NONE, line_reader_feed(&lr, '\b'));
    TEST_ASSERT_EQUAL(LR_NONE, line_reader_feed(&lr, (char)0x7F)); /* DEL */
    TEST_ASSERT_EQUAL(LR_NONE, line_reader_feed(&lr, (char)0xC3)); /* UTF-8 lead byte */
    TEST_ASSERT_EQUAL_size_t(0U, lr.len);
}

static void fill_line(void)
{
    for (size_t i = 0; i < LINE_READER_MAX; i++) {
        TEST_ASSERT_EQUAL(LR_NONE, line_reader_feed(&lr, 'a'));
    }
}

static void test_full_line_is_kept(void)
{
    fill_line();
    TEST_ASSERT_EQUAL(LR_LINE_READY, line_reader_feed(&lr, '\r'));
    TEST_ASSERT_EQUAL_size_t(LINE_READER_MAX, strlen(line_reader_line(&lr)));
}

static void test_overflowed_line_is_rejected_when_it_ends(void)
{
    fill_line();
    TEST_ASSERT_EQUAL(LR_NONE, line_reader_feed(&lr, 'b'));
    TEST_ASSERT_EQUAL(LR_TOO_LONG, line_reader_feed(&lr, '\r'));
    TEST_ASSERT_EQUAL(LR_NONE, line_reader_feed(&lr, '\n')); /* CR LF still counts once */
    (void)feed("ok");
    TEST_ASSERT_EQUAL(LR_LINE_READY, line_reader_feed(&lr, '\r'));
    TEST_ASSERT_EQUAL_STRING("ok", line_reader_line(&lr));
}

static void test_ctrl_c_clears_overflow(void)
{
    fill_line();
    TEST_ASSERT_EQUAL(LR_NONE, line_reader_feed(&lr, 'b'));
    TEST_ASSERT_EQUAL(LR_NONE, line_reader_feed(&lr, '\x03'));
    (void)feed("ok");
    TEST_ASSERT_EQUAL(LR_LINE_READY, line_reader_feed(&lr, '\r'));
    TEST_ASSERT_EQUAL_STRING("ok", line_reader_line(&lr));
}

int main(int argc, char **argv)
{
    if (UnityParseOptions(argc, argv) != 0) {
        return 1;
    }
    UNITY_BEGIN();
    UNIT_TEST(test_printable_bytes_are_collected);
    UNIT_TEST(test_cr_completes_line);
    UNIT_TEST(test_lf_completes_line);
    UNIT_TEST(test_crlf_counts_as_one_line_end);
    UNIT_TEST(test_lf_lf_gives_two_lines);
    UNIT_TEST(test_cr_cr_gives_two_lines);
    UNIT_TEST(test_next_byte_after_line_starts_fresh_line);
    UNIT_TEST(test_ctrl_c_discards_line);
    UNIT_TEST(test_control_and_non_ascii_bytes_are_ignored);
    UNIT_TEST(test_full_line_is_kept);
    UNIT_TEST(test_overflowed_line_is_rejected_when_it_ends);
    UNIT_TEST(test_ctrl_c_clears_overflow);
    return UNITY_END();
}
